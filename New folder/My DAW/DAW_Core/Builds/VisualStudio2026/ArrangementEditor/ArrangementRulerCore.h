// ===========================================================================
// ArrangementRulerCore.h
// Time ruler at the top of the arrangement view.
// Shows bars + beats, playhead position, loop region.
// Clicking moves playhead, dragging sets loop region.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementZoomCore.h"
#include <functional>

namespace ArrangementEditor
{
    class ArrangementRulerCore : public juce::Component
    {
    public:
        ArrangementRulerCore(ArrangementZoomCore& zoom);

        void setTempo(double bpm);
        void setPlayheadPosition(double timeSeconds);
        void setLoopRegion(double startTime, double endTime);
        void setScrollOffsetX(int scrollOffsetX);

        void paint(juce::Graphics& g) override;
        void resized() override {}

        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

        std::function<void(double)> onPlayheadMoved;
        std::function<void(double, double)> onLoopRegionChanged;

    private:
        ArrangementZoomCore& m_zoom;

        double m_bpm = 120.0;
        double m_playheadTime = 0.0;
        double m_loopStart = -1.0;
        double m_loopEnd = -1.0;
        int m_scrollOffsetX = 0;

        bool m_draggingPlayhead = false;
        bool m_draggingLoop = false;
        juce::Point<int> m_dragStart;

        void drawGrid(juce::Graphics& g);
        void drawPlayhead(juce::Graphics& g);
        void drawLoopRegion(juce::Graphics& g);

        double xToTime(int x) const;
        int timeToX(double time) const;

        static juce::Colour fromU32(uint32_t c)
        {
            return juce::Colour(
                (uint8_t)(c >> 16), (uint8_t)(c >> 8),
                (uint8_t)c,         (uint8_t)(c >> 24));
        }
    };

} // namespace ArrangementEditor
