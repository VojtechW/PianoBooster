/*********************************************************************************/
/*!
@file           test_core.cpp

@brief          Unit tests for the practice logic that does not need a GUI or MIDI device:
                timing judgements, accuracy and stars (CPerformance), and the progress store
                with its day streaks (CProgress).

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
#include <QTemporaryDir>
#include <QtTest>

#include "../Performance.h"
#include "../Progress.h"
#include "../KeyboardFit.h"

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void classifyTiming()
    {
        QCOMPARE(CPerformance::classify(0, true), Judgement::perfect);
        QCOMPARE(CPerformance::classify(-40, true), Judgement::perfect);
        QCOMPARE(CPerformance::classify(41, true), Judgement::good);
        QCOMPARE(CPerformance::classify(-100, true), Judgement::good);
        QCOMPARE(CPerformance::classify(-101, true), Judgement::early);
        QCOMPARE(CPerformance::classify(150, true), Judgement::late);
        QCOMPARE(CPerformance::classify(500, false), Judgement::perfect); // wait mode ignores timing
    }

    void accuracyStarsAndStreaks()
    {
        CPerformance p;
        p.reset(true);
        for (int i = 0; i < 10; i++)
            p.recordHit(5);
        QCOMPARE(p.streak(), 10);
        QCOMPARE(p.result().accuracy, 100.0);
        QCOMPARE(p.result().stars, 3);

        p.recordMissed();
        QCOMPARE(p.streak(), 0);
        p.recordHit(-60); // good
        p.recordWrong();
        PerformanceResult r = p.result();
        QCOMPARE(r.notes(), 12);
        QCOMPARE(r.bestStreak, 10);
        QCOMPARE(r.wrong, 1);
        // (10 * 1 + 0.85) / (12 + 0.5)
        QVERIFY(qAbs(r.accuracy - 100.0 * 10.85 / 12.5) < 1e-9);
        QCOMPARE(r.stars, 2);
        QVERIFY(qAbs(r.meanOffsetMs - (10 * 5 - 60) / 11.0) < 1e-9);
    }

    void waitModeCapsStars()
    {
        CPerformance p;
        p.reset(false);
        for (int i = 0; i < 5; i++)
            p.recordHit(300);
        QCOMPARE(p.result().accuracy, 100.0);
        QCOMPARE(p.result().stars, CPerformance::MAX_STARS_WITHOUT_TIMING);
        QCOMPARE(p.result().timedHits, 0);
    }

    void emptyRun()
    {
        CPerformance p;
        QCOMPARE(p.result().accuracy, 0.0);
        QCOMPARE(p.result().stars, 0);
    }

    void progressStoreRoundTrip()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("sub/progress.json");
        const QDate today(2026, 10, 25);
        {
            CProgress progress(file);
            PerformanceResult r;
            r.perfect = 8; r.missed = 2; r.accuracy = 80; r.stars = 2; r.bestStreak = 6;
            QVERIFY(progress.recordRun("Book/Song.mid", r, today));
            r.accuracy = 70; r.stars = 1;
            QVERIFY(!progress.recordRun("Book/Song.mid", r, today)); // not a new best
            progress.addPracticeSeconds(90, today);
            progress.addPracticeSeconds(30, today.addDays(-1));
            progress.addPracticeSeconds(600, today.addDays(-2));
            progress.setDailyGoalMinutes(10);
            progress.save();
        }
        CProgress progress(file);
        progress.load();
        const SongProgress song = progress.song("Book/Song.mid");
        QCOMPARE(song.stars, 2);
        QCOMPARE(song.plays, 2);
        QCOMPARE(song.bestAccuracy, 80.0);
        QCOMPARE(progress.dailyGoalMinutes(), 10);
        QCOMPARE(progress.practiceSeconds(today), 90.0);
        QVERIFY(progress.goalReached(today.addDays(-2)));
        QVERIFY(!progress.goalReached(today));
        // yesterday had less than a minute, so the streak is just today
        QCOMPARE(progress.currentStreak(today), 1);
        QCOMPARE(progress.bestStreak(), 1);
    }

    void keyboardModels()
    {
        using namespace KeyboardFit;
        QCOMPARE(keysFromName("LPK25"), 25);
        QCOMPARE(keysFromName("MPK mini 3"), 25);
        QCOMPARE(keysFromName("Launchkey 49 MK3 LKMK3 MIDI Out"), 49);
        QCOMPARE(keysFromName("Keystation 88 II"), 88);
        QCOMPARE(keysFromName("microKEY-37"), 37);
        QCOMPARE(keysFromName("IAC Driver Bus 1"), 0);
        QCOMPARE(identify("0 - LPK25", 0, 127).name, QString("LPK25"));
        QCOMPARE(identify("0 - LPK25", 0, 127).keys, 25);
        const Keyboard setup = identify("1 - Some Keyboard", 36, 96); // the keyboard setup wins
        QCOMPARE(setup.keys, 61);
        QCOMPARE(setup.fixedLowest, 36);
        QCOMPARE(noteName(60), QString("C4"));
        QCOMPARE(noteName(61), QString("C#4"));
    }

    void keyboardFit()
    {
        using namespace KeyboardFit;
        Keyboard lpk25;
        lpk25.keys = 25;
        Result r = check(lpk25, 60, 67); // C4..G4
        QCOMPARE(r.verdict, Verdict::fits);
        QCOMPARE(r.lowestKey, 60);
        QCOMPARE(r.keysNeeded, 8);
        r = check(lpk25, 50, 70); // D3..A#4: the C window from C3 reaches C5
        QCOMPARE(r.verdict, Verdict::fits);
        QCOMPARE(r.lowestKey, 48);
        r = check(lpk25, 55, 79); // G3..G5, 25 keys but not from a C
        QCOMPARE(r.verdict, Verdict::transpose);
        QCOMPARE(r.transpose, 5);
        QCOMPARE(r.lowestKey, 60);
        r = check(lpk25, 50, 75); // 26 keys
        QCOMPARE(r.verdict, Verdict::tooWide);
        QCOMPARE(r.keysNeeded, 26);

        const Keyboard piano = identify("0 - Digital Piano", 0, 127);
        QCOMPARE(piano.keys, 88);
        QCOMPARE(check(piano, 21, 108).verdict, Verdict::fits);
        r = check(piano, 19, 40); // below A0
        QCOMPARE(r.verdict, Verdict::transpose);
        QCOMPARE(r.transpose, 2);

        Keyboard unknown;
        QCOMPARE(check(unknown, 60, 72).verdict, Verdict::unknown);
        QCOMPARE(check(unknown, 60, 72).keysNeeded, 13);
    }

    void streaks()
    {
        QTemporaryDir dir;
        CProgress progress(dir.filePath("p.json"));
        const QDate today(2026, 3, 1);
        for (int i = 1; i <= 4; i++) // four days before today, across a month boundary
            progress.addPracticeSeconds(120, today.addDays(-i));
        QCOMPARE(progress.currentStreak(today), 4); // today not practised yet still keeps the streak
        progress.addPracticeSeconds(61, today);
        QCOMPARE(progress.currentStreak(today), 5);
        QCOMPARE(progress.currentStreak(today.addDays(2)), 0);
        progress.addPracticeSeconds(300, today.addDays(-10));
        QCOMPARE(progress.bestStreak(), 5);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
