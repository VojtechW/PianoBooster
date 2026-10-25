/*********************************************************************************/
/*!
@file           MidiDeviceFluidSynth.cpp

@brief          MidiDeviceFluidSynth talks to the MidiRt  Real Time Version.

@author         L. J. Barman

    Copyright (c)   2008-2020, L. J. Barman, all rights reserved

    This file is part of the PianoBooster application

    PianoBooster is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    PianoBooster is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PianoBooster.  If not, see <http://www.gnu.org/licenses/>.

*/
/*********************************************************************************/

#include "MidiDeviceFluidSynth.h"

#include <QFileInfo>

#include <algorithm>

static fluid_settings_t* s_debug_fluid_settings;

static void debug_settings_foreach_func (void *data,
#if FLUIDSYNTH_VERSION_MAJOR >= 2
    const char *name,
#else
    char *name,
#endif
    int type)
{
    Q_UNUSED(data)
    Q_UNUSED(type)
    char buffer[300];
    fluid_settings_copystr(s_debug_fluid_settings, name, buffer, sizeof (buffer));
    ppLogDebug("settings_foreach_func %s : %s", name , buffer );
}

CMidiDeviceFluidSynth::CMidiDeviceFluidSynth()
{
    m_synth = nullptr;
    m_fluidSettings = nullptr;
    m_audioDriver = nullptr;
    m_rawDataIndex = 0;
    m_validConnection = false;
}

CMidiDeviceFluidSynth::~CMidiDeviceFluidSynth()
{
    CMidiDeviceFluidSynth::closeMidiPort(MIDI_OUTPUT, -1); // not possible to make virtual call in d'tor
}

void CMidiDeviceFluidSynth::init()
{
}

QStringList CMidiDeviceFluidSynth::getMidiPortList(midiType_t type)
{
    if (type != MIDI_OUTPUT) // Only has an output
        return QStringList();

    if (qsettings==nullptr)
        return QStringList();

    QStringList fontList = qsettings->value("FluidSynth/SoundFont").toStringList();

    if (fontList.size() > 0){
        return QStringList(getFluidInternalName());
    }
    return fontList;
}

bool CMidiDeviceFluidSynth::openMidiPort(midiType_t type, const QString &portName)
{
    closeMidiPort(MIDI_OUTPUT, -1);

    if (portName.isEmpty())
        return false;

    if (type == MIDI_INPUT)
        return false;

    if (!portName.endsWith(FLUID_NAME)) {return false;}

    if (getMidiPortList(type).size()==0) {return false;}

    // Load a SoundFont
    QStringList fontList = qsettings->value("FluidSynth/SoundFont").toStringList();
    if (fontList.size() == 0) {return false;}

    // Create the settings.
    m_fluidSettings = new_fluid_settings();

    // Change the settings if necessary
    fluid_settings_setnum(m_fluidSettings, "synth.sample-rate", qsettings->value("FluidSynth/sampleRateCombo", defaultSampleRate()).toInt());
    fluid_settings_setint(m_fluidSettings, "audio.period-size", qsettings->value("FluidSynth/bufferSizeCombo", defaultPeriodSize()).toInt());
    fluid_settings_setint(m_fluidSettings, "audio.periods", qsettings->value("FluidSynth/bufferCountCombo", defaultPeriods()).toInt());

#if !defined (Q_OS_WINDOWS)
    fluid_settings_setstr(m_fluidSettings, "audio.driver", qsettings->value("FluidSynth/audioDriverCombo", defaultAudioDriver()).toString().toStdString().c_str());
#endif

    // A little room ambience makes the piano sound much more natural; reverb and chorus cost some
    // CPU in the audio thread but add no latency.
    fluid_settings_setint(m_fluidSettings, "synth.reverb.active", qsettings->value("FluidSynth/reverbCheck", true).toBool() ? 1 : 0);
    fluid_settings_setint(m_fluidSettings, "synth.chorus.active", qsettings->value("FluidSynth/chorusCheck", false).toBool() ? 1 : 0);
    fluid_settings_setnum(m_fluidSettings, "synth.reverb.room-size", 0.45);
    fluid_settings_setnum(m_fluidSettings, "synth.reverb.damp", 0.35);
    fluid_settings_setnum(m_fluidSettings, "synth.reverb.width", 0.7);
    fluid_settings_setnum(m_fluidSettings, "synth.reverb.level", 0.4);

    // Create the synthesizer.
    m_synth = new_fluid_synth(m_fluidSettings);

    // Create the audio driver.
    m_audioDriver = new_fluid_audio_driver(m_fluidSettings, m_synth);

    // Extra SoundFonts provide instruments for the pianist (e.g. a dedicated grand piano or
    // electric piano). Only the one in use is loaded now (a big piano bank takes a few seconds and
    // a lot of memory), the others when they are chosen. It is loaded before the main General
    // MIDI SoundFont, so that one stays on top of FluidSynth's font stack and is what the song's
    // program changes select.
    m_fontIds.clear();
    const QString pianistFont = qsettings->value("Sound/PianistFont").toString();
    if (!pianistFont.isEmpty() && pianistFont != fontList.at(0))
        addSoundFont(pianistFont);

    QString pathName = fontList.at(0);
    ppLogDebug("Sound font %s", qPrintable(pathName));
    m_soundFontId = fluid_synth_sfload(m_synth, qPrintable(pathName), 0);
    if (m_soundFontId == -1)
        return false;
    m_fontIds.insert(pathName, m_soundFontId);
    for (bool &locked : m_channelLocked)
        locked = false;

    for (int channel = 0; channel < MAX_MIDI_CHANNELS ; channel++)
    {
        m_channelBank[channel] = (channel == MIDI_DRUM_CHANNEL) ? DRUM_BANK : 0;
        setMainFontProgram(channel, GM_PIANO_PATCH);
    }
    fluid_synth_set_gain(m_synth, qsettings->value("FluidSynth/masterGainSpin", FLUID_DEFAULT_GAIN).toFloat()/100.0f );
    m_validConnection = true;
    if (Cfg::logLevel >= LOG_LEVEL_DEBUG) {
        s_debug_fluid_settings = m_fluidSettings;
        fluid_settings_foreach(m_fluidSettings, 0, debug_settings_foreach_func);
    }
    return true;
}

void CMidiDeviceFluidSynth::closeMidiPort(midiType_t type, int index)
{
    Q_UNUSED(index)
    m_validConnection = false;

    if (type != MIDI_OUTPUT)
        return;

    if (m_fluidSettings == nullptr)
        return;

    /* Clean up */
    delete_fluid_audio_driver(m_audioDriver);
    delete_fluid_synth(m_synth);
    m_fontIds.clear();
    delete_fluid_settings(m_fluidSettings);
    m_audioDriver = nullptr;
    m_synth = nullptr;
    m_fluidSettings = nullptr;
    m_rawDataIndex = 0;

}

//! add a midi event to be played immediately
void CMidiDeviceFluidSynth::playMidiEvent(const CMidiEvent & event)
{
    if (m_synth == nullptr)
        return;

    int channel = event.channel() & 0x0f;
    switch(event.type())
    {
        case MIDI_NOTE_OFF: // NOTE_OFF
            fluid_synth_noteoff(m_synth, channel, event.note());
            break;
        case MIDI_NOTE_ON:      // NOTE_ON
            fluid_synth_noteon(m_synth, channel, event.note(), event.velocity());
            break;

        case MIDI_NOTE_PRESSURE: //POLY_AFTERTOUCH: 3 bytes
            //fluid_synth_key_pressure(m_synth, channel, event.data1(), event.data2());
            break;

        case MIDI_CONTROL_CHANGE: //CONTROL_CHANGE:
            if (event.data1() == 0 && channel != MIDI_DRUM_CHANNEL) // bank select (MSB), used by the next program change
                m_channelBank[channel] = event.data2();
            if (m_channelLocked[channel] && (event.data1() == 0 || event.data1() == 32))
                break; // keep the chosen instrument: ignore bank selects
            fluid_synth_cc(m_synth, channel, event.data1(), event.data2());
            //ppLogTrace("MIDI_CONTROL_CHANGE %d %d %d", channel, event.data1(), event.data2());
            break;

        case MIDI_PROGRAM_CHANGE: //PROGRAM_CHANGE:
            if (m_channelLocked[channel])
                break; // keep the chosen instrument
            setMainFontProgram(channel, event.programme());
            break;

        case MIDI_CHANNEL_PRESSURE: //AFTERTOUCH: 2 bytes only
            fluid_synth_channel_pressure(m_synth, channel, event.programme());
            break;

        case MIDI_PITCH_BEND: //PITCH_BEND:
            // a 14 bit number LSB first 0x4000 is the off positions
            fluid_synth_pitch_bend(m_synth, channel, (event.data2() << 7) | event.data1());
            break;

        case  MIDI_PB_collateRawMidiData: //used for a SYSTEM_EVENT
            if (m_rawDataIndex < arraySizeAs<unsigned int>(m_savedRawBytes))
                m_savedRawBytes[m_rawDataIndex++] = static_cast<unsigned char>(event.data1());
            return; // Don't output any thing yet so just return

        case  MIDI_PB_outputRawMidiData: //used for a SYSTEM_EVENT
            //for (size_t i = 0; i < m_rawDataIndex; i++)
                //message.push_back( m_savedRawBytes[i]);
            m_rawDataIndex = 0;
            // FIXME missing
            break;
    }
    //event.printDetails(); // useful for debugging
}

// The song's instruments always come from the main General MIDI SoundFont. FluidSynth's own
// program change would search all loaded SoundFonts, newest first, so an instrument bank loaded
// later (e.g. a piano with its only preset at 0:0) would replace the accompaniment's pianos.
void CMidiDeviceFluidSynth::setMainFontProgram(int channel, int program)
{
    if (fluid_synth_program_select(m_synth, channel, m_soundFontId, m_channelBank[channel], program) == FLUID_OK)
        return;
    // the bank is not in the main SoundFont: try its default bank, then whatever FluidSynth finds
    if (fluid_synth_program_select(m_synth, channel, m_soundFontId, (channel == MIDI_DRUM_CHANNEL) ? DRUM_BANK : 0, program) != FLUID_OK)
        fluid_synth_program_change(m_synth, channel, program);
}

bool CMidiDeviceFluidSynth::addSoundFont(const QString &font)
{
    if (m_synth == nullptr || font.isEmpty())
        return false;
    if (m_fontIds.contains(font))
        return true;
    if (!QFileInfo(font).isFile())
        return false;
    // loading reads the samples into memory, which can take a moment for a big piano SoundFont
    const int id = fluid_synth_sfload(m_synth, qPrintable(font), 0);
    if (id == FLUID_FAILED)
    {
        ppLogWarn("Could not load the SoundFont %s", qPrintable(font));
        return false;
    }
    m_fontIds.insert(font, id);
    return true;
}

QList<SoundPreset> CMidiDeviceFluidSynth::soundPresets()
{
    QList<SoundPreset> presets;
    if (m_synth == nullptr)
        return presets;
    for (auto it = m_fontIds.constBegin(); it != m_fontIds.constEnd(); ++it)
    {
        fluid_sfont_t *sfont = fluid_synth_get_sfont_by_id(m_synth, it.value());
        if (sfont == nullptr)
            continue;
        const bool mainFont = it.value() == m_soundFontId;
        const QString fontName = QFileInfo(it.key()).completeBaseName();
        fluid_sfont_iteration_start(sfont);
        while (fluid_preset_t *preset = fluid_sfont_iteration_next(sfont))
        {
            const int bank = fluid_preset_get_banknum(preset);
            const int program = fluid_preset_get_num(preset);
            // from the General MIDI bank offer the keyboard instruments: pianos, electric pianos,
            // chromatic percussion (vibraphone, ...) and organs; other banks are offered complete
            if (mainFont && (bank != 0 || program > 23))
                continue;
            if (bank == 128) // drum kits
                continue;
            presets.append({it.key(), fontName, bank, program, QString::fromUtf8(fluid_preset_get_name(preset))});
        }
    }
    // the extra SoundFonts that are not loaded yet: offered as one entry each (program -1)
    for (const QString &font : qsettings->value("FluidSynth/ExtraSoundFonts").toStringList())
        if (!m_fontIds.contains(font) && QFileInfo(font).isFile())
            presets.append({font, QFileInfo(font).completeBaseName(), -1, -1, QFileInfo(font).completeBaseName()});

    std::sort(presets.begin(), presets.end(), [this](const SoundPreset &a, const SoundPreset &b) {
        const bool aMain = m_fontIds.value(a.font) == m_soundFontId;
        const bool bMain = m_fontIds.value(b.font) == m_soundFontId;
        if (aMain != bMain)
            return !aMain; // the dedicated instrument banks first
        if (a.font != b.font)
            return a.fontName < b.fontName;
        return (a.bank != b.bank) ? a.bank < b.bank : a.program < b.program;
    });
    return presets;
}

bool CMidiDeviceFluidSynth::selectChannelSound(int channel, const QString &font, int bank, int program)
{
    if (m_synth == nullptr || channel < 0 || channel >= MAX_MIDI_CHANNELS)
        return false;
    if (font.isEmpty())
    {
        if (m_channelLocked[channel]) // give the channel back with the main SoundFont's piano
            setMainFontProgram(channel, GM_PIANO_PATCH);
        m_channelLocked[channel] = false;
        return true;
    }
    if (!addSoundFont(font))
        return false;
    if (program < 0) // "the SoundFont's first instrument"
    {
        fluid_sfont_t *sfont = fluid_synth_get_sfont_by_id(m_synth, m_fontIds.value(font));
        fluid_sfont_iteration_start(sfont);
        fluid_preset_t *preset = fluid_sfont_iteration_next(sfont);
        if (preset == nullptr)
            return false;
        bank = fluid_preset_get_banknum(preset);
        program = fluid_preset_get_num(preset);
    }
    if (fluid_synth_program_select(m_synth, channel, m_fontIds.value(font), bank, program) != FLUID_OK)
        return false;
    m_channelLocked[channel] = true;

    // Free the memory of instrument banks that are no longer used (a big piano takes over a GB).
    // Only the pianist's channel is ever locked, so the font just selected is the only one in use.
    for (auto it = m_fontIds.begin(); it != m_fontIds.end();)
    {
        if (it.value() != m_soundFontId && it.key() != font)
        {
            fluid_synth_sfunload(m_synth, static_cast<unsigned int>(it.value()), 1);
            it = m_fontIds.erase(it);
        }
        else
            ++it;
    }
    return true;
}

// Return the number of events waiting to be read from the midi device
int CMidiDeviceFluidSynth::checkMidiInput()
{
    return 0;
}

// reads the real midi event
CMidiEvent CMidiDeviceFluidSynth::readMidiInput()
{
    CMidiEvent midiEvent;
    return midiEvent;
}

int CMidiDeviceFluidSynth::midiSettingsSetStr(const QString &name, const QString &str)
{
    if (!m_fluidSettings)
        return 0;

    return fluid_settings_setstr(m_fluidSettings, (char *)qPrintable(name), (char *)qPrintable(str));
}

int CMidiDeviceFluidSynth::midiSettingsSetNum(const QString &name, double val)
{
    if (!m_fluidSettings)
        return 0;
    return fluid_settings_setnum(m_fluidSettings, (char *)qPrintable(name), val);
}

int CMidiDeviceFluidSynth::midiSettingsSetInt(const QString &name, int val)
{
    if (!m_fluidSettings)
        return 0;

    return fluid_settings_setint(m_fluidSettings, (char *)qPrintable(name), val);
}

QString CMidiDeviceFluidSynth::midiSettingsGetStr(const QString &name)
{
    Q_UNUSED(name)
    //char buffer[200];
    //if (!m_fluidSettings)
    //    return QString();
    //fluid_settings_getstr(m_fluidSettings, (char *)qPrintable(name), buffer );
    //return QString( buffer );
    return QString(); // FIXME: buffer is never initialized here, let's just return QString() for now
}

double CMidiDeviceFluidSynth::midiSettingsGetNum(const QString &name)
{
    if (!m_fluidSettings)
        return 0.0;
    double val;
    fluid_settings_getnum(m_fluidSettings, (char *)qPrintable(name), &val);
    return val;
}

int CMidiDeviceFluidSynth::midiSettingsGetInt(const QString &name)
{
    if (!m_fluidSettings)
        return 0;
    int val = 0;
    fluid_settings_getint(m_fluidSettings, (char *)qPrintable(name),&val);
    return val;
}
