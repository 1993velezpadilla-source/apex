// ===========================================================================
// ArrangementZoomCore.h
// Owns horizontal and vertical zoom state for the arrangement view.
// ===========================================================================
#pragma once
#include <functional>
#include <cmath>
#include <cstdint>

namespace ArrangementEditor
{
    // ── Grid LOD Policy ──────────────────────────────────────────────────
    // Pure zero-dependency helper shared by ArrangementRulerCore and
    // ArrangementViewCore.  Both callers compute actual pxPerBeat and
    // pxPerBar from their real BPM / time-signature / zoom state and ask
    // this single source of truth what to draw.
    //
    // Thresholds:
    //   beat  lines → drawn only when pxPerBeat >= 6.0
    //   bar   lines → aim for major spacing of ~28 px
    //   barStep      → ceil(28 / pxPerBar), rounded to musically natural
    //                   powers-of-two: 1,2,4,8,16…
    // Alignment and snapping are never affected — only visual line density.
    struct GridLodPolicy
    {
        static constexpr double kMinPxPerBeat = 6.0;
        static constexpr double kMinMajorSpacing = 24.0;

        /** True when beat subdivision lines should be suppressed entirely. */
        static bool suppressBeats(double pixelsPerBeat) noexcept
        {
            return pixelsPerBeat < kMinPxPerBeat;
        }

        /** Musically natural bar step for the given pixel density.
         *  Guarantees major spacing >= kMinMajorSpacing. */
        static int barStep(double pixelsPerBar) noexcept
        {
            if (pixelsPerBar <= 0.0) return 1;
            const int raw = static_cast<int>(std::ceil(kMinMajorSpacing / pixelsPerBar));
            if (raw <= 1) return 1;
            if (raw <= 2) return 2;
            if (raw <= 4) return 4;
            if (raw <= 8) return 8;
            if (raw <= 16) return 16;
            return 32;
        }
    };

    // ── Clip rendering LOD policy ────────────────────────────────────────
    // At the minimum horizontal and vertical zoom a clip is often only a few
    // pixels wide and a very shallow lane.  The clip block and selection
    // outline remain authoritative, but waveform/detail layers become either
    // unreadable or disproportionately expensive.  Keep these thresholds in
    // a pure policy so the renderer and deterministic tests share one contract.
    struct ArrangementClipLodPolicy
    {
        static constexpr int kViewportOverscanPx = 32;
        static constexpr int kWaveformMinWidthPx = 8;
        static constexpr int kWaveformMinHeightPx = 28;
        static constexpr int kNameMinWidthPx = 24;
        static constexpr int kDetailMinWidthPx = 36;
        static constexpr int kDetailMinHeightPx = 28;
        static constexpr int kFadeMinWidthPx = 18;
        static constexpr int kFadeMinHeightPx = 24;

        static bool drawWaveform(int width, int height) noexcept
        {
            return width >= kWaveformMinWidthPx && height >= kWaveformMinHeightPx;
        }

        static bool drawPatternGrid(int width, int height) noexcept
        {
            return drawWaveform(width, height);
        }

        static bool drawClipName(int width, int height) noexcept
        {
            return width >= kNameMinWidthPx && height >= kNameHForPolicy();
        }

        static bool drawSecondaryDetails(int width, int height) noexcept
        {
            return width >= kDetailMinWidthPx && height >= kDetailMinHeightPx;
        }

        static bool drawFades(int width, int height) noexcept
        {
            return width >= kFadeMinWidthPx && height >= kFadeMinHeightPx;
        }

        static bool drawGainLine(int width, int height) noexcept
        {
            return width >= kWaveformMinWidthPx && height >= kFadeMinHeightPx;
        }

        static bool intersectsViewport(const juce::Rectangle<int>& clipBounds,
                                       const juce::Rectangle<int>& viewport,
                                       int trackHeightPx) noexcept
        {
            const int overscanY = juce::jmax(kViewportOverscanPx, trackHeightPx);
            return viewport.expanded(kViewportOverscanPx, overscanY)
                .intersects(clipBounds);
        }

    private:
        static constexpr int kNameHForPolicy() noexcept { return 18; }
    };

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
            // Extended zoom-out range: down to 2 px/second (~2x farther than
            // before) so whole songs fit comfortably on one screen.
            m_pixelsPerSecond = juce::jlimit(2.0, 4000.0, pps);
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
