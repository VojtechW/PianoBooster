/*********************************************************************************/
/*!
@file           Conductor.cpp

@brief          xxxx.

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

#define OPTION_DEBUG_CONDUCTOR     0
#if OPTION_DEBUG_CONDUCTOR
#define ppDEBUG_CONDUCTOR(args)     ppLogDebug args
#else
#define ppDEBUG_CONDUCTOR(args)
#endif

#include "Conductor.h"
#include "Score.h"
#include "Piano.h"
#include "Cfg.h"

playMode_t CConductor::m_playMode = PB_PLAY_MODE_listen;

CConductor::CConductor()
{
    m_scoreWin = nullptr;
    m_settings = nullptr;
    m_piano = nullptr;

    m_songEventQueue = new CQueue<CMidiEvent>(1000);
    m_wantedChordQueue = new CQueue<CChord>(1000);
    m_savedNoteQueue = new CQueue<CMidiEvent>(200);
    m_savedNoteOffQueue = new CQueue<CMidiEvent>(200);
    m_playing = false;
    m_transpose = 0;
    m_latencyFix = 0;
    m_leadLagAdjust = 0;
    setSpeed(1.0);
    setLatencyFix(0);
    m_boostVolume = 0;
    m_pianoVolume = 0;
    m_activeChannel = 0;
    m_skill = 0;
    m_silenceTimeOut = 0;
    m_realTimeEventBits = 0;
    m_mutePianistPart = false;
    setPianistChannels(1-1,2-1);
    cfg_timingMarkersFlag = false;
    cfg_stopPointMode = PB_STOP_POINT_MODE_automatic;
    cfg_rhythmTapping = PB_RHYTHM_TAP_drumsOnly;
    m_cfg_rhythmTapRightHandDrumSound = 40;
    m_cfg_rhythmTapLeftHandDrumSound = 41;

    setPianoSoundPatches(1-1, 7-1); // 6-1
    m_tempo.setSavedWantedChord(&m_savedWantedChord);

    reset();
    rewind();
    testWrongNoteSound(false);
}

CConductor::~CConductor()
{
    delete m_songEventQueue;
    delete m_wantedChordQueue;
    delete m_savedNoteQueue;
    delete m_savedNoteOffQueue;
}

void CConductor::reset()
{
    int i;
    for ( i = 0; i < MAX_MIDI_TRACKS; i++)
    {
        mapTrack2Channel(i,   i);
        if (i >= MAX_MIDI_CHANNELS)
            mapTrack2Channel(i, -1);
    }
}

//! add a midi event to be analysed and displayed on the score
void CConductor::midiEventInsert(CMidiEvent event)
{
    m_songEventQueue->push(event);
}

//! first check if there is space to add a midi event
int CConductor::midiEventSpace()
{
    return m_songEventQueue->space();

}

void CConductor::channelSoundOff(int channel)
{
    if (channel < 0 || channel >= MAX_MIDI_CHANNELS)
    {
        return;
    }

    CMidiEvent midi;
    midi.controlChangeEvent(0, channel, MIDI_ALL_NOTES_OFF, 0);
    playMidiEvent(midi);
    // remove the sustain pedal as well
    midi.controlChangeEvent(0, channel, MIDI_SUSTAIN, 0);
    playMidiEvent(midi);
}

void CConductor::allSoundOff()
{
    int channel;

    for ( channel = 0; channel < MAX_MIDI_CHANNELS; channel++)
    {
        if (channel != m_pianistGoodChan)
            channelSoundOff(channel);
    }
    m_savedNoteQueue->clear();
    m_savedNoteOffQueue->clear();
}

void CConductor::resetAllChannels()
{
    int channel;

    CMidiEvent midi;
    for ( channel = 0; channel < MAX_MIDI_CHANNELS; channel++)
    {
        midi.controlChangeEvent(0, channel, MIDI_RESET_ALL_CONTROLLERS, 0);
        playMidiEvent(midi);
    }
}

/* calculate the new solo_volume */
int CConductor::calcBoostVolume(int channel, int volume)
{
    int returnVolume;
    bool activePart;

    if (volume == -1)
        volume = m_savedMainVolume[channel];
    else
        m_savedMainVolume[channel] = volume;

    returnVolume = volume;
    activePart = false;
    if (CNote::hasPianoPart(m_activeChannel))
    {
        if (m_playMode == PB_PLAY_MODE_listen) // only boost one hand in listen mode
        {
            if (channel == CNote::leftHandChan() && CNote::getActiveHand() != PB_PART_right)
                activePart = true;
            if (channel == CNote::rightHandChan() && CNote::getActiveHand() != PB_PART_left)
                activePart = true;
        }
        else // otherwise always boost both hands
        {
            if ( CNote::hasPianoPart(channel))
                activePart = true;
        }
    }
    else
    {
        if (channel == m_activeChannel)
            activePart= true;
    }
    if (channel == m_activeChannel)
        activePart= true;

    //if (channel == 5)  activePart= true; // for debugging

    if (activePart)
    {
        if (returnVolume == 0 )
            ;               /* Don't adjust the volume if zero */
        else if (m_boostVolume < 0 )
            returnVolume = (returnVolume * (m_boostVolume + 100)) / 100;
        else
            returnVolume += m_boostVolume;
    }
    else
    {
        if (m_boostVolume > 0)
            returnVolume = (returnVolume * (100 - m_boostVolume)) / 100;
    }
        // The piano volume can reduce the volume of the music
    if (m_pianoVolume>0)
        returnVolume = (returnVolume * (100 - m_pianoVolume)) / 100;;

    if (returnVolume > 127)
        returnVolume = 127;
    return returnVolume;
}

/* send boost volume by adjusting all channels */
void CConductor::outputBoostVolume()
{
    int chan;

    for ( chan =0; chan <MAX_MIDI_CHANNELS; chan++ )
    {
        if (hasPianistKeyboardChannel(chan))
            continue;
        CMidiEvent midi;
        midi.controlChangeEvent(0, chan, MIDI_MAIN_VOLUME, calcBoostVolume(chan,-1));
        playTrackEvent(midi);
    }
    outputPianoVolume();
}

void CConductor::transpose(int transpose)
{
    if (m_transpose != transpose)
    {
        allSoundOff();
        m_transpose = transpose;
        if (m_transpose >  12)
            m_transpose = 12;
        if (m_transpose < -12)
            m_transpose = -12;
        m_scoreWin->transpose(m_transpose);
    }
}

void CConductor::mutePianistPart(bool state)
{
    m_mutePianistPart = state;
}

void CConductor::setActiveHand(whichPart_t hand)
{
    if (CNote::getActiveHand() == hand)
        return;
    CNote::setActiveHand(hand);
    outputBoostVolume();
    resetWantedChord();

    findSplitPoint();
    forceScoreRedraw();
}

void CConductor::setPlayMode(playMode_t mode)
{
    m_playMode = mode;
    m_performance.reset(m_playMode == PB_PLAY_MODE_playAlong);
    if ( m_playMode == PB_PLAY_MODE_listen )
        resetWantedChord();
    outputBoostVolume();
    m_piano->setRhythmTapping(m_playMode == PB_PLAY_MODE_rhythmTapping);
}

void CConductor::setActiveChannel(int channel)
{
    m_activeChannel = channel;
    outputBoostVolume();
    resetWantedChord();
    fetchNextChord();
}

void CConductor::outputPianoVolume()
{
    CMidiEvent event;
    int volume = 127;
    // if piano volume is between -100 and 0 reduce the volume accordingly
    if (m_pianoVolume < 0)
        volume = (volume * (100 + m_pianoVolume)) / 100;

    event.controlChangeEvent(0, m_pianistGoodChan, MIDI_MAIN_VOLUME, volume);
    playTrackEvent(event); // Play the midi note or event
    event.controlChangeEvent(0, m_pianistBadChan, MIDI_MAIN_VOLUME, volume);
    playTrackEvent(event); // Play the midi note or event
}

void CConductor::setPianistChannels(int goodChan, int badChan)
{
    if (goodChan != m_pianistGoodChan)
        selectChannelSound(m_pianistGoodChan, QString(), 0, 0); // give the old channel back to the song
    m_pianistGoodChan = goodChan;
    m_pianistBadChan = badChan;
    if (m_scoreWin) // not while the constructor sets the defaults (no output device yet)
        updatePianoSounds();
}

void CConductor::setPianistSound(const QString &font, int bank, int program, bool useSongInstrument)
{
    m_pianistSoundFont = font;
    m_pianistSoundBank = bank;
    m_pianistSoundProgram = program;
    m_useSongInstrument = useSongInstrument;
    updatePianoSounds();
}

void CConductor::updatePianoSounds()
{
    CMidiEvent event;

    if (m_cfg_rightNoteSound>=0) // ignore if set to -1 (tr("None"))
    {
        // a chosen SoundFont instrument (internal synthesizer only), else a General MIDI patch:
        // the song's own instrument for this part, or the one from the keyboard setup
        if (m_useSongInstrument || m_pianistSoundFont.isEmpty() ||
                !selectChannelSound(m_pianistGoodChan, m_pianistSoundFont, m_pianistSoundBank, m_pianistSoundProgram))
        {
            if (!m_useSongInstrument && !m_pianistSoundFont.isEmpty())
                ppLogWarn("Could not select the sound %s %d:%d, using a General MIDI patch", qPrintable(m_pianistSoundFont),
                          m_pianistSoundBank, m_pianistSoundProgram);
            selectChannelSound(m_pianistGoodChan, QString(), 0, 0);
            const int program = (m_useSongInstrument && m_songPianistProgram >= 0) ? m_songPianistProgram : m_cfg_rightNoteSound;
            event.programChangeEvent(0, m_pianistGoodChan, program);
            playTrackEvent( event );
        }
    }
    if (m_cfg_wrongNoteSound>=0)
    {
        event.programChangeEvent(0, m_pianistBadChan, m_cfg_wrongNoteSound);
        playTrackEvent( event );
    }
}

void CConductor::testWrongNoteSound(bool enable)
{
    m_testWrongNoteSound = enable;
    updatePianoSounds();
}

void CConductor::reconnectMidi()
{
    if (!validMidiOutput()) {
        QString midiInputName = m_settings->value("Midi/Input").toString();
        if (midiInputName.startsWith(tr("None"))) {
            CChord::setPianoRange(PC_KEY_LOWEST_NOTE, PC_KEY_HIGHEST_NOTE);
        } else {
            CChord::setPianoRange(m_settings->value("Keyboard/LowestNote", 0).toInt(),
                             m_settings->value("Keyboard/HighestNote", 127).toInt());
        }
        openMidiPort(CMidiDevice::MIDI_INPUT, midiInputName);
        openMidiPort(CMidiDevice::MIDI_OUTPUT,m_settings->value("Midi/Output").toString());
    }
    m_settings->updateWarningMessages();
}

void CConductor::playMusic(bool start)
{
    reconnectMidi();
    m_playing = start;
    allSoundOff();
    m_countingIn = false;
    if (start)
    {
        resetAllChannels();
        m_lastMetronomeBeat = -1;
        if (m_cfg_countIn && m_playMode != PB_PLAY_MODE_listen)
        {
            m_countingIn = true;
            m_countInBeatsLeft = m_bar.getTimeSigTop();
            m_countInTicks = 0;
        }

        testWrongNoteSound(false);
        outputBoostVolume();
        if (seekingBarNumber())
            resetWantedChord();

        /*
        const unsigned char gsModeEnterData[] =  {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7};

        for (auto &d : gsModeEnterData)
        {
            event.collateRawByte(0, d);
            playTrackEvent(event);
        }
        event.outputCollatedRawBytes(0);
        playTrackEvent(event);
        */
    }
}

// This will allow us to map midi tracks onto midi channels
// tacks will eventually allow for more than the 16 midi channels (eg with two mid devices)
// At the moment tracks are only used to remap midi events from channel 0 when using the keyboard lights.
void CConductor::playTrackEvent(CMidiEvent event)
{
    int track = event.channel();
    int chan = track2Channel(track);
    if (chan == -1)
        return;
    event.setChannel(chan);
    playMidiEvent(event);
}

void CConductor::playTransposeEvent(CMidiEvent event)
{
    if (m_transpose != 0 && event.channel() != MIDI_DRUM_CHANNEL &&
                (event.type() == MIDI_NOTE_ON || event.type() == MIDI_NOTE_OFF) )
        event.transpose(m_transpose);

     if (event.type() == MIDI_NOTE_ON && CChord::isPianistNote(event, m_transpose, getActiveChannel()) && shouldMutePianistPart()) {
        return; // mute the note by not playing it
     }

    // boost any volume events
    if (event.type() == MIDI_CONTROL_CHANGE && event.data1() == MIDI_MAIN_VOLUME)
        event.setDatat2(calcBoostVolume(event.channel(), event.data2() ));

    // Don't output note on if we are seeking to bar
    if (!seekingBarNumber())
        playTrackEvent(event); // Play the midi note or event
    else
    {
        if (event.type() == MIDI_PROGRAM_CHANGE || event.type() == MIDI_CONTROL_CHANGE)
            playTrackEvent(event); // Play the midi note or event
    }
}

void CConductor::outputSavedNotes()
{
    // The saved notes off are note needed any more
    // (as the are also in the savedNoteQueue
    m_savedNoteOffQueue->clear();

    // output any the saved up notes
    while (m_savedNoteQueue->length() > 0)
        playTransposeEvent(m_savedNoteQueue->pop());
}

void CConductor::resetWantedChord()
{
    m_wantedChord.clear();
    ppDEBUG_CONDUCTOR(("resetWantedChord m_chordDeltaTime %d m_playingDeltaTime %d", m_chordDeltaTime, m_playingDeltaTime ));

    m_followPlayingTimeOut = false;
    m_chordDeltaTime = m_playingDeltaTime;
    m_pianistTiming = m_chordDeltaTime;
    m_pianistSplitPoint = MIDDLE_C;

    outputSavedNotes();
    m_followState = PB_FOLLOW_searching;
}

// switch modes if we are playing well enough (i.e. don't slow down if we are playing late)
void CConductor::setFollowSkillAdvanced(bool enable)
{
    if (m_settings==nullptr || m_scoreWin == nullptr)
        return;

    m_settings-> setAdvancedMode(enable);

    if (getLatencyFix() > 0)
    {
        // behave differently if we are using the latency fix
        m_cfg_earlyNotesPoint = m_cfg_imminentNotesOffPoint; //disable the early notes
    }

    if (cfg_stopPointMode == PB_STOP_POINT_MODE_onTheBeat)
        enable = false;
    if (cfg_stopPointMode == PB_STOP_POINT_MODE_afterTheBeat)
        enable = true;
    if (m_playMode == PB_PLAY_MODE_rhythmTapping)
        enable = true;

    m_followSkillAdvanced = enable;
    m_stopPoint = (enable) ? m_cfg_stopPointAdvanced: m_cfg_stopPointBeginner ;
}

void CConductor::findSplitPoint()
{
    // find the split point
    int lowestTreble = MIDDLE_C + 37;
    int highestBase =  MIDDLE_C - 37;
    CNote note;

    // Find where to put the split point
    for(int i = 0; i < m_wantedChord.length(); i++)
    {
        note = m_wantedChord.getNote(i);
        if (note.part() == PB_PART_right && note.pitch() < lowestTreble)
            lowestTreble = note.pitch();
        if (note.part() == PB_PART_left && note.pitch() > highestBase)
            highestBase = note.pitch();
    }

    //put the split point in the middle
    m_pianistSplitPoint = ((lowestTreble + highestBase) /2 ) + m_transpose;
}

void CConductor::turnOnKeyboardLights(bool on)
{
    int note;
    int i;
    CMidiEvent event;

    // exit if not enable
    if (Cfg::keyboardLightsChan == -1)
        return;

    // exit if keyboard light are already on
    if (m_KeyboardLightsOn && on)
        return;

    m_KeyboardLightsOn = on;
    for(i = 0; i < m_wantedChord.length(); i++)
    {
        note = m_wantedChord.getNote(i).pitch();
        if (on == true)
            event.noteOnEvent(0, Cfg::keyboardLightsChan, note, 1);
        else
            event.noteOffEvent(0, Cfg::keyboardLightsChan, note, 1);
       playMidiEvent( event ); // don't use the track  settings
    }
}

void CConductor::fetchNextChord()
{
    m_judgedNotes.clear();
    m_followState = PB_FOLLOW_searching;
    m_followPlayingTimeOut = false;

    outputSavedNotes();
    turnOnKeyboardLights(false);

    do // Remove notes or chords that are out of our range
    {
        if (m_wantedChordQueue->length() == 0)
        {
            m_wantedChord.clear();
            m_pianistSplitPoint = MIDDLE_C;
            return;
        }

        m_wantedChord = m_wantedChordQueue->pop();
        m_savedWantedChord = m_wantedChord;
        m_chordDeltaTime -= m_wantedChord.getDeltaTime() * SPEED_ADJUST_FACTOR;
        m_pianistTiming = m_chordDeltaTime;
    }
    while (m_wantedChord.trimOutOfRangeNotes(m_transpose)==0);

    // now find the split point
    findSplitPoint();
}

bool CConductor::validatePianistNote( const CMidiEvent & inputNote)
{
    if ( m_chordDeltaTime <= -m_cfg_playZoneEarly)
        return false;

    return m_wantedChord.searchChord(inputNote.note(), m_transpose);
}

void CConductor::playWantedChord (CChord chord, CMidiEvent inputNote)
{
    int pitch;
    for(int i = 0; i < chord.length(); i++)
    {
        pitch = chord.getNote(i).pitch();
        inputNote.setNote(pitch);
        playTrackEvent(inputNote);
    }
}

bool CConductor::validatePianistChord()
{
    if (m_piano->pianistBadNotesDown() >= 2)
        return false;

    if (m_skill>=3)
    {
        if (m_goodPlayedNotes.length() == m_wantedChord.length())
            return true;
    }
    else
    {
        if (m_goodPlayedNotes.length() >= 1)
            return true;

    }
    return false;
}

/**
 * Add in the extra notes in rhythm practice
 */
void CConductor::expandPianistInput(CMidiEvent inputNote)
{
    if (m_playMode == PB_PLAY_MODE_rhythmTapping)
    {
        CChord chord;
        int i;
        CMidiEvent newNote = inputNote;
        if (inputNote.type() == MIDI_NOTE_ON || inputNote.type() == MIDI_NOTE_OFF)
        {
            chord = m_wantedChord;
            CChord chordForOneHand;
            int notesFound = 0;

            if (inputNote.type() == MIDI_NOTE_OFF)
            {
                chord = m_piano->removeSavedChord(inputNote.note());
                for(i = 0; i < chord.length(); i++)
                {
                    inputNote.setNote( chord.getNote(i).pitch());
                    pianistInput(inputNote);
                }

                // We have already played and removed this chord
                if (chord.length() > 0)
                    return;
            }

            whichPart_t targetPart = (inputNote.note() >= MIDDLE_C) ? PB_PART_right : PB_PART_left;
            for(i = 0; i < chord.length(); i++)
            {
                if (chord.getNote(i).part() == targetPart  && playingMusic())
                {
                    newNote.setNote(chord.getNote(i).pitch());
                    if ( notesFound >= 1 && cfg_rhythmTapping == PB_RHYTHM_TAP_drumsOnly)
                        newNote.setVelocity(-1);
                    else
                        chordForOneHand.addNote(targetPart, chord.getNote(i).pitch());
                    pianistInput(newNote);
                    notesFound++;
                }
            }
            if (notesFound > 0)
                m_piano->addSavedChord(inputNote, chordForOneHand);
            else
            {
                inputNote.setChannel(MIDI_DRUM_CHANNEL);
                pianistInput(inputNote);
            }
        }
        else
        {
            pianistInput(inputNote);
        }
    }
    else
    {
        pianistInput(inputNote);
    }
}

void CConductor::pianistInput(CMidiEvent inputNote)
{
    bool goodSound = true;

    // inputNote.transpose(+12); fixme

    if (m_testWrongNoteSound)
        goodSound = false;

    whichPart_t hand;
    hand = (inputNote.note() >= m_pianistSplitPoint)? PB_PART_right : PB_PART_left;

    // for rhythm tapping
    if ( inputNote.channel() == MIDI_DRUM_CHANNEL)
        hand = (inputNote.note() >= MIDDLE_C) ? PB_PART_right : PB_PART_left;

    if (inputNote.type() == MIDI_NOTE_ON)
    {

        // during the count-in nothing is judged yet: the keys just sound
        if ( !m_countingIn && validatePianistNote(inputNote) == true)
        {
            judgeGoodNote(inputNote.note());
            m_goodPlayedNotes.addNote(hand, inputNote.note());
            m_piano->addPianistNote(hand, inputNote,true);
            qint64 pianistTiming;
            if  ( ( cfg_timingMarkersFlag && m_followSkillAdvanced ) || m_playMode == PB_PLAY_MODE_rhythmTapping )
                pianistTiming = m_pianistTiming;
            else
                pianistTiming = NOT_USED;
            m_scoreWin->setPlayedNoteColor(inputNote.note(),
                        (!m_followPlayingTimeOut)? Cfg::playedGoodColor():Cfg::playedBadColor(),
                        m_chordDeltaTime, pianistTiming);

            if (validatePianistChord() == true)
            {
                if (m_chordDeltaTime < 0)
                    m_tempo.removePlayingTicks(-m_chordDeltaTime);

                judgeRemainingNotes(Judgement::skipped);
                m_goodPlayedNotes.clear();
                fetchNextChord();
                // count the good notes so that the live percentage looks OK
                m_rating.totalNotes(m_wantedChord.length());
                m_rating.calculateAccuracy();
                m_settings->pianistActive();
                if (m_rating.isAccuracyGood() || m_playMode == PB_PLAY_MODE_playAlong)
                    setFollowSkillAdvanced(true); // change the skill level only when they are good enough
                else
                    setFollowSkillAdvanced(false);
                setEventBits( EVENT_BITS_forceRatingRedraw);
            }
        }
        else
        {
            if (m_playing == true && !m_countingIn) // playing along with the count-in is not wrong
            {
                goodSound = false;

                m_piano->addPianistNote(hand, inputNote, false);
                m_rating.wrongNotes(1);
                m_performance.recordWrong();
                setKeyState(inputNote.note(), CHighway::KEY_BAD);

                if (m_settings->followThroughErrors() && m_playMode == PB_PLAY_MODE_followYou) // If the setting is checked, errors cause following too
                  {
                    if (m_chordDeltaTime <= -m_cfg_playZoneEarly) // We're hitting bad notes, but earlier than the zone (so ignore them)
                      {
                    m_piano->clear();
                    m_savedNoteQueue->clear();
                    m_savedNoteOffQueue->clear();
                      }
                    else // Hitting bad notes within the zone (so register them)
                      {
                    // Register the wrong note in the ratings calculation (if not as missed notes, I don't believe it's factored in)
                    missedNotesColor(Cfg::pianoBadColor());
                    judgeRemainingNotes(Judgement::missed);
                    m_rating.lateNotes(m_wantedChord.length() - m_goodPlayedNotes.length());
                    setEventBits(EVENT_BITS_forceRatingRedraw);
                    fetchNextChord(); // Skip through the wrong note and continue to the next

                    // Was the next note the one keyed by accident (dyslexia)?  If so, validate it instead and continue as usual.
                    if (m_wantedChord.searchChord(inputNote.note(), m_transpose)) // replaces validatePianistNote ignoring out-of-zone
                      {
                        judgeGoodNote(inputNote.note());
                        m_goodPlayedNotes.addNote(hand, inputNote.note());
                        m_piano->addPianistNote(hand, inputNote,true);
                        qint64 pianistTiming;
                        if  ( ( cfg_timingMarkersFlag && m_followSkillAdvanced ) || m_playMode == PB_PLAY_MODE_rhythmTapping )
                          pianistTiming = m_pianistTiming;
                        else
                          pianistTiming = NOT_USED;
                        m_scoreWin->setPlayedNoteColor(inputNote.note(),
                                    (!m_followPlayingTimeOut)? Cfg::playedGoodColor():Cfg::playedBadColor(),
                            m_chordDeltaTime, pianistTiming);

                        if (validatePianistChord() == true)
                          {
                        if (m_chordDeltaTime < 0)
                          m_tempo.removePlayingTicks(-m_chordDeltaTime);

                        judgeRemainingNotes(Judgement::skipped);
                        m_goodPlayedNotes.clear();
                        fetchNextChord();
                        // count the good notes so that the live percentage looks OK
                        m_rating.totalNotes(m_wantedChord.length());
                        m_rating.calculateAccuracy();
                        m_settings->pianistActive();
                        if (m_rating.isAccuracyGood() || m_playMode == PB_PLAY_MODE_playAlong)
                          setFollowSkillAdvanced(true); // change the skill level only when they are good enough
                        else
                          setFollowSkillAdvanced(false);
                        setEventBits( EVENT_BITS_forceRatingRedraw);
                          }
                      }

                    // Clear & ignore any further slips until in range of the next note's zone
                    m_piano->clear();
                    m_savedNoteQueue->clear();
                    m_savedNoteOffQueue->clear();

                      }
                  }

            }
            else
            {
                m_piano->addPianistNote(hand, inputNote, true);
                setKeyState(inputNote.note(), CHighway::KEY_GOOD);
            }
        }
    }
    else if (inputNote.type() == MIDI_NOTE_OFF)
    {
        setKeyState(inputNote.note(), CHighway::KEY_UP);
        if (m_piano->removePianistNote(inputNote.note()) ==  true)
            goodSound = false;
        bool hasNote = m_goodPlayedNotes.removeNote(inputNote.note());

        if (hasNote)
            m_scoreWin->setPlayedNoteColor(inputNote.note(),
                    (!m_followPlayingTimeOut)? Cfg::noteColor():Cfg::playedStoppedColor(),
                    m_chordDeltaTime);

        outputSavedNotesOff();
    }

    if ( inputNote.velocity() == -1 )
        return;

    if (goodSound == true || m_cfg_wrongNoteSound < 0)
    {
        if (m_cfg_rightNoteSound >= 0) // don't play anything if the sound is set to -1 (none)
        {
            bool playDrumBeat = false;
            if ( inputNote.channel() != MIDI_DRUM_CHANNEL)
            {
                if (cfg_rhythmTapping != PB_RHYTHM_TAP_drumsOnly || m_playMode != PB_PLAY_MODE_rhythmTapping)
                {
                    inputNote.setChannel(m_pianistGoodChan);
                    playTrackEvent( inputNote );
                }
            }
            else
            {
                playDrumBeat = true;
            }

            if (cfg_rhythmTapping != PB_RHYTHM_TAP_mellodyOnly && m_playMode == PB_PLAY_MODE_rhythmTapping)
                playDrumBeat = true;

            if (playDrumBeat)
            {
                inputNote.setChannel(MIDI_DRUM_CHANNEL);
                ppLogTrace("note %d", inputNote.note());
                inputNote.setNote((hand == PB_PART_right)? m_cfg_rhythmTapRightHandDrumSound : m_cfg_rhythmTapLeftHandDrumSound);
                playTrackEvent( inputNote );
            }
        }
    }
    else
    {
        inputNote.setChannel(m_pianistBadChan);
        if (m_playMode == PB_PLAY_MODE_rhythmTapping)
        {
            inputNote.setChannel(MIDI_DRUM_CHANNEL);
            ppLogTrace("note %d", inputNote.note());
            inputNote.setNote((hand == PB_PART_right)? m_cfg_rhythmTapRightHandDrumSound : m_cfg_rhythmTapLeftHandDrumSound);
        }
        playTrackEvent( inputNote );
    }

    /*
    // use the same channel for the right and wrong note
    int pianoSound = (goodSound == true) ? m_cfg_rightNoteSound : m_cfg_wrongNoteSound;

    if (pianoSound != m_lastSound)
    {
        m_lastSound = pianoSound;

        CMidiEvent midiSound;
        midiSound.programChangeEvent(0,inputNote.channel(),pianoSound);
        playTrackEvent( midiSound );
    }
    */

}

void CConductor::addDeltaTime(qint64 ticks)
{
    m_scoreWin->scrollDeltaTime(ticks);
    m_playingDeltaTime += ticks;
    m_chordDeltaTime +=ticks;
}

void CConductor::followPlaying()
{
    if ( m_playMode == PB_PLAY_MODE_listen )
        return;

    if (m_wantedChord.length() == 0)
        fetchNextChord();

    if (m_wantedChord.length() == 0)
        return;

    if (seekingBarNumber())
    {
        if (deltaAdjustL(m_chordDeltaTime) > -m_stopPoint )
            fetchNextChord();
    }
    else if ( m_playMode == PB_PLAY_MODE_followYou ||  m_playMode == PB_PLAY_MODE_rhythmTapping )
    {
        if (deltaAdjustL(m_chordDeltaTime) > -m_cfg_earlyNotesPoint )
            m_followState = PB_FOLLOW_earlyNotes;
        if (deltaAdjustL(m_chordDeltaTime) > -m_stopPoint )
        {
            m_followState = PB_FOLLOW_waiting;
            // Throw away the time past the stop point (by adding a negative ticks)
            addDeltaTime( -m_stopPoint*SPEED_ADJUST_FACTOR - m_chordDeltaTime);
        }
    }
    else // m_playMode == PB_PLAY_MODE_playAlong
    {
        if (m_chordDeltaTime > m_cfg_playZoneLate )
        {
            missedNotesColor(Cfg::playedStoppedColor());
            judgeRemainingNotes(Judgement::missed);
            fetchNextChord();
            m_rating.lateNotes(m_wantedChord.length() - m_goodPlayedNotes.length());
            setEventBits( EVENT_BITS_forceRatingRedraw);
        }
    }
    if (deltaAdjustL(m_chordDeltaTime) > -m_cfg_earlyNotesPoint )
        turnOnKeyboardLights(true);
}

void CConductor::outputSavedNotesOff()
{
    while (m_savedNoteOffQueue->length() > 0)
        playTransposeEvent(m_savedNoteOffQueue->pop()); // Output the saved note off events
}

// untangle the sound in case there is any notes off just after we have stopped
void CConductor::findImminentNotesOff()
{
    CMidiEvent event = m_nextMidiEvent;
    int i = 0;
    qint64 aheadDelta = 0;

    while (deltaAdjustL(m_playingDeltaTime) + aheadDelta   > m_cfg_imminentNotesOffPoint)
    {
        if (event.type() == MIDI_NOTE_OFF )
            m_savedNoteOffQueue->push(event);
        if ( i >= m_songEventQueue->length())
            break;
        event = m_songEventQueue->index(i);
        aheadDelta -= event.deltaTime();
        i++;
    }
}

void CConductor::setKeyState(int note, CHighway::keyState_t state)
{
    if (m_scoreWin)
        m_scoreWin->getHighway()->setKeyState(note, state);
}

// A correct note of the wanted chord was pressed: judge its timing (once per note).
void CConductor::judgeGoodNote(int inputNote)
{
    setKeyState(inputNote, CHighway::KEY_GOOD);
    if (m_judgedNotes.searchChord(inputNote) || m_scoreWin == nullptr)
        return; // each note of the wanted chord is judged once, even if it is played again
    m_judgedNotes.addNote(PB_PART_both, inputNote);
    Judgement judgement;
    if (m_followPlayingTimeOut)
        judgement = m_performance.recordLateHit(); // the music had to stop and wait
    else
    {
        // m_pianistTiming was advanced before the input queue was read, so take off the time
        // the event spent waiting in the queue.
        const double offsetMs = m_tempo.ticksToMSec(m_pianistTiming) - static_cast<double>(m_inputAgeUs) / 1000.0
                                - m_timingCalibrationMs;
        judgement = m_performance.recordHit(offsetMs);
    }
    m_scoreWin->getHighway()->judgeNote(inputNote - m_transpose, judgement);
}

// The wanted chord is finished: judge the notes of it that were not played.
void CConductor::judgeRemainingNotes(Judgement judgement)
{
    for (int i = 0; i < m_wantedChord.length(); i++)
    {
        CNote note = m_wantedChord.getNote(i);
        if (m_judgedNotes.searchChord(note.pitch() + m_transpose) || m_scoreWin == nullptr) // judged notes are sounding pitches
            continue;
        if (judgement == Judgement::missed)
            m_performance.recordMissed();
        m_scoreWin->getHighway()->judgeNote(note.pitch(), judgement);
    }
}

void CConductor::missedNotesColor(CColor color)
{
    int i;
    CNote note;
    for (i = 0; i < m_wantedChord.length(); i++)
    {
        note = m_wantedChord.getNote(i);
        if (m_goodPlayedNotes.searchChord(note.pitch(),m_transpose) == false)
            m_scoreWin->setPlayedNoteColor(note.pitch() + m_transpose, color, m_chordDeltaTime);
    }
}

void CConductor::realTimeEngine(qint64 mSecTicks)
{
    auto ticks = m_tempo.mSecToTicks(mSecTicks);
    if (!m_followPlayingTimeOut)
        m_pianistTiming += ticks;

    static const bool logLatency = qEnvironmentVariableIsSet("PB_LATENCY_LOG");
    while (checkMidiInput() > 0)
    {
        CMidiEvent inputEvent = readMidiInput();
        m_inputAgeUs = lastInputAgeUs();
        m_lastPianistInputUs = monotonicTimeUs();
        if (logLatency && inputEvent.type() == MIDI_NOTE_ON)
        {
            fprintf(stderr, "PB_LATENCY input_age_us=%lld\n", static_cast<long long>(m_inputAgeUs));
            fprintf(stderr, "PB_NOTE t=%lld note=%d songBeat=%.3f wanted=%d chordDeltaMs=%.1f pianistTimingMs=%.1f\n",
                    static_cast<long long>(monotonicTimeUs() / 1000), inputEvent.note(),
                    static_cast<double>(m_scoreWin->getHighway()->now()) / (CMidiFile::getPulsesPerQuarterNote() * SPEED_ADJUST_FACTOR),
                    m_wantedChord.length() ? m_wantedChord.getNote(0).pitch() : -1,
                    m_tempo.ticksToMSec(m_chordDeltaTime), m_tempo.ticksToMSec(m_pianistTiming));
        }
        expandPianistInput(inputEvent);
    }
    m_inputAgeUs = 0;

    // count in once any seek to the start bar is done (seeking needs the engine to run)
    if (m_countingIn && m_playing && !seekingBarNumber() && countIn(ticks))
    {
        if (!m_followPlayingTimeOut)
            m_pianistTiming -= ticks; // the song time does not move during the count-in
        return;
    }

    if (getfollowState() == PB_FOLLOW_waiting )
    {
        if (m_silenceTimeOut > 0)
        {
            m_silenceTimeOut -= mSecTicks;
            if (m_silenceTimeOut <= 0)
            {
                allSoundOff();
                m_silenceTimeOut = 0;
            }
        }

        m_tempo.insertPlayingTicks(ticks);

        if (m_pianistTiming > m_cfg_playZoneLate)
        {
            if (m_followPlayingTimeOut == false)
            {
                m_followPlayingTimeOut = true;

                m_tempo.clearPlayingTicks();
                m_rating.lateNotes(m_wantedChord.length() - m_goodPlayedNotes.length());
                setEventBits( EVENT_BITS_forceRatingRedraw);

                missedNotesColor(Cfg::playedStoppedColor());
                findImminentNotesOff();
                // Don't keep any saved notes off if there are no notes down
                if (m_piano->pianistAllNotesDown() == 0)
                    outputSavedNotesOff();
                m_silenceTimeOut = Cfg::silenceTimeOut();
            }
        }
        return;
    }

    m_silenceTimeOut = 0;
    if (m_playing == false)
        return;

    if (seekingBarNumber())
        ticks = 0;

    m_tempo.adjustTempo(&ticks);

    ticks = m_bar.addDeltaTime(ticks);

    if (seekingBarNumber())
        ticks = m_bar.goToBarNumer();

    setEventBits( m_bar.readEventBits());
    metronomeTick();

#if OPTION_DEBUG_CONDUCTOR
    if (m_realTimeEventBits | EVENT_BITS_newBarNumber)
    {
        ppDEBUG_CONDUCTOR(("m_savedNoteQueue %d m_playingDeltaTime %d",m_savedNoteQueue->space() , m_playingDeltaTime ));
        ppDEBUG_CONDUCTOR(("getfollowState() %d  %d %d",getfollowState() , m_leadLagAdjust, m_songEventQueue->length() ));
    }
#endif

    addDeltaTime(ticks);

    followPlaying();
    int type;
    while ( m_playingDeltaTime >= m_leadLagAdjust)
    {
        type = m_nextMidiEvent.type();

        if (m_songEventQueue->length() == 0 && type == MIDI_PB_EOF)
        {
            ppLogInfo("The End of the song");
            setEventBits(EVENT_BITS_playingStopped);
            m_playing = false;
            break;
        }

        if (type == MIDI_PB_tempo)
        {
            m_tempo.setMidiTempo(m_nextMidiEvent.data1());
            m_leadLagAdjust = m_tempo.mSecToTicks( -getLatencyFix() );
        }
        else if (type == MIDI_PB_timeSignature)
        {
            m_bar.setTimeSig(m_nextMidiEvent.data1(), m_nextMidiEvent.data2());
            ppLogDebug("MIDI Time Signature %d/%d", m_nextMidiEvent.data1(),m_nextMidiEvent.data2());

        }
        else if ( type != MIDI_NONE )   // this marks the end of the piece of music
        {
            int channel = m_nextMidiEvent.channel();

            // Is this channel_muted
            if (!hasPianistKeyboardChannel(channel))
            {
                if (getfollowState() >= PB_FOLLOW_earlyNotes &&
                        (m_playMode == PB_PLAY_MODE_followYou || m_playMode == PB_PLAY_MODE_rhythmTapping) &&
                        !seekingBarNumber() &&
                        m_followSkillAdvanced == false)
                {
                    // Save up the notes until the pianist presses the right key
                    if (m_savedNoteQueue->space()>0)
                        m_savedNoteQueue->push(m_nextMidiEvent);
                    else
                        ppLogWarn("Warning the m_savedNoteQueue is full");

                    if (type == MIDI_NOTE_OFF)
                    {
                        if (m_savedNoteOffQueue->space()>0)
                            m_savedNoteOffQueue->push(m_nextMidiEvent);
                        else
                            ppLogDebug("Warning the m_savedNoteOffQueue is full");
                    }
                }
                else
                {
                    playTransposeEvent(m_nextMidiEvent); // Play the midi note or event
                    ppDEBUG_CONDUCTOR(("playEvent() chan %d type %d note %d", m_nextMidiEvent.channel() , m_nextMidiEvent.type() , m_nextMidiEvent.note(), m_songEventQueue->length() ));
                }
            }
        }
        if (m_songEventQueue->length() > 0)
            m_nextMidiEvent = m_songEventQueue->pop();
        else
        {
            ppDEBUG_CONDUCTOR(("no data in song queue"));
            m_nextMidiEvent.clear();
            break;
        }

        m_playingDeltaTime -= m_nextMidiEvent.deltaTime() * SPEED_ADJUST_FACTOR;
        followPlaying();
    }
}

void CConductor::metronomeClick(bool accent)
{
    CMidiEvent event;
    if (m_lastClickNote >= 0)
    {
        event.noteOffEvent(0, MIDI_DRUM_CHANNEL, m_lastClickNote, 0);
        playMidiEvent(event);
    }
    m_lastClickNote = accent ? 76 : 77; // GM hi / low wood block
    event.noteOnEvent(0, MIDI_DRUM_CHANNEL, m_lastClickNote, accent ? 115 : 80);
    playMidiEvent(event);
}

// Count one bar in before the music starts; returns true while still counting.
bool CConductor::countIn(qint64 ticks)
{
    // Use the tempo the song starts with, it is only read from the song when the music plays.
    for (int i = 0; i < m_songEventQueue->length() && i < 20; i++)
    {
        CMidiEvent event = m_songEventQueue->index(i);
        if (event.deltaTime() > 0)
            break;
        if (event.type() == MIDI_PB_tempo)
        {
            m_tempo.setMidiTempo(event.data1());
            break;
        }
    }

    m_countInTicks -= ticks;
    while (m_countInTicks <= 0)
    {
        if (m_countInBeatsLeft <= 0)
        {
            m_countingIn = false;
            if (qEnvironmentVariableIsSet("PB_LATENCY_LOG"))
                fprintf(stderr, "PB_MUSIC_START t=%lld\n", static_cast<long long>(monotonicTimeUs() / 1000));
            setEventBits(EVENT_BITS_forceFullRedraw);
            return false;
        }
        metronomeClick(m_countInBeatsLeft == m_bar.getTimeSigTop());
        m_countInBeatsLeft--;
        m_countInTicks += m_bar.getBeatLength() * SPEED_ADJUST_FACTOR;
    }
    return true;
}

void CConductor::metronomeTick()
{
    if (!m_cfg_metronome || seekingBarNumber())
        return;
    const int beat = m_bar.getBarNumber() * 64 + m_bar.getBeatNumber();
    if (beat == m_lastMetronomeBeat)
        return;
    m_lastMetronomeBeat = beat;
    metronomeClick(m_bar.getBeatNumber() == 0);
}

void CConductor::rewind()
{
    int chan;

    for ( chan = 0; chan < MAX_MIDI_CHANNELS; chan++)
    {
        m_savedMainVolume[chan] = 100;
    }
    m_lastSound = -1;
    m_rating.reset();
    m_performance.reset(m_playMode == PB_PLAY_MODE_playAlong);
    m_playingDeltaTime = 0;
    m_tempo.reset();

    m_songEventQueue->clear();
    m_savedNoteQueue->clear();
    m_savedNoteOffQueue->clear();
    m_wantedChordQueue->clear();
    m_nextMidiEvent.clear();
    m_bar.rewind();

    m_goodPlayedNotes.clear();  // The good notes the pianist plays
    m_judgedNotes.clear();
    if (m_piano)
        m_piano->clear();
    resetWantedChord();
    setFollowSkillAdvanced(false);

    m_cfg_earlyNotesPoint = CMidiFile::ppqnAdjust(15); // was 10 playZoneEarly
    m_cfg_stopPointBeginner = CMidiFile::ppqnAdjust(-0); //was -3; // stop just after the beat
    m_cfg_stopPointAdvanced = CMidiFile::ppqnAdjust(-15); //was -3; // stop just after the beat
    m_cfg_imminentNotesOffPoint = CMidiFile::ppqnAdjust(-15);  // look ahead and find an Notes off coming up

    // Annie song 25

    m_cfg_playZoneEarly = CMidiFile::ppqnAdjust(static_cast<float>(Cfg::playZoneEarly())) * SPEED_ADJUST_FACTOR; // when playing along
    m_cfg_playZoneLate = CMidiFile::ppqnAdjust(static_cast<float>(Cfg::playZoneLate())) * SPEED_ADJUST_FACTOR;
}

void CConductor::init2(CScore * scoreWin, CSettings* settings)
{
    m_scoreWin = scoreWin;
    m_settings = settings;
    setQSettings(settings);

    setFollowSkillAdvanced(false);

    m_followState = PB_FOLLOW_searching;
    this->CMidiDevice::init();

    assert(m_scoreWin);
    if (m_scoreWin)
    {
        m_scoreWin->setRatingObject(&m_rating);
        m_piano = m_scoreWin->getPianoObject();
    }

    rewind();
}
