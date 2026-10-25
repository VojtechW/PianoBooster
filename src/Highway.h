/*********************************************************************************/
/*!
@file           Highway.h

@brief          The falling-notes ("note highway") view with an on-screen keyboard.

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

#ifndef __HIGHWAY_H__
#define __HIGHWAY_H__

#include <vector>

#include "MidiEvent.h"
#include "Chord.h"
#include "Performance.h"

/*!
 * The whole song's notes are collected once when it is loaded (absolute times in the same
 * tick units the scroll uses), and the current position ("now") follows the score scroll,
 * so rewind, looping, seeking and "wait for me" behave exactly like the notation view.
 */
class CHighway
{
public:
    CHighway();

    // song loading (called with every event of the file, in order)
    void beginSong();
    void collectEvent(CMidiEvent event);
    void endSong();

    //! back to the start of the song: clear the position and all judgements
    void reset();
    void scrollDeltaTime(qint64 ticks) { m_now += ticks; }
    qint64 now() const { return m_now; }

    void setTranspose(int transpose) { m_transpose = transpose; m_rangeDirty = true; }
    void setActiveChannel(int channel) { m_activeChannel = channel; m_rangeDirty = true; }
    //! the lowest/highest note of the pianist's keyboard (0/127 = unknown)
    void setKeyboardRange(int lowest, int highest);

    //! a wanted note (untransposed pitch) was judged by the conductor
    void judgeNote(int pitch, Judgement judgement);
    //! the pianist pressed (good/bad) or released (up) a key (sounding pitch)
    enum keyState_t { KEY_UP = 0, KEY_GOOD, KEY_BAD };
    void setKeyState(int pitch, keyState_t state);

    //! number of quarter notes visible above the hit line
    void setLookAheadBeats(float beats);
    float lookAheadBeats() const { return m_lookAheadBeats; }

    //! draw into the current GL context with an orthographic projection of width x height pixels
    void draw(float width, float height, float topMargin, CChord wantedChord);

    // geometry of the last draw, used by the view to place text
    float hitLineY() const { return m_hitY; }
    float keyCenterX(int pitch) const;
    int lowestKey() const { return m_lowKey; }
    int highestKey() const { return m_highKey; }
    static bool isBlackKey(int pitch);

    //! where to write the names of the notes visible in the last draw
    struct Label { float x; float y; int pitch; };
    const std::vector<Label> &labels() const { return m_labels; }

    // bar positions (bars counted from 0 at the start of the song, as CBar does)
    qint64 barToTicks(double bar) const;
    double ticksToBar(qint64 ticks) const;
    qint64 songLength() const { return m_songLength; }
    //! draw the song progress strip with the loop region; dragFrom/dragTo < 0 when not dragging
    void drawProgressBar(float x0, float y0, float x1, float y1, double loopFrom, double loopBars,
                         double dragFrom, double dragTo);

    struct Flash { int pitch; Judgement judgement; qint64 timeUs; };
    //! the most recent judgement (for the popup text)
    const Flash &lastFlash() const { return m_lastFlash; }

private:
    struct Note
    {
        qint64 start;
        qint64 end;
        unsigned char pitch;
        unsigned char channel;
        unsigned char track;
        Judgement judgement;
    };
    struct TimeSig { qint64 start; int top; int bottom; };

    whichPart_t handOf(const Note &note) const;
    void updateRange();
    void layoutKeys(float width);
    void drawKeyboard(CChord &wantedChord);
    void drawBeatLines(float width, float top);

    std::vector<Note> m_notes;          // sorted by start
    std::vector<TimeSig> m_timeSigs;
    std::vector<Label> m_labels;
    qint64 m_songLength = 0;
    qint64 m_maxNoteLength;
    qint64 m_collectTime;
    qint64 m_openNote[MAX_MIDI_CHANNELS][MAX_MIDI_NOTES]; // index into m_notes of sounding notes, -1 if none

    qint64 m_now;
    int m_transpose;
    int m_activeChannel;
    int m_keyboardLowest;
    int m_keyboardHighest;
    float m_lookAheadBeats;

    bool m_rangeDirty;
    int m_lowKey;
    int m_highKey;
    float m_layoutWidth;
    float m_keyX0[MAX_MIDI_NOTES];
    float m_keyX1[MAX_MIDI_NOTES];
    float m_hitY;
    float m_keyboardHeight;

    keyState_t m_keyState[MAX_MIDI_NOTES];
    Flash m_keyFlash[MAX_MIDI_NOTES];
    Flash m_lastFlash;
};

#endif // __HIGHWAY_H__
