/*********************************************************************************/
/*!
@file           GuiProgressDialog.h

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

#ifndef __GUI_PROGRESS_DIALOG_H__
#define __GUI_PROGRESS_DIALOG_H__

#include <QDialog>

class CProgress;
class CSettings;

class GuiProgressDialog : public QDialog
{
    Q_OBJECT

public:
    GuiProgressDialog(QWidget *parent, CProgress *progress, CSettings *settings);

    //! "★★☆" for the given number of stars
    static QString starsText(int stars);
    //! a short "Today 3/5 min · 4 day streak" summary
    static QString summaryText(const CProgress &progress);
};

#endif // __GUI_PROGRESS_DIALOG_H__
