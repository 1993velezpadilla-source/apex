// ============================================================================
// TouchNavigationMath.h - deterministic viewport math for native touch input.
// No JUCE dependency, allocation, or audio-thread interaction.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace ArrangementEditor
{
    struct TouchNavigationMath final
    {
        // Store drag anchors in SCREEN coordinates: content-local coordinates
        // shift when a Viewport scrolls, feeding its own motion back into a pan.
        static int panPosition(int startViewPx, double screenDeltaPx, int maxViewPx) noexcept
        {
            return std::clamp(startViewPx - static_cast<int>(std::lround(screenDeltaPx)),
                              0, std::max(0, maxViewPx));
        }

        // Keep the timeline point under the gesture centroid fixed on screen.
        static int zoomPosition(double contentAnchorPx, double viewAnchorPx,
                                double newToOldZoom, int maxViewPx) noexcept
        {
            return std::clamp(
                static_cast<int>(std::lround(contentAnchorPx * newToOldZoom - viewAnchorPx)),
                0, std::max(0, maxViewPx));
        }

        // Compare to the LAST APPLIED pinch distance, not the last sampled
        // distance. Tiny slow movements must accumulate rather than vanish.
        static bool pinchReady(double appliedSeparationPx, double currentSeparationPx) noexcept
        {
            return appliedSeparationPx > 1.0
                && std::abs(currentSeparationPx - appliedSeparationPx) >= 2.0;
        }

        static double pinchRatio(double appliedSeparationPx, double currentSeparationPx) noexcept
        {
            if (appliedSeparationPx <= 1.0)
                return 1.0;
            return std::clamp(currentSeparationPx / appliedSeparationPx, 0.85, 1.18);
        }
    };
}
