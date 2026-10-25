/*********************************************************************************/
/*!
@file           KeyboardFit.h

@brief          Does the song fit on the pianist's MIDI keyboard?

    Small controllers (e.g. a 25-key Akai LPK25) only cover two octaves at a time and move by
    octaves with their octave buttons, so the keys they reach always start on a C. This works out
    the size of the keyboard (from the keyboard setup, or from the device name) and whether, and
    how, the notes of the part being played fit on it: as they are, after an octave shift, after
    transposing, or only one hand at a time.

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

#ifndef __KEYBOARD_FIT_H__
#define __KEYBOARD_FIT_H__

#include <QString>

namespace KeyboardFit
{
    //! What is known about the pianist's keyboard
    struct Keyboard
    {
        int keys = 0;          //!< number of keys, 0 = unknown
        int fixedLowest = -1;  //!< lowest note if the range is fixed (from the keyboard setup), else -1
        bool startsOnC = true; //!< the lowest key is a C (true for controllers; an 88-key piano starts on A)
        QString name;          //!< for messages, e.g. "LPK25"
    };

    //! The keyboard size from the device (MIDI port) name, e.g. "0 - LPK25" -> 25 keys.
    //! The keyboard setup range (lowest/highest note, 0/127 = not set) wins when it is set.
    Keyboard identify(const QString &portName, int setupLowest, int setupHighest);

    //! The number of keys of a known keyboard model, or a number of keys in the name (0 = unknown)
    int keysFromName(const QString &deviceName);

    enum class Verdict
    {
        unknown,     //!< the keyboard size or the song's notes are not known
        fits,        //!< fits; `lowestKey` is where the keyboard's lowest key must be
        transpose,   //!< fits after transposing by `transpose` semitones
        tooWide      //!< needs more keys than the keyboard has
    };

    struct Result
    {
        Verdict verdict = Verdict::unknown;
        int keysNeeded = 0;  //!< from the lowest to the highest note, inclusive
        int lowestKey = -1;  //!< the note the keyboard's lowest key should play (fits/transpose)
        int transpose = 0;   //!< extra semitones of transposition needed (transpose)
    };

    //! Where the notes from `low` to `high` (sounding pitches) fit on the keyboard, if at all
    Result check(const Keyboard &keyboard, int low, int high);

    //! "C4" for MIDI note 60 (the same names as the on-screen keyboard)
    QString noteName(int note);
}

#endif // __KEYBOARD_FIT_H__
