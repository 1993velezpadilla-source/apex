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
    ArrangementEditor::ArrangementZoomCore zoom;
    int rebuildCallbacks = 0;
    zoom.onZoomChanged = [&] { ++rebuildCallbacks; };

    // A saturated pinch/wheel must not rebuild all clip renderers on each
    // repeated callback; actual changes still update the lane height.
    assert(zoom.getTrackHeightPx() == 80.0);
    zoom.setTrackHeightPx(80.0);
    assert(rebuildCallbacks == 0);
    zoom.setTrackHeightPx(1000.0);
    assert(zoom.getTrackHeightPx() == 180.0);
    assert(rebuildCallbacks == 1);
    for (int i = 0; i < 1000; ++i)
        zoom.setTrackHeightPx(1000.0);
    assert(rebuildCallbacks == 1);

    // Malformed coordinates/gesture math must never poison the zoom state
    // or request renderer reconstruction.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    zoom.setTrackHeightPx(nan);
    zoom.setTrackHeightPx(inf);
    zoom.setTrackHeightPx(-inf);
    assert(zoom.getTrackHeightPx() == 180.0);
    assert(rebuildCallbacks == 1);

    zoom.setTrackHeightPx(-1e300);
    assert(zoom.getTrackHeightPx() == 24.0);
    assert(rebuildCallbacks == 2);
    zoom.setTrackHeightPx(-1e300);
    assert(rebuildCallbacks == 2);
    zoom.setTrackHeightPx(80.0);
    assert(zoom.getTrackHeightPx() == 80.0);
    assert(rebuildCallbacks == 3);

    // Mouse-wheel zoom must anchor to the SAME clamped height that the setter
    // applies (old wheel path used 240 while the setter capped at 180).
    const double wheelHeight = ArrangementEditor::ArrangementZoomCore::clampTrackHeightPx(240.0, 80.0);
    assert(wheelHeight == 180.0);
    zoom.setTrackHeightPx(wheelHeight);
    assert(zoom.getTrackHeightPx() == wheelHeight);
    assert(rebuildCallbacks == 4);
    for (int i = 0; i < 1000; ++i)
        zoom.setTrackHeightPx(ArrangementEditor::ArrangementZoomCore::clampTrackHeightPx(240.0, zoom.getTrackHeightPx()));
    assert(rebuildCallbacks == 4);
    assert(ArrangementEditor::ArrangementZoomCore::clampTrackHeightPx(nan, 180.0) == 180.0);
    assert(ArrangementEditor::ArrangementZoomCore::clampTrackHeightPx(inf, 180.0) == 180.0);
    assert(ArrangementEditor::ArrangementZoomCore::clampTrackHeightPx(-999.0, 180.0) == 24.0);

    // Horizontal zoom has no callback, but must remain finite after invalid
    // input and respect both bounds when the requested value is finite.
    assert(zoom.getPixelsPerSecond() == 100.0);
    zoom.setPixelsPerSecond(nan);
    zoom.setPixelsPerSecond(inf);
    zoom.setPixelsPerSecond(-inf);
    assert(zoom.getPixelsPerSecond() == 100.0);
    zoom.setPixelsPerSecond(1e300);
    assert(zoom.getPixelsPerSecond() == 4000.0);
    zoom.setPixelsPerSecond(-1e300);
    assert(zoom.getPixelsPerSecond() == 2.0);
    zoom.setPixelsPerSecond(100.0);
    assert(zoom.getPixelsPerSecond() == 100.0);

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

    // Both axes must be visible to the ONE callback, including clamps and
    // malformed input. A diagonal gesture used to trigger two host layouts.
    ArrangementEditor::ArrangementZoomCore gestureZoom;
    int gestureLayouts = 0;
    gestureZoom.onZoomChanged = [&]
    {
        ++gestureLayouts;
        assert(gestureZoom.getPixelsPerSecond() == 250.0);
        assert(gestureZoom.getTrackHeightPx() == 120.0);
    };
    assert(gestureZoom.setZoom(250.0, 120.0));
    assert(gestureLayouts == 1);
    for (int i = 0; i < 1000; ++i)
        assert(!gestureZoom.setZoom(250.0, 120.0));
    assert(!gestureZoom.setZoom(nan, inf));
    assert(gestureLayouts == 1);
    gestureZoom.onZoomChanged = [&] { ++gestureLayouts; };
    assert(gestureZoom.setZoom(1e300, -1e300));
    assert(gestureZoom.getPixelsPerSecond() == 4000.0);
    assert(gestureZoom.getTrackHeightPx() == 24.0);
    assert(gestureLayouts == 2);
    assert(!gestureZoom.setZoom(inf, -inf));

    // Count real generated grid candidates: work must depend on the dirty
    // viewport width, even many hours into a long, highly zoomed timeline.
    const auto countBars = [](GridLodPolicy::BarRange range)
    {
        return range.last < range.first ? int64_t(0)
            : (range.last - range.first) / range.step + 1;
    };
    for (double pps : {2.0, 5.0, 100.0, 4000.0})
        for (double bpm : {60.0, 120.0, 300.0})
        {
            const double pxPerBar = pps * 240.0 / bpm;
            const int step = GridLodPolicy::barStep(pxPerBar);
            for (double origin : {0.0, 43200.0 * pps, 1000000000.0})
            {
                const auto visible = GridLodPolicy::visibleBars(origin, origin + 720.0,
                                                               pxPerBar, step);
                const auto strip = GridLodPolicy::visibleBars(origin, origin + 7.0,
                                                             pxPerBar, step);
                assert(countBars(visible) <= 720.0 / (pxPerBar * step) + 3);
                assert(countBars(strip) <= 7.0 / (pxPerBar * step) + 3);
                assert(visible.first % step == 0);
                assert(visible.first * pxPerBar <= origin);
                assert(visible.last * pxPerBar >= origin + 720.0);
            }
            const int labelStep = GridLodPolicy::labelStep(pxPerBar);
            assert(labelStep * pxPerBar >= 48.0);
            assert((labelStep & (labelStep - 1)) == 0);
        }
    assert(countBars(GridLodPolicy::visibleBars(5, 5, 10, 1)) == 0);
    assert(countBars(GridLodPolicy::visibleBars(5, 4, 10, 1)) == 0);
    assert(countBars(GridLodPolicy::visibleBars(0, 720, nan, 1)) == 0);
    assert(countBars(GridLodPolicy::visibleBars(0, inf, 10, 1)) == 0);
    assert(countBars(GridLodPolicy::visibleBars(-80, -10, 10, 1)) == 0);
    const auto largeRange = GridLodPolicy::visibleBars(1e300, 1.1e300, 1e-300, 0);
    assert(largeRange.first >= 0 && largeRange.last >= largeRange.first);

    std::cout << "ArrangementZoomCore: bounded grid and batched pinch regressions passed\n";
}
