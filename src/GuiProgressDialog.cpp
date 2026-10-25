/*********************************************************************************/
/*!
@file           GuiProgressDialog.cpp

@brief          Shows the daily goal, practice streak and the best results per song.

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

#include "GuiProgressDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Progress.h"
#include "Settings.h"

QString GuiProgressDialog::starsText(int stars)
{
    QString text;
    for (int i = 0; i < 3; i++)
        text += (i < stars) ? QChar(0x2605) : QChar(0x2606);
    return text;
}

QString GuiProgressDialog::summaryText(const CProgress &progress)
{
    const QDate today = QDate::currentDate();
    const int minutes = static_cast<int>(progress.practiceSeconds(today) / 60.0);
    QString text = tr("Today %1/%2 min").arg(minutes).arg(progress.dailyGoalMinutes());
    if (progress.goalReached(today))
        text += QStringLiteral(" ✓");
    const int streak = progress.currentStreak(today);
    if (streak > 0)
        text += QStringLiteral("  ·  ") + tr("%n day streak", nullptr, streak);
    return text;
}

GuiProgressDialog::GuiProgressDialog(QWidget *parent, CProgress *progress, CSettings *settings)
    : QDialog(parent)
{
    setWindowTitle(tr("Your Progress"));
    resize(560, 560);
    auto *layout = new QVBoxLayout(this);

    const QDate today = QDate::currentDate();
    auto *form = new QFormLayout;

    auto *goalBar = new QProgressBar;
    goalBar->setRange(0, progress->dailyGoalMinutes() * 60);
    goalBar->setValue(static_cast<int>(std::min(progress->practiceSeconds(today), progress->dailyGoalMinutes() * 60.0)));
    goalBar->setFormat(tr("%1 of %2 minutes").arg(static_cast<int>(progress->practiceSeconds(today) / 60.0))
                       .arg(progress->dailyGoalMinutes()));
    form->addRow(tr("Today:"), goalBar);

    auto *goalSpin = new QSpinBox;
    goalSpin->setRange(1, 240);
    goalSpin->setSuffix(tr(" min"));
    goalSpin->setValue(progress->dailyGoalMinutes());
    connect(goalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [progress, goalBar](int minutes) {
        progress->setDailyGoalMinutes(minutes);
        progress->save();
        goalBar->setRange(0, minutes * 60);
    });
    form->addRow(tr("Daily goal:"), goalSpin);

    form->addRow(tr("Streak:"), new QLabel(tr("%n day(s)", nullptr, progress->currentStreak(today)) +
                                           "   (" + tr("best %n", nullptr, progress->bestStreak()) + ")"));

    // the last two weeks as a row of marks
    QString week;
    for (int i = 13; i >= 0; i--)
    {
        const QDate day = today.addDays(-i);
        week += progress->goalReached(day) ? QChar(0x25CF) : (progress->practiceSeconds(day) > 0 ? QChar(0x25D0) : QChar(0x25CB));
        week += ' ';
    }
    auto *weekLabel = new QLabel(week);
    weekLabel->setToolTip(tr("The last 14 days: ● goal reached, ◐ practised, ○ no practice"));
    form->addRow(tr("Last 14 days:"), weekLabel);

    const int totalMinutes = static_cast<int>(progress->totalPracticeSeconds() / 60.0);
    form->addRow(tr("Total practice:"), new QLabel(tr("%1 h %2 min").arg(totalMinutes / 60).arg(totalMinutes % 60)));
    form->addRow(tr("Songs with 3 stars:"), new QLabel(QString::number(progress->songsWithStars(3))));
    layout->addLayout(form);

    // the best results for the songs in the current book
    auto *tree = new QTreeWidget;
    tree->setHeaderLabels({tr("Song"), tr("Stars"), tr("Best"), tr("Plays")});
    tree->setRootIsDecorated(false);
    const QString book = settings->getCurrentBookName();
    for (const QString &songName : settings->getSongList())
    {
        const SongProgress song = progress->song(book + '/' + songName);
        auto *item = new QTreeWidgetItem({songName, starsText(song.stars),
                                          song.plays ? QString::number(qRound(song.bestAccuracy)) + "%" : QString(),
                                          song.plays ? QString::number(song.plays) : QString()});
        tree->addTopLevelItem(item);
    }
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(new QLabel(tr("Book: %1").arg(book)));
    layout->addWidget(tree);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
