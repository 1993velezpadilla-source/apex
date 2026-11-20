// ===========================================================================
// ArrangementRulerCore.cpp
// ===========================================================================
#include "ArrangementRulerCore.h"
#include "../../../Source/DiagnosticsCore/TimelinePaintMetrics.h"

namespace ArrangementEditor
{
    namespace Col
    {
        // APEX signal-core tokens (see ThemeCore::ApexTokens)
        constexpr uint32_t bg = 0xFF070A10;          // deepestB
        constexpr uint32_t panel = 0xFF0A0D15;       // panelA
        constexpr uint32_t text = 0xFFA6ADBC;        // textSecondary
        constexpr uint32_t textDim = 0xFF687083;     // textMuted
        constexpr uint32_t accent = 0xFFFF2A91;      // magentaBright (playhead)
        constexpr uint32_t gridLine = 0xFF22283A;    // borderSoftB
        constexpr uint32_t gridLineMajor = 0xFF2A3348; // bars read above beats
        constexpr uint32_t loopRegion = 0x33813CFF;  // violet wash
    }

    ArrangementRulerCore::ArrangementRulerCore(const TimelineZoomController& zoom)
        : m_zoom(zoom)
    {
        setSize(800, 32);
    }

    void ArrangementRulerCore::setTempo(double bpm)
    {
        if (m_bpm == bpm)
            return;
        m_bpm = bpm;
        repaint();
    }

    // ── Local helper: ruler playhead dirty rect (F1 bounded repaint) ─────
    namespace {
        /** Ruler playhead: 1px line + 10px-wide triangle marker (x-5..x+5)
         *  + 1px antialiasing margin = 13px wide centered on playheadX.
         *  Clamped to ruler local bounds. */
        constexpr int kRulerPlayheadHalfWidth = 6;

        /** Song-time label "m:ss" for a given time in seconds. */
        juce::String formatTimeSeconds(double seconds)
        {
            const int totalSecs = juce::jmax(0, (int) std::llround(seconds));
            return juce::String::formatted("%d:%02d", totalSecs / 60, totalSecs % 60);
        }

        juce::Rectangle<int> rulerPlayheadDirtyRect(
            int playheadX, int rulerWidth, int rulerHeight) noexcept
        {
            const auto bounds = juce::Rectangle<int>(0, 0, rulerWidth, rulerHeight);
            const auto glow = juce::Rectangle<int>(
                playheadX - kRulerPlayheadHalfWidth, 0,
                kRulerPlayheadHalfWidth * 2 + 1, rulerHeight);
            return glow.getIntersection(bounds);
        }
    } // anonymous namespace

    void ArrangementRulerCore::setPlayheadPosition(double timeSeconds)
    {
        const double oldTime = m_playheadTime;
        const int oldX = timeToX(oldTime);
        m_playheadTime = timeSeconds;
        const int newX = timeToX(timeSeconds);

        if (oldX == newX)
            return;

        const bool metricsActive = TimelinePaintMetrics::active.load(std::memory_order_relaxed);
        for (const int x : { oldX, newX })
        {
            const auto dirty = rulerPlayheadDirtyRect(x, getWidth(), getHeight());
            if (dirty.isEmpty())
                continue;
            repaint(dirty);
            if (metricsActive)
            {
                const auto area = static_cast<uint64_t>(dirty.getWidth()) * dirty.getHeight();
                TimelinePaintMetrics::requestedPartialRepaints.fetch_add(1);
                TimelinePaintMetrics::requestedDirtyAreaPixels.fetch_add(static_cast<int64_t>(area));
                TimelinePaintMetrics::requestedDirtyAreaPixelsRing.push(area);
            }
        }
    }

    void ArrangementRulerCore::setScrollOffsetX(int scrollOffsetX)
    {
        scrollOffsetX = juce::jmax(0, scrollOffsetX);
        if (m_scrollOffsetX == scrollOffsetX)
            return;

        m_scrollOffsetX = scrollOffsetX;
        repaint();
    }

    void ArrangementRulerCore::setLoopRegion(double startTime, double endTime)
    {
        m_loopStart = startTime;
        m_loopEnd = endTime;
        repaint();
    }

    void ArrangementRulerCore::paint(juce::Graphics& g)
    {
        if (TimelinePaintMetrics::active.load(std::memory_order_relaxed))
            TimelinePaintMetrics::rulerPaintCount.fetch_add(1);
        auto b = getLocalBounds().toFloat();

        // Background
        g.setColour(fromU32(Col::panel));
        g.fillRect(b);

        // Grid + labels
        drawGrid(g);

        // Loop region
        if (m_loopStart >= 0.0 && m_loopEnd > m_loopStart)
            drawLoopRegion(g);

        // Playhead
        drawPlayhead(g);

        // Bottom border
        g.setColour(fromU32(Col::gridLine));
        g.drawHorizontalLine((int)b.getBottom() - 1, b.getX(), b.getRight());
    }

    void ArrangementRulerCore::drawGrid(juce::Graphics& g)
    {
        const auto dirty = g.getClipBounds().getIntersection(getLocalBounds());
        if (dirty.isEmpty())
            return;

        const double beatDuration = 60.0 / m_bpm;
        const double barDuration = beatDuration * 4.0;
        const double pxPerBeat = beatDuration * m_zoom.getPixelsPerSecond();
        const double pxPerBar = barDuration * m_zoom.getPixelsPerSecond();
        if (!std::isfinite(pxPerBar) || pxPerBar <= 0.0)
            return;

        const int barStep = GridLodPolicy::barStep(pxPerBar);
        const int labelStep = GridLodPolicy::barLabelStep(pxPerBar);
        const bool drawBeats = !GridLodPolicy::suppressBeats(pxPerBeat);
        const bool drawSeconds = pxPerBar >= 60.0;
        constexpr int labelReach = 46;
        const int firstBar = static_cast<int>(
            xToTime(juce::jmax(0, dirty.getX() - labelReach)) / barDuration);
        const int lastBar = static_cast<int>(xToTime(dirty.getRight()) / barDuration) + 1;
        static const juce::Font barFont("Segoe UI", 9.f, juce::Font::plain);
        static const juce::Font secondsFont("Segoe UI", 8.f, juce::Font::plain);
        const float bottom = static_cast<float>(getHeight());

        for (int bar = firstBar - firstBar % barStep; bar <= lastBar; bar += barStep)
        {
            const double barTime = bar * barDuration;
            const int x = timeToX(barTime);
            if (x >= dirty.getRight())
                break;

            if (dirty.intersects(juce::Rectangle<int>{ x, 20, 1, getHeight() - 20 }))
            {
                g.setColour(fromU32(Col::gridLineMajor));
                g.drawVerticalLine(x, 20.f, bottom);
            }

            if (bar % labelStep == 0)
            {
                const juce::Rectangle<int> barLabel(x + 2, 4, 40, 14);
                if (dirty.intersects(barLabel))
                {
                    g.setColour(fromU32(Col::text));
                    g.setFont(barFont);
                    g.drawText(juce::String(bar + 1), barLabel, juce::Justification::centredLeft);
                }
                const juce::Rectangle<int> secondsLabel(x + 2, 18, 44, 12);
                if (drawSeconds && dirty.intersects(secondsLabel))
                {
                    g.setColour(fromU32(Col::textDim));
                    g.setFont(secondsFont);
                    g.drawText(formatTimeSeconds(barTime), secondsLabel, juce::Justification::centredLeft);
                }
            }

            if (!drawBeats || dirty.getBottom() <= 24)
                continue;

            g.setColour(fromU32(Col::gridLine).withAlpha(0.3f));
            for (int beat = 1; beat < 4; ++beat)
            {
                const int beatX = timeToX(barTime + beat * beatDuration);
                if (beatX >= dirty.getX() && beatX < dirty.getRight())
                    g.drawVerticalLine(beatX, 24.f, bottom);
            }
        }
    }

    void ArrangementRulerCore::drawPlayhead(juce::Graphics& g)
    {
        int x = timeToX(m_playheadTime);
        if (x < 0 || x > getWidth())
            return;

        g.setColour(fromU32(Col::accent));
        g.drawVerticalLine(x, 0.f, (float)getHeight());

        // Triangle marker
        juce::Path triangle;
        triangle.addTriangle((float)x - 5.f, 0.f,
                             (float)x + 5.f, 0.f,
                             (float)x, 8.f);
        g.fillPath(triangle);
    }

    void ArrangementRulerCore::drawLoopRegion(juce::Graphics& g)
    {
        int x1 = timeToX(m_loopStart);
        int x2 = timeToX(m_loopEnd);

        if (x2 <= 0 || x1 >= getWidth())
            return;

        juce::Rectangle<float> loopRect((float)x1, 0.f,
                                         (float)(x2 - x1), (float)getHeight());

        g.setColour(fromU32(Col::loopRegion));
        g.fillRect(loopRect);

        g.setColour(fromU32(0xFFA34CFF)); // violetBright loop edge
        g.drawRect(loopRect, 1.f);
    }

    void ArrangementRulerCore::mouseDown(const juce::MouseEvent& e)
    {
        m_dragStart = e.getPosition();

        if (e.mods.isShiftDown())
        {
            // Start loop region drag
            m_draggingLoop = true;
            m_loopStart = xToTime(e.x);
            m_loopEnd = m_loopStart;
        }
        else
        {
            // Move playhead
            m_draggingPlayhead = true;
            double time = xToTime(e.x);
            m_playheadTime = juce::jmax(0.0, time);

            if (onPlayheadMoved)
                onPlayheadMoved(m_playheadTime);
        }

        repaint();
    }

    void ArrangementRulerCore::mouseDrag(const juce::MouseEvent& e)
    {
        if (m_draggingLoop)
        {
            m_loopEnd = xToTime(e.x);
            if (m_loopEnd < m_loopStart)
                std::swap(m_loopStart, m_loopEnd);
        }
        else if (m_draggingPlayhead)
        {
            double time = xToTime(e.x);
            m_playheadTime = juce::jmax(0.0, time);

            if (onPlayheadMoved)
                onPlayheadMoved(m_playheadTime);
        }

        repaint();
    }

    void ArrangementRulerCore::mouseUp(const juce::MouseEvent&)
    {
        if (m_draggingLoop)
        {
            if (onLoopRegionChanged && m_loopEnd > m_loopStart)
                onLoopRegionChanged(m_loopStart, m_loopEnd);
        }

        m_draggingPlayhead = false;
        m_draggingLoop = false;
    }

    double ArrangementRulerCore::xToTime(int x) const
    {
        return m_zoom.xToTime((double)(x + m_scrollOffsetX));
    }

    int ArrangementRulerCore::timeToX(double time) const
    {
        return (int)m_zoom.timeToX(time) - m_scrollOffsetX;
    }

} // namespace ArrangementEditor
