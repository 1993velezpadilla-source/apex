// C++17 regression test for ArrangementZoomCore's UI-only setters.
// JUCE geometry shim: no rendering/audio code is exercised here.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace juce
{
    template <typename T> T jlimit(T lo, T hi, T value)
    {
        return std::clamp(value, lo, hi);
    }

    template <typename T> T jmax(T lhs, T rhs)
    {
        return std::max(lhs, rhs);
    }

    template <typename T> struct Rectangle
    {
        Rectangle expanded(int, int) const { return *this; }
        bool intersects(const Rectangle&) const { return false; }
    };
}

#include "../../Builds/VisualStudio2026/ArrangementEditor/ArrangementZoomCore.h"

int main()
{
    using Zoom = ArrangementEditor::TimelineZoomController;
    Zoom camera;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    camera.apply({ 1e300, 1000.0 });
    assert(camera.getPixelsPerSecond() == 4000.0);
    assert(camera.getTrackHeightPx() == 180);
    camera.apply({ nan, -inf });
    assert(camera.getPixelsPerSecond() == 4000.0);
    assert(camera.getTrackHeightPx() == 180);
    camera.apply({ -1e300, -1e300 });
    assert(camera.getPixelsPerSecond() == 2.0);
    assert(camera.getTrackHeightPx() == 24);
    assert(Zoom::wheelFactor(nan) == 1.0);
    assert(Zoom::contentWidth(0.0, 100.0, 360) == 361);
    assert(Zoom::contentWidth(1e300, 4000.0, 360) == std::numeric_limits<int>::max() / 2);

    // Preserve the exact time and track under the focus over repeated
    // in/out cycles with integer scrollbar positions.
    camera.apply({ 100.0, 80.0 });
    Zoom::Point actual{ 372003.0, 147.0 };
    const Zoom::Point focus{ 120.5, 235.0 };
    const Zoom::Point limit{ 1e8, 10000.0 };
    Zoom::Lanes lanes{ 80, 80, 1, 132 };
    const auto anchor = Zoom::capture(camera.scale(), actual, focus, lanes);
    for (int i = 0; i < 10000; ++i)
    {
        const bool enlarged = i % 2 == 0;
        Zoom::Request request{ { enlarged ? 173.5 : 100.0, enlarged ? 126.0 : 80.0 },
                              Zoom::capture(camera.scale(), camera.origin(actual), focus, lanes), focus, true, true };
        lanes = enlarged ? Zoom::Lanes{ 80, 126, 1, 208 } : Zoom::Lanes{ 80, 80, 1, 132 };
        const auto next = Zoom::projectOrigin(request, lanes, camera.origin(actual), limit);
        const Zoom::Point rounded{ std::round(next.x), std::round(next.y) };
        camera.apply(request.scale);
        camera.remember(next, rounded, rounded);
        actual = rounded;
        assert(std::abs(camera.xToTime(next.x + focus.x) - anchor.seconds) < 1e-8);
        assert(std::abs(Zoom::projectY(anchor, lanes) - (next.y + focus.y)) < 1e-8);
    }
    assert(actual.x == 372003.0 && actual.y == 147.0);
    const auto take = Zoom::capture(camera.scale(), {}, { 0.0, 290.0 }, { 80, 80, 1, 132 });
    assert(take.band == Zoom::Anchor::Band::takes);
    assert(std::abs(Zoom::projectY(take, { 80, 126, 1, 208 }) - (332.0 + 50.0 * 208.0 / 132.0)) < 1e-10);
    const auto outside = Zoom::projectOrigin({ { 4000.0, 80.0 }, { 1000.0, 0.0 }, {}, true, false },
                                               lanes, { 0.0, 400.0 }, { 440.0, 110.0 });
    assert(outside.x == 440.0 && outside.y == 110.0);

    // Large missed-frame movement must keep the full pinch ratio. The first
    // sample after reaching a zoom limit must reverse immediately.
    Zoom::Pinch pinch;
    camera.apply({ 3500.0, 80.0 });
    pinch.begin({ 200.0, 120.0 }, { 50.0, 12.0 });
    auto request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { 100.0, 12.0 }, camera.scale());
    assert(request.scale.pixelsPerSecond == 4000.0);
    camera.apply(request.scale);
    request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { 99.0, 12.0 }, camera.scale());
    assert(request.scale.pixelsPerSecond == 3960.0);
    request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { -99.0, 12.0 }, camera.scale());
    assert(request.scale.pixelsPerSecond == 4000.0);
    pinch.begin({ 200.0, 120.0 }, { 50.0, 50.0 });
    camera.apply({ 100.0, 80.0 });
    request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { 125.0, 125.0 }, camera.scale());
    assert(request.scale.pixelsPerSecond == 250.0);
    assert(request.scale.laneHeight == 180.0);

    // Grid density is used on every Arrangement paint. A very small
    // px-per-bar used to overflow the ceil(24 / density) -> int conversion.
    // Invalid input must also suppress beat detail rather than drawing it.
    using ArrangementEditor::GridLodPolicy;
    assert(GridLodPolicy::barStep(24.0) == 1);
    assert(GridLodPolicy::barStep(23.0) == 2);
    assert(GridLodPolicy::barStep(12.0) == 2);
    assert(GridLodPolicy::barStep(11.0) == 4);
    assert(GridLodPolicy::barStep(6.0) == 4);
    assert(GridLodPolicy::barStep(5.0) == 8);
    assert(GridLodPolicy::barStep(3.0) == 8);
    assert(GridLodPolicy::barStep(2.0) == 16);
    assert(GridLodPolicy::barStep(1.5) == 16);
    assert(GridLodPolicy::barStep(1.0) == 32);
    assert(GridLodPolicy::barStep(std::numeric_limits<double>::denorm_min()) == 32);
    assert(GridLodPolicy::barStep(1e-300) == 32);
    assert(GridLodPolicy::barStep(0.0) == 1);
    assert(GridLodPolicy::barStep(-1.0) == 1);
    assert(GridLodPolicy::barStep(nan) == 1);
    assert(GridLodPolicy::barStep(inf) == 1);
    assert(GridLodPolicy::barStep(-inf) == 1);
    assert(GridLodPolicy::suppressBeats(nan));
    assert(GridLodPolicy::suppressBeats(-inf));
    assert(GridLodPolicy::suppressBeats(5.9));
    assert(!GridLodPolicy::suppressBeats(6.0));
    assert(!GridLodPolicy::suppressBeats(inf));
    // For ordinary finite densities, preserve the previous power-of-two
    // step mapping exactly (including all the transition boundaries).
    for (int quarter = 1; quarter <= 400; ++quarter)
    {
        const double density = quarter * 0.25;
        const int raw = static_cast<int>(std::ceil(24.0 / density));
        const int expected = raw <= 1 ? 1 : raw <= 2 ? 2 : raw <= 4 ? 4
                           : raw <= 8 ? 8 : raw <= 16 ? 16 : 32;
        assert(GridLodPolicy::barStep(density) == expected);
    }

    std::cout << "TimelineZoomController: anchor, limit, pinch and grid regressions passed\n";
}
