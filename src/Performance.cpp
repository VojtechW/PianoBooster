/*********************************************************************************/
/*!
@file           Performance.cpp

@brief          Per-note timing judgements, streaks, accuracy and stars for a run.

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

#include "Performance.h"

#include <algorithm>
#include <cmath>

void CPerformance::reset(bool timingJudged)
{
    m_result = PerformanceResult();
    m_result.timingJudged = timingJudged;
    m_streak = 0;
    m_offsetSumMs = 0.0;
    m_lastJudgement = Judgement::pending;
    m_lastOffsetMs = 0.0;
    m_judgementCount = 0;
}

Judgement CPerformance::classify(double offsetMs, bool timingJudged)
{
    if (!timingJudged)
        return Judgement::perfect; // "wait for me": any correct note is a hit
    const double distance = std::fabs(offsetMs);
    if (distance <= PERFECT_WINDOW_MS)
        return Judgement::perfect;
    if (distance <= GOOD_WINDOW_MS)
        return Judgement::good;
    return (offsetMs < 0) ? Judgement::early : Judgement::late;
}

Judgement CPerformance::recordHit(double offsetMs)
{
    const Judgement judgement = classify(offsetMs, m_result.timingJudged);
    if (m_result.timingJudged)
    {
        m_offsetSumMs += offsetMs;
        m_result.timedHits++;
    }
    count(judgement, offsetMs);
    return judgement;
}

Judgement CPerformance::recordLateHit()
{
    count(Judgement::late, 0.0);
    return Judgement::late;
}

void CPerformance::recordMissed(int count)
{
    for (int i = 0; i < count; i++)
        this->count(Judgement::missed, 0.0);
}

void CPerformance::recordWrong()
{
    m_result.wrong++;
    m_streak = 0;
}

// Count one judged note: a miss breaks the streak, any hit extends it.
void CPerformance::count(Judgement judgement, double offsetMs)
{
    switch (judgement)
    {
    case Judgement::perfect: m_result.perfect++; break;
    case Judgement::good:    m_result.good++; break;
    case Judgement::early:   m_result.early++; break;
    case Judgement::late:    m_result.late++; break;
    case Judgement::missed:  m_result.missed++; break;
    default: return;
    }
    if (judgement == Judgement::missed)
        m_streak = 0;
    else
        m_result.bestStreak = std::max(m_result.bestStreak, ++m_streak);
    m_lastJudgement = judgement;
    m_lastOffsetMs = offsetMs;
    m_judgementCount++;
}

// Weighted hits over all notes: Perfect 1, Good 0.85, Early/Late 0.6, Missed 0.
double CPerformance::accuracy() const
{
    const PerformanceResult &r = m_result;
    // every wrong note costs half a note, so mashing keys does not pay
    const double total = r.notes() + 0.5 * r.wrong;
    if (total <= 0)
        return 0.0;
    const double points = r.perfect * 1.0 + r.good * 0.85 + (r.early + r.late) * 0.6;
    return std::min(100.0, 100.0 * points / total);
}

int CPerformance::starsForAccuracy(double accuracy)
{
    if (accuracy >= STAR3_ACCURACY)
        return 3;
    if (accuracy >= STAR2_ACCURACY)
        return 2;
    if (accuracy >= STAR1_ACCURACY)
        return 1;
    return 0;
}

PerformanceResult CPerformance::result() const
{
    PerformanceResult r = m_result;
    r.accuracy = accuracy();
    r.stars = starsForAccuracy(r.accuracy);
    if (!r.timingJudged)
        r.stars = std::min(r.stars, MAX_STARS_WITHOUT_TIMING); // the last star needs playing in time
    r.meanOffsetMs = (r.timedHits > 0) ? m_offsetSumMs / r.timedHits : 0.0;
    return r;
}
