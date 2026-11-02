// Build standalone on any C++17 compiler:
// c++ -std=c++17 -Wall -Wextra -Werror TouchNavigationMathPortableTests.cpp -o touch-tests && ./touch-tests
#include "../../Builds/VisualStudio2026/ArrangementEditor/TouchNavigationMath.h"
#include "../../Source/UICore/BeatMakingFeaturePolicy.h"

// Audio-first release invariant: UI code must retain but must not expose
// Piano Roll until the explicit future beat-making feature update.
static_assert(!DAW::BeatMakingFeaturePolicy::kPianoRollEnabled,
              "Current APEX release must not enable the Piano Roll");

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using ArrangementEditor::TouchNavigationMath;

int main()
{
    // A swipe is measured in screen coordinates, so viewport motion does not
    // become new finger motion. The same finger displacement gives the same
    // result regardless of the current content zoom or view origin.
    assert(TouchNavigationMath::panPosition(250, 30.0, 2000) == 220);
    assert(TouchNavigationMath::panPosition(250, -30.0, 2000) == 280);
    assert(TouchNavigationMath::panPosition(5, 50.0, 2000) == 0);
    assert(TouchNavigationMath::panPosition(999, -50.0, 1000) == 1000);

    // The world coordinate under a pinch centroid remains screen-stationary:
    // anchor at content pixel 320, viewport pixel 120, doubling zoom moves
    // scroll offset to 520 (640 - 120).
    assert(TouchNavigationMath::zoomPosition(320.0, 120.0, 2.0, 4000) == 520);
    assert(TouchNavigationMath::zoomPosition(320.0, 120.0, 0.5, 4000) == 40);
    assert(TouchNavigationMath::zoomPosition(320.0, 120.0, 2.0, 400) == 400);
    assert(TouchNavigationMath::zoomPosition(5.0, 120.0, 0.5, 400) == 0);

    // Slow pinch motion accumulates against the last APPLIED distance.
    // A 0.75px/frame motion should eventually trigger, not be discarded.
    const double applied = 100.0;
    assert(!TouchNavigationMath::pinchReady(applied, 100.75));
    assert(!TouchNavigationMath::pinchReady(applied, 101.50));
    assert(TouchNavigationMath::pinchReady(applied, 102.25));
    assert(TouchNavigationMath::pinchReady(applied, 97.9));
    assert(!TouchNavigationMath::pinchReady(0.0, 110.0));

    assert(std::abs(TouchNavigationMath::pinchRatio(100.0, 110.0) - 1.10) < 1e-8);
    assert(std::abs(TouchNavigationMath::pinchRatio(100.0, 200.0) - 1.18) < 1e-8);
    assert(std::abs(TouchNavigationMath::pinchRatio(100.0, 10.0) - 0.85) < 1e-8);
    assert(TouchNavigationMath::pinchRatio(0.0, 200.0) == 1.0);

    // A malformed touch sample must not propagate NaN/Inf to JUCE zoom,
    // trigger a phantom pinch, or request a clip-renderer rebuild.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    assert(!TouchNavigationMath::pinchReady(100.0, nan));
    assert(!TouchNavigationMath::pinchReady(100.0, inf));
    assert(!TouchNavigationMath::pinchReady(inf, 120.0));
    assert(TouchNavigationMath::pinchRatio(100.0, nan) == 1.0);
    assert(TouchNavigationMath::pinchRatio(100.0, inf) == 1.0);
    assert(TouchNavigationMath::pinchRatio(inf, 120.0) == 1.0);
    assert(TouchNavigationMath::pinchZoomTarget(100.0, nan, 2.0, 4000.0) == 100.0);
    assert(TouchNavigationMath::pinchZoomTarget(100.0, inf, 2.0, 4000.0) == 100.0);
    assert(TouchNavigationMath::pinchZoomTarget(nan, 1.1, 2.0, 4000.0) == 2.0);
    assert(!TouchNavigationMath::pinchZoomChanges(100.0,
        TouchNavigationMath::pinchZoomTarget(100.0, nan, 2.0, 4000.0)));

    // Saturated phone pinch must not rebuild every clip renderer at the
    // horizontal/vertical zoom limits. A reversed pinch can zoom again.
    assert(TouchNavigationMath::pinchZoomTarget(4000.0, 1.18, 2.0, 4000.0) == 4000.0);
    assert(TouchNavigationMath::pinchZoomTarget(2.0, 0.85, 2.0, 4000.0) == 2.0);
    assert(TouchNavigationMath::pinchZoomTarget(180.0, 1.18, 24.0, 180.0) == 180.0);
    assert(TouchNavigationMath::pinchZoomTarget(24.0, 0.85, 24.0, 180.0) == 24.0);
    assert(!TouchNavigationMath::pinchZoomChanges(4000.0,
        TouchNavigationMath::pinchZoomTarget(4000.0, 1.18, 2.0, 4000.0)));
    assert(!TouchNavigationMath::pinchZoomChanges(180.0,
        TouchNavigationMath::pinchZoomTarget(180.0, 1.18, 24.0, 180.0)));
    assert(TouchNavigationMath::pinchZoomChanges(180.0,
        TouchNavigationMath::pinchZoomTarget(180.0, 0.85, 24.0, 180.0)));
    assert(TouchNavigationMath::pinchZoomChanges(4000.0,
        TouchNavigationMath::pinchZoomTarget(4000.0, 0.85, 2.0, 4000.0)));
    assert(std::abs(TouchNavigationMath::pinchZoomTarget(100.0, 1.1, 2.0, 4000.0)
                    - 110.0) < 1e-8);
    for (int i = 0; i < 1000; ++i)
        assert(!TouchNavigationMath::pinchZoomChanges(180.0,
            TouchNavigationMath::pinchZoomTarget(180.0, 1.18, 24.0, 180.0)));

    // Selected-region second tap => menu. A drag must cancel it, including
    // diagonal movements that stay below the per-axis threshold.
    assert(TouchNavigationMath::withinTapSlop(100, 100, 100, 100));
    assert(TouchNavigationMath::withinTapSlop(100, 100, 104, 104));
    assert(!TouchNavigationMath::withinTapSlop(100, 100, 106, 106));
    assert(!TouchNavigationMath::withinTapSlop(100, 100, 112, 100));
    assert(!TouchNavigationMath::withinTapSlop(100, 100, 100, 92));

    // Menu arms at 320ms, but never on a normal tap or a drag.
    assert(!TouchNavigationMath::contextHoldReady(100, 100, 100, 100, 100));
    assert(!TouchNavigationMath::contextHoldReady(319, 100, 100, 100, 100));
    assert(TouchNavigationMath::contextHoldReady(320, 100, 100, 104, 104));
    assert(!TouchNavigationMath::contextHoldReady(700, 100, 100, 108, 100));

    // Only a physical TOUCH finger gets second-tap context action. A mouse
    // first+second click must not open the radial/classic menu; an explicit
    // mouse right-click still does so in ArrangementViewCore::mouseUp.
    assert(TouchNavigationMath::selectedSecondTapReady(true, true, 80, 100, 100, 100, 100));
    assert(TouchNavigationMath::selectedSecondTapReady(true, true, 280, 100, 100, 104, 104));
    assert(!TouchNavigationMath::selectedSecondTapReady(false, true, 80, 100, 100, 100, 100)); // mouse / pen
    assert(!TouchNavigationMath::selectedSecondTapReady(true, false, 80, 100, 100, 100, 100)); // first tap
    assert(!TouchNavigationMath::selectedSecondTapReady(true, true, 281, 100, 100, 100, 100)); // press too long
    assert(!TouchNavigationMath::selectedSecondTapReady(true, true, 120, 100, 100, 108, 100)); // drag
    assert(TouchNavigationMath::mouseDoubleClickMayOpenProperties(false, false)); // mouse L-button
    assert(!TouchNavigationMath::mouseDoubleClickMayOpenProperties(true, false)); // no touch property popup
    assert(!TouchNavigationMath::mouseDoubleClickMayOpenProperties(false, true)); // right click stays contextual
    std::cout << "TouchNavigationMath: touchscreen/mouse click routing assertions passed\n";
    return 0;
}
