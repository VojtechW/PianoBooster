/*********************************************************************************/
/*!
@file           Progress.cpp

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

#include "Progress.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

CProgress &CProgress::instance()
{
    static CProgress progress(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/progress.json");
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        progress.load();
    }
    return progress;
}

CProgress::CProgress(const QString &fileName)
    : m_fileName(fileName), m_dailyGoalMinutes(5), m_dirty(false)
{
}

void CProgress::load()
{
    QFile file(m_fileName);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_songs = root.value("songs").toObject();
    m_days = root.value("days").toObject();
    m_dailyGoalMinutes = std::max(1, root.value("dailyGoalMinutes").toInt(5));
    m_dirty = false;
}

void CProgress::save()
{
    if (!m_dirty)
        return;
    QDir().mkpath(QFileInfo(m_fileName).path());
    QJsonObject root;
    root["version"] = 1;
    root["songs"] = m_songs;
    root["days"] = m_days;
    root["dailyGoalMinutes"] = m_dailyGoalMinutes;
    QSaveFile file(m_fileName); // written atomically, a crash never leaves a broken file
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (file.commit())
            m_dirty = false;
    }
}

bool CProgress::recordRun(const QString &songKey, const PerformanceResult &result, const QDate &day)
{
    if (songKey.isEmpty() || result.notes() == 0)
        return false;
    QJsonObject entry = m_songs.value(songKey).toObject();
    const bool newBest = result.accuracy > entry.value("bestAccuracy").toDouble(-1.0);
    if (newBest)
        entry["bestAccuracy"] = result.accuracy;
    entry["stars"] = std::max(entry.value("stars").toInt(), result.stars);
    entry["bestStreak"] = std::max(entry.value("bestStreak").toInt(), result.bestStreak);
    entry["plays"] = entry.value("plays").toInt() + 1;
    entry["lastPlayed"] = day.toString(Qt::ISODate);
    m_songs[songKey] = entry;
    m_dirty = true;
    return newBest;
}

SongProgress CProgress::song(const QString &songKey) const
{
    SongProgress progress;
    const QJsonObject entry = m_songs.value(songKey).toObject();
    progress.bestAccuracy = entry.value("bestAccuracy").toDouble();
    progress.stars = entry.value("stars").toInt();
    progress.bestStreak = entry.value("bestStreak").toInt();
    progress.plays = entry.value("plays").toInt();
    progress.lastPlayed = entry.value("lastPlayed").toString();
    return progress;
}

void CProgress::addPracticeSeconds(double seconds, const QDate &day)
{
    if (seconds <= 0)
        return;
    const QString key = day.toString(Qt::ISODate);
    m_days[key] = m_days.value(key).toDouble() + seconds;
    m_dirty = true;
}

double CProgress::practiceSeconds(const QDate &day) const
{
    return m_days.value(day.toString(Qt::ISODate)).toDouble();
}

double CProgress::totalPracticeSeconds() const
{
    double total = 0;
    for (auto it = m_days.begin(); it != m_days.end(); ++it)
        total += it.value().toDouble();
    return total;
}

void CProgress::setDailyGoalMinutes(int minutes)
{
    m_dailyGoalMinutes = std::max(1, minutes);
    m_dirty = true;
}

int CProgress::currentStreak(const QDate &today) const
{
    QDate day = today;
    if (!practisedOn(day))
        day = day.addDays(-1); // today still counts as open
    int streak = 0;
    while (practisedOn(day))
    {
        streak++;
        day = day.addDays(-1);
    }
    return streak;
}

int CProgress::bestStreak() const
{
    QStringList days;
    for (auto it = m_days.begin(); it != m_days.end(); ++it)
        if (it.value().toDouble() >= STREAK_MINIMUM_SECONDS)
            days.append(it.key());
    std::sort(days.begin(), days.end()); // ISO dates sort chronologically
    int best = 0, run = 0;
    QDate previous;
    for (const QString &text : days)
    {
        const QDate day = QDate::fromString(text, Qt::ISODate);
        run = (previous.isValid() && previous.addDays(1) == day) ? run + 1 : 1;
        best = std::max(best, run);
        previous = day;
    }
    return best;
}

int CProgress::songsWithStars(int stars) const
{
    int count = 0;
    for (auto it = m_songs.begin(); it != m_songs.end(); ++it)
        if (it.value().toObject().value("stars").toInt() >= stars)
            count++;
    return count;
}
