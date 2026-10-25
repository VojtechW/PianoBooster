/****************************************************************************

    Copyright (c)   2008-2013, L. J. Barman, all rights reserved

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

****************************************************************************/

#include <QApplication>

#include <cstdlib>

#include "QtWindow.h"
#include <QOpenGLWidget>
#include "GlView.h"
#include "GuiTopBar.h"
#include "version.h"

int main(int argc, char *argv[]){
    QCoreApplication::setOrganizationName(QStringLiteral("PianoBooster"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("https://github.com/pianobooster/PianoBooster"));
    QCoreApplication::setApplicationName(QStringLiteral("Piano Booster"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PB_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("pianobooster"));

    {
        QCoreApplication app(argc, argv);
        QStringList argList = QCoreApplication::arguments();
        for (const QString &arg : argList){
            if (arg == QLatin1String("--version")) {
                fprintf(stdout, "pianobooster " PB_VERSION "\n");
                return EXIT_SUCCESS;
            }
        }
    }

    QApplication app(argc, argv);
    QtWindow window;
    window.show();

    // Developer automation hooks (used by the smoke tests in tools/):
    //   PB_AUTOPLAY=<ms>                start playing from the start after <ms>
    //   PB_SCREENSHOT=<file>@<ms>[;...] save a capture of the main window
    //   PB_QUIT_AFTER=<ms>              quit the application
    //   PB_SPEED=<percent>              set the speed before playing
    //   PB_LOOP_BARS=<bars>             loop <bars> bars (0 = no loop) ...
    //   PB_LOOP_FROM=<bar>              ... starting at <bar>
    //   PB_MIDI_INPUT=<port name>       use this MIDI input without saving it (QtWindow::checkMidiInputs)
    //   PB_LATENCY_LOG, PB_FRAME_LOG    print timing measurements to stderr
    const QString autoPlay = qEnvironmentVariable("PB_AUTOPLAY");
    if (!autoPlay.isEmpty())
        QTimer::singleShot(autoPlay.toInt(), &window, [&window]() {
            if (auto *topBar = window.findChild<GuiTopBar*>(); topBar && qEnvironmentVariableIsSet("PB_SPEED"))
                topBar->setSpeed(qEnvironmentVariableIntValue("PB_SPEED"));
            if (auto *view = window.findChild<CGLView*>(); view && qEnvironmentVariableIsSet("PB_LOOP_BARS"))
            {
                view->getSongObject()->setPlayFromBar(qEnvironmentVariableIntValue("PB_LOOP_FROM"));
                view->getSongObject()->setLoopingBars(qEnvironmentVariableIntValue("PB_LOOP_BARS"));
            }
            QMetaObject::invokeMethod(&window, "on_playFromStart");
        });
    const QStringList shots = qEnvironmentVariable("PB_SCREENSHOT").split(';', Qt::SkipEmptyParts);
    for (const QString &shot : shots) {
        const QString file = shot.section('@', 0, 0);
        const int delay = shot.section('@', 1, 1).toInt();
        QTimer::singleShot(delay, &window, [&window, file]() {
            // QWidget::grab() does not work with the legacy GL text rendering, so grab the
            // GL framebuffer and paint it over a capture of the other widgets.
            QPixmap pixmap(window.size() * window.devicePixelRatio());
            pixmap.setDevicePixelRatio(window.devicePixelRatio());
            pixmap.fill(Qt::black);
            QPainter painter(&pixmap);
            for (QWidget *child : window.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
                if (!child->isVisible())
                    continue;
                if (auto *gl = qobject_cast<QOpenGLWidget*>(child))
                    painter.drawImage(QRect(gl->pos(), gl->size()), gl->grabFramebuffer());
                else if (child->findChildren<QOpenGLWidget*>().isEmpty())
                    child->render(&painter, child->pos());
            }
            if (auto *central = window.centralWidget()) {
                for (QWidget *child : central->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
                    if (!child->isVisible())
                        continue;
                    const QPoint pos = central->mapTo(&window, child->pos());
                    if (auto *gl = qobject_cast<QOpenGLWidget*>(child))
                        painter.drawImage(QRect(pos, gl->size()), gl->grabFramebuffer());
                    else
                        child->render(&painter, pos);
                }
            }
            painter.end();
            fprintf(stderr, "PB_SCREENSHOT %s saved=%d\n", qPrintable(file), pixmap.save(file, "PNG"));
        });
    }
    const QString quitAfter = qEnvironmentVariable("PB_QUIT_AFTER");
    if (!quitAfter.isEmpty())
        QTimer::singleShot(quitAfter.toInt(), &app, &QApplication::quit);

    int value = app.exec();
    closeLogs();
    return value;
}
