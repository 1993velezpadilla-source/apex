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

    std::cout << "ArrangementZoomCore: saturated/invalid UI zoom regression passed\n";
}
