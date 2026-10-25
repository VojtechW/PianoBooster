/*********************************************************************************/
/*!
@file           Performance.h

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

#ifndef __PERFORMANCE_H__
#define __PERFORMANCE_H__

enum class Judgement : unsigned char
{
    pending,  // not yet played
    perfect,
    good,     // in time but not perfect
    early,
    late,
    missed,
    skipped   // not required (e.g. the other notes of a chord in beginner mode)
};

struct PerformanceResult
{
    int perfect = 0;
    int good = 0;
    int early = 0;
    int late = 0;
    int missed = 0;
    int wrong = 0;
    int bestStreak = 0;
    double accuracy = 0.0;      // 0 .. 100
    int stars = 0;              // 0 .. 3
    double meanOffsetMs = 0.0;  // < 0 rushing, > 0 dragging (only when timing is judged)
    int timedHits = 0;          // number of hits contributing to meanOffsetMs
    bool timingJudged = false;  // false in "wait for me" mode

    int notes() const { return perfect + good + early + late + missed; }
    int hits() const { return perfect + good + early + late; }
};

class CPerformance
{
public:
    // Timing windows in milliseconds of real time (beyond the "good" window a hit is early/late,
    // beyond the conductor's play zone the note is missed).
    static constexpr double PERFECT_WINDOW_MS = 40.0;
    static constexpr double GOOD_WINDOW_MS = 100.0;

    static constexpr double STAR1_ACCURACY = 60.0;
    static constexpr double STAR2_ACCURACY = 80.0;
    static constexpr double STAR3_ACCURACY = 92.0;
    static constexpr int MAX_STARS_WITHOUT_TIMING = 2;

    CPerformance() { reset(); }

    void reset(bool timingJudged = true);

    //! classify a hit that was offsetMs from the wanted time (negative = early)
    static Judgement classify(double offsetMs, bool timingJudged);

    //! record a correct note, returns its judgement
    Judgement recordHit(double offsetMs);
    //! record a correct note that was played after the music had to stop and wait for it
    Judgement recordLateHit();
    void recordMissed(int count = 1);
    void recordWrong();

    int streak() const { return m_streak; }
    bool isTimingJudged() const { return m_result.timingJudged; }
    double accuracy() const;
    static int starsForAccuracy(double accuracy);
    PerformanceResult result() const;

    Judgement lastJudgement() const { return m_lastJudgement; }
    double lastOffsetMs() const { return m_lastOffsetMs; }
    //! incremented on every judgement, lets the display notice new ones
    unsigned judgementCount() const { return m_judgementCount; }

private:
    void count(Judgement judgement, double offsetMs);

    PerformanceResult m_result;
    int m_streak;
    double m_offsetSumMs;
    Judgement m_lastJudgement;
    double m_lastOffsetMs;
    unsigned m_judgementCount;
};

#endif // __PERFORMANCE_H__
