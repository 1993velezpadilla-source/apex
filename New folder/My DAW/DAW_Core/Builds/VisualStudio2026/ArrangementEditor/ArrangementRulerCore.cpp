// ===========================================================================
// ArrangementRulerCore.cpp
// ===========================================================================
#include "ArrangementRulerCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t bg = 0xFF1E1E1E;
        constexpr uint32_t panel = 0xFF252525;
        constexpr uint32_t text = 0xFFD4D4D4;
        constexpr uint32_t textDim = 0xFF707070;
        constexpr uint32_t accent = 0xFFE07B39;
        constexpr uint32_t gridLine = 0xFF404040;
        constexpr uint32_t loopRegion = 0x4040A040;
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

    void ArrangementRulerCore::setPlayheadPosition(double timeSeconds)
    {
        m_playheadTime = timeSeconds;
        repaint();
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

        int barStart = (int)(viewStart / barDuration);
        int barEnd = (int)(viewEnd / barDuration) + 1;

        for (int bar = barStart; bar <= barEnd; ++bar)
        {
            double barTime = bar * barDuration;
            int x = timeToX(barTime);

            if (x < 0 || x > getWidth())
                continue;

            // Bar line
            g.setColour(fromU32(Col::gridLine));
            g.drawVerticalLine(x, 20.f, (float)getHeight());

            // Bar number
            g.setColour(fromU32(Col::text));
            g.setFont(juce::Font("Segoe UI", 9.f, juce::Font::plain));
            g.drawText(juce::String(bar + 1), x + 2, 4, 40, 14,
                       juce::Justification::centredLeft);

            // Beat lines
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

        g.setColour(fromU32(0xFF40A040));
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
