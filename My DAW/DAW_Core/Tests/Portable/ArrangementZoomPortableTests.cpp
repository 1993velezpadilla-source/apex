#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace juce
{
    template <typename T> T jmax(T lhs, T rhs) { return lhs > rhs ? lhs : rhs; }
    template <typename T> struct Rectangle
    {
        Rectangle expanded(int, int) const { return *this; }
        bool intersects(const Rectangle&) const { return false; }
    };
}

#include "../../Builds/VisualStudio2026/ArrangementEditor/ArrangementZoomCore.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/TouchNavigationMath.h"
#include "../../Source/UICore/TrackPlaylistInlineLayoutCore.h"

int main()
{
    using ArrangementEditor::ArrangementZoomCore;
    using ArrangementEditor::GridLodPolicy;
    using ArrangementEditor::TouchNavigationMath;
    using Layout = DAW::TrackPlaylistInlineLayoutCore;
    using Band = Layout::Anchor::Band;
    using Axis = TouchNavigationMath::PinchAxis;

    ArrangementZoomCore zoom;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    assert(!zoom.setPixelsPerSecond(nan));
    assert(!zoom.setPixelsPerSecond(inf));
    assert(!zoom.setTrackHeightPx(nan));
    assert(!zoom.setTrackHeightPx(-inf));
    assert(zoom.getPixelsPerSecond() == 100.0);
    assert(zoom.getTrackHeightPx() == 80.0);
    assert(!zoom.setTrackHeightPx(80.5));
    assert(zoom.getTrackHeightPx() == 80.5);
    assert(zoom.setTrackHeightPx(81.25));
    assert(zoom.setTrackHeightPx(400.0));
    assert(zoom.getTrackHeightPx() == 180.0);
    assert(zoom.setTrackHeightPx(1.0));
    assert(zoom.getTrackHeightPx() == 24.0);
    assert(zoom.setPixelsPerSecond(10000.0));
    assert(zoom.getPixelsPerSecond() == 4000.0);
    for (int i = 0; i < 1000; ++i)
        assert(!zoom.setPixelsPerSecond(10000.0) && !zoom.setTrackHeightPx(1.0));
    assert(zoom.setPixelsPerSecond(0.0));
    assert(zoom.getPixelsPerSecond() == 2.0);

    constexpr int viewWidth = 400;
    const auto cap = ArrangementZoomCore::maxContentWidth;
    assert(ArrangementZoomCore::contentWidth(2.0, 30.0, viewWidth) == 401);
    assert(ArrangementZoomCore::contentWidth(4000.0, 7200.0, viewWidth) == 28800000);
    assert(ArrangementZoomCore::contentWidth(4000.0, 1e300, viewWidth) == cap);
    assert(ArrangementZoomCore::contentWidth(4000.0, nan, viewWidth) == 401);

    // Round trips with a moving centroid conserve the same timeline second.
    constexpr double seconds = 183.625;
    for (int i = 0; i < 1000; ++i)
    {
        const double focus = 90.0 + i * 0.125;
        zoom.setPixelsPerSecond(i % 2 == 0 ? 20.0 : 217.0);
        const int width = ArrangementZoomCore::contentWidth(zoom.getPixelsPerSecond(), 500.0, viewWidth);
        const double origin = ArrangementZoomCore::scrollOrigin(zoom.timeToX(seconds), focus, width, viewWidth);
        assert(std::abs(zoom.xToTime(origin + focus) - seconds) < 1e-10);
        assert(std::abs(std::round(origin) - origin) <= 0.5);
    }
    assert(ArrangementZoomCore::scrollOrigin(20.0, 100.0, 900, 400) == 0.0);
    assert(ArrangementZoomCore::scrollOrigin(880.0, 100.0, 900, 400) == 500.0);

    // The take rows clamp independently from the physical track height.
    constexpr int expandedLane = 2;
    const int oldExtra = 3 * Layout::takeHeightForTrackHeight(24);
    const int newExtra = 3 * Layout::takeHeightForTrackHeight(120);
    const auto header = Layout::anchorAtY(37.25, 24, expandedLane, oldExtra);
    assert(header.band == Band::header && Layout::yForAnchor(header, 120, expandedLane, newExtra) == 37.25);
    const auto take = Layout::anchorAtY(80.0 + 72.0 + oldExtra * 0.5, 24, expandedLane, oldExtra);
    assert(take.band == Band::takes && take.lane == expandedLane && take.position == 0.5);
    assert(Layout::yForAnchor(take, 120, expandedLane, newExtra) == 80.0 + 360.0 + newExtra * 0.5);
    const auto track = Layout::anchorAtY(80.0 + 4.25 * 24.0 + oldExtra, 24, expandedLane, oldExtra);
    assert(track.band == Band::track && track.lane == 4 && track.position == 0.25);
    assert(Layout::yForAnchor(track, 120, expandedLane, newExtra) == 80.0 + 4.25 * 120.0 + newExtra);
    assert(Layout::laneAtY(80 + 72, 24, expandedLane, oldExtra) == -1);
    assert(Layout::laneAtY(80 + 72 + oldExtra, 24, expandedLane, oldExtra) == 3);

    assert(TouchNavigationMath::pinchAxis(120.0, 35.0, 121.5, 36.0) == Axis::undecided);
    assert(TouchNavigationMath::pinchAxis(120.0, 35.0, 134.0, 38.0) == Axis::horizontal);
    assert(TouchNavigationMath::pinchAxis(35.0, 120.0, 38.0, 134.0) == Axis::vertical);
    assert(TouchNavigationMath::pinchAxis(85.0, 90.0, 99.0, 104.0) == Axis::both);
    assert(TouchNavigationMath::pinchAxis(10.0, 150.0, 15.0, 152.0) == Axis::undecided);

    const auto dense = GridLodPolicy::visibleBars(153001.0, 153401.0, 4.0, 8);
    assert(dense.first % 8 == 0 && dense.last - dense.first < 102);
    const auto partial = GridLodPolicy::visibleBars(45.0, 200.0, 40.0, 1);
    assert(partial.first == 1 && partial.last == 5);
    assert(GridLodPolicy::visibleBars(0.0, 100.0, nan, 1).last < 0);
    assert(GridLodPolicy::barStep(std::numeric_limits<double>::denorm_min()) == 32);
    assert(GridLodPolicy::barStep(nan) == 1 && GridLodPolicy::suppressBeats(nan));

    std::cout << "Arrangement zoom, anchor, grid and pinch regressions passed\n";
}
