// ===========================================================================
// ArrangementZoomCore.h
// Owns horizontal and vertical zoom state for the arrangement view.
// ===========================================================================
#pragma once
#include <algorithm>
#include <limits>
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
            return !(pixelsPerBeat >= kMinPxPerBeat);
        }

        /** Musically natural bar step for the given pixel density.
         *  Guarantees major spacing >= kMinMajorSpacing. */
        static int barStep(double pixelsPerBar) noexcept
        {
            if (!(pixelsPerBar > 0.0) || !std::isfinite(pixelsPerBar)) return 1;
            const double raw = kMinMajorSpacing / pixelsPerBar;
            if (raw <= 1) return 1;
            if (raw <= 2) return 2;
            if (raw <= 4) return 4;
            if (raw <= 8) return 8;
            if (raw <= 16) return 16;
            return 32;
        }

        struct BarRange
        {
            int64_t first = 0;
            int64_t last = -1;
        };

        // The first (possibly partial) bar supplies the leading beat lines.
        // Major bars are aligned to song time so they never shift with scroll.
        static BarRange visibleBars(double left, double right, double pixelsPerBar,
                                    int majorStep) noexcept
        {
            if (!(pixelsPerBar > 0.0) || !std::isfinite(pixelsPerBar)
                || !std::isfinite(left) || !std::isfinite(right) || right <= left)
                return {};

            constexpr double maxBar = std::numeric_limits<int>::max();
            const auto first = static_cast<int64_t>(
                std::clamp(std::floor(left / pixelsPerBar), 0.0, maxBar));
            const auto last = static_cast<int64_t>(
                std::clamp(std::ceil(right / pixelsPerBar), 0.0, maxBar));
            const int step = std::max(1, majorStep);
            return { ((first + step - 1) / step) * step, last };
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
        static constexpr int kWaveformMinWidthPx = 4;
        static constexpr int kWaveformMinHeightPx = 21;
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
            return width >= 8 && height >= 28;
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
            return width >= 8 && height >= kFadeMinHeightPx;
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
        static constexpr double minPixelsPerSecond = 2.0;
        static constexpr double maxPixelsPerSecond = 4000.0;
        static constexpr double minTrackHeightPx = 24.0;
        static constexpr double maxTrackHeightPx = 180.0;

        double getPixelsPerSecond() const noexcept { return m_pixelsPerSecond; }
        double getTrackHeightPx() const noexcept { return m_trackHeightPx; }

        bool setPixelsPerSecond(double requested) noexcept
        {
            if (!std::isfinite(requested))
                return false;
            const double scale = std::clamp(requested, minPixelsPerSecond, maxPixelsPerSecond);
            if (scale == m_pixelsPerSecond)
                return false;
            m_pixelsPerSecond = scale;
            return true;
        }

        bool setTrackHeightPx(double requested) noexcept
        {
            if (!std::isfinite(requested))
                return false;

            // Keep fractional height so slow pinches accumulate, while the UI
            // updates only when the effective integer lane height has changed.
            const auto oldHeight = static_cast<int>(m_trackHeightPx);
            m_trackHeightPx = std::clamp(requested, minTrackHeightPx, maxTrackHeightPx);
            return static_cast<int>(m_trackHeightPx) != oldHeight;
        }

        double timeToX(double time) const noexcept { return time * m_pixelsPerSecond; }
        double xToTime(double x) const noexcept { return x / m_pixelsPerSecond; }

        static double scrollOrigin(double contentAnchor, double viewportAnchor,
                                   int contentSize, int visibleSize) noexcept
        {
            return std::clamp(contentAnchor - viewportAnchor, 0.0,
                              static_cast<double>(std::max(0, contentSize - visibleSize)));
        }

        // Leave headroom for renderer overhangs and JUCE's int rectangles.
        static constexpr int maxContentWidth = std::numeric_limits<int>::max() / 2;

        static int contentWidth(double pixelsPerSecond, double seconds,
                                int viewportWidth) noexcept
        {
            const double requestedWidth = std::ceil(seconds * pixelsPerSecond);
            const double width = std::isnan(requestedWidth) ? 0.0
                : std::clamp(requestedWidth, 0.0, static_cast<double>(maxContentWidth));
            return std::max(viewportWidth + 1, static_cast<int>(width));
        }

    private:
        double m_pixelsPerSecond = 100.0;
        double m_trackHeightPx = 80.0;
    };

} // namespace ArrangementEditor
