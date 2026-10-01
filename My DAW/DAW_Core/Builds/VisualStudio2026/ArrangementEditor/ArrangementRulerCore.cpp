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

    ArrangementRulerCore::ArrangementRulerCore(ArrangementZoomCore& zoom)
        : m_zoom(zoom)
    {
        setSize(800, 32);
    }

    void ArrangementRulerCore::setTempo(double bpm)
    {
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

        const int rulerW = getWidth();
        const int rulerH = getHeight();

        if (TimelinePaintMetrics::active.load(std::memory_order_relaxed))
        {
            auto recordOne = [&](int x) {
                auto r = rulerPlayheadDirtyRect(x, rulerW, rulerH);
                if (r.isEmpty()) return;
                TimelinePaintMetrics::requestedPartialRepaints.fetch_add(1);
                auto area = static_cast<uint64_t>(r.getWidth())
                          * static_cast<uint64_t>(r.getHeight());
                TimelinePaintMetrics::requestedDirtyAreaPixels.fetch_add(
                    static_cast<int64_t>(area));
                TimelinePaintMetrics::requestedDirtyAreaPixelsRing.push(area);
            };
            if (oldX == newX) {
                recordOne(newX);
            } else {
                recordOne(oldX);
                recordOne(newX);
            }
        }

        // Issue partial repaints for old and new playhead positions
        if (oldX == newX) {
            auto r = rulerPlayheadDirtyRect(newX, rulerW, rulerH);
            if (!r.isEmpty()) repaint(r);
        } else {
            auto oldR = rulerPlayheadDirtyRect(oldX, rulerW, rulerH);
            if (!oldR.isEmpty()) repaint(oldR);
            auto newR = rulerPlayheadDirtyRect(newX, rulerW, rulerH);
            if (!newR.isEmpty()) repaint(newR);
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
        double beatDuration = 60.0 / m_bpm;
        double barDuration = beatDuration * 4.0;

        double viewStart = xToTime(0);
        double viewEnd = xToTime(getWidth());

        // Compute pixels-per-beat / pixels-per-bar from actual BPM + zoom
        const double pxPerBeat = beatDuration * m_zoom.getPixelsPerSecond();
        const double pxPerBar  = barDuration  * m_zoom.getPixelsPerSecond();
        const int    barStep   = GridLodPolicy::barStep(pxPerBar);
        const bool   noBeats   = GridLodPolicy::suppressBeats(pxPerBeat);

        // Seconds labels (m:ss) only when bars are spaced wide enough that the
        // ~44 px label cannot collide with its neighbours — bars-only beyond.
        const bool drawSeconds = pxPerBar >= 60.0;

        int barStart = (int)(viewStart / barDuration);
        int barEnd = (int)(viewEnd / barDuration) + 1;

        for (int bar = barStart; bar <= barEnd; ++bar)
        {
            // Skip bars at extreme zoom-out to reduce GPU draw calls
            if (barStep > 1 && (bar % barStep) != 0)
            {
                // Still draw bar numbers for skipped bars if they are
                // far enough apart to be legible
                double barTime = bar * barDuration;
                int x = timeToX(barTime);
                if (x >= 0 && x <= getWidth())
                {
                    g.setColour(fromU32(Col::text));
                    g.setFont(juce::Font("Segoe UI", 9.f, juce::Font::plain));
                    g.drawText(juce::String(bar + 1), x + 2, 4, 40, 14,
                               juce::Justification::centredLeft);
                    if (drawSeconds)
                    {
                        g.setColour(fromU32(Col::textDim));
                        g.setFont(juce::Font("Segoe UI", 8.f, juce::Font::plain));
                        g.drawText(formatTimeSeconds(barTime), x + 2, 18, 44, 12,
                                   juce::Justification::centredLeft);
                    }
                }
                continue;
            }

            double barTime = bar * barDuration;
            int x = timeToX(barTime);

            if (x < 0 || x > getWidth())
                continue;

            // Bar line — major divisions read above minor ones
            g.setColour(fromU32(Col::gridLineMajor));
            g.drawVerticalLine(x, 20.f, (float)getHeight());

            // Bar number
            g.setColour(fromU32(Col::text));
            g.setFont(juce::Font("Segoe UI", 9.f, juce::Font::plain));
            g.drawText(juce::String(bar + 1), x + 2, 4, 40, 14,
                       juce::Justification::centredLeft);

            // Song-time seconds label (m:ss) — the user's audible clock
            if (drawSeconds)
            {
                g.setColour(fromU32(Col::textDim));
                g.setFont(juce::Font("Segoe UI", 8.f, juce::Font::plain));
                g.drawText(formatTimeSeconds(barTime), x + 2, 18, 44, 12,
                           juce::Justification::centredLeft);
            }

            // Beat lines — suppressed when beats are too dense
            if (noBeats)
                continue;

            g.setColour(fromU32(Col::gridLine).withAlpha(0.3f));
            for (int beat = 1; beat < 4; ++beat)
            {
                double beatTime = barTime + beat * beatDuration;
                int beatX = timeToX(beatTime);
                if (beatX >= 0 && beatX <= getWidth())
                    g.drawVerticalLine(beatX, 24.f, (float)getHeight());
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
