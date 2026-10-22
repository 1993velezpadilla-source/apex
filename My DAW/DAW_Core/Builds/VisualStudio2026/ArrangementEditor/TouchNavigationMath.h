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

        // Distinguish a region tap from a drag using Euclidean SCREEN
        // displacement. A tap opens the contextual menu only when the region
        // was already selected at pointer-down; higher-level code enforces
        // tool ownership and edit-handle exclusion.
        static bool withinTapSlop(double startX, double startY,
                                  double currentX, double currentY) noexcept
        {
            constexpr double kTouchTapSlopPx = 7.0;
            return std::hypot(currentX - startX, currentY - startY) <= kTouchTapSlopPx;
        }

        // Logic for iPad: context menu belongs to a stationary press-and-hold
        // on an already selected region, not a routine second short tap.
        static bool contextHoldReady(int durationMs, double x0, double y0,
                                     double x1, double y1) noexcept
        {
            return durationMs >= 320 && withinTapSlop(x0, y0, x1, y1);
        }

        // Logic for iPad: an already selected clip accepts a second short tap;
        // the first tap only selects. Any real movement must cancel the menu
        // so drag-to-move remains instantaneous and unambiguous.
        static bool selectedSecondTapReady(bool selectedAtPointerDown,
                                           int durationMs, double x0, double y0,
                                           double x1, double y1) noexcept
        {
            return selectedAtPointerDown && durationMs >= 0
                && durationMs <= 280
                && withinTapSlop(x0, y0, x1, y1);
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
