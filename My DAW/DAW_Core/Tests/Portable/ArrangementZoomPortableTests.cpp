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
        T x {}, y {}, w {}, h {};
        T getX() const { return x; }
        T getY() const { return y; }
        T getWidth() const { return w; }
        T getHeight() const { return h; }
        T getRight() const { return x + w; }
        T getBottom() const { return y + h; }

        Rectangle expanded(int dx, int dy) const
        {
            return { x - dx, y - dy, w + 2 * dx, h + 2 * dy };
        }

        Rectangle withX(T newX) const { return { newX, y, w, h }; }
        Rectangle withWidth(T width) const { return { x, y, width, h }; }
        Rectangle withLeft(T left) const { return { left, y, getRight() - left, h }; }
        Rectangle withRight(T right) const { return { x, y, right - x, h }; }
        Rectangle withTop(T top) const { return { x, top, w, getBottom() - top }; }
        Rectangle withBottom(T bottom) const { return { x, y, w, bottom - y }; }

        bool intersects(const Rectangle& other) const
        {
            return w > 0 && h > 0 && other.w > 0 && other.h > 0
                && x < other.getRight() && other.x < getRight()
                && y < other.y + other.h && other.y < y + h;
        }

        bool contains(const Rectangle& other) const
        {
            return x <= other.x && getRight() >= other.getRight()
                && y <= other.y && y + h >= other.y + other.h;
        }
    };
}

#include "../../Builds/VisualStudio2026/ArrangementEditor/ArrangementZoomCore.h"

int main()
{
    using Camera = ArrangementEditor::TimelineZoomController;
    assert(Camera::timelineExtentSeconds(30.0, 28.0) == 600.0);
    assert(Camera::timelineExtentSeconds(30.0, 480.0) == 600.0);
    const auto expanded = Camera::timelineExtentSeconds(600.0, 481.0);
    assert(expanded == 1081.0);
    assert(Camera::timelineExtentSeconds(expanded, 482.0) == expanded);
    assert(Camera::timelineExtentSeconds(1500.0, 10.0) == 1500.0);
    assert(Camera::timelineExtentSeconds(600.0, std::numeric_limits<double>::infinity()) == 600.0);

    using Zoom = ArrangementEditor::TimelineZoomController;
    Zoom camera;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    camera.apply({ 1e300, 1000.0 });
    assert(camera.getPixelsPerSecond() == 16'000.0);
    assert(camera.getTrackHeightPx() == 360);
    camera.apply({ nan, -inf });
    assert(camera.getPixelsPerSecond() == 16'000.0);
    assert(camera.getTrackHeightPx() == 360);
    camera.apply({ -1e300, -1e300 });
    assert(camera.getPixelsPerSecond() == 2.0);
    assert(camera.getTrackHeightPx() == 24);
    assert(Zoom::wheelFactor(nan) == 1.0);
    assert(camera.timeToPixel(nan) == 0);
    assert(camera.timeToPixel(inf) == Zoom::maxContentWidth);
    assert(camera.timeToPixel(-1e300) == -Zoom::maxContentWidth);
    assert(camera.timeToPixel(0.75) == 1);
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
        Zoom::Request request{ { enlarged ? 16'000.0 : 100.0, enlarged ? 360.0 : 80.0 },
                              Zoom::capture(camera.scale(), camera.origin(actual), focus, lanes), focus, true, true };
        lanes = enlarged ? Zoom::Lanes{ 80, 360, 1, 594 } : Zoom::Lanes{ 80, 80, 1, 132 };
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
    camera.apply({ 15'500.0, 80.0 });
    pinch.begin({ 200.0, 120.0 }, { 50.0, 12.0 });
    auto request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { 100.0, 12.0 }, camera.scale());
    assert(request.scale.pixelsPerSecond == 16'000.0);
    camera.apply(request.scale);
    request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { 99.0, 12.0 }, camera.scale());
    assert(request.scale.pixelsPerSecond == 15'840.0);
    request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { -99.0, 12.0 }, camera.scale());
    assert(request.scale.pixelsPerSecond == 16'000.0);
    pinch.begin({ 200.0, 120.0 }, { 50.0, 50.0 });
    camera.apply({ 100.0, 150.0 });
    request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { 125.0, 125.0 }, camera.scale());
    assert(request.scale.pixelsPerSecond == 250.0);
    assert(request.scale.laneHeight == 360.0);
    camera.apply(request.scale);
    request = pinch.move(camera, {}, lanes, { 200.0, 120.0 }, { 125.0, 124.0 }, camera.scale());
    assert(Zoom::displayHeight(request.scale.laneHeight) == 357);
    assert(request.scale.pixelsPerSecond == 250.0);

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

    assert(GridLodPolicy::barLabelStep(4.0) == 16);
    assert(GridLodPolicy::barLabelStep(20.0) == 2);
    assert(GridLodPolicy::barLabelStep(40.0) == 1);
    for (int quarter = 1; quarter <= 400; ++quarter)
    {
        const double density = quarter * 0.25;
        assert(GridLodPolicy::barLabelStep(density) % GridLodPolicy::barStep(density) == 0);
    }

    // Horizontal scroll must leave the prepared window only when a hidden
    // clip could enter, including one reached by a small diagonal pan.
    using LOD = ArrangementEditor::ArrangementClipLodPolicy;
    const juce::Rectangle<int> viewport { 1200, 200, 400, 600 };
    const auto region = LOD::renderRegion(viewport, 80);
    assert(region.intersects({ 1750, 240, 50, 80 }));
    assert(!region.intersects({ 1950, 240, 50, 80 }));
    auto guard = LOD::visibilityGuard(viewport, 20000, 80);
    assert(guard.contains(viewport.withX(17000)));
    assert(guard.contains(viewport.withX(0)));
    LOD::excludeHiddenClip(guard, viewport, { 2500, 235, 70, 80 }, 80);
    LOD::excludeHiddenClip(guard, viewport, { 1950, 810, 70, 80 }, 80);
    LOD::excludeHiddenClip(guard, viewport, { 1850, 900, 70, 80 }, 80);
    LOD::excludeHiddenClip(guard, viewport, { 675, 250, 200, 80 }, 80);
    // The expanded half-viewport render region is authoritative. A hidden
    // clip starts painting exactly when that region reaches its first pixel.
    assert(guard.contains(viewport.withX(1075)));
    assert(!guard.contains(viewport.withX(1074)));
    assert(guard.contains(viewport.withX(1350)));
    assert(!guard.contains(viewport.withX(1351)));
    assert(guard.contains({ 1200, 220, 400, 600 }));
    assert(!guard.contains({ 1200, 221, 400, 600 }));

    // The old guard failed here: at x=2 the clip is in the prefetch region
    // but was kept invisible until x=701 (a half-screen late pop-in).
    const juce::Rectangle<int> nearRightViewport { 0, 0, 1400, 800 };
    auto nearRightGuard = LOD::visibilityGuard(nearRightViewport, 5000, 80);
    const juce::Rectangle<int> nearRightClip { 2101, 240, 100, 80 };
    assert(!LOD::renderRegion(nearRightViewport, 80).intersects(nearRightClip));
    LOD::excludeHiddenClip(nearRightGuard, nearRightViewport, nearRightClip, 80);
    assert(nearRightGuard.contains(nearRightViewport.withX(1)));
    assert(!nearRightGuard.contains(nearRightViewport.withX(2)));
    assert(LOD::renderRegion(nearRightViewport.withX(2), 80).intersects(nearRightClip));

    // Prefer vertical separation for clips in another track. Those clips
    // should not create "chunk loading" scans while the user pans left/right.
    auto otherLaneGuard = LOD::visibilityGuard(nearRightViewport, 50000, 80);
    const juce::Rectangle<int> otherLaneClip { 2101, 881, 100, 80 };
    LOD::excludeHiddenClip(otherLaneGuard, nearRightViewport, otherLaneClip, 80);
    assert(otherLaneGuard.contains(nearRightViewport.withX(30000)));
    assert(otherLaneGuard.contains({ 0, 1, 1400, 800 }));
    assert(!otherLaneGuard.contains({ 0, 2, 1400, 800 }));

    // Exhaustive containment contract for representative hidden-clips:
    // if the viewport fits in the guard, no excluded clip may enter the
    // expanded paint region. Sweep BOTH directions and diagonal movement.
    const juce::Rectangle<int> proofViewport { 1000, 200, 400, 600 };
    for (int clipX = 400; clipX <= 2100; clipX += 85)
        for (int clipY = -100; clipY <= 1200; clipY += 65)
        {
            const juce::Rectangle<int> hidden { clipX, clipY, 40, 40 };
            if (LOD::renderRegion(proofViewport, 80).intersects(hidden))
                continue;
            auto trial = LOD::visibilityGuard(proofViewport, 3000, 80);
            LOD::excludeHiddenClip(trial, proofViewport, hidden, 80);
            assert(trial.contains(proofViewport));
            for (int dx = -250; dx <= 250; dx += 5)
                for (int dy = -45; dy <= 45; dy += 5)
                {
                    const juce::Rectangle<int> moved {
                        proofViewport.x + dx, proofViewport.y + dy,
                        proofViewport.w, proofViewport.h };
                    if (trial.contains(moved))
                        assert(!LOD::renderRegion(moved, 80).intersects(hidden));
                }
        }

    // Reproduce the Windows regression: a detached 1400px arranger was only
    // 1400px wide, so the old guard ended at the visible right edge and all
    // 100 small horizontal scroll steps rescanned every clip. Keep a bounded
    // preparation runway even before a live REC extends the content width.
    const juce::Rectangle<int> shortContentViewport { 0, 0, 1400, 1500 };
    const auto shortContentGuard = LOD::visibilityGuard(shortContentViewport, 1400, 80);
    assert(shortContentGuard.contains(shortContentViewport));
    assert(shortContentGuard.contains(shortContentViewport.withX(8)));
    assert(shortContentGuard.contains(shortContentViewport.withX(600)));
    assert(shortContentGuard.contains(shortContentViewport.withX(700)));
    assert(!shortContentGuard.contains(shortContentViewport.withX(701)));

    std::cout << "TimelineZoomController: anchor, limit, pinch, grid and clip guard regressions passed\n";
}
