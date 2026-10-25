/*********************************************************************************/
/*!
@file           KeyboardFit.cpp

@brief          Does the song fit on the pianist's MIDI keyboard?

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

#include "KeyboardFit.h"

#include <QRegularExpression>

#include <cstdlib>

namespace KeyboardFit {

int keysFromName(const QString &deviceName)
{
    // Well-known controllers whose names do not contain their number of keys. Names that do
    // (e.g. "Launchkey 49", "Keystation 61", "microKEY-37") are handled below.
    static const struct { const char *pattern; int keys; } models[] = {
        {"LPK25", 25},
        {"MPK mini", 25},
        {"MPKmini", 25},
        {"nanoKEY", 25},
        {"Launchkey Mini", 25},
        {"KeyStep Pro", 37},
        {"KeyStep", 32},
        {"MiniLab", 25},
        {"Digital Piano", 88},
        {"Clavinova", 88},
        {"P-45", 88}, {"P-125", 88}, {"P-145", 88}, {"P-225", 88}, {"P-515", 88},
        {"FP-10", 88}, {"FP-30", 88}, {"FP-60", 88}, {"FP-90", 88},
        {"CDP-", 88}, {"PX-S", 88}, {"Privia", 88},
        {"Kawai", 88}, {"ES110", 88}, {"ES120", 88}, {"ES920", 88},
    };
    for (const auto &model : models)
        if (deviceName.contains(QLatin1String(model.pattern), Qt::CaseInsensitive))
            return model.keys;

    // a usual keyboard size in the name, as its own number ("Keystation 61 MK3", "microKEY-37")
    static const QRegularExpression sizeInName("(?:^|[^0-9])(25|32|37|49|61|73|76|88)(?:$|[^0-9])");
    const QRegularExpressionMatch match = sizeInName.match(deviceName);
    if (match.hasMatch())
        return match.captured(1).toInt();
    return 0;
}

Keyboard identify(const QString &portName, int setupLowest, int setupHighest)
{
    Keyboard keyboard;
    keyboard.name = portName.section(" - ", 1); // the port index is not part of the name
    if (keyboard.name.isEmpty())
        keyboard.name = portName;

    if (setupLowest > 0 || setupHighest < 127) // the range from Setup / Keyboard
    {
        keyboard.keys = setupHighest - setupLowest + 1;
        keyboard.fixedLowest = setupLowest;
        return keyboard;
    }
    keyboard.keys = keysFromName(keyboard.name);
    keyboard.startsOnC = keyboard.keys != 88; // a full piano goes from A0 to C8
    if (keyboard.keys == 88)
        keyboard.fixedLowest = 21;
    return keyboard;
}

// the C at or below a note
static int cBelow(int note) { return note - ((note % 12) + 12) % 12; }

Result check(const Keyboard &keyboard, int low, int high)
{
    Result result;
    if (high < low)
        return result;
    result.keysNeeded = high - low + 1;
    if (keyboard.keys <= 0)
        return result;
    if (result.keysNeeded > keyboard.keys)
    {
        result.verdict = Verdict::tooWide;
        return result;
    }

    if (keyboard.fixedLowest >= 0) // the keys cannot move: transpose into the range if needed
    {
        const int fixedHighest = keyboard.fixedLowest + keyboard.keys - 1;
        result.lowestKey = keyboard.fixedLowest;
        if (low >= keyboard.fixedLowest && high <= fixedHighest)
            result.verdict = Verdict::fits;
        else
        {
            result.verdict = Verdict::transpose;
            result.transpose = (low < keyboard.fixedLowest) ? keyboard.fixedLowest - low : fixedHighest - high;
        }
        return result;
    }

    if (!keyboard.startsOnC)
    {
        result.verdict = Verdict::fits;
        result.lowestKey = low;
        return result;
    }

    // the octave buttons put the lowest key on any C: find a C window that covers the notes
    const int window = cBelow(low);
    if (high <= window + keyboard.keys - 1)
    {
        result.verdict = Verdict::fits;
        result.lowestKey = window;
        return result;
    }
    // it would fit, but not between two Cs: the smallest transposition (up or down) that does
    for (int shift = 1; shift < 12; shift++)
        for (int sign : {-1, 1})
        {
            const int t = sign * shift;
            const int start = cBelow(low + t);
            if (high + t <= start + keyboard.keys - 1)
            {
                result.verdict = Verdict::transpose;
                result.transpose = t;
                result.lowestKey = start;
                return result;
            }
        }
    result.verdict = Verdict::tooWide; // cannot happen for keys >= 13, kept for safety
    return result;
}

QString noteName(int note)
{
    static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0 || note > 127)
        return QString();
    return QString("%1%2").arg(names[note % 12]).arg(note / 12 - 1);
}

} // namespace KeyboardFit
