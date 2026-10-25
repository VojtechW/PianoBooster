/*********************************************************************************/
/*!
@file           GlView.cpp

@brief          xxxx

@author         L. J. Barman

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

*/
/*********************************************************************************/

#include <QtWidgets>
#include <QtOpenGL>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_1_1>
#include <QPaintEngine>

#include <cmath>

#include "QtWindow.h"
#include "GlView.h"
#include "Cfg.h"
#include "Draw.h"

// This defines the PB Open GL frame per seconds.
// Try to make sure this runs a bit faster than the screen refresh rate of 60z (or 16.6 msec)
// Ask for a repaint on every engine tick: Qt coalesces the requests to the display's refresh
// (60 or 120 Hz), so the picture is always as fresh as the screen allows.
#define SCREEN_FRAME_RATE 4

#define REDRAW_COUNT ((m_cfg_openGlOptimise >= 2) ? 1 : 2) // there are two gl buffers but redrawing once is best (set 2 with buggy gl drivers)

#define TEXT_LEFT_MARGIN 30

CGLView::CGLView(QtWindow* parent, CSettings* settings)
    : QOpenGLWidget(parent)
{
    m_qtWindow = parent;
    m_settings = settings;
    m_rating = nullptr;
    m_fullRedrawFlag = true;
    m_forcefullRedraw = 0;
    m_forceRatingRedraw = 0;
    m_forceBarRedraw = 0;
    m_allowedTimerEvent = true;

    m_backgroundColor = QColor(0, 0, 0);

    m_song = new CSong();
    m_score = new CScore(m_settings);
    m_displayUpdateTicks = 0;
    m_cfg_openGlOptimise = 0; // zero is no GlOptimise
    m_eventBits = 0;
    BENCHMARK_INIT();
}

CGLView::~CGLView()
{
    delete m_song;
    delete m_score;
    m_titleHeight = 0;
}

QSize CGLView::minimumSizeHint() const
{
    return QSize(200, 200);
}

QSize CGLView::sizeHint() const
{
    return QSize(200, 800); //fixme this does not work
}

void CGLView::stopTimerEvent()
{
    m_allowedTimerEvent=false;
}

void CGLView::startTimerEvent()
{
    m_allowedTimerEvent=true;
}

void CGLView::paintGL()
{
    BENCHMARK(2, "enter");

    if (m_highwayMode)
    {
        paintHighway();
        return;
    }
    if (m_restoreNotationViewport)
    {
        m_restoreNotationViewport = false;
        m_score->setNotationShown(true);
        m_score->refreshScroll();
        CColor color = Cfg::backgroundColor();
        glClearColor(color.red, color.green, color.blue, 0.0);
        resizeGL(m_width, m_height);
        m_fullRedrawFlag = true;
    }

    m_displayUpdateTicks = 0;

    if (m_fullRedrawFlag)
        m_forcefullRedraw = m_forceRatingRedraw = m_forceBarRedraw = REDRAW_COUNT;

    if (m_forcefullRedraw) // clear the screen only if we are doing a full redraw
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glLoadIdentity();
    //BENCHMARK(3, "glLoadIdentity");

    drawDisplayText();
    BENCHMARK(4, "drawDisplayText");

    drawAccurracyBar();
    BENCHMARK(5, "drawAccurracyBar");

    drawBarNumber();
    BENCHMARK(6, "drawBarNumber");

    if (m_forcefullRedraw)
        m_score->drawScore();

    drawTimeSignature();

    updateMidiTask();
    m_score->drawScroll(m_forcefullRedraw);
    BENCHMARK(10, "drawScroll");

    if (m_forcefullRedraw) m_forcefullRedraw--;
    BENCHMARK(11, "exit");
    BENCHMARK_RESULTS();
}

void CGLView::drawTimeSignature()
{
    if (m_forcefullRedraw == 0)
        return;

    float x,y;
    int topNumber, bottomNumber;

    if (m_song == nullptr) return;

    m_song->getTimeSig(&topNumber, &bottomNumber);
    if (topNumber == 0 ) return;

    char bufferTop[10], bufferBottom[10];
    sprintf(bufferTop, "%d", topNumber);
    sprintf(bufferBottom, "%d", bottomNumber);

    x = Cfg::timeSignatureX();

    CDraw::drColor ((CDraw::getDisplayHand() != PB_PART_left) ? Cfg::noteColor() : Cfg::noteColorDim());

    y = CStavePos(PB_PART_right,  0).getPosY() + 5;
    renderText(x,y, 0, bufferTop, m_timeSigFont);
    y = CStavePos(PB_PART_right, -3).getPosY() - 2;
    renderText(x,y, 0, bufferBottom, m_timeSigFont);

    CDraw::drColor ((CDraw::getDisplayHand() != PB_PART_right) ? Cfg::noteColor() : Cfg::noteColorDim());

    y = CStavePos(PB_PART_left,   0).getPosY() + 5;
    renderText(x,y, 0, bufferTop, m_timeSigFont);
    y = CStavePos(PB_PART_left,  -3).getPosY() - 2;
    renderText(x,y, 0, bufferBottom, m_timeSigFont);
}

void CGLView::drawAccurracyBar()
{
    if (m_song->getPlayMode() == PB_PLAY_MODE_listen || !m_settings->getWarningMessage().isEmpty())
        return;

    if (m_forceRatingRedraw == 0)
        return;
    m_forceRatingRedraw--;

    float accuracy;
    CColor color;

    float y = static_cast<float>(Cfg::getAppHeight() - 14);
    const float x = static_cast<float>(accuracyBarStart);
    const int width = 360;
    const int lineWidth = 8/2;

    m_rating->calculateAccuracy();

    accuracy = m_rating->getAccuracyValue();
    color = m_rating->getAccuracyColor();
    CDraw::drColor (color);
    glRectf(x, y - lineWidth, x + width * accuracy, y + lineWidth);
    CDraw::drColor (Cfg::backgroundColor());
    glRectf(x + width * accuracy, y - lineWidth, x + width, y + lineWidth);

    glLineWidth (1);
    CDraw::drColor (CColor(1.0, 1.0, 1.0));
    glBegin(GL_LINE_LOOP);
    glVertex2f (x, y + lineWidth);
    glVertex2f (x+ width, y  + lineWidth);
    glVertex2f (x + width, y - lineWidth);
    glVertex2f (x, y - lineWidth);
    glEnd();
}

void CGLView::drawDisplayText()
{
    if (m_rating == nullptr)
    {
        m_rating = m_song->getRating();
        return; // don't run this func the first time it is called
    }

    if (m_forcefullRedraw == 0)
        return;

    int y = Cfg::getAppHeight() - 14;

    if (!m_settings->getWarningMessage().isEmpty())
    {
        glColor3f(1.0f,0.2f,0.0f);
        renderText(TEXT_LEFT_MARGIN, y-4, 0, m_settings->getWarningMessage(), m_timeRatingFont);
        return;
    }

    glColor3f(1.0f,1.0f,1.0f);

    if (m_song->getPlayMode() != PB_PLAY_MODE_listen) {
        if (accuracyBarStart == 0) {
            QFontMetrics fm(m_timeRatingFont);
            accuracyText = tr("Accuracy:");
            accuracyBarStart=fm.boundingRect(accuracyText + "  ").right() + TEXT_LEFT_MARGIN;
       }

        renderText(TEXT_LEFT_MARGIN, y-4,0 ,accuracyText, m_timeRatingFont);
    }

    if (m_titleHeight < 45 )
        return;

    y = Cfg::getAppHeight() - m_titleHeight;

    renderText(TEXT_LEFT_MARGIN, y+6, 0,tr("Song:") + " " + m_song->getSongTitle(), m_timeRatingFont);
    /*
    char buffer[100];
    sprintf(buffer, "Notes %d wrong %d Late %d Score %4.1f%%",
    m_rating->totalNoteCount(), m_rating->wrongNoteCount(),
    m_rating->lateNoteCount(), m_rating->rating());
    renderText(10,20, buffer, m_timeRatingFont);
    */
}

void CGLView::drawBarNumber()
{
    if (m_forceBarRedraw == 0)
        return;
    m_forceBarRedraw--;

    const auto y = static_cast<float>(Cfg::getAppHeight() - m_titleHeight - 34);
    const auto x = static_cast<float>(TEXT_LEFT_MARGIN);

    //CDraw::drColor (Cfg::backgroundColor());
    //CDraw::drColor (Cfg::noteColorDim());
    //glRectf(x+30+10, y-2, x + 80, y + 16);
    glColor3f(1.0f,1.0f,1.0f);
    renderText(x, y, 0, tr("Bar:") + " " + QString::number(m_song->getBarNumber()), m_timeRatingFont);
}

void CGLView::resizeGL(int width, int height)
{
    m_width = width;
    m_height = height;
    const int maxSoreWidth = 1024;
    const int staveEndGap = 20;
    const int heightAboveStave =  static_cast<int>(CStavePos::verticalNoteSpacing() * MAX_STAVE_INDEX);
    const int heightBelowStave =  static_cast<int>(CStavePos::verticalNoteSpacing() * - MIN_STAVE_INDEX);
    const int minTitleHeight = 20;
    const int minStaveGap = 120;
    int staveGap;
    int maxSoreHeight;

    //int space = height - (heightAboveStave + heightBelowStave + minTitleHeight + minStaveGap);
    //m_titleHeight = qBound(minTitleHeight, minTitleHeight + space/2, 70);
    // staveGap = qBound(minStaveGap, minStaveGap+ space/2, static_cast<int>(CStavePos::staveHeight() * 3));
    if (height < 430)  // So it works on an eeepc 701 (for Trev)
    {
        staveGap = minStaveGap;
        m_titleHeight = minTitleHeight;
    }
    else
    {
        staveGap = static_cast<int>(CStavePos::staveHeight() * 3);
        m_titleHeight = 60;
    }
    maxSoreHeight = heightAboveStave + heightBelowStave + staveGap + m_titleHeight;
    if (height > maxSoreHeight) {
        int scoreExtraTopGap = (height - maxSoreHeight);
        maxSoreHeight += qMin(200, scoreExtraTopGap);
    }
    int sizeX = qMin(width, maxSoreWidth);
    int sizeY = qMin(height, maxSoreHeight);
    int x = 0;
    int y = (height - sizeY) - 5;
    glViewport (x, y, sizeX, sizeY);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, sizeX, 0, sizeY, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    CStavePos::setStaveCenterY(static_cast<float>(sizeY) - static_cast<float>(maxSoreHeight)/2.0f - static_cast<float>(m_titleHeight)/2.0f);
    Cfg::setAppDimentions(x, y, sizeX, sizeY);
    Cfg::setStaveEndX(static_cast<float>(sizeX - staveEndGap));
    CStavePos::setStaveCentralOffset(static_cast<float>(staveGap)/2.0f);
    CDraw::forceCompileRedraw();
}

static constexpr float HUD_HEIGHT = 80.0f;            // the title / score area above the falling notes
static constexpr float PROGRESS_BAR_BOTTOM = 72.0f;   // measured down from the top of the view
static constexpr float PROGRESS_BAR_TOP = 64.0f;
static constexpr float PROGRESS_BAR_MARGIN = 20.0f;

bool CGLView::inProgressBar(const QPointF &pos) const
{
    return m_highwayMode && pos.y() >= PROGRESS_BAR_TOP - 10 && pos.y() <= PROGRESS_BAR_BOTTOM + 10;
}

double CGLView::barAt(const QPointF &pos) const
{
    CHighway *highway = m_score->getHighway();
    const double fraction = qBound(0.0, (pos.x() - PROGRESS_BAR_MARGIN) / (m_width - 2 * PROGRESS_BAR_MARGIN), 1.0);
    const double bar = std::floor(highway->ticksToBar(static_cast<qint64>(fraction * static_cast<double>(highway->songLength()))));
    const double lastBar = std::ceil(highway->ticksToBar(highway->songLength())) - 1; // the bar the song ends in
    return std::max(0.0, std::min(bar, lastBar));
}

void CGLView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && inProgressBar(event->position()))
    {
        m_dragFromBar = m_dragToBar = barAt(event->position());
        update();
    }
}

void CGLView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragFromBar < 0)
        return;
    m_dragToBar = barAt(event->position());
    const double from = std::min(m_dragFromBar, m_dragToBar);
    const double bars = (m_dragFromBar == m_dragToBar) ? 0.0 : std::fabs(m_dragToBar - m_dragFromBar) + 1;
    m_dragFromBar = m_dragToBar = -1;
    emit barRangeSelected(from, bars);
    update();
}

void CGLView::wheelEvent(QWheelEvent *event)
{
    if (!m_highwayMode)
        return;
    // scroll to zoom the falling notes in and out
    CHighway *highway = m_score->getHighway();
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;
    highway->setLookAheadBeats(highway->lookAheadBeats() * std::pow(1.15f, -steps));
    m_settings->setValue("View/HighwayLookAhead", highway->lookAheadBeats());
    event->accept();
}

void CGLView::setHighwayMode(bool enable)
{
    m_highwayMode = enable;
    m_restoreNotationViewport = !enable;
    m_score->getHighway()->setLookAheadBeats(m_settings->value("View/HighwayLookAhead", 6.0).toFloat());
    m_score->getHighway()->setKeyboardRange(m_settings->value("Keyboard/LowestNote", 0).toInt(),
                                            m_settings->value("Keyboard/HighestNote", 127).toInt());
    m_settings->setValue("View/Highway", enable);
    update();
}

void CGLView::finishRun(bool loopPass)
{
    if (m_song->getPlayMode() == PB_PLAY_MODE_listen)
        return;
    const PerformanceResult result = m_song->getPerformance()->result();
    if (result.notes() == 0)
        return;
    m_result = result;
    m_resultVisible = true;
    m_resultIsLoopPass = loopPass;
    m_resultTimeUs = monotonicTimeUs();
    emit runFinished(result, loopPass);
}

// Draw the queued text with a single QPainter (creating a painter per string is what makes text
// expensive in a GL widget).
void CGLView::flushText()
{
    if (m_textQueue.empty())
        return;
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        for (const QueuedText &item : m_textQueue)
        {
            painter.setFont(item.font);
            painter.setPen(item.color);
            float x = item.x;
            if (item.align != Qt::AlignLeft)
            {
                const auto textWidth = static_cast<float>(painter.fontMetrics().horizontalAdvance(item.text));
                x -= (item.align == Qt::AlignRight) ? textWidth : textWidth / 2.0f;
            }
            painter.drawText(QPointF(x, static_cast<float>(m_height) - item.y), item.text);
        }
    }
    m_textQueue.clear();
    context()->functions()->glUseProgram(0); // back to the fixed-function pipeline for the next frame
}

void CGLView::paintHighway()
{
    static const bool logFrames = qEnvironmentVariableIsSet("PB_FRAME_LOG");
    const qint64 frameStartUs = monotonicTimeUs();
    const auto width = static_cast<float>(m_width);
    const auto height = static_cast<float>(m_height);
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, static_cast<GLsizei>(m_width * dpr), static_cast<GLsizei>(m_height * dpr));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_width, 0, m_height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.035f, 0.04f, 0.055f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // Qt's glyph cache uploads expect the default alignment

    updateMidiTask();
    m_score->setNotationShown(false);
    m_score->drawScrollingSymbols(false); // keep the notation queues flowing, they feed the song reader
    BENCHMARK(8, "highway task");

    m_score->getHighway()->draw(width, height, HUD_HEIGHT, m_song->getWantedChord());
    BENCHMARK(9, "highway draw");
    const qint64 hudStartUs = monotonicTimeUs();
    drawHighwayHud(width, height);
    flushText();
    BENCHMARK(10, "highway hud");
    if (logFrames)
        fprintf(stderr, "PB_FRAME total_us=%lld hud_us=%lld\n", static_cast<long long>(monotonicTimeUs() - frameStartUs),
                static_cast<long long>(monotonicTimeUs() - hudStartUs));
    BENCHMARK_RESULTS();
}

void CGLView::drawTextAligned(float x, float y, const QString &text, const QFont &font, int align)
{
    if (m_highwayMode)
    {
        GLfloat color[4];
        glGetFloatv(GL_CURRENT_COLOR, &color[0]);
        m_textQueue.push_back({x, y, text, font, QColor::fromRgbF(color[0], color[1], color[2], color[3]), align});
        return;
    }
    if (align != Qt::AlignLeft)
    {
        const int textWidth = QFontMetrics(font).horizontalAdvance(text);
        x -= (align == Qt::AlignRight) ? static_cast<float>(textWidth) : static_cast<float>(textWidth) / 2.0f;
    }
    renderText(x, y, 0, text, font);
}

static QString starsText(int stars)
{
    QString text;
    for (int i = 0; i < 3; i++)
        text += (i < stars) ? QChar(0x2605) : QChar(0x2606); // filled / empty star
    return text;
}

void CGLView::drawHighwayHud(float width, float height)
{
    CHighway *highway = m_score->getHighway();
    const CPerformance *performance = m_song->getPerformance();
    const qint64 nowUs = monotonicTimeUs();
    const bool playing = m_song->playingMusic();
    const playMode_t mode = m_song->getPlayMode();

    // top bar
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.35f);
    glRectf(0, height - HUD_HEIGHT, width, height);
    glDisable(GL_BLEND);
    highway->drawProgressBar(PROGRESS_BAR_MARGIN, height - PROGRESS_BAR_BOTTOM, width - PROGRESS_BAR_MARGIN, height - PROGRESS_BAR_TOP,
                             m_song->getPlayFromBar(), m_song->getLoopingBars(), m_dragFromBar, m_dragToBar);

    // note names on the falling notes
    if (m_settings->showNoteNames())
    {
        static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        glColor4f(0.0f, 0.0f, 0.0f, 0.75f);
        for (const CHighway::Label &label : highway->labels())
            drawTextAligned(label.x, label.y, names[label.pitch % 12], m_hudSmallFont, Qt::AlignHCenter);
    }

    glColor3f(1.0f, 1.0f, 1.0f);
    drawTextAligned(20, height - 30, m_song->getSongTitle(), m_hudFont, Qt::AlignLeft);
    QString modeText;
    switch (mode)
    {
    case PB_PLAY_MODE_listen:        modeText = tr("Listen"); break;
    case PB_PLAY_MODE_rhythmTapping: modeText = tr("Rhythm tap"); break;
    case PB_PLAY_MODE_followYou:     modeText = tr("Wait for me"); break;
    default:                         modeText = tr("Play along"); break;
    }
    glColor3f(0.65f, 0.68f, 0.75f);
    drawTextAligned(20, height - 54, tr("Bar %1").arg(m_song->getBarNumber()) + "   " +
                    tr("Speed %1%").arg(qRound(m_song->getSpeed() * 100.0f)) + "   " + modeText,
                    m_hudSmallFont, Qt::AlignLeft);

    if (!m_keyboardFitText.isEmpty())
    {
        if (m_keyboardFitLevel == 1)
            glColor3f(0.45f, 0.85f, 0.55f);
        else if (m_keyboardFitLevel == 2)
            glColor3f(1.0f, 0.7f, 0.3f);
        else
            glColor3f(0.65f, 0.68f, 0.75f);
        drawTextAligned(width / 2, height - 54, m_keyboardFitText, m_hudSmallFont, Qt::AlignHCenter);
    }

    if (mode != PB_PLAY_MODE_listen)
    {
        const PerformanceResult live = performance->result();
        glColor3f(1.0f, 1.0f, 1.0f);
        const QString accuracy = (live.notes() > 0) ? QString::number(qRound(live.accuracy)) + "%" : QStringLiteral("--");
        drawTextAligned(width - 20, height - 30, tr("Accuracy %1").arg(accuracy), m_hudFont, Qt::AlignRight);
        glColor3f(0.65f, 0.68f, 0.75f);
        drawTextAligned(width - 20, height - 54, tr("Streak %1   Best %2").arg(performance->streak()).arg(live.bestStreak),
                        m_hudSmallFont, Qt::AlignRight);
    }

    // C labels on the keyboard
    glColor3f(0.35f, 0.36f, 0.42f);
    for (int key = highway->lowestKey(); key <= highway->highestKey(); key++)
    {
        if (key % 12 == 0)
            drawTextAligned(highway->keyCenterX(key), 8, QString("C%1").arg(key / 12 - 1), m_hudSmallFont, Qt::AlignHCenter);
    }

    // the judgement popup
    const CHighway::Flash &flash = highway->lastFlash();
    const qint64 popupAge = nowUs - flash.timeUs;
    if (flash.judgement != Judgement::pending && popupAge < 700000)
    {
        QString text;
        float r = 1, g = 1, b = 1;
        switch (flash.judgement)
        {
        case Judgement::perfect: text = tr("Perfect"); r = 0.3f; g = 0.95f; b = 0.45f; break;
        case Judgement::good:    text = tr("Good"); r = 0.65f; g = 0.95f; b = 0.4f; break;
        case Judgement::early:   text = tr("Early"); r = 1.0f; g = 0.62f; b = 0.2f; break;
        case Judgement::late:    text = tr("Late"); r = 0.78f; g = 0.55f; b = 1.0f; break;
        default:                 text = tr("Miss"); r = 1.0f; g = 0.3f; b = 0.3f; break;
        }
        const float fade = 1.0f - static_cast<float>(popupAge) / 700000.0f;
        glColor4f(r, g, b, fade);
        const float x = qBound(80.0f, highway->keyCenterX(flash.pitch), width - 80.0f);
        drawTextAligned(x, highway->hitLineY() + 40 + 25 * (1.0f - fade), text, m_popupFont, Qt::AlignHCenter);
    }

    const int countIn = m_song->getCountInDisplay();
    if (countIn > 0)
    {
        glColor3f(1.0f, 1.0f, 1.0f);
        QFont font = m_popupFont;
        font.setPointSize(font.pointSize() * 3);
        drawTextAligned(width / 2, height * 0.5f, QString::number(countIn), font, Qt::AlignHCenter);
    }

    if (m_resultVisible)
    {
        const qint64 resultAge = nowUs - m_resultTimeUs;
        if ((m_resultIsLoopPass && resultAge > 2500000) || (!m_resultIsLoopPass && playing && resultAge > 1000000))
            m_resultVisible = false;
        else
            drawResultPanel(width, height);
    }
    else if (!playing && countIn == 0 && mode != PB_PLAY_MODE_listen)
    {
        glColor3f(0.8f, 0.82f, 0.9f);
        drawTextAligned(width / 2, height * 0.55f, tr("Press Space to start"), m_hudFont, Qt::AlignHCenter);
    }
}

void CGLView::drawResultPanel(float width, float height)
{
    const PerformanceResult &r = m_result;
    const float panelWidth = qMin(520.0f, width - 40);
    const float panelHeight = m_resultIsLoopPass ? 110.0f : 290.0f;
    const float x0 = (width - panelWidth) / 2;
    const float y0 = height * 0.58f - panelHeight / 2;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.08f, 0.09f, 0.13f, 0.92f);
    glRectf(x0, y0, x0 + panelWidth, y0 + panelHeight);
    glColor4f(1.0f, 1.0f, 1.0f, 0.12f);
    glRectf(x0, y0 + panelHeight - 2, x0 + panelWidth, y0 + panelHeight);
    glDisable(GL_BLEND);

    const float cx = width / 2;
    float y = y0 + panelHeight - 50;
    glColor3f(1.0f, 0.82f, 0.25f);
    drawTextAligned(cx, y, starsText(r.stars), m_popupFont, Qt::AlignHCenter);
    y -= 38;
    glColor3f(1.0f, 1.0f, 1.0f);
    QString headline = tr("%1% accuracy").arg(qRound(r.accuracy));
    if (m_resultIsLoopPass)
        headline = tr("Loop pass: ") + headline;
    drawTextAligned(cx, y, headline, m_hudFont, Qt::AlignHCenter);
    if (m_resultIsLoopPass)
        return;

    y -= 40;
    glColor3f(0.75f, 0.78f, 0.85f);
    if (r.timingJudged)
        drawTextAligned(cx, y, tr("Perfect %1   Good %2   Early %3   Late %4").arg(r.perfect).arg(r.good).arg(r.early).arg(r.late),
                        m_resultFont, Qt::AlignHCenter);
    else
        drawTextAligned(cx, y, tr("Correct %1   Late %2").arg(r.perfect).arg(r.late), m_resultFont, Qt::AlignHCenter);
    y -= 30;
    drawTextAligned(cx, y, tr("Missed %1   Wrong notes %2   Best streak %3").arg(r.missed).arg(r.wrong).arg(r.bestStreak),
                    m_resultFont, Qt::AlignHCenter);
    y -= 38;
    QString tip;
    if (r.timingJudged && r.timedHits >= 4 && std::fabs(r.meanOffsetMs) >= 20.0)
        tip = (r.meanOffsetMs < 0) ? tr("You are rushing by about %1 ms").arg(qRound(-r.meanOffsetMs))
                                   : tr("You are dragging by about %1 ms").arg(qRound(r.meanOffsetMs));
    else if (!r.timingJudged && r.stars >= CPerformance::MAX_STARS_WITHOUT_TIMING)
        tip = tr("Great! Play it in Play Along mode to earn the third star");
    else if (r.stars >= 3)
        tip = tr("Excellent! Try a faster speed");
    else
        tip = tr("Press Space to try again");
    glColor3f(0.55f, 0.85f, 1.0f);
    drawTextAligned(cx, y, tip, m_resultFont, Qt::AlignHCenter);
}

void CGLView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragFromBar >= 0)
    {
        m_dragToBar = barAt(event->position());
        update();
    }
}

void CGLView::initializeGL()
{
    CColor color = Cfg::backgroundColor();
    glClearColor (color.red, color.green, color.blue, 0.0);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glShadeModel (GL_FLAT);
    //glEnable(GL_TEXTURE_2D);                        // Enable Texture Mapping

    //from initCheck();
    glShadeModel(GL_FLAT);
    //glEnable(GL_DEPTH_TEST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    //glGenTextures(1, &texName);
    //glBindTexture(GL_TEXTURE_2D, texName);

    if (format().samples() > 0) {
        glEnable(GL_MULTISAMPLE);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    //enableAntialiasedLines();

    // This is a work around for Windows different display scaling option
    int widgetPointSize = m_qtWindow->font().pointSize();
    m_timeSigFont =  QFont("Arial", widgetPointSize*2 );
    m_timeRatingFont =  QFont("Arial", static_cast<int>(widgetPointSize * 1.2) );
    m_hudFont = QFont("Helvetica Neue", static_cast<int>(widgetPointSize * 1.5), QFont::DemiBold);
    m_hudSmallFont = QFont("Helvetica Neue", static_cast<int>(widgetPointSize * 1.05));
    m_popupFont = QFont("Helvetica Neue", static_cast<int>(widgetPointSize * 1.9), QFont::Bold);
    m_resultFont = QFont("Helvetica Neue", static_cast<int>(widgetPointSize * 1.4));
    setHighwayMode(m_settings->value("View/Highway", true).toBool());

    Cfg::setStaveEndX(400);        //This value get changed by the resizeGL func

    m_song->setActiveHand(PB_PART_both);

    setFocusPolicy(Qt::ClickFocus);
    m_qtWindow->init();

    m_score->init();

    m_song->regenerateChordQueue();

    // increased the tick time for MIDI handling

    // repaint at about the display's refresh rate (the engine ticks faster than that)
    const qreal refreshRate = screen() ? screen()->refreshRate() : 60.0;
    m_frameIntervalMs = qMax(SCREEN_FRAME_RATE, static_cast<int>(1000.0 / qMax(30.0, refreshRate)) - 1);
    m_timer.start(Cfg::tickRate, Qt::PreciseTimer, this);

    m_realtime.start();

    //startMediaTimer(12, this );
}

void CGLView::updateMidiTask()
{
    // Hand whole milliseconds to the engine but keep the remainder, so no time is lost when this
    // is called many times per millisecond-fraction (it runs on every timer tick and every frame).
    const qint64 elapsedNs = m_realtime.nsecsElapsed();
    const qint64 ticks = (elapsedNs - m_realtimeConsumedNs) / 1000000;
    m_realtimeConsumedNs += ticks * 1000000;
    m_displayUpdateTicks += ticks;
    m_eventBits |= m_song->task(ticks);
}

void CGLView::timerEvent(QTimerEvent *event)
{
    if (!m_allowedTimerEvent) return;

    BENCHMARK(0, "timer enter");
    if (event->timerId() != m_timer.timerId())
    {
         QWidget::timerEvent(event);
         return;
    }

    updateMidiTask();
    BENCHMARK(1, "m_song task");

    if (m_displayUpdateTicks < m_frameIntervalMs)
        return;

    m_displayUpdateTicks = 0;

    if (m_eventBits != 0)
    {
        if ((m_eventBits & EVENT_BITS_playingStopped) != 0)
            finishRun(false);
        if ((m_eventBits & EVENT_BITS_UptoBarReached) != 0)
        {
            finishRun(true); // the rewind below clears the statistics
            m_song->playFromStartBar();
        }
        if ((m_eventBits & EVENT_BITS_forceFullRedraw) != 0)
            m_forcefullRedraw = m_forceRatingRedraw = m_forceBarRedraw = REDRAW_COUNT;
        if ((m_eventBits & EVENT_BITS_forceRatingRedraw) != 0)
            m_forceRatingRedraw = REDRAW_COUNT;
        if ((m_eventBits & EVENT_BITS_newBarNumber) != 0)
            m_forcefullRedraw = m_forceRatingRedraw = m_forceBarRedraw = REDRAW_COUNT; // fixme this did not work so redraw everything

        m_qtWindow->songEventUpdated(m_eventBits);
        m_eventBits = 0;
    }

    if( m_cfg_openGlOptimise == 0 ) // zero is no GlOptimise
        m_fullRedrawFlag = true;
    else
        m_fullRedrawFlag = false;

    //glDraw();
    update();
    m_fullRedrawFlag = true;
    BENCHMARK(19, "timer exit");
}

void CGLView::mediaTimerEvent(int ticks)
{
    Q_UNUSED(ticks)
}

#ifndef QT_OPENGL_ES

static void qt_save_gl_state(QOpenGLFunctions_1_1 *functions1_1)
{
    functions1_1->glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    functions1_1->glPushAttrib(GL_ALL_ATTRIB_BITS);
    functions1_1->glMatrixMode(GL_TEXTURE);
    functions1_1->glPushMatrix();
    functions1_1->glLoadIdentity();
    functions1_1->glMatrixMode(GL_PROJECTION);
    functions1_1->glPushMatrix();
    functions1_1->glMatrixMode(GL_MODELVIEW);
    functions1_1->glPushMatrix();

    functions1_1->glShadeModel(GL_FLAT);
    functions1_1->glDisable(GL_CULL_FACE);
    functions1_1->glDisable(GL_LIGHTING);
    functions1_1->glDisable(GL_STENCIL_TEST);
    functions1_1->glDisable(GL_DEPTH_TEST);
    functions1_1->glEnable(GL_BLEND);
    functions1_1->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void qt_restore_gl_state(QOpenGLFunctions_1_1 *functions1_1)
{
    functions1_1->glMatrixMode(GL_TEXTURE);
    functions1_1->glPopMatrix();
    functions1_1->glMatrixMode(GL_PROJECTION);
    functions1_1->glPopMatrix();
    functions1_1->glMatrixMode(GL_MODELVIEW);
    functions1_1->glPopMatrix();
    functions1_1->glPopAttrib();
    functions1_1->glPopClientAttrib();
}

static void qt_gl_draw_text(QOpenGLFunctions *functions, QPainter *p, int x, int y, const QString &str,
                            const QFont &font)
{
    GLfloat color[4];
    functions->glGetFloatv(GL_CURRENT_COLOR, &color[0]);

    const auto col = QColor::fromRgbF(color[0], color[1], color[2],color[3]);
    QPen old_pen = p->pen();
    QFont old_font = p->font();

    p->setPen(col);
    p->setFont(font);
    p->drawText(x, y, str);

    p->setPen(old_pen);
    p->setFont(old_font);
}

static inline void transform_point(GLdouble out[4], const GLdouble m[16], const GLdouble in[4])
{
#define M(row,col)  m[col*4+row]
    out[0] =
        M(0, 0) * in[0] + M(0, 1) * in[1] + M(0, 2) * in[2] + M(0, 3) * in[3];
    out[1] =
        M(1, 0) * in[0] + M(1, 1) * in[1] + M(1, 2) * in[2] + M(1, 3) * in[3];
    out[2] =
        M(2, 0) * in[0] + M(2, 1) * in[1] + M(2, 2) * in[2] + M(2, 3) * in[3];
    out[3] =
        M(3, 0) * in[0] + M(3, 1) * in[1] + M(3, 2) * in[2] + M(3, 3) * in[3];
#undef M
}

static inline GLint qgluProject(GLdouble objx, GLdouble objy, GLdouble objz,
           const GLdouble model[16], const GLdouble proj[16],
           const GLint viewport[4],
           GLdouble * winx, GLdouble * winy, GLdouble * winz)
{
   GLdouble in[4], out[4];

   in[0] = objx;
   in[1] = objy;
   in[2] = objz;
   in[3] = 1.0;
   transform_point(out, model, in);
   transform_point(in, proj, out);

   if (in[3] == 0.0)
      return GL_FALSE;

   in[0] /= in[3];
   in[1] /= in[3];
   in[2] /= in[3];

   *winx = viewport[0] + (1 + in[0]) * viewport[2] / 2;
   *winy = viewport[1] + (1 + in[1]) * viewport[3] / 2;

   *winz = (1 + in[2]) / 2;
   return GL_TRUE;
}

#endif // !QT_OPENGL_ES

/*!
 * \brief Renders the string \a str into the GL context of this widget.
 *
 * \a x, \a y and \a z are specified in scene or object coordinates
 * relative to the currently set projection and model matrices. This
 * can be useful if you want to annotate models with text labels and
 * have the labels move with the model as it is rotated etc.
 *
 * \remarks
 * - Taken from Qt 5's QGLWidget implementation (found in
 *   `qtbase/src/opengl/qgl.cpp`). The same counts for the static helper
 *   functions defined above.
 * - Handling the translation for the \a z coordinate has been removed
 *   as it is not used here anyways.
 */
void CGLView::renderText(double x, double y, double z, const QString &str, const QFont & font = QFont()) {
#ifndef QT_OPENGL_ES
    auto *const ctx = context();
    auto *const gl1funcs =
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_1_1>(ctx);
#else
ctx->versionFunctions<QOpenGLFunctions_1_1>();
#endif
    if (!ctx->isOpenGLES() && gl1funcs) {
        if (str.isEmpty() || !isValid())
            return;

        QOpenGLFunctions *const funcs = ctx->functions();

        QPaintEngine *engine = paintEngine();

        QPainter *p;
        bool reuse_painter = false;
        bool use_depth_testing = funcs->glIsEnabled(GL_DEPTH_TEST);
        bool use_scissor_testing = funcs->glIsEnabled(GL_SCISSOR_TEST);

        qt_save_gl_state(gl1funcs);

        if (engine->isActive()) {
            reuse_painter = true;
            p = engine->painter();
        } else {
            p = new QPainter(this);
        }

        const auto *const dev = p->device();
        const auto width = dev ? dev->width() : this->width();
        const auto height = dev ? dev->height() : this->height();
        const auto dpr = dev ? dev->devicePixelRatioF() : 1.0;
        GLdouble model[4 * 4], proj[4 * 4];
        GLint view[4];
        gl1funcs->glGetDoublev(GL_MODELVIEW_MATRIX, &model[0]);
        gl1funcs->glGetDoublev(GL_PROJECTION_MATRIX, &proj[0]);
        funcs->glGetIntegerv(GL_VIEWPORT, &view[0]);
        GLdouble win_x = 0, win_y = 0, win_z = 0;
        qgluProject(x, y, z, &model[0], &proj[0], &view[0],
                    &win_x, &win_y, &win_z);
        win_x /= dpr;
        win_y /= dpr;
        win_y = height + 4 - win_y; // y is inverted
        // note: Not sure why this is rendered 4 pixels to high, just putting "+ 4" there for now.

        QRect viewport(view[0], view[1], view[2], view[3]);
        if (!use_scissor_testing && viewport != rect()) {
            funcs->glScissor(view[0], view[1], view[2], view[3]);
            funcs->glEnable(GL_SCISSOR_TEST);
        } else if (use_scissor_testing) {
            funcs->glEnable(GL_SCISSOR_TEST);
        }
        funcs->glViewport(0, 0, static_cast<GLsizei>(width * dpr), static_cast<GLsizei>(height * dpr));
        gl1funcs->glAlphaFunc(GL_GREATER, 0.0);
        funcs->glEnable(GL_ALPHA_TEST);
        if (use_depth_testing)
            funcs->glEnable(GL_DEPTH_TEST);

        qt_gl_draw_text(funcs, p, qRound(win_x), qRound(win_y), str, font);

        if (!reuse_painter) {
            p->end();
            delete p;
        }

        qt_restore_gl_state(gl1funcs);

        return;
    }
#else // QT_OPENGL_ES
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(z);
    Q_UNUSED(str);
    Q_UNUSED(font);
#endif
    qWarning("QGLWidget::renderText is not supported under OpenGL/ES");
}
