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

    // Selected-region second tap => menu. A drag must cancel it, including
    // diagonal movements that stay below the per-axis threshold.
    assert(TouchNavigationMath::withinTapSlop(100, 100, 100, 100));
    assert(TouchNavigationMath::withinTapSlop(100, 100, 104, 104));
    assert(!TouchNavigationMath::withinTapSlop(100, 100, 106, 106));
    assert(!TouchNavigationMath::withinTapSlop(100, 100, 112, 100));
    assert(!TouchNavigationMath::withinTapSlop(100, 100, 100, 92));

    // Context menu must not appear on a routine second short tap or drag.
    assert(!TouchNavigationMath::contextHoldReady(100, 100, 100, 100, 100));
    assert(!TouchNavigationMath::contextHoldReady(499, 100, 100, 100, 100));
    assert(TouchNavigationMath::contextHoldReady(500, 100, 100, 104, 104));
    assert(!TouchNavigationMath::contextHoldReady(700, 100, 100, 108, 100));

    std::cout << "TouchNavigationMath: 26 deterministic assertions passed\n";
    return 0;
}
