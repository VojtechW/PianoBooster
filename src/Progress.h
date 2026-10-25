/*********************************************************************************/
/*!
@file           Progress.h

@brief          Persistent practice progress: best results per song, practice time per day,
                daily goal and day streaks.

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

#ifndef __PROGRESS_H__
#define __PROGRESS_H__

#include <QDate>
#include <QJsonObject>
#include <QString>

#include "Performance.h"

struct SongProgress
{
    double bestAccuracy = 0;
    int stars = 0;
    int bestStreak = 0;
    int plays = 0;
    QString lastPlayed; // ISO date
};

class CProgress
{
public:
    //! the store used by the application (in the user's application data folder)
    static CProgress &instance();

    //! use a given file (e.g. for tests); does not load it
    explicit CProgress(const QString &fileName);

    void load();
    void save();
    QString fileName() const { return m_fileName; }

    //! record a finished run, returns true when it is a new best for the song
    bool recordRun(const QString &songKey, const PerformanceResult &result, const QDate &day = QDate::currentDate());
    SongProgress song(const QString &songKey) const;
    int songStars(const QString &songKey) const { return song(songKey).stars; }

    void addPracticeSeconds(double seconds, const QDate &day = QDate::currentDate());
    double practiceSeconds(const QDate &day) const;
    double totalPracticeSeconds() const;

    int dailyGoalMinutes() const { return m_dailyGoalMinutes; }
    void setDailyGoalMinutes(int minutes);
    bool goalReached(const QDate &day) const { return practiceSeconds(day) >= m_dailyGoalMinutes * 60.0; }

    //! consecutive practice days ending today (or yesterday, if today has no practice yet)
    int currentStreak(const QDate &today = QDate::currentDate()) const;
    int bestStreak() const;
    int songCount() const { return m_songs.size(); }
    int songsWithStars(int stars) const;

    //! the minimum practice time for a day to count towards a streak
    static constexpr double STREAK_MINIMUM_SECONDS = 60.0;

private:
    bool practisedOn(const QDate &day) const { return practiceSeconds(day) >= STREAK_MINIMUM_SECONDS; }

    QString m_fileName;
    QJsonObject m_songs; // songKey -> {bestAccuracy, stars, bestStreak, plays, lastPlayed}
    QJsonObject m_days;  // ISO date -> seconds
    int m_dailyGoalMinutes;
    bool m_dirty;
};

#endif // __PROGRESS_H__
