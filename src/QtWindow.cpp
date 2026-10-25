/*!
    @file           QtWindow.cpp

    @brief          xxx.

    @author         L. J. Barman

    Copyright (c)   2008-2020, L. J. Barman and others, all rights reserved

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

#include "GlView.h"
#include "QtWindow.h"
#include "version.h"

#include <QDebug>
#include <QSurfaceFormat>

#include "GlView.h"
#include "GuiProgressDialog.h"
#include "Progress.h"
#if WITH_INTERNAL_FLUIDSYNTH
#include "MidiDeviceFluidSynth.h"
#endif
#include <QStringBuilder>

#ifdef __linux__
#ifndef USE_REALTIME_PRIORITY
#define USE_REALTIME_PRIORITY 0
#endif
#endif

#if USE_REALTIME_PRIORITY
#include <sched.h>

/* sets the process to "policy" policy at given priority */
static int set_realtime_priority(int policy, int prio)
{
    struct sched_param schp;
    memset(&schp, 0, sizeof(schp));

    schp.sched_priority = prio;
    if (sched_setscheduler(0, policy, &schp) != 0) {
        perror("sched_setscheduler");
        return -1;
    }
    return 0;
}
#endif

QtWindow::QtWindow()
{
    m_settings = new CSettings(this);
    setWindowIcon(QIcon(":/images/pianobooster.png"));
    setWindowTitle(tr("Piano Booster"));

    Cfg::setDefaults();

    decodeCommandLine();

    auto fmt = QSurfaceFormat::defaultFormat();
#if defined (Q_OS_DARWIN)
    // The MIDI engine runs on the GUI thread; a vsync-blocking buffer swap would stall it for up
    // to a frame and delay the reaction to key presses. The macOS compositor prevents tearing anyway.
    if (Cfg::experimentalSwapInterval == -1)
        Cfg::experimentalSwapInterval = 0;
#endif
    if (Cfg::experimentalSwapInterval != -1)
    {
        fmt.setSwapInterval(Cfg::experimentalSwapInterval);
        int value = fmt.swapInterval();
        ppLogInfo("Open GL Swap Interval %d", value);
    }

    for (int i = 0; i < maxRecentFiles(); ++i)
         m_recentFileActs[i] = nullptr;
    m_separatorAct = nullptr;

#if USE_REALTIME_PRIORITY
    int rt_prio = sched_get_priority_max(SCHED_FIFO);
    set_realtime_priority(SCHED_FIFO, rt_prio);
#endif

    QString antiAliasingSetting = m_settings->value("anti-aliasing").toString();
    if (antiAliasingSetting.isEmpty() || antiAliasingSetting=="on"){
        fmt.setSamples(4);
    }

    QSurfaceFormat::setDefaultFormat(fmt);

    m_glWidget = new CGLView(this, m_settings);
    m_glWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_song = m_glWidget->getSongObject();
    m_score = m_glWidget->getScoreObject();

    QHBoxLayout *mainLayout = new QHBoxLayout;
    QVBoxLayout *columnLayout = new QVBoxLayout;

    m_sidePanel = new GuiSidePanel(this, m_settings);
    m_topBar = new GuiTopBar(this, m_settings);
    m_tutorWindow = new QTextBrowser(this);
    m_tutorWindow->hide();

    m_settings->init(m_song, m_sidePanel, m_topBar);

    mainLayout->addWidget(m_sidePanel);
    columnLayout->addWidget(m_topBar);
    columnLayout->addWidget(m_glWidget);
    columnLayout->addWidget(m_tutorWindow);
    mainLayout->addLayout(columnLayout);

    m_song->init2(m_score, m_settings);

    m_sidePanel->init(m_song, m_song->getTrackList(), m_topBar);
    m_topBar->init(m_song);

    QWidget *centralWin = new QWidget();
    centralWin->setLayout(mainLayout);

    setCentralWidget(centralWin);

    m_glWidget->setFocus(Qt::ActiveWindowFocusReason);

    m_song->setPianoSoundPatches(m_settings->value("Keyboard/RightSound", Cfg::defaultRightPatch()).toInt() - 1,
                                 m_settings->value("Keyboard/WrongSound", Cfg::defaultWrongPatch()).toInt() - 1, true);

    m_song->setLatencyFix(m_settings->value("Midi/Latency", 0).toInt());

    m_song->cfg_timingMarkersFlag = m_settings->value("Score/TimingMarkers", m_song->cfg_timingMarkersFlag ).toBool();
    m_song->cfg_stopPointMode = static_cast<stopPointMode_t> (m_settings->value("Score/StopPointMode", m_song->cfg_stopPointMode ).toInt());
    m_song->cfg_rhythmTapping = static_cast<rhythmTapping_t> (m_settings->value("Score/RtyhemTappingMode", m_song->cfg_rhythmTapping ).toInt());

    m_song->reconnectMidi();

    readSettings();

    QTimer::singleShot(100, this, [&](){
        QString songName = m_settings->value("CurrentSong").toString();
        if (!songName.isEmpty())
            m_settings->openSongFile( songName );
    });
}

void QtWindow::init()
{

    m_settings->loadSettings();

    createActions();
    createMenus();
    createPracticeFeatures();
    readSettings();

    refreshTranslate();
    show();
}

QtWindow::~QtWindow()
{
    delete m_settings;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief               Displays the usage
void QtWindow::displayUsage()
{
    fprintf(stdout, "Usage: pianobooster [flags] [midifile]\n");
    fprintf(stdout, "  -d, --debug             Increase the debug level.\n");
    fprintf(stdout, "      --Xnote-length      Displays the note length (experimental)\n");
    fprintf(stdout, "  -h, --help              Displays this help message.\n");
    fprintf(stdout, "  -v, --version           Displays version number and then exits.\n");
    fprintf(stdout, "  -l   --log              Write debug info to the \"pb.log\" log file.\n");
    fprintf(stdout, "       --midi-input-dump  Displays the midi input in hex.\n");
    fprintf(stdout, "       --lights           Turns on the keyboard lights.\n");
}

int QtWindow::decodeIntegerParam(const QString &arg, int defaultParam)
{
    int n = arg.lastIndexOf('=');
    if (n == -1 || (n + 1) >= arg.size())
        return defaultParam;
    bool ok;
    int value = arg.mid(n+1).toInt(&ok);
    if (ok)
        return value;
    return defaultParam;
}

bool QtWindow::validateIntegerParam(const QString &arg)
{
    int n = arg.lastIndexOf('=');
    if (n == -1 || (n + 1) >= arg.size())
        return false;
    bool ok;
    arg.mid(n+1).toInt(&ok);
     return ok;
}
bool QtWindow::validateIntegerParamWithMessage(const QString &arg)
{
    bool ok = validateIntegerParam(arg);
    if (!ok) {
        fprintf(stderr, "ERROR: Invalid parameter to a command line argument \"%s\".\n", qPrintable(arg));
        exit(0);
    }
     return ok;
}

void QtWindow::decodeMidiFileArg(const QString &arg)
{

    QFileInfo fileInfo(arg);

    if (!fileInfo.exists() )
    {
        QMessageBox::warning(nullptr, tr("PianoBooster MIDI File Error"),
                 tr("Cannot open \"%1\"").arg(QString(fileInfo.absoluteFilePath())));
        exit(1);
    }
        else if ( !(fileInfo.fileName().endsWith(".mid", Qt::CaseInsensitive ) ||
             fileInfo.fileName().endsWith(".midi", Qt::CaseInsensitive ) ||
             fileInfo.fileName().endsWith(".kar", Qt::CaseInsensitive )) )
    {
        QMessageBox::warning(nullptr, tr("PianoBooster MIDI File Error"),
                 tr("\"%1\" is not a MIDI File").arg(QString(fileInfo.fileName())));
        exit(1);
    }
    else
    {
        bool vaildMidiFile = true;
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            vaildMidiFile = false;
        else
        {
            QByteArray bytes = file.read(4);
            for (int i = 0; i < 4; i++)
            {
                if (bytes[i] !="MThd"[i] )
                    vaildMidiFile = false;
            }
            file.close();
        }
        if (vaildMidiFile ==  true)
            m_settings->setValue("CurrentSong", fileInfo.absoluteFilePath());
        else
        {
            QMessageBox::warning(nullptr, tr("PianoBooster MIDI File Error"),
                 tr("\"%1\" is not a valid MIDI file").arg(QString(fileInfo.absoluteFilePath())));
            exit(1);
        }
    }
}

void QtWindow::decodeCommandLine()
{
    bool hasMidiFile = false;
    QStringList argList = QCoreApplication::arguments();
    QString arg;
    for (int i = 0; i < argList.size(); ++i)
    {
        arg = argList[i];
        if (arg.startsWith("-"))
        {
            if (arg.startsWith("-d") || arg.startsWith("--debug"))
                Cfg::logLevel++;
            else if (arg.startsWith("--Xnote-length"))
                Cfg::experimentalNoteLength = true;
            else if (arg.startsWith("--Xtick-rate")) {
                if (validateIntegerParamWithMessage(arg)) {
                    Cfg::tickRate = decodeIntegerParam(arg, 12);
                }
            } else if (arg.startsWith("-l") || arg.startsWith("--log"))
                Cfg::useLogFile = true;
            else if (arg.startsWith("--midi-input-dump"))
                Cfg::midiInputDump = true;
            else if (arg.startsWith("-X1"))
                Cfg::experimentalTempo = true;
            else if (arg.startsWith("-Xswap"))
                Cfg::experimentalSwapInterval = decodeIntegerParam(arg, 100);

            else if (arg.startsWith("--lights"))
                Cfg::keyboardLightsChan = 1-1;  // Channel 1 (really a zero)

            else if (arg.startsWith("-h") || arg.startsWith("-?") || arg.startsWith("--help"))
            {
                displayUsage();
                exit(0);
            }
            else if (arg.startsWith("-v") || arg.startsWith("--version"))
            {
                fprintf(stdout, "pianobooster Version " PB_VERSION"\n");
                exit(0);
            }
            else
            {
                fprintf(stderr, "ERROR: Unknown arguments.\n");
                displayUsage();
                exit(0);
            }
        }
        else {
            if ( hasMidiFile == false && i > 0)
            {
                hasMidiFile = true;
                decodeMidiFileArg(arg);
            }
        }
    }
}

void QtWindow::addShortcutAction(const QString & key, const char * method)
{
    QAction* act = new QAction(this);
    act->setShortcut(m_settings->value(key).toString());
    connect(act, SIGNAL(triggered()), this, method);
    addAction(act);
}

void QtWindow::createActions()
{
    m_openAct = new QAction(QIcon(":/images/open.png"), tr("&Open..."), this);
    m_openAct->setShortcut(tr("Ctrl+O"));
    m_openAct->setToolTip(tr("Open an existing file"));
    connect(m_openAct, SIGNAL(triggered()), this, SLOT(open()));

    m_exitAct = new QAction(tr("E&xit"), this);
    m_exitAct->setShortcut(tr("Ctrl+Q"));
    m_exitAct->setToolTip(tr("Exit the application"));
    connect(m_exitAct, SIGNAL(triggered()), this, SLOT(close()));

    m_aboutAct = new QAction(tr("&About"), this);
    m_aboutAct->setToolTip(tr("Show the application's About box"));
    connect(m_aboutAct, SIGNAL(triggered()), this, SLOT(about()));

    m_shortcutAct = new QAction(tr("&PC Shortcut Keys"), this);
    m_shortcutAct->setToolTip(tr("The PC Keyboard shortcut keys"));
    connect(m_shortcutAct, SIGNAL(triggered()), this, SLOT(keyboardShortcuts()));

    m_setupMidiAct = new QAction(tr("&MIDI Setup ..."), this);
    m_setupMidiAct->setShortcut(tr("Ctrl+S"));
    m_setupMidiAct->setToolTip(tr("Setup the MIDI input and output"));
    connect(m_setupMidiAct, SIGNAL(triggered()), this, SLOT(showMidiSetup()));

    m_setupKeyboardAct = new QAction(tr("Piano &Keyboard Setting ..."), this);
    m_setupKeyboardAct->setShortcut(tr("Ctrl+K"));
    m_setupKeyboardAct->setToolTip(tr("Change the piano keyboard settings"));
    connect(m_setupKeyboardAct, SIGNAL(triggered()), this, SLOT(showKeyboardSetup()));

    m_fullScreenStateAct = new QAction(tr("&Fullscreen"), this);
    m_fullScreenStateAct->setToolTip(tr("Fullscreen mode"));
    m_fullScreenStateAct->setShortcut(tr("F11"));
    m_fullScreenStateAct->setCheckable(true);
    connect(m_fullScreenStateAct, SIGNAL(triggered()), this, SLOT(onFullScreenStateAct()));

    m_sidePanelStateAct = new QAction(tr("&Show the Side Panel"), this);
    m_sidePanelStateAct->setToolTip(tr("Show the Left Side Panel"));
    m_sidePanelStateAct->setShortcut(tr("F12"));
    m_sidePanelStateAct->setCheckable(true);
    m_sidePanelStateAct->setChecked(true);
    connect(m_sidePanelStateAct, SIGNAL(triggered()), this, SLOT(toggleSidePanel()));

    m_viewPianoKeyboard = new QAction(tr("Show Piano &Keyboard"), this);
    m_viewPianoKeyboard->setToolTip(tr("Show Piano Keyboard Widget"));
    m_viewPianoKeyboard->setCheckable(true);
    m_viewPianoKeyboard->setChecked(false);
    if (m_settings->value("View/PianoKeyboard").toString()=="on"){
        m_viewPianoKeyboard->setChecked(true);
    }
    connect(m_viewPianoKeyboard, SIGNAL(triggered()), this, SLOT(onViewPianoKeyboard()));

    m_setupPreferencesAct = new QAction(tr("&Preferences ..."), this);
    m_setupPreferencesAct->setToolTip(tr("Settings"));
    m_setupPreferencesAct->setShortcut(tr("Ctrl+P"));
    connect(m_setupPreferencesAct, SIGNAL(triggered()), this, SLOT(showPreferencesDialog()));

    m_songDetailsAct = new QAction(tr("Song &Details ..."), this);
    m_songDetailsAct->setToolTip(tr("Song Settings"));
    m_songDetailsAct->setShortcut(tr("Ctrl+D"));
    connect(m_songDetailsAct, SIGNAL(triggered()), this, SLOT(showSongDetailsDialog()));

    QAction* act = new QAction(this);
    act->setShortcut(tr("Shift+F1"));
    connect(act, SIGNAL(triggered()), this, SLOT(enableFollowTempo()));
    addAction(act);

    act = new QAction(this);
    act->setShortcut(tr("Alt+F1"));
    connect(act, SIGNAL(triggered()), this, SLOT(disableFollowTempo()));
    addAction(act);

    addShortcutAction("ShortCuts/RightHand",        SLOT(on_rightHand()));
    addShortcutAction("ShortCuts/BothHands",        SLOT(on_bothHands()));
    addShortcutAction("ShortCuts/LeftHand",         SLOT(on_leftHand()));
    addShortcutAction("ShortCuts/PlayFromStart",    SLOT(on_playFromStart()));
    addShortcutAction("ShortCuts/PlayPause",        SLOT(on_playPause()));
    addShortcutAction("ShortCuts/Faster",           SLOT(on_faster()));
    addShortcutAction("ShortCuts/Slower",           SLOT(on_slower()));
    addShortcutAction("ShortCuts/NextSong",         SLOT(on_nextSong()));
    addShortcutAction("ShortCuts/PreviousSong",     SLOT(on_previousSong()));
    addShortcutAction("ShortCuts/NextBook",         SLOT(on_nextBook()));
    addShortcutAction("ShortCuts/PreviousBook",     SLOT(on_previousBook()));

     for (int i = 0; i < maxRecentFiles(); ++i) {
         m_recentFileActs[i] = new QAction(this);
         m_recentFileActs[i]->setVisible(false);
         connect(m_recentFileActs[i], SIGNAL(triggered()),
                 this, SLOT(openRecentFile()));
     }
}

void QtWindow::createMenus()
{
    m_fileMenu = menuBar()->addMenu(tr("&File"));
    m_fileMenu->setToolTipsVisible(true);
    m_fileMenu->addAction(m_openAct);
    m_separatorAct = m_fileMenu->addSeparator();
    for (int i = 0; i < maxRecentFiles(); ++i)
       m_fileMenu->addAction(m_recentFileActs[i]);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exitAct);
    updateRecentFileActions();

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->setToolTipsVisible(true);
    m_viewMenu->addAction(m_sidePanelStateAct);
    m_viewMenu->addAction(m_fullScreenStateAct);
    m_viewMenu->addAction(m_viewPianoKeyboard);

    m_songMenu = menuBar()->addMenu(tr("&Song"));
    m_songMenu->setToolTipsVisible(true);
    m_songMenu->addAction(m_songDetailsAct);

    m_setupMenu = menuBar()->addMenu(tr("Set&up"));
    m_setupMenu->setToolTipsVisible(true);
    m_setupMenu->addAction(m_setupMidiAct);
    m_setupMenu->addAction(m_setupKeyboardAct);
    m_setupMenu->addAction(m_setupPreferencesAct);

    m_helpMenu = menuBar()->addMenu(tr("&Help"));
    m_helpMenu->setToolTipsVisible(true);

    QAction* act;
    act = new QAction(tr("&Help"), this);
    act->setToolTip(tr("Piano Booster Help"));
    connect(act, SIGNAL(triggered()), this, SLOT(help()));
    m_helpMenu->addAction(act);

    act = new QAction(tr("&Website"), this);
    act->setToolTip(tr("Piano Booster Website"));
    connect(act, SIGNAL(triggered()), this, SLOT(website()));
    m_helpMenu->addAction(act);

    m_helpMenu->addAction(m_shortcutAct);
    m_helpMenu->addAction(m_aboutAct);
}

void QtWindow::openRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
     if (action)
         m_settings->openSongFile(action->data().toString());
}

void QtWindow::showMidiSetup(){

    m_topBar->stopMuiscPlaying();

    m_glWidget->stopTimerEvent();
    GuiMidiSetupDialog midiSetupDialog(this);
    midiSetupDialog.init(m_song, m_settings);
    midiSetupDialog.exec();
    m_connectedMidiInput = m_settings->value("Midi/Input").toString(); // the dialog opened it
    m_song->flushMidiInput();
    applyPianistSound(); // the dialog may have restarted the synthesizer
    m_glWidget->startTimerEvent();
}

// load the recent file list from the config file into the file menu
void QtWindow::updateRecentFileActions()
{

    QStringList files = m_settings->value("RecentFileList").toStringList();

    int numRecentFiles = qMin(files.size(), maxRecentFiles());

    for (int i = 0; i < numRecentFiles; ++i) {
        QString text = tr("&%1 %2").arg(i + 1).arg(strippedName(files[i]));
        if (m_recentFileActs[i] == nullptr)
            break;
        m_recentFileActs[i]->setText(text);
        m_recentFileActs[i]->setData(files[i]);
        m_recentFileActs[i]->setVisible(true);
    }

    for (int j = numRecentFiles; j < maxRecentFiles(); ++j) {
        if (m_recentFileActs[j] == nullptr)
            break;
        m_recentFileActs[j]->setVisible(false);
    }

    if (m_separatorAct)
        m_separatorAct->setVisible(numRecentFiles > 0);
}

QString QtWindow::strippedName(const QString &fullFileName)
{
    return QFileInfo(fullFileName).fileName();
}

// Just used for the RecentFileList
void QtWindow::setCurrentFile(const QString &fileName)
{

    setWindowFilePath(fileName);

    QStringList files = m_settings->value("RecentFileList").toStringList();
    files.removeAll(fileName);
    files.prepend(fileName);
    while (files.size() > maxRecentFiles())
        files.removeLast();

    m_settings->setValue("RecentFileList", files);

    updateRecentFileActions();

}

void QtWindow::website()
{
    QDesktopServices::openUrl(QUrl("https://www.pianobooster.org/"));
}

void QtWindow::help()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle (tr("Piano Booster Help"));
    msgBox.setText(
    tr("<h3>Getting Started</h3>") %
    tr("<p>You need a <b>MIDI Piano Keyboard </b> and a <b>MIDI interface</b> for the PC. If you "
       "don't have a MIDI keyboard you can still try out PianoBooster using the PC keyboard, 'X' is "
       "middle C.</p>") %

    tr("<p>PianoBooster now includes a built-in sound generator called FluidSynth "
    "which requires a General MIDI (GM) SoundFont. "
    "Use the ‘Setup/MIDI Setup’ menu option and then the load button on the FluidSynth tab to install the SoundFont.</p>")   %

    tr("<p>PianoBooster works best with MIDI files that have separate left and right piano parts "
       "using MIDI channels 3 and 4.") %
    tr("<h3>Setting Up</h3>") %
    tr("<p>First use the <i>Setup/MIDI Setup</i> menu and in the dialog box select the MIDI input and MIDI "
       "output interfaces that match your hardware. ") %
    tr("Next use <i>File/Open</i> to open the MIDI file \".mid\" or a karaoke \".kar\" file. "
       "Now select whether you want to just <i>listen</i> to the music or "
       "<i>play along</i> on the piano keyboard by setting the <i>skill</i> level on the side panel. Finally when "
       "you are ready click the <i>play icon</i> (or press the <i>space bar</i>) to roll the music.") %
    tr("<h3>Hints on Playing the Piano</h3>"
       "<p>For hints on how to play the piano see: ") %
       "<a href=\"https://www.pianobooster.org/music-info.html\" ><b>" % tr("Piano Hints") % QStringLiteral("</b></a></p>") %
    tr("<h3>More Information</h3>"
       "<p>For more help please visit the PianoBooster ") %
       "<a href=\"https://www.pianobooster.org\" ><b>" + tr("website") + "</b></a>, " %
    tr("the PianoBooster") + " <a href=\"https://www.pianobooster.org/faq.html\" ><b> " + tr("FAQ") + QStringLiteral("</b></a> ") %
    tr("and the") % QStringLiteral(" <a href=\"http://piano-booster.2625608.n2.nabble.com/Piano-Booster-Users-f1591936.html\"><b>") % tr("user forum") % QStringLiteral("</b></a>.")
    );

    msgBox.setMinimumWidth(600);
    msgBox.exec();
}

void QtWindow::about()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle (tr("About Piano Booster"));
    msgBox.setText(
            tr("<b>PianoBooster - Version %1</b> <br><br>").arg(PB_VERSION) %
            tr("<b>Boost</b> your <b>Piano</b> playing skills!<br><br>") %
            QStringLiteral("<a href=\"https://www.pianobooster.org/\" ><b>https://www.pianobooster.org/</b></a><br><br>") %
            tr("Copyright(c) L. J. Barman, 2008-2020; All rights reserved.<br>") %
            tr("Copyright(c) Fabien Givors, 2018-2019; All rights reserved.<br>") %
            tr("Copyright(c) Marius Kittler, 2021-2022; All rights reserved.<br>") %
            QStringLiteral("<br>") %
            tr("This program is made available "
                "under the terms of the GNU General Public License version 3 as published by "
                "the Free Software Foundation.<br><br>"
            )
            #ifdef USE_BUNDLED_RTMIDI
             %
            tr("This program also contains RtMIDI: realtime MIDI i/o C++ classes<br>") %
            tr("Copyright(c) Gary P. Scavone, 2003-2019; All rights reserved.")
            #endif
    );
    msgBox.setMinimumWidth(600);
    msgBox.exec();
}

QString QtWindow::displayShortCut(const QString &key, const QString &description)
{
    QString str = QStringLiteral("<tr>"
                "<td>%1</td>"
                "<td>%2</td>"
                "</tr>").arg( description, tr(m_settings->value(key).toString().toUtf8().data()));
    return str;

}

void QtWindow::keyboardShortcuts()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle (tr("PC Keyboard ShortCuts"));
    QString msg =
            tr(
                "<h2><center>Keyboard shortcuts</center></h2>"
                "<p>The following PC keyboard shortcuts have been defined.</p>"
                "<center><table  border='1' cellspacing='0' cellpadding='4' >"
                );

    msg += tr(
                "<tr>"
                "<th>Action</th>"
                "<th>Key</th>"
                "</tr>"
            );
    msg += displayShortCut("ShortCuts/RightHand", tr("Choose the right hand"));
    msg += displayShortCut("ShortCuts/BothHands", tr("Choose both hands"));
    msg += displayShortCut("ShortCuts/LeftHand", tr("Choose the left Hand"));
    msg += displayShortCut("ShortCuts/PlayFromStart", tr("Play from start toggle"));
    msg += displayShortCut("ShortCuts/PlayPause", tr("Play Pause Toggle"));
    msg += displayShortCut("ShortCuts/Faster",  tr("Increase the speed by 5%"));
    msg += displayShortCut("ShortCuts/Slower", tr("Increase the speed by 5%"));
    msg += displayShortCut("ShortCuts/NextSong", tr("Change to the Next Song"));
    msg += displayShortCut("ShortCuts/PreviousSong", tr("Change to the Previous Song"));
    msg += displayShortCut("ShortCuts/NextBook", tr("Change to the Next Book"));
    msg += displayShortCut("ShortCuts/PreviousBook", tr("Change to the Previous Book"));

    msg += tr(
                "<tr><td>Fake Piano keys</td><td>X is middle C</td></tr>"
                "</table> </center><br>"
                );
    msgBox.setText(msg);

    msgBox.setMinimumWidth(600);
    msgBox.exec();
}

void QtWindow::open()
{
    m_glWidget->stopTimerEvent();

    const auto currentSong = QFileInfo(m_settings->getCurrentSongLongFileName());
    const auto dir = currentSong.isFile() ? currentSong.path() : QDir::homePath();
    const auto fileName = QFileDialog::getOpenFileName(this,tr("Open MIDI File"),
                            dir, tr("MIDI Files") + " (*.mid *.MID *.midi *.MIDI *.kar *.KAR)");
    if (!fileName.isEmpty()) {
        m_settings->openSongFile(fileName);
        setCurrentFile(fileName);
    }
    m_song->flushMidiInput();
    m_glWidget->startTimerEvent();
}

void QtWindow::readSettings()
{
    QPoint pos = m_settings->value("Window/Pos", QPoint(25, 25)).toPoint();
    QSize size = m_settings->value("Window/Size", QSize(1200, 800)).toSize();
    resize(size);
    move(pos);
}

void QtWindow::writeSettings()
{
    m_settings->setValue("Window/Pos", pos());
    m_settings->setValue("Window/Size", size());
    m_settings->writeSettings();
}

void QtWindow::createPracticeFeatures()
{
    // View: falling notes or notation
    m_highwayAct = new QAction(tr("&Falling Notes"), this);
    m_highwayAct->setToolTip(tr("Show falling notes above a keyboard instead of the music notation (scroll to zoom)"));
    m_highwayAct->setShortcut(tr("F7"));
    m_highwayAct->setCheckable(true);
    m_highwayAct->setChecked(m_glWidget->isHighwayMode());
    connect(m_highwayAct, &QAction::toggled, this, [this](bool checked) { m_glWidget->setHighwayMode(checked); });
    m_viewMenu->insertAction(m_viewMenu->actions().value(0), m_highwayAct);

    // Practice menu
    m_practiceMenu = new QMenu(tr("&Practice"), this);
    m_practiceMenu->setToolTipsVisible(true);
    menuBar()->insertMenu(m_setupMenu->menuAction(), m_practiceMenu);

    m_countInAct = m_practiceMenu->addAction(tr("&Count-in"));
    m_countInAct->setToolTip(tr("Count one bar in before the music starts"));
    m_countInAct->setCheckable(true);
    m_countInAct->setChecked(m_settings->value("Practice/CountIn", true).toBool());
    m_song->setCountIn(m_countInAct->isChecked());
    connect(m_countInAct, &QAction::toggled, this, [this](bool checked) {
        m_song->setCountIn(checked);
        m_settings->setValue("Practice/CountIn", checked);
    });

    m_metronomeAct = m_practiceMenu->addAction(tr("&Metronome"));
    m_metronomeAct->setToolTip(tr("Click on every beat"));
    m_metronomeAct->setShortcut(tr("F6"));
    m_metronomeAct->setCheckable(true);
    m_metronomeAct->setChecked(m_settings->value("Practice/Metronome", false).toBool());
    m_song->setMetronome(m_metronomeAct->isChecked());
    connect(m_metronomeAct, &QAction::toggled, this, [this](bool checked) {
        m_song->setMetronome(checked);
        m_settings->setValue("Practice/Metronome", checked);
    });

    m_autoSpeedAct = m_practiceMenu->addAction(tr("&Auto Speed-up in Loops"));
    m_autoSpeedAct->setToolTip(tr("When looping bars, raise the speed by 5% after every pass with at least 90% accuracy"));
    m_autoSpeedAct->setCheckable(true);
    m_autoSpeedAct->setChecked(m_settings->value("Practice/AutoSpeed", true).toBool());
    connect(m_autoSpeedAct, &QAction::toggled, this, [this](bool checked) {
        m_settings->setValue("Practice/AutoSpeed", checked);
    });

    m_soundMenu = m_practiceMenu->addMenu(tr("Your &Sound"));
    m_soundMenu->setToolTipsVisible(true);
    connect(m_soundMenu, &QMenu::aboutToShow, this, &QtWindow::buildSoundMenu);
    setupSoundBanks();

    m_practiceMenu->addSeparator();
    QAction *act = m_practiceMenu->addAction(tr("&Next Lesson"));
    act->setToolTip(tr("Go to the first song of this book without three stars"));
    act->setShortcut(tr("Ctrl+L"));
    connect(act, &QAction::triggered, this, &QtWindow::nextLesson);
    act = m_practiceMenu->addAction(tr("Your &Progress..."));
    act->setShortcut(tr("Ctrl+Shift+P"));
    connect(act, &QAction::triggered, this, &QtWindow::showProgressDialog);
    act = m_practiceMenu->addAction(tr("&Timing Calibration..."));
    act->setToolTip(tr("Compensate for the delay of your sound system when judging early and late notes"));
    connect(act, &QAction::triggered, this, &QtWindow::setTimingCalibration);
    m_song->setTimingCalibration(m_settings->value("Practice/TimingCalibrationMs", 0).toInt());

    connect(m_glWidget, &CGLView::runFinished, this, &QtWindow::onRunFinished);
    connect(m_glWidget, &CGLView::barRangeSelected, this, [this](double fromBar, double bars) {
        m_topBar->setLoop(fromBar, bars);
        if (bars > 0)
            statusBar()->showMessage(tr("Looping bars %1 to %2. Press Space to start, click the progress bar to stop looping.")
                                     .arg(fromBar).arg(fromBar + bars - 1), 8000);
        else
            statusBar()->showMessage(tr("Playing from bar %1. Press Space to start.").arg(fromBar), 5000);
    });

    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);
    m_practiceTimer = new QTimer(this);
    connect(m_practiceTimer, &QTimer::timeout, this, &QtWindow::practiceTick);
    m_practiceTimer->start(1000);
    auto *midiTimer = new QTimer(this);
    connect(midiTimer, &QTimer::timeout, this, &QtWindow::checkMidiInputs);
    midiTimer->start(2000);
    QTimer::singleShot(0, this, &QtWindow::checkMidiInputs);
    // m_connectedMidiInput stays empty: the first check confirms (or reopens) the configured input
    m_lastPracticeTickUs = monotonicTimeUs();
    updateStatusBar();
}

// Find the installed SoundFonts, upgrade from the fallback bank and choose a first piano sound.
void QtWindow::setupSoundBanks()
{
#if WITH_INTERNAL_FLUIDSYNTH
    if (m_settings->updateSoundBanks() && m_settings->value("Midi/Output").toString() == CMidiDeviceFluidSynth::getFluidInternalName())
        m_song->openMidiPort(CMidiDevice::MIDI_OUTPUT, CMidiDeviceFluidSynth::getFluidInternalName()); // restart with the new bank
    if (!m_settings->contains("Sound/Mode"))
    {
        // the first time: the best piano bank installed (the Salamander grand, else any grand/piano)
        const QStringList extras = m_settings->value("FluidSynth/ExtraSoundFonts").toStringList();
        QString piano;
        for (const QString &pattern : {QStringLiteral("salamander"), QStringLiteral("grand"), QStringLiteral("piano")})
        {
            for (const QString &font : extras)
                if (QFileInfo(font).completeBaseName().contains(pattern, Qt::CaseInsensitive))
                {
                    piano = font;
                    break;
                }
            if (!piano.isEmpty())
                break;
        }
        if (!piano.isEmpty())
        {
            m_settings->setValue("Sound/Mode", "preset");
            m_settings->setValue("Sound/PianistFont", piano);
            m_settings->setValue("Sound/PianistBank", -1);
            m_settings->setValue("Sound/PianistProgram", -1); // its first instrument
        }
    }
#endif
    applyPianistSound();
}

void QtWindow::useKeyboardSetupSound()
{
    m_settings->setValue("Sound/Mode", "keyboard");
    applyPianistSound();
}

void QtWindow::applyPianistSound()
{
    const QString mode = m_settings->value("Sound/Mode", "keyboard").toString();
    QApplication::setOverrideCursor(Qt::WaitCursor); // a big piano SoundFont takes a moment to load
    if (mode == "song")
        m_song->setPianistSound(QString(), 0, 0, true);
    else if (mode == "preset")
    {
        const QString font = m_settings->value("Sound/PianistFont").toString();
        if (m_settings->value("Sound/PianistProgram").toInt() < 0 && m_song->addSoundFont(font))
        {
            // "the bank's first instrument": remember which one that is
            for (const SoundPreset &preset : m_song->soundPresets())
                if (preset.font == font && preset.program >= 0)
                {
                    m_settings->setValue("Sound/PianistBank", preset.bank);
                    m_settings->setValue("Sound/PianistProgram", preset.program);
                    break;
                }
        }
        m_song->setPianistSound(m_settings->value("Sound/PianistFont").toString(), m_settings->value("Sound/PianistBank").toInt(),
                                m_settings->value("Sound/PianistProgram").toInt(), false);
    }
    else // the General MIDI patch from Setup / Keyboard
        m_song->setPianistSound(QString(), 0, 0, false);
    QApplication::restoreOverrideCursor();
    if (qEnvironmentVariableIsSet("PB_LATENCY_LOG"))
        fprintf(stderr, "PB_SOUND mode=%s font=%s bank=%d program=%d presets=%d\n", qPrintable(mode),
                qPrintable(m_settings->value("Sound/PianistFont").toString()), m_settings->value("Sound/PianistBank").toInt(),
                m_settings->value("Sound/PianistProgram").toInt(), static_cast<int>(m_song->soundPresets().size()));
}

// The Practice / Your Sound menu lists the instruments of the installed SoundFonts.
void QtWindow::buildSoundMenu()
{
    m_soundMenu->clear();
    const QString mode = m_settings->value("Sound/Mode", "keyboard").toString();
    auto addChoice = [this](const QString &text, bool checked, const std::function<void()> &choose) {
        QAction *act = m_soundMenu->addAction(text);
        act->setCheckable(true);
        act->setChecked(checked);
        connect(act, &QAction::triggered, this, [this, choose, text]() {
            choose();
            applyPianistSound();
            statusBar()->showMessage(tr("Your sound: %1").arg(text), 4000);
        });
        return act;
    };

    addChoice(tr("The Song's Instrument"), mode == "song", [this]() { m_settings->setValue("Sound/Mode", "song"); })
        ->setToolTip(tr("Play with the instrument the song uses for your part"));
    addChoice(tr("Keyboard Setup Sound"), mode == "keyboard", [this]() { m_settings->setValue("Sound/Mode", "keyboard"); })
        ->setToolTip(tr("The General MIDI sound chosen in Setup / Keyboard"));

    const QList<SoundPreset> presets = m_song->soundPresets();
    if (presets.isEmpty())
    {
        m_soundMenu->addSection(tr("Instruments"));
        m_soundMenu->addAction(tr("(only available with the internal sound)"))->setEnabled(false);
    }
    QString section;
    for (const SoundPreset &preset : presets)
    {
        if (preset.fontName != section)
        {
            section = preset.fontName;
            m_soundMenu->addSection(section);
        }
        const bool chosen = mode == "preset" && m_settings->value("Sound/PianistFont").toString() == preset.font &&
                            m_settings->value("Sound/PianistBank").toInt() == preset.bank &&
                            m_settings->value("Sound/PianistProgram").toInt() == preset.program;
        const QString text = (preset.program < 0) ? preset.name + QStringLiteral(" \u2026") : preset.name;
        QAction *act = addChoice(text, chosen, [this, preset]() {
            m_settings->setValue("Sound/Mode", "preset");
            m_settings->setValue("Sound/PianistFont", preset.font);
            m_settings->setValue("Sound/PianistBank", preset.bank);
            m_settings->setValue("Sound/PianistProgram", preset.program);
        });
        if (preset.program < 0)
            act->setToolTip(tr("Loads this sound bank when chosen"));
    }

    m_soundMenu->addSeparator();
    connect(m_soundMenu->addAction(tr("Add Sound Bank...")), &QAction::triggered, this, [this]() {
        const QString font = QFileDialog::getOpenFileName(this, tr("Add a SoundFont"), QDir::homePath(), tr("SoundFonts (*.sf2 *.sf3)"));
        if (font.isEmpty())
            return;
        QStringList extras = m_settings->value("FluidSynth/ExtraSoundFonts").toStringList();
        if (!extras.contains(font))
            extras.append(font);
        m_settings->setValue("FluidSynth/ExtraSoundFonts", extras);
        m_settings->setValue("Sound/Mode", "preset");
        m_settings->setValue("Sound/PianistFont", font);
        m_settings->setValue("Sound/PianistBank", -1);
        m_settings->setValue("Sound/PianistProgram", -1);
        applyPianistSound();
    });
    connect(m_soundMenu->addAction(tr("Open the Sound Banks Folder")), &QAction::triggered, this, []() {
        const QString folder = CSettings::soundFontFolders().value(1);
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
}

QString QtWindow::currentSongKey()
{
    if (!m_settings->getCurrentSongName().isEmpty())
        return m_settings->getCurrentBookName() + '/' + m_settings->getCurrentSongName();
    return m_settings->value("CurrentSong").toString();
}

void QtWindow::refreshHighwaySettings()
{
    m_glWidget->setHighwayMode(m_glWidget->isHighwayMode()); // re-reads the keyboard range
}

void QtWindow::updateStatusBar()
{
    if (m_statusLabel)
        m_statusLabel->setText(GuiProgressDialog::summaryText(CProgress::instance()));
}

// Count the time spent actually practising: playing with the pianist active recently.
void QtWindow::practiceTick()
{
    const qint64 nowUs = monotonicTimeUs();
    const double seconds = static_cast<double>(nowUs - m_lastPracticeTickUs) / 1e6;
    m_lastPracticeTickUs = nowUs;
    const bool practising = m_song->playingMusic() && m_song->getPlayMode() != PB_PLAY_MODE_listen &&
                            nowUs - m_song->lastPianistInputUs() < 15 * 1000000LL;
    if (!practising || seconds > 5.0)
        return;
    CProgress &progress = CProgress::instance();
    const bool goalWasReached = progress.goalReached(QDate::currentDate());
    progress.addPracticeSeconds(seconds);
    if (++m_unsavedPracticeSeconds >= 30)
    {
        m_unsavedPracticeSeconds = 0;
        progress.save();
    }
    if (!goalWasReached && progress.goalReached(QDate::currentDate()))
        statusBar()->showMessage(tr("Daily goal reached, well done!"), 8000);
    updateStatusBar();
}

void QtWindow::onRunFinished(const PerformanceResult &result, bool loopPass)
{
    CProgress &progress = CProgress::instance();
    if (loopPass)
    {
        // Melodics-style "auto BPM": speed up after every good pass, up to the original tempo
        if (m_autoSpeedAct->isChecked() && result.accuracy >= 90.0)
        {
            const int speed = qRound(m_song->getSpeed() * 100.0f);
            if (speed < 100)
            {
                const int newSpeed = qMin(100, speed + 5);
                m_topBar->setSpeed(newSpeed);
                statusBar()->showMessage(tr("Good pass! Speed raised to %1%").arg(newSpeed), 4000);
            }
        }
        return;
    }
    const bool newBest = progress.recordRun(currentSongKey(), result);
    progress.save();
    m_sidePanel->updateSongStars();
    if (newBest && progress.song(currentSongKey()).plays > 1)
        statusBar()->showMessage(tr("New personal best: %1%").arg(qRound(result.accuracy)), 6000);
    if (qEnvironmentVariableIsSet("PB_LATENCY_LOG"))
        fprintf(stderr, "PB_RESULT accuracy=%.1f stars=%d perfect=%d good=%d early=%d late=%d missed=%d wrong=%d meanOffsetMs=%.1f\n",
                result.accuracy, result.stars, result.perfect, result.good, result.early, result.late, result.missed,
                result.wrong, result.meanOffsetMs);
    updateStatusBar();
}

// Hot-plugging: connect the MIDI keyboard when it is plugged in (also after it was unplugged, or
// when it comes back with a different port number), so it just works without visiting the setup.
void QtWindow::checkMidiInputs()
{
    if (QApplication::activeModalWidget())
        return; // e.g. the MIDI setup dialog is open
    const QStringList ports = m_song->getMidiPortList(CMidiDevice::MIDI_INPUT);
    const bool portsChanged = ports != m_lastMidiInputPorts;
    m_lastMidiInputPorts = ports;
    auto withoutIndex = [](const QString &port) { return port.section(" - ", 1); };
    // the smoke tests name their port here, without touching the saved settings
    const QString testInput = qEnvironmentVariable("PB_MIDI_INPUT");
    const QString wanted = testInput.isEmpty() ? m_settings->value("Midi/Input").toString() : "0 - " + testInput;
    if (wanted.startsWith(tr("None")))
        return; // the user chose the PC keyboard
    QString found;
    for (const QString &port : ports) // the configured keyboard, maybe under a new number
        if (!wanted.isEmpty() && withoutIndex(port) == withoutIndex(wanted))
            found = port;
    if (found.isEmpty())
        m_connectedMidiInput.clear(); // unplugged: reconnect when it comes back
    else if (found == m_connectedMidiInput && !portsChanged)
        return; // all fine (when devices come and go, reopen: it may have been unplugged and back)
    if (found.isEmpty() && testInput.isEmpty())
    {
        // the configured keyboard is not there: take the first real-looking device that is
        for (const QString &port : ports)
        {
            const QString name = withoutIndex(port);
            if (!name.contains("IAC", Qt::CaseInsensitive) && !name.contains("Through", Qt::CaseInsensitive) &&
                !name.contains("Network", Qt::CaseInsensitive) && !name.contains("Virtual", Qt::CaseInsensitive))
            {
                found = port;
                break;
            }
        }
    }
    if (found.isEmpty() || (found == m_connectedMidiInput && !portsChanged))
        return;
    if (m_song->openMidiPort(CMidiDevice::MIDI_INPUT, found))
    {
        m_connectedMidiInput = found;
        if (testInput.isEmpty())
            m_settings->setValue("Midi/Input", found);
        CChord::setPianoRange(m_settings->value("Keyboard/LowestNote", 0).toInt(),
                              m_settings->value("Keyboard/HighestNote", 127).toInt());
        m_settings->updateWarningMessages();
        statusBar()->showMessage(tr("MIDI keyboard connected: %1").arg(withoutIndex(found)), 6000);
    }
}

void QtWindow::showProgressDialog()
{
    GuiProgressDialog dialog(this, &CProgress::instance(), m_settings);
    dialog.exec();
    updateStatusBar();
}

void QtWindow::nextLesson()
{
    if (!m_sidePanel->selectNextLesson())
        statusBar()->showMessage(tr("Every song in this book has three stars. Try the next book!"), 6000);
}

void QtWindow::setTimingCalibration()
{
    bool ok = false;
    const int ms = QInputDialog::getInt(this, tr("Timing Calibration"),
        tr("If notes you play on the beat are judged as late, enter that delay here (milliseconds).\n"
           "Positive values make the judgement more forgiving for late notes."),
        m_song->getTimingCalibration(), -200, 200, 5, &ok);
    if (!ok)
        return;
    m_song->setTimingCalibration(ms);
    m_settings->setValue("Practice/TimingCalibrationMs", ms);
}

void QtWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    if (m_song->playingMusic())
    {
        m_song->playMusic(false);
    }
    CProgress::instance().save();

    writeSettings();
}

void QtWindow::keyPressEvent ( QKeyEvent * event )
{
    if (event->text().length() == 0)
        return;

    if (event->isAutoRepeat() == true)
        return;

    if (event->key() == Qt::Key_F1)
        return;

    int c = event->text().toLatin1().at(0);
    m_song->pcKeyPress( c, true);
}

void QtWindow::keyReleaseEvent ( QKeyEvent * event )
{
    if (event->isAutoRepeat() == true)
        return;

    if (event->text().length() == 0)
        return;

    int c = event->text().toLatin1().at(0);
    m_song->pcKeyPress( c, false);
}

void QtWindow::loadTutorHtml(const QString & name)
{
    if (name.isEmpty())
    {
        m_tutorWindow->hide();
        m_tutorWindow->clear();
    }
    else
    {
        QFile file(name);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

        QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        out.setEncoding(QStringConverter::Utf8);
#else
        out.setCodec("UTF-8");
#endif

        QString htmlStart = "<head><style> body{background-color:#FFFFC0;color: black} p{font-size: 18px;} blockquote{color: #ff0000;}</style></head><body>";
        QString htmlBody = out.readAll();
        QString htmlEnd = "</body>";
        QString htmlText = htmlStart + htmlBody + htmlEnd;
        m_tutorWindow->setHtml(htmlText.toUtf8().data());

        // TODO get this working again on small screens
        //_tutorWindow->setFixedHeight(130);
        m_tutorWindow->setFixedHeight(180);

        m_tutorWindow->show();

        file.close();
    }

}

void QtWindow::refreshTranslate(){
#ifndef NO_LANGS
    QString locale = m_settings->selectedLangauge();

    qApp->removeTranslator(&translator);
    qApp->removeTranslator(&translatorMusic);
    qApp->removeTranslator(&qtTranslator);

    // save original
    if (listWidgetsRetranslateUi.size()==0){
        QList<QWidget*> l2 = this->findChildren<QWidget *>();
        for (auto &w:l2){
            QMap<QString,QString> m;
            m["toolTip"]=w->toolTip();
            m["whatsThis"]=w->whatsThis();
            m["windowTitle"]=w->windowTitle();
            m["statusTip"]=w->statusTip();
            listWidgetsRetranslateUi[w]=m;
        }

        QList<QAction*> l = this->findChildren<QAction *>();
        for (auto &w:l){
            QMap<QString,QString> m;
            m["toolTip"]=w->toolTip();
            m["whatsThis"]=w->whatsThis();
            m["statusTip"]=w->statusTip();
            m["text"]=w->text();
            listActionsRetranslateUi[w]=m;
        }
    }

    QString translationsDir = QApplication::applicationDirPath() + "/translations/";

    QFile fileTestLocale(translationsDir);
    if (!fileTestLocale.exists()){
 #if defined (Q_OS_LINUX) || defined (Q_OS_UNIX)
        translationsDir=Util::dataDir()+"/translations/";
 #endif
 #ifdef Q_OS_DARWIN
        translationsDir=QApplication::applicationDirPath() + "/../Resources/translations/";
 #endif
    }
    ppLogInfo("Translations loaded from '%s'",  qPrintable(translationsDir));

    // set translator for app
    auto ok = true;
    if (!translator.load(QSTR_APPNAME + QString("_") + locale , translationsDir))
        ok = ok & translator.load(QSTR_APPNAME + QString("_") + locale, QApplication::applicationDirPath());
    qApp->installTranslator(&translator);

    // set translator for music
    if (!translatorMusic.load(QString("music_") + locale , translationsDir))
       if (!translatorMusic.load(QString("music_") + locale, QApplication::applicationDirPath()  + "/translations/"))
           ok = ok & translatorMusic.load(QString("music_") + locale, QApplication::applicationDirPath());
    qApp->installTranslator(&translatorMusic);

    // set translator for default widget's text (for example: QMessageBox's buttons)
#ifdef __WIN32
    ok = ok & qtTranslator.load("qt_"+locale, translationsDir);
#elif QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    ok = ok & qtTranslator.load("qt_"+locale, QLibraryInfo::path(QLibraryInfo::TranslationsPath));
#else
    ok = ok & qtTranslator.load("qt_"+locale, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
#endif
    qApp->installTranslator(&qtTranslator);

    if (!ok) {
        qDebug() << "Unable to load all translations";
    }

    // retranslate UI
    QList<QWidget*> l2 = this->findChildren<QWidget *>();
    for (auto &w:l2){
        if (!w->toolTip().isEmpty()) w->setToolTip(tr(listWidgetsRetranslateUi[w]["toolTip"].toStdString().c_str()));
        if (!w->whatsThis().isEmpty()) w->setWhatsThis(tr(listWidgetsRetranslateUi[w]["whatsThis"].toStdString().c_str()));
        if (!w->windowTitle().isEmpty()) w->setWindowTitle(tr(listWidgetsRetranslateUi[w]["windowTitle"].toStdString().c_str()));
        if (!w->statusTip().isEmpty()) w->setStatusTip(tr(listWidgetsRetranslateUi[w]["statusTip"].toStdString().c_str()));
    }

    QList<QAction*> l = this->findChildren<QAction *>();
    for (auto &w:l){
        if (!w->toolTip().isEmpty()) w->setToolTip(tr(listActionsRetranslateUi[w]["toolTip"].toStdString().c_str()));
        if (!w->whatsThis().isEmpty()) w->setWhatsThis(tr(listActionsRetranslateUi[w]["whatsThis"].toStdString().c_str()));
        if (!w->statusTip().isEmpty()) w->setStatusTip(tr(listActionsRetranslateUi[w]["statusTip"].toStdString().c_str()));
        if (!w->text().isEmpty()) w->setText(tr(listActionsRetranslateUi[w]["text"].toStdString().c_str()));
    }

    m_sidePanel->updateTranslate();
    m_topBar->updateTranslate();
    m_settings->updateWarningMessages();
    m_settings->updateTutorPage();

#endif
}
