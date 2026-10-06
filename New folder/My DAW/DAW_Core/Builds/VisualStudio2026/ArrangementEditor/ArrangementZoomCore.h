// ===========================================================================
// ArrangementZoomCore.h
// Owns horizontal and vertical zoom state for the arrangement view.
// ===========================================================================
#pragma once
#include <functional>
#include <cmath>

namespace ArrangementEditor
{
    class ArrangementZoomCore
    {
    public:
        ArrangementZoomCore()
            : m_pixelsPerSecond(100.0)
            , m_trackHeightPx(80.0)
        {}

        // -----------------------------------------------------------------------
        // Horizontal zoom (time axis)
        // -----------------------------------------------------------------------
        double getPixelsPerSecond() const { return m_pixelsPerSecond; }

        void setPixelsPerSecond(double pps)
        {
            m_pixelsPerSecond = juce::jlimit(5.0, 4000.0, pps);
            if (onZoomChanged) onZoomChanged();
        }

        void zoomIn(double centreTime = 0.0)
        {
            setPixelsPerSecond(m_pixelsPerSecond * 1.2);
        }

        void zoomOut(double centreTime = 0.0)
        {
            setPixelsPerSecond(m_pixelsPerSecond / 1.2);
        }

        // -----------------------------------------------------------------------
        // Vertical zoom (track height)
        // -----------------------------------------------------------------------
        double getTrackHeightPx() const { return m_trackHeightPx; }

        void setTrackHeightPx(double height)
        {
            m_trackHeightPx = juce::jlimit(24.0, 180.0, height);
            if (onZoomChanged) onZoomChanged();
        }

        void zoomVerticalIn()
        {
            setTrackHeightPx(m_trackHeightPx * 1.15);
        }

        void zoomVerticalOut()
        {
            setTrackHeightPx(m_trackHeightPx / 1.15);
        }

        // -----------------------------------------------------------------------
        // Utility conversions
        // -----------------------------------------------------------------------
        double timeToX(double time) const
        {
            return time * m_pixelsPerSecond;
        }

        double xToTime(double x) const
        {
            return x / m_pixelsPerSecond;
        }

        // -----------------------------------------------------------------------
        // Callbacks
        // -----------------------------------------------------------------------
        std::function<void()> onZoomChanged;

    private:
        double m_pixelsPerSecond;
        double m_trackHeightPx;
    };

} // namespace ArrangementEditor
