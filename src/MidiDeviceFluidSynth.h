/*********************************************************************************/
/*!
@file           MidiDeviceFluidSynth.h

@brief          xxxxxx.

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

#ifndef __MIDI_DEVICE_FLUIDSYNTH_H__
#define __MIDI_DEVICE_FLUIDSYNTH_H__

#include "MidiDeviceBase.h"

#include <fluidsynth.h>

#include <QMap>

#define FLUID_DEFAULT_GAIN 80

class CMidiDeviceFluidSynth : public CMidiDeviceBase
{
    virtual void init();
    //! add a midi event to be played immediately
    virtual void playMidiEvent(const CMidiEvent & event);
    virtual int checkMidiInput();
    virtual CMidiEvent readMidiInput();
    virtual QStringList getMidiPortList(midiType_t type);

    virtual bool openMidiPort(midiType_t type, const QString &portName);
    virtual void closeMidiPort(midiType_t type, int index);

    virtual bool validMidiConnection() {return m_validConnection;}

    // based on the fluid synth settings
    virtual int     midiSettingsSetStr(const QString &name, const QString &str);
    virtual int     midiSettingsSetNum(const QString &name, double val);
    virtual int     midiSettingsSetInt(const QString &name, int val);
    virtual QString midiSettingsGetStr(const QString &name);
    virtual double  midiSettingsGetNum(const QString &name);
    virtual int     midiSettingsGetInt(const QString &name);

    QList<SoundPreset> soundPresets();
    bool selectChannelSound(int channel, const QString &font, int bank, int program);
    bool addSoundFont(const QString &font);

public:
    CMidiDeviceFluidSynth();
    ~CMidiDeviceFluidSynth();

    static QString getFluidInternalName()
    {
        return QString(tr("Internal Sound") + " " + FLUID_NAME );
    }

    // Platform defaults. On macOS the buffer size is the output device's I/O buffer (see
    // MacAudio.h): 128 frames @ 44.1 kHz is about 3 ms from key press to sound.
#if defined (Q_OS_DARWIN)
    static const char* defaultAudioDriver() { return "coreaudio"; }
    static int defaultSampleRate() { return 44100; }
    static int defaultPeriodSize() { return 128; }
    static int defaultPeriods() { return 2; }
    static constexpr int MAC_FLUID_PERIOD_SIZE = 512;
    static constexpr int MAC_FLUID_PERIODS = 8;
#else
    static const char* defaultAudioDriver() { return "pulseaudio"; }
    static int defaultSampleRate() { return 22050; }
    static int defaultPeriodSize() { return 128; }
    static int defaultPeriods() { return 4; }
#endif

private:
    static constexpr const char* FLUID_NAME = "(FluidSynth)";
    unsigned char m_savedRawBytes[40]; // Raw data is used for used for a SYSTEM_EVENT
    unsigned int m_rawDataIndex;

    fluid_settings_t* m_fluidSettings;
    fluid_synth_t* m_synth;
    fluid_audio_driver_t* m_audioDriver;
    int m_soundFontId;            // the main (General MIDI) SoundFont, used for the accompaniment
    QMap<QString, int> m_fontIds; // every loaded SoundFont file -> its FluidSynth id
    bool m_channelLocked[MAX_MIDI_CHANNELS] = {}; // channels with a fixed instrument (see selectChannelSound)
    int m_channelBank[MAX_MIDI_CHANNELS] = {};    // the bank selected on each channel (CC 0)
    static constexpr int DRUM_BANK = 128;         // FluidSynth's bank of the General MIDI drum kits
    void setMainFontProgram(int channel, int program);
    static int probeAudio(void *data, int len, int nfx, float *fx[], int nout, float *out[]);
    bool m_validConnection;
};

#endif //__MIDI_DEVICE_FLUIDSYNTH_H__
