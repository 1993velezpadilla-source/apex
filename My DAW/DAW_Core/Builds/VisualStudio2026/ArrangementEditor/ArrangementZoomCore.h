// ===========================================================================
// ArrangementZoomCore.h
// Owns horizontal and vertical zoom state for the arrangement view.
// ===========================================================================
#pragma once
#include <functional>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>

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
            // Invalid density must not enable unreadable beat subdivisions.
            return !(pixelsPerBeat >= kMinPxPerBeat);
        }

        /** Musically natural bar step for the given pixel density.
         *  Cap the step at 32 bars. Compare spacing thresholds directly:
         *  ceil(24 / pxPerBar) can overflow int at extreme zoom-out or
         *  convert NaN to int after malformed tempo/viewport input. */
        static int barStep(double pixelsPerBar) noexcept
        {
            if (!(pixelsPerBar > 0.0)) return 1; // also rejects NaN
            if (pixelsPerBar >= kMinMajorSpacing) return 1;
            if (pixelsPerBar >= kMinMajorSpacing / 2.0) return 2;
            if (pixelsPerBar >= kMinMajorSpacing / 4.0) return 4;
            if (pixelsPerBar >= kMinMajorSpacing / 8.0) return 8;
            if (pixelsPerBar >= kMinMajorSpacing / 16.0) return 16;
            return 32;
        }

        // Labels need more space than grid lines. In particular, do not
        // lay out a text label for every bar skipped by the line LOD.
        static int labelStep(double pixelsPerBar) noexcept
        {
            if (!(pixelsPerBar > 0.0) || !std::isfinite(pixelsPerBar))
                return 1;
            int step = 1;
            while (step < (1 << 20) && pixelsPerBar * step < 48.0)
                step *= 2;
            return step;
        }

        struct BarRange
        {
            int64_t first = 0;
            int64_t last = -1;
            int step = 1;
        };

        // Pixel bounds must come from the visible Graphics clip, never the
        // complete song component width. Include the preceding major bar
        // so its beats/partly clipped label can still enter the dirty strip.
        static BarRange visibleBars(double left, double right,
                                    double pixelsPerBar, int step) noexcept
        {
            if (!std::isfinite(left) || !std::isfinite(right)
                || !(right > left) || !(right > 0.0)
                || !(pixelsPerBar > 0.0) || !std::isfinite(pixelsPerBar))
                return {};
            const auto maxBar = std::numeric_limits<int64_t>::max() / 4;
            const auto bound = [maxBar](double bar) -> int64_t
            {
                return static_cast<int64_t>(std::max(0.0,
                    std::min(static_cast<double>(maxBar), bar)));
            };
            const int stride = std::max(1, step);
            const auto first = bound(std::floor(std::max(0.0, left) / pixelsPerBar));
            return { first - first % stride,
                     bound(std::ceil(right / pixelsPerBar)), stride };
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
            if (!std::isfinite(pps))
                return;
            m_pixelsPerSecond = juce::jlimit(2.0, 4000.0, pps);
        }

        void zoomIn([[maybe_unused]] double centreTime = 0.0)
        {
            setPixelsPerSecond(m_pixelsPerSecond * 1.2);
        }

        void zoomOut([[maybe_unused]] double centreTime = 0.0)
        {
            setPixelsPerSecond(m_pixelsPerSecond / 1.2);
        }

        // -----------------------------------------------------------------------
        // Vertical zoom (track height)
        // -----------------------------------------------------------------------
        double getTrackHeightPx() const { return m_trackHeightPx; }

        // Shared clamp for touch/wheel anchors and the actual lane setter.
        // Never anchor the viewport to a height the setter cannot apply.
        static double clampTrackHeightPx(double requested, double current) noexcept
        {
            return std::isfinite(requested) ? juce::jlimit(24.0, 180.0, requested) : current;
        }

        void setTrackHeightPx(double height)
        {
            const double clamped = clampTrackHeightPx(height, m_trackHeightPx);
            if (clamped == m_trackHeightPx)
                return;
            m_trackHeightPx = clamped;
            if (onZoomChanged) onZoomChanged();
        }

        // A diagonal pinch is one presentation transaction. Publish both
        // axes before notifying: no intermediate horizontal-only layout.
        bool setZoom(double pixelsPerSecond, double trackHeight)
        {
            const double nextPps = std::isfinite(pixelsPerSecond)
                ? juce::jlimit(2.0, 4000.0, pixelsPerSecond) : m_pixelsPerSecond;
            const double nextHeight = clampTrackHeightPx(trackHeight, m_trackHeightPx);
            if (nextPps == m_pixelsPerSecond && nextHeight == m_trackHeightPx)
                return false;
            m_pixelsPerSecond = nextPps;
            m_trackHeightPx = nextHeight;
            if (onZoomChanged) onZoomChanged();
            return true;
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
