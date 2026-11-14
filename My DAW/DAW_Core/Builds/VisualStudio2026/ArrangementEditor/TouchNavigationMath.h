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
            const int maxScrollPx = std::max(0, maxViewPx);
            const int safeStartPx = std::clamp(startViewPx, 0, maxScrollPx);

            // Malformed touch samples must not move the viewport. Clamp in
            // double precision BEFORE rounding so very large finite deltas
            // cannot overflow the integer conversion and snap to song start.
            if (!std::isfinite(screenDeltaPx))
                return safeStartPx;

            const double targetPx = static_cast<double>(safeStartPx) - screenDeltaPx;
            if (targetPx <= 0.0)
                return 0;
            if (targetPx >= static_cast<double>(maxScrollPx))
                return maxScrollPx;
            return static_cast<int>(std::lround(targetPx));
        }

        // Keep the timeline point under the gesture centroid fixed on screen.
        static int zoomPosition(double contentAnchorPx, double viewAnchorPx,
                                double newToOldZoom, int maxViewPx,
                                int currentViewPx) noexcept
        {
            const int maxScrollPx = std::max(0, maxViewPx);
            const int safeCurrentPx = std::clamp(currentViewPx, 0, maxScrollPx);

            // Reject malformed pinch anchors without snapping the viewport to
            // song start. Finite-but-huge coordinates may overflow the double
            // product: saturate BEFORE converting to int, as with touch pan.
            if (!std::isfinite(contentAnchorPx) || !std::isfinite(viewAnchorPx)
                || !std::isfinite(newToOldZoom) || newToOldZoom <= 0.0)
                return safeCurrentPx;

            const double targetPx = contentAnchorPx * newToOldZoom - viewAnchorPx;
            if (std::isnan(targetPx))
                return safeCurrentPx;
            if (targetPx <= 0.0)
                return 0;
            if (targetPx >= static_cast<double>(maxScrollPx))
                return maxScrollPx;
            return static_cast<int>(std::lround(targetPx));
        }

        // A touch sample at a viewport edge (or subpixel jitter) must not
        // schedule an unchanged Viewport position or enter low-detail paint.
        // This applies to two-finger translation independently of zoom.
        static constexpr bool touchPanChangesViewport(int beforeX, int beforeY,
                                                       int afterX, int afterY) noexcept
        {
            return beforeX != afterX || beforeY != afterY;
        }

        // A viewport axis cannot continue inertial motion when the clamped
        // destination equals its current pixel. Stop only that axis, preserving
        // diagonal movement on the other axis.
        static constexpr double inertiaVelocityAfterMove(double velocity,
                                                         int beforePx,
                                                         int afterPx) noexcept
        {
            return beforePx == afterPx ? 0.0 : velocity;
        }

        // A pan-only gesture never changes clip geometry. Once lightweight
        // waveform painting is disabled, repaint the visible viewport instead
        // of rebuilding all clip renderers. An active fling owns its own end.
        static constexpr bool touchPanNeedsDetailRepaint(bool wasLightweight,
                                                          bool momentumActive) noexcept
        {
            return wasLightweight && !momentumActive;
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

        // Only a real touchscreen FINGER can use second-tap-to-context-menu.
        // Physical mouse clicks (including a double-click) and pen taps must
        // never be misclassified as this gesture. This is also checked before
        // forwarding JUCE's mouseDoubleClick callback to Clip Properties.
        static bool selectedSecondTapReady(bool isFingerInput,
                                           bool selectedAtPointerDown,
                                           int durationMs, double x0, double y0,
                                           double x1, double y1) noexcept
        {
            return isFingerInput && selectedAtPointerDown && durationMs >= 0
                && durationMs <= 280
                && withinTapSlop(x0, y0, x1, y1);
        }

        // Clip Properties belongs to a desktop mouse/pen double-click, NOT
        // a touch double-tap (which has its own selected-clip context menu).
        // Right-click is exclusively the explicit desktop context action.
        static bool mouseDoubleClickMayOpenProperties(bool isFingerInput,
                                                      bool isRightButton) noexcept
        {
            return !isFingerInput && !isRightButton;
        }

        enum class PinchAxis { undecided, horizontal, vertical, both };

        static PinchAxis pinchAxis(double initialX, double initialY,
                                   double currentX, double currentY) noexcept
        {
            // Ignore tiny orthogonal separation: a nearly horizontal pinch
            // must not resize tracks when the fingers wobble vertically.
            const double dx = std::max(initialX, currentX) >= 16.0
                ? std::abs(currentX - initialX) : 0.0;
            const double dy = std::max(initialY, currentY) >= 16.0
                ? std::abs(currentY - initialY) : 0.0;
            if (std::max(dx, dy) < 3.0)
                return PinchAxis::undecided;
            if (dx >= 1.5 * dy)
                return PinchAxis::horizontal;
            if (dy >= 1.5 * dx)
                return PinchAxis::vertical;
            return PinchAxis::both;
        }

        // Ignore collapsed/crossed finger-axis separations (<=1 px) so a
        // transient zero at an axis crossing cannot trigger a phantom zoom.
        // Compare to the LAST APPLIED pinch distance, not the last sampled
        // distance. Tiny slow movements must accumulate rather than vanish.
        static bool pinchReady(double appliedSeparationPx, double currentSeparationPx) noexcept
        {
            return std::isfinite(appliedSeparationPx)
                && std::isfinite(currentSeparationPx)
                && appliedSeparationPx > 1.0
                && currentSeparationPx > 1.0
                && std::abs(currentSeparationPx - appliedSeparationPx) >= 2.0;
        }

        // Mouse wheel zoom has the same saturation contract as pinch. Reject
        // malformed wheel deltas and bound huge finite deltas before pow()
        // so invalid input cannot poison viewport anchor calculations.
        static double wheelZoomTarget(double current, double deltaY,
                                      double minimum, double maximum) noexcept
        {
            if (!std::isfinite(current) || current <= 0.0)
                return minimum;
            if (!std::isfinite(deltaY) || deltaY == 0.0)
                return current;
            const double boundedDelta = std::clamp(deltaY, -100.0, 100.0);
            const double factor = std::pow(1.2, boundedDelta * 2.0);
            return std::clamp(current * factor, minimum, maximum);
        }

        // Avoid a full clip-renderer rebuild when a pinch is already at a
        // zoom limit. Gesture anchors still advance so reversing feels immediate.
        static double pinchZoomTarget(double current, double ratio,
                                      double minimum, double maximum) noexcept
        {
            // Malformed touch coordinates must not feed NaN/Inf into JUCE zoom
            // setters or cause an unnecessary renderer rebuild.
            if (!std::isfinite(current))
                return minimum;
            if (!std::isfinite(ratio) || ratio <= 0.0)
                return std::clamp(current, minimum, maximum);
            return std::clamp(current * ratio, minimum, maximum);
        }

        static bool pinchZoomChanges(double current, double target) noexcept
        {
            return current != target;
        }

        static double pinchRatio(double appliedSeparationPx, double currentSeparationPx) noexcept
        {
            if (!std::isfinite(appliedSeparationPx)
                || !std::isfinite(currentSeparationPx)
                || appliedSeparationPx <= 1.0
                || currentSeparationPx <= 1.0)
                return 1.0;
            return std::clamp(currentSeparationPx / appliedSeparationPx, 0.85, 1.18);
        }
    };
}
