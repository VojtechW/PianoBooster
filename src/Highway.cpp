/*********************************************************************************/
/*!
@file           Highway.cpp

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

#ifdef __APPLE__
  #include <OpenGL/gl.h>
#else
  #include <GL/gl.h>
#endif

#include <algorithm>
#include <cmath>

#include "Highway.h"
#include "MidiFile.h"

namespace {

struct Rgba { float r, g, b, a; };

// Melodics-like palette: left hand blue, right hand yellow, results green/orange/purple/red
constexpr Rgba LEFT_HAND_COLOR    {0.30f, 0.60f, 0.98f, 1.0f};
constexpr Rgba RIGHT_HAND_COLOR   {0.99f, 0.78f, 0.22f, 1.0f};
constexpr Rgba OTHER_PART_COLOR   {0.45f, 0.47f, 0.52f, 0.55f};
constexpr Rgba PERFECT_COLOR      {0.25f, 0.88f, 0.42f, 1.0f};
constexpr Rgba GOOD_COLOR         {0.62f, 0.90f, 0.35f, 1.0f};
constexpr Rgba EARLY_COLOR        {1.00f, 0.58f, 0.18f, 1.0f};
constexpr Rgba LATE_COLOR         {0.72f, 0.45f, 1.00f, 1.0f};
constexpr Rgba MISSED_COLOR       {0.92f, 0.22f, 0.25f, 1.0f};
constexpr Rgba SKIPPED_COLOR      {0.40f, 0.42f, 0.46f, 0.8f};
constexpr Rgba BACKGROUND_COLOR   {0.07f, 0.08f, 0.11f, 1.0f};
constexpr Rgba BLACK_LANE_COLOR   {0.05f, 0.055f, 0.075f, 1.0f};
constexpr Rgba OCTAVE_LINE_COLOR  {0.20f, 0.22f, 0.28f, 1.0f};
constexpr Rgba BEAT_LINE_COLOR    {0.15f, 0.16f, 0.21f, 1.0f};
constexpr Rgba BAR_LINE_COLOR     {0.30f, 0.32f, 0.40f, 1.0f};
constexpr Rgba HIT_LINE_COLOR     {0.85f, 0.88f, 1.00f, 1.0f};
constexpr Rgba KEY_GOOD_COLOR     {0.25f, 0.88f, 0.42f, 1.0f};
constexpr Rgba KEY_BAD_COLOR      {0.95f, 0.25f, 0.25f, 1.0f};

constexpr qint64 FLASH_DURATION_US = 450000;

inline void color(const Rgba &c, float alpha = 1.0f) { glColor4f(c.r, c.g, c.b, c.a * alpha); }
inline Rgba mix(const Rgba &a, const Rgba &b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
// must be called between glBegin(GL_QUADS) / glEnd()
inline void quad(float x0, float y0, float x1, float y1)
{
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
}

const Rgba &judgementColor(Judgement judgement)
{
    switch (judgement)
    {
    case Judgement::perfect: return PERFECT_COLOR;
    case Judgement::good:    return GOOD_COLOR;
    case Judgement::early:   return EARLY_COLOR;
    case Judgement::late:    return LATE_COLOR;
    case Judgement::missed:  return MISSED_COLOR;
    default:                 return SKIPPED_COLOR;
    }
}

// a filled rectangle with rounded corners, as triangles (between glBegin(GL_TRIANGLES) / glEnd())
void roundedRect(float x0, float y0, float x1, float y1, float r)
{
    auto tri = [](float ax, float ay, float bx, float by, float cx, float cy) {
        glVertex2f(ax, ay); glVertex2f(bx, by); glVertex2f(cx, cy);
    };
    auto rect = [&](float a0, float b0, float a1, float b1) {
        tri(a0, b0, a1, b0, a1, b1);
        tri(a0, b0, a1, b1, a0, b1);
    };
    r = std::max(0.0f, std::min(r, std::min((x1 - x0) / 2, (y1 - y0) / 2)));
    if (r < 0.75f)
    {
        rect(x0, y0, x1, y1);
        return;
    }
    rect(x0 + r, y0, x1 - r, y1);          // middle column
    rect(x0, y0 + r, x0 + r, y1 - r);      // left
    rect(x1 - r, y0 + r, x1, y1 - r);      // right
    static const float cosTable[5] = {1.0f, 0.92388f, 0.70711f, 0.38268f, 0.0f};
    const float cx[4] = {x1 - r, x0 + r, x0 + r, x1 - r};
    const float cy[4] = {y1 - r, y1 - r, y0 + r, y0 + r};
    const float sx[4] = {1, -1, -1, 1};
    const float sy[4] = {1, 1, -1, -1};
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            tri(cx[c], cy[c],
                cx[c] + sx[c] * r * cosTable[i], cy[c] + sy[c] * r * cosTable[4 - i],
                cx[c] + sx[c] * r * cosTable[i + 1], cy[c] + sy[c] * r * cosTable[3 - i]);
}

// a radial glow: opaque-ish centre fading to nothing (between glBegin(GL_TRIANGLES) / glEnd())
void glowFan(float x, float y, float radius, const Rgba &c, float alpha)
{
    constexpr int SEGMENTS = 20;
    for (int i = 0; i < SEGMENTS; i++)
    {
        const float a0 = 6.2832f * static_cast<float>(i) / SEGMENTS;
        const float a1 = 6.2832f * static_cast<float>(i + 1) / SEGMENTS;
        glColor4f(c.r, c.g, c.b, alpha);
        glVertex2f(x, y);
        glColor4f(c.r, c.g, c.b, 0.0f);
        glVertex2f(x + radius * std::cos(a0), y + radius * 0.6f * std::sin(a0));
        glVertex2f(x + radius * std::cos(a1), y + radius * 0.6f * std::sin(a1));
    }
}

qint64 quarterTicks() { return static_cast<qint64>(CMidiFile::getPulsesPerQuarterNote()) * SPEED_ADJUST_FACTOR; }

} // namespace

CHighway::CHighway()
{
    m_transpose = 0;
    m_activeChannel = 0;
    m_keyboardLowest = 0;
    m_keyboardHighest = 127;
    m_lookAheadBeats = 6.0f;
    m_layoutWidth = 0;
    m_hitY = 0;
    m_keyboardHeight = 0;
    m_lowKey = 48;
    m_highKey = 83;
    for (int i = 0; i < MAX_MIDI_NOTES; i++)
        m_keyState[i] = KEY_UP;
    beginSong();
    endSong();
}

void CHighway::beginSong()
{
    m_notes.clear();
    m_timeSigs.clear();
    m_maxNoteLength = 0;
    m_collectTime = 0;
    for (auto &channel : m_openNote)
        for (auto &index : channel)
            index = -1;
}

// Builds the note list: a note-on opens a note on its channel and pitch, the next note-off (or
// note-on) of the same key closes it. Times are absolute song ticks.
void CHighway::collectEvent(CMidiEvent event)
{
    m_collectTime += static_cast<qint64>(event.deltaTime()) * SPEED_ADJUST_FACTOR;
    const int type = event.type();
    if (type == MIDI_PB_timeSignature)
    {
        if (event.data1() > 0 && event.data2() > 0)
            m_timeSigs.push_back({m_collectTime, event.data1(), event.data2()});
        return;
    }
    if (type != MIDI_NOTE_ON && type != MIDI_NOTE_OFF)
        return;
    const int channel = event.channel();
    const int pitch = event.note();
    if (channel < 0 || channel >= MAX_MIDI_CHANNELS || channel == MIDI_DRUM_CHANNEL || pitch < 0 || pitch >= MAX_MIDI_NOTES)
        return;

    qint64 &open = m_openNote[channel][pitch];
    if (open >= 0) // close the sounding note (also when a note is re-struck without a note off)
    {
        Note &note = m_notes[static_cast<size_t>(open)];
        note.end = std::max(m_collectTime, note.start + 1);
        m_maxNoteLength = std::max(m_maxNoteLength, note.end - note.start);
        open = -1;
    }
    if (type == MIDI_NOTE_ON && event.velocity() > 0)
    {
        open = static_cast<qint64>(m_notes.size());
        m_notes.push_back({m_collectTime, m_collectTime, static_cast<unsigned char>(pitch),
                           static_cast<unsigned char>(channel), static_cast<unsigned char>(event.track() & 0xff),
                           Judgement::pending});
    }
}

void CHighway::endSong()
{
    for (auto &channel : m_openNote)
        for (auto &index : channel)
        {
            if (index >= 0)
            {
                // a note that is never switched off: give it a bar, not the rest of the song
                // (a huge length would make every frame scan the notes from the start)
                Note &note = m_notes[static_cast<size_t>(index)];
                note.end = std::max(std::min(m_collectTime, note.start + 4 * quarterTicks()), note.start + 1);
                m_maxNoteLength = std::max(m_maxNoteLength, note.end - note.start);
            }
            index = -1;
        }
    // events arrive in time order, but keep this robust
    std::stable_sort(m_notes.begin(), m_notes.end(), [](const Note &a, const Note &b) { return a.start < b.start; });
    if (m_timeSigs.empty() || m_timeSigs.front().start > 0)
        m_timeSigs.insert(m_timeSigs.begin(), {0, 4, 4});
    m_songLength = m_collectTime;
    m_rangeDirty = true;
    reset();
}

qint64 CHighway::barToTicks(double bar) const
{
    double barsBefore = 0;
    for (size_t i = 0; i < m_timeSigs.size(); i++)
    {
        const TimeSig &sig = m_timeSigs[i];
        const double barTicks = static_cast<double>(quarterTicks() * 4 / sig.bottom * sig.top);
        const bool last = i + 1 == m_timeSigs.size();
        const double segmentBars = last ? 1e12 : static_cast<double>(m_timeSigs[i + 1].start - sig.start) / barTicks;
        if (bar < barsBefore + segmentBars)
            return sig.start + static_cast<qint64>((bar - barsBefore) * barTicks);
        barsBefore += segmentBars;
    }
    return 0;
}

double CHighway::ticksToBar(qint64 ticks) const
{
    double barsBefore = 0;
    for (size_t i = 0; i < m_timeSigs.size(); i++)
    {
        const TimeSig &sig = m_timeSigs[i];
        const double barTicks = static_cast<double>(quarterTicks() * 4 / sig.bottom * sig.top);
        const bool last = i + 1 == m_timeSigs.size();
        if (last || ticks < m_timeSigs[i + 1].start)
            return barsBefore + static_cast<double>(ticks - sig.start) / barTicks;
        barsBefore += static_cast<double>(m_timeSigs[i + 1].start - sig.start) / barTicks;
    }
    return 0;
}

void CHighway::drawProgressBar(float x0, float y0, float x1, float y1, double loopFrom, double loopBars,
                               double dragFrom, double dragTo)
{
    if (m_songLength <= 0)
        return;
    const double scale = static_cast<double>(x1 - x0) / static_cast<double>(m_songLength);
    auto xOf = [&](qint64 ticks) { return x0 + static_cast<float>(static_cast<double>(std::max<qint64>(0, ticks)) * scale); };
    const float played = std::min(x1, xOf(m_now));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4f(1, 1, 1, 0.08f);
    quad(x0, y0, x1, y1);
    glColor4f(0.22f, 0.57f, 1.0f, 0.9f);
    quad(x0, y0, played, y1);

    // bar ticks (every bar, or every 4th when they get too dense)
    const double totalBars = ticksToBar(m_songLength);
    const int step = ((x1 - x0) / std::max(1.0, totalBars) < 5.0f) ? 4 : 1;
    for (int bar = 0; bar <= static_cast<int>(totalBars); bar += step)
    {
        const float x = xOf(barToTicks(bar));
        glColor4f(1, 1, 1, x < played ? 0.45f : 0.25f);
        quad(x - 0.5f, y0, x + 0.5f, y1);
    }

    // the loop region, or the one being dragged out
    double from = loopFrom, bars = loopBars;
    if (dragFrom >= 0)
    {
        from = std::min(dragFrom, dragTo);
        bars = std::fabs(dragTo - dragFrom) + 1;
    }
    if (bars > 0)
    {
        const float lx0 = xOf(barToTicks(from));
        const float lx1 = std::min(x1, xOf(barToTicks(from + bars)));
        glColor4f(1.0f, 0.22f, 0.73f, 0.35f);
        quad(lx0, y0 - 3, lx1, y1 + 3);
        glColor4f(1.0f, 0.22f, 0.73f, 1.0f);
        quad(lx0, y0 - 3, lx0 + 2, y1 + 3);
        quad(lx1 - 2, y0 - 3, lx1, y1 + 3);
    }
    else if (loopFrom > 0)
    {
        const float sx = xOf(barToTicks(loopFrom));
        glColor4f(1.0f, 0.22f, 0.73f, 1.0f);
        quad(sx - 1, y0 - 3, sx + 1, y1 + 3);
    }
    // play head
    glColor4f(1, 1, 1, 1);
    quad(played - 1, y0 - 2, played + 1, y1 + 2);
    glEnd();
    glDisable(GL_BLEND);
}

void CHighway::reset()
{
    m_now = 0;
    for (auto &note : m_notes)
        note.judgement = Judgement::pending;
    for (int i = 0; i < MAX_MIDI_NOTES; i++)
        m_keyFlash[i] = {i, Judgement::pending, 0};
    m_lastFlash = {0, Judgement::pending, 0};
}

bool CHighway::partRange(whichPart_t hand, int *low, int *high) const
{
    *low = 128;
    *high = -1;
    for (const auto &note : m_notes)
    {
        const whichPart_t noteHand = handOf(note);
        if (noteHand == PB_PART_none || (hand != PB_PART_both && noteHand != hand))
            continue;
        *low = std::min(*low, note.pitch + m_transpose);
        *high = std::max(*high, note.pitch + m_transpose);
    }
    return *high >= 0;
}

void CHighway::setKeyboardRange(int lowest, int highest)
{
    m_keyboardLowest = lowest;
    m_keyboardHighest = highest;
    m_rangeDirty = true;
}

void CHighway::setLookAheadBeats(float beats)
{
    m_lookAheadBeats = std::min(16.0f, std::max(2.0f, beats));
}

whichPart_t CHighway::handOf(const Note &note) const
{
    CMidiEvent event;
    event.noteOnEvent(0, note.channel, note.pitch, 64);
    event.setTrack(note.track);
    return CNote::findHand(event, m_activeChannel, PB_PART_both);
}

bool CHighway::isBlackKey(int pitch)
{
    switch (((pitch % 12) + 12) % 12)
    {
    case 1: case 3: case 6: case 8: case 10: return true;
    default: return false;
    }
}

void CHighway::judgeNote(int pitch, Judgement judgement)
{
    // find the closest pending note of the pianist's part with this pitch
    const qint64 window = 2 * quarterTicks();
    auto first = std::lower_bound(m_notes.begin(), m_notes.end(), m_now - window,
                                  [](const Note &note, qint64 time) { return note.start < time; });
    Note *best = nullptr;
    qint64 bestDistance = 0;
    for (auto it = first; it != m_notes.end() && it->start <= m_now + window; ++it)
    {
        if (it->pitch != pitch || it->judgement != Judgement::pending || handOf(*it) == PB_PART_none)
            continue;
        const qint64 distance = std::llabs(it->start - m_now);
        if (best == nullptr || distance < bestDistance)
        {
            best = &*it;
            bestDistance = distance;
        }
    }
    if (best)
        best->judgement = judgement;

    if (judgement == Judgement::skipped)
        return;
    const int key = pitch + m_transpose;
    if (key >= 0 && key < MAX_MIDI_NOTES)
    {
        m_keyFlash[key] = {key, judgement, monotonicTimeUs()};
        m_lastFlash = m_keyFlash[key];
    }
}

void CHighway::setKeyState(int pitch, keyState_t state)
{
    if (pitch >= 0 && pitch < MAX_MIDI_NOTES)
        m_keyState[pitch] = state;
}

// Choose the keys to show: the range of the pianist's part (transposed) widened to whole octaves
// and at least two of them, or the pianist's whole keyboard if it is known and the song fits on it.
void CHighway::updateRange()
{
    int low = 128, high = -1;
    for (const auto &note : m_notes)
    {
        if (handOf(note) == PB_PART_none)
            continue;
        low = std::min(low, note.pitch + m_transpose);
        high = std::max(high, note.pitch + m_transpose);
    }
    if (high < 0)
    {
        low = MIDDLE_C - 12;
        high = MIDDLE_C + 11;
    }
    // Show the pianist's whole keyboard when it is known and the song fits on it.
    const bool keyboardKnown = m_keyboardLowest > 0 || m_keyboardHighest < 127;
    if (keyboardKnown && low >= m_keyboardLowest && high <= m_keyboardHighest)
    {
        low = m_keyboardLowest;
        high = m_keyboardHighest;
    }
    else
    {
        low -= low % 12;              // down to a C
        high += 11 - (high % 12);     // up to a B
        while (high - low < 2 * 12 - 1) // at least two octaves
        {
            if (low >= 12 && (MIDDLE_C - low) > (high - MIDDLE_C))
                low -= 12;
            else
                high += 12;
        }
    }
    low = std::max(0, low);
    high = std::min(MAX_MIDI_NOTES - 1, high);
    if (isBlackKey(low)) low--;
    if (isBlackKey(high)) high++;
    m_lowKey = low;
    m_highKey = high;
    m_rangeDirty = false;
    m_layoutWidth = -1;
}

// Horizontal geometry: the white keys share the width equally, each black key sits centred on the
// boundary between its two white neighbours.
void CHighway::layoutKeys(float width)
{
    int whiteCount = 0;
    for (int pitch = m_lowKey; pitch <= m_highKey; pitch++)
        if (!isBlackKey(pitch))
            whiteCount++;
    const float whiteWidth = width / static_cast<float>(std::max(1, whiteCount));
    const float blackWidth = whiteWidth * 0.62f;
    int whiteIndex = 0;
    for (int pitch = 0; pitch < MAX_MIDI_NOTES; pitch++)
        m_keyX0[pitch] = m_keyX1[pitch] = -1;
    for (int pitch = m_lowKey; pitch <= m_highKey; pitch++)
    {
        if (isBlackKey(pitch))
        {
            const float center = static_cast<float>(whiteIndex) * whiteWidth;
            m_keyX0[pitch] = center - blackWidth / 2;
            m_keyX1[pitch] = center + blackWidth / 2;
        }
        else
        {
            m_keyX0[pitch] = static_cast<float>(whiteIndex) * whiteWidth;
            m_keyX1[pitch] = m_keyX0[pitch] + whiteWidth;
            whiteIndex++;
        }
    }
    m_layoutWidth = width;
}

float CHighway::keyCenterX(int pitch) const
{
    if (pitch < m_lowKey || pitch > m_highKey)
        return -1;
    return (m_keyX0[pitch] + m_keyX1[pitch]) / 2;
}

// One frame: background lanes, beat and bar lines, the falling notes (only those in the visible time
// window, found by binary search), the hit line with glows, and the keyboard. Everything is batched
// into a handful of glBegin/glEnd blocks; nothing is allocated per frame (m_labels keeps its capacity).
void CHighway::draw(float width, float height, float topMargin, CChord wantedChord)
{
    if (m_rangeDirty)
        updateRange();
    if (width != m_layoutWidth)
        layoutKeys(width);

    m_keyboardHeight = std::max(70.0f, std::min(height * 0.17f, (width / 52.0f) * 6.5f));
    m_hitY = m_keyboardHeight;
    const float top = height - topMargin;
    const qint64 lookAhead = static_cast<qint64>(static_cast<double>(m_lookAheadBeats) * static_cast<double>(quarterTicks()));
    const double pixelsPerTick = static_cast<double>(top - m_hitY) / static_cast<double>(std::max<qint64>(1, lookAhead));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    // background and the darker lanes of the black keys
    color(BACKGROUND_COLOR);
    quad(0, m_hitY, width, top);
    color(BLACK_LANE_COLOR);
    for (int pitch = m_lowKey; pitch <= m_highKey; pitch++)
        if (isBlackKey(pitch))
            quad(m_keyX0[pitch], m_hitY, m_keyX1[pitch], top);
    // octave lines at every C and a fainter line between E and F
    for (int pitch = m_lowKey; pitch <= m_highKey; pitch++)
    {
        if (pitch % 12 == 0)
            color(OCTAVE_LINE_COLOR);
        else if (pitch % 12 == 5)
            color(BEAT_LINE_COLOR);
        else
            continue;
        quad(m_keyX0[pitch] - 0.5f, m_hitY, m_keyX0[pitch] + 0.5f, top);
    }
    glEnd();

    drawBeatLines(width, top);

    // the falling notes (rounded rectangles, as triangles so the whole lot is one batch)
    const qint64 windowStart = m_now - m_maxNoteLength;
    const qint64 windowEnd = m_now + lookAhead;
    auto first = std::lower_bound(m_notes.begin(), m_notes.end(), windowStart,
                                  [](const Note &note, qint64 time) { return note.start < time; });
    const whichPart_t activeHand = CNote::getActiveHand();
    m_labels.clear();
    int glowCount = 0;
    struct Glow { float x; Rgba color; } glows[16];
    glBegin(GL_TRIANGLES);
    for (auto it = first; it != m_notes.end() && it->start <= windowEnd; ++it)
    {
        if (it->end <= m_now)
            continue;
        const int key = it->pitch + m_transpose;
        if (key < m_lowKey || key > m_highKey)
            continue;
        const whichPart_t hand = handOf(*it);
        if (hand == PB_PART_none)
            continue;
        float y0 = m_hitY + static_cast<float>(static_cast<double>(it->start - m_now) * pixelsPerTick);
        float y1 = m_hitY + static_cast<float>(static_cast<double>(it->end - m_now) * pixelsPerTick) - 2.0f; // gap between repeated notes
        y1 = std::max(y1, y0 + 6.0f); // very short notes stay visible
        const bool sounding = y0 <= m_hitY;
        y0 = std::max(y0, m_hitY);
        y1 = std::min(y1, top);
        if (y1 <= y0)
            continue;

        const bool played = activeHand == PB_PART_both || hand == activeHand;
        Rgba fill = played ? (hand == PB_PART_left ? LEFT_HAND_COLOR : RIGHT_HAND_COLOR) : OTHER_PART_COLOR;
        if (isBlackKey(key))
            fill = mix(fill, {0, 0, 0, fill.a}, 0.28f); // sharps in a darker shade, as on the keyboard
        if (it->judgement != Judgement::pending)
            fill = judgementColor(it->judgement);
        const float x0 = m_keyX0[key] + 1.0f;
        const float x1 = m_keyX1[key] - 1.0f;
        color(fill);
        roundedRect(x0, y0, x1, y1, std::min((x1 - x0) * 0.22f, (y1 - y0) / 2.0f));
        // a lighter cap on the leading edge
        color(mix(fill, {1, 1, 1, fill.a}, 0.35f));
        if (y1 - y0 > 8 && !sounding)
            roundedRect(x0 + 2, y0 + 2, x1 - 2, y0 + 4, 1.0f);

        if (sounding && played && glowCount < 16)
            glows[glowCount++] = {(x0 + x1) / 2, fill};
        if (y1 - y0 >= 16 && m_labels.size() < 64)
            m_labels.push_back({(x0 + x1) / 2, std::max(y0, m_hitY) + 4, key});
    }
    glEnd();

    // the hit line, and a glow where notes are being played
    glBegin(GL_QUADS);
    color(HIT_LINE_COLOR, 0.12f);
    quad(0, m_hitY, width, m_hitY + 10);
    color(HIT_LINE_COLOR, 0.9f);
    quad(0, m_hitY, width, m_hitY + 2);
    glEnd();
    if (glowCount > 0)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive
        const float radius = std::max(28.0f, (m_keyX1[m_lowKey] - m_keyX0[m_lowKey]) * 2.2f);
        const float pulse = 0.85f + 0.15f * std::sin(static_cast<float>(monotonicTimeUs() % 1000000) * 6.2832e-6f * 2.0f);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < glowCount; i++)
            glowFan(glows[i].x, m_hitY, radius, glows[i].color, 0.45f * pulse);
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    drawKeyboard(wantedChord);
    glDisable(GL_BLEND);
}

// Horizontal lines for every beat (brighter for the first beat of a bar), walking through the time
// signature changes that overlap the visible window.
void CHighway::drawBeatLines(float width, float top)
{
    const qint64 lookAhead = static_cast<qint64>(static_cast<double>(m_lookAheadBeats) * static_cast<double>(quarterTicks()));
    const double pixelsPerTick = static_cast<double>(top - m_hitY) / static_cast<double>(std::max<qint64>(1, lookAhead));
    const qint64 windowStart = std::max<qint64>(0, m_now);
    const qint64 windowEnd = m_now + lookAhead;

    glBegin(GL_QUADS);
    for (size_t i = 0; i < m_timeSigs.size(); i++)
    {
        const TimeSig &sig = m_timeSigs[i];
        const qint64 segmentEnd = (i + 1 < m_timeSigs.size()) ? m_timeSigs[i + 1].start : windowEnd + 1;
        if (segmentEnd <= windowStart || sig.start > windowEnd)
            continue;
        const qint64 beatTicks = std::max<qint64>(1, quarterTicks() * 4 / sig.bottom);
        qint64 beat = (windowStart > sig.start) ? (windowStart - sig.start + beatTicks - 1) / beatTicks : 0;
        for (qint64 t = sig.start + beat * beatTicks; t < segmentEnd && t <= windowEnd; t += beatTicks, beat++)
        {
            const float y = m_hitY + static_cast<float>(static_cast<double>(t - m_now) * pixelsPerTick);
            const bool barLine = (beat % sig.top) == 0;
            color(barLine ? BAR_LINE_COLOR : BEAT_LINE_COLOR);
            const float half = barLine ? 1.0f : 0.5f;
            quad(0, y - half, width, y + half);
        }
    }
    glEnd();
}

// The keyboard: keys held by the pianist are green (right) or red (wrong), the keys of the chord to
// play next are tinted in their hand's colour, and judged notes flash above the hit line.
void CHighway::drawKeyboard(CChord &wantedChord)
{
    // the keys the pianist has to play next, coloured by hand
    whichPart_t wanted[MAX_MIDI_NOTES];
    for (auto &part : wanted)
        part = PB_PART_none;
    for (int i = 0; i < wantedChord.length(); i++)
    {
        CNote note = wantedChord.getNote(i);
        const int key = note.pitch() + m_transpose;
        if (key >= 0 && key < MAX_MIDI_NOTES)
            wanted[key] = note.part();
    }

    const qint64 nowUs = monotonicTimeUs();
    const float whiteBottom = 0;
    const float whiteTop = m_keyboardHeight;
    const float blackBottom = m_keyboardHeight * 0.36f;

    auto keyFill = [&](int key, const Rgba &base) {
        if (m_keyState[key] == KEY_GOOD)
            return KEY_GOOD_COLOR;
        if (m_keyState[key] == KEY_BAD)
            return KEY_BAD_COLOR;
        if (wanted[key] != PB_PART_none)
            return mix(base, wanted[key] == PB_PART_left ? LEFT_HAND_COLOR : RIGHT_HAND_COLOR, 0.55f);
        return base;
    };

    glBegin(GL_QUADS);
    color({0.02f, 0.02f, 0.03f, 1.0f});
    quad(0, whiteBottom, m_layoutWidth, whiteTop);
    for (int key = m_lowKey; key <= m_highKey; key++)
    {
        if (isBlackKey(key))
            continue;
        const Rgba white {0.93f, 0.93f, 0.95f, 1.0f};
        color(keyFill(key, white));
        quad(m_keyX0[key] + 1, whiteBottom + 1, m_keyX1[key] - 1, whiteTop);
    }
    for (int key = m_lowKey; key <= m_highKey; key++)
    {
        if (!isBlackKey(key))
            continue;
        const Rgba black {0.10f, 0.10f, 0.12f, 1.0f};
        color({0.0f, 0.0f, 0.0f, 1.0f});
        quad(m_keyX0[key], blackBottom - 1, m_keyX1[key], whiteTop);
        color(keyFill(key, black));
        quad(m_keyX0[key] + 1.5f, blackBottom + 1, m_keyX1[key] - 1.5f, whiteTop);
    }
    // the keys the pianist's keyboard reaches: a bar along the bottom, the others dimmed
    if (m_reachLow <= m_reachHigh)
    {
        for (int key = m_lowKey; key <= m_highKey; key++)
        {
            if (key >= m_reachLow && key <= m_reachHigh)
                continue;
            color({0.0f, 0.0f, 0.0f, 0.45f});
            quad(m_keyX0[key], isBlackKey(key) ? blackBottom : whiteBottom, m_keyX1[key], whiteTop);
        }
        const int first = std::max(m_reachLow, m_lowKey);
        const int last = std::min(m_reachHigh, m_highKey);
        if (first <= last)
        {
            color({0.25f, 0.85f, 0.95f, 0.9f});
            quad(m_keyX0[first], whiteBottom, m_keyX1[last], whiteBottom + 4);
        }
    }

    // judgement flashes rising from the hit line
    for (int key = m_lowKey; key <= m_highKey; key++)
    {
        const Flash &flash = m_keyFlash[key];
        if (flash.judgement == Judgement::pending)
            continue;
        const qint64 age = nowUs - flash.timeUs;
        if (age > FLASH_DURATION_US)
            continue;
        const float fade = 1.0f - static_cast<float>(age) / static_cast<float>(FLASH_DURATION_US);
        const float grow = 16.0f + 24.0f * (1.0f - fade);
        color(judgementColor(flash.judgement), 0.55f * fade);
        quad(m_keyX0[key] - 2, m_hitY, m_keyX1[key] + 2, m_hitY + grow);
        color(judgementColor(flash.judgement), 0.85f * fade);
        quad(m_keyX0[key] + 1, m_hitY - 6, m_keyX1[key] - 1, m_hitY);
    }
    glEnd();
}
