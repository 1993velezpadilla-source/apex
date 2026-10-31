#include "../../Source/UICore/MobilePanelGeometry.h"
#include "../../Source/UICore/MobileTrackRowGeometry.h"
#include <cassert>
#include <initializer_list>
#include <limits>
using DAW::mobile::fitBubblegumSends;
using DAW::mobile::phoneCloseTouchBounds;
constexpr bool inside(DAW::mobile::PanelBounds p, int w, int h)
{
    return p.x >= 0 && p.y >= 0 && p.x + p.width <= w && p.y + p.height <= h;
}
int main()
{
    // The painted 30x30 close orb gets a 44x44 touch target on phones.
    constexpr auto closePortrait = phoneCloseTouchBounds(360, 420);
    static_assert(closePortrait.x == 313 && closePortrait.y == 3
                  && closePortrait.width == 44 && closePortrait.height == 44
                  && inside(closePortrait, 360, 420));
    constexpr auto closeLandscape = phoneCloseTouchBounds(360, 80);
    static_assert(closeLandscape.width == 44 && closeLandscape.height == 44
                  && inside(closeLandscape, 360, 80));
    constexpr auto closeTiny = phoneCloseTouchBounds(27, 20);
    static_assert(closeTiny.width == 27 && closeTiny.height == 20
                  && inside(closeTiny, 27, 20));
    constexpr auto closeZero = phoneCloseTouchBounds(0, 0);
    static_assert(closeZero.width == 0 && closeZero.height == 0);
    for (int w = 1; w <= 600; w += 7)
        for (int h = 1; h <= 500; h += 9)
        {
            const auto target = phoneCloseTouchBounds(w, h);
            assert(inside(target, w, h));
            assert(target.width == (w < 44 ? w : 44));
            assert(target.height == (h < 44 ? h : 44));
        }

    constexpr auto portrait = fitBubblegumSends(393, 852, 360, 70, true);
    static_assert(portrait.width == 360 && portrait.height == 420
                  && inside(portrait, 393, 852));
    constexpr auto landscape = fitBubblegumSends(852, 300, 820, 60, true);
    static_assert(landscape.width == 360 && landscape.height == 284
                  && inside(landscape, 852, 300));
    constexpr auto tiny = fitBubblegumSends(245, 220, 220, 200, true);
    static_assert(tiny.width == 229 && tiny.height == 204
                  && inside(tiny, 245, 220));
    constexpr auto desktop = fitBubblegumSends(1800, 900, 450, 490, false);
    static_assert(desktop.width == 260 && desktop.height == 380
                  && inside(desktop, 1800, 900));
    // A panel dragged beyond any phone edge must have a valid on-screen
    // recovery position. The UI calls this fitter when the drag ends.
    constexpr auto offLeft = fitBubblegumSends(393, 852, -1000, -1000, true);
    static_assert(offLeft.x == 8 && offLeft.y == 8
                  && inside(offLeft, 393, 852));
    constexpr auto offRight = fitBubblegumSends(393, 852, 20000, 20000, true);
    static_assert(offRight.x == 25 && offRight.y == 424
                  && inside(offRight, 393, 852));
    for (int w : {1, 2, 10, 160, 245, 393, 852})
        for (int h : {1, 2, 10, 120, 220, 300, 852})
            for (int anchor : {-20000, -100, 0, 100, 20000})
            {
                const auto fitted = fitBubblegumSends(w, h, anchor, anchor, true);
                assert(inside(fitted, w, h));
            }
    // Extreme coordinates from restored windows or drag gesture corruption
    // previously caused signed-int overflow in anchorX - width - 16.
    constexpr int lo = std::numeric_limits<int>::min();
    constexpr int hi = std::numeric_limits<int>::max();
    constexpr auto extremeLeft = fitBubblegumSends(393, 852, lo, lo, true);
    constexpr auto extremeRight = fitBubblegumSends(393, 852, hi, hi, true);
    static_assert(extremeLeft.x == 8 && extremeLeft.y == 8
                  && inside(extremeLeft, 393, 852));
    static_assert(extremeRight.x == 25 && extremeRight.y == 424
                  && inside(extremeRight, 393, 852));
    constexpr auto landscapeExtreme = fitBubblegumSends(852, 300, hi, lo, true);
    static_assert(landscapeExtreme.x == 484 && landscapeExtreme.y == 8
                  && inside(landscapeExtreme, 852, 300));
    constexpr auto desktopExtreme = fitBubblegumSends(1800, 900, lo, hi, false);
    static_assert(desktopExtreme.x == 8 && desktopExtreme.y == 512
                  && inside(desktopExtreme, 1800, 900));
    constexpr auto zero = fitBubblegumSends(0, 0, 0, 0, true);
    static_assert(zero.width == 0 && zero.height == 0);
    for (int w = 1; w <= 900; w += 9)
        for (int h = 1; h <= 900; h += 11)
            for (bool isPhone : {false, true})
            {
                const auto fitted = fitBubblegumSends(w, h, w + 50, -100, isPhone);
                assert(inside(fitted, w, h));
            }

    // iPhone previously used desktop's 220-unit Track List at 393 units,
    // leaving too little horizontal Arrangement space. Android already had
    // the compact policy; both phones now share the same bounded calculation.
    using DAW::mobile::phoneTrackListWidth;
    static_assert(phoneTrackListWidth(220, 393) == 196);
    static_assert(phoneTrackListWidth(220, 360) == 180);
    static_assert(phoneTrackListWidth(220, 852) == 220);
    static_assert(phoneTrackListWidth(80, 393) == 196);
    static_assert(phoneTrackListWidth(220, 100) == 50);
    static_assert(phoneTrackListWidth(220, 1) == 0);
    static_assert(phoneTrackListWidth(220, 0) == 0);
    static_assert(phoneTrackListWidth(-1, 393) == 0);
    static_assert(phoneTrackListWidth(220, std::numeric_limits<int>::max()) == 220);
    for (int workspace = 0; workspace <= 900; ++workspace)
    {
        const int width = phoneTrackListWidth(220, workspace);
        assert(width >= 0 && width <= 220);
        assert(width < workspace || workspace <= 1);
    }

    // Phone track controls must NEVER overlap or reach beyond the row.
    // At the target 360dp screen: 180dp side panel, 3 x 50dp touch columns,
    // 44dp tall controls in an 180dp track lane.
    using DAW::mobile::phoneTrackControlLayout;
    using DAW::mobile::TrackControlRect;
    constexpr auto phoneRow = phoneTrackControlLayout(12, 162, 180, false, true, true);
    static_assert(phoneRow.mute.width == 50 && phoneRow.mute.height == 44);
    static_assert(phoneRow.solo.width == 50 && phoneRow.monitor.width == 50);
    static_assert(phoneRow.input.width == 50 && phoneRow.target.height == 44);
    static_assert(phoneRow.target.y + phoneRow.target.height <= 180);
    const auto touching = [](TrackControlRect a, TrackControlRect b)
    {
        if (a.empty() || b.empty()) return false;
        return a.x < b.x + b.width && b.x < a.x + a.width
            && a.y < b.y + b.height && b.y < a.y + a.height;
    };
    for (int rowWidth : {18, 108, 122, 180, 196, 220, 260})
        for (int rowHeight : {24, 72, 108, 180, 216})
            for (bool targetEnabled : {false, true})
            {
                const int contentWidth = rowWidth > 18 ? rowWidth - 18 : 0;
                const auto g = phoneTrackControlLayout(
                    12, contentWidth, rowHeight, false, true, targetEnabled);
                const TrackControlRect boxes[] = {
                    g.mute, g.solo, g.monitor, g.arm, g.automation, g.input, g.target
                };
                for (unsigned i = 0; i < 7; ++i)
                {
                    const auto a = boxes[i];
                    if (a.empty()) continue;
                    assert(a.x >= 0 && a.y >= 0 && a.x + a.width <= rowWidth);
                    assert(a.y + a.height <= rowHeight);
                    for (unsigned j = i + 1; j < 7; ++j)
                        assert(!touching(a, boxes[j]));
                }
            }
    constexpr auto masterGrid = phoneTrackControlLayout(12, 162, 180, true, true, true);
    static_assert(masterGrid.mute.width == 50 && masterGrid.solo.width == 50);
    static_assert(masterGrid.monitor.empty() && masterGrid.arm.empty());
    static_assert(masterGrid.automation.empty() && masterGrid.input.empty());
    static_assert(masterGrid.target.empty());

    // Floating phone Mixer must not cover the persistent bottom Transport,
    // including landscape and when an activity becomes very short.
    using DAW::mobile::fitFloatingMixer;
    constexpr auto mixerPortrait = fitFloatingMixer(393, 852, 52, 144);
    static_assert(inside(mixerPortrait.safe, 393, 852));
    static_assert(inside(mixerPortrait.window, 393, 852));
    static_assert(mixerPortrait.window.y >= mixerPortrait.safe.y);
    static_assert(mixerPortrait.window.y + mixerPortrait.window.height <= 852 - 144);
    static_assert(mixerPortrait.minimumWidth <= mixerPortrait.window.width);
    constexpr auto mixerLandscape = fitFloatingMixer(852, 300, 52, 96);
    static_assert(inside(mixerLandscape.safe, 852, 300));
    static_assert(mixerLandscape.window.y + mixerLandscape.window.height <= 300 - 96);
    static_assert(mixerLandscape.window.height > 0);
    constexpr auto mixerTiny = fitFloatingMixer(360, 96, 50, 46);
    static_assert(mixerTiny.window.width == 0 && mixerTiny.window.height == 0);
    constexpr auto mixerExtreme = fitFloatingMixer(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 52, 144);
    static_assert(inside(mixerExtreme.safe,
                         std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
    static_assert(inside(mixerExtreme.window,
                         std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
    // Drag-release recovery: a mixer entirely inside the safe area is kept;
    // any edge crossing the top header or bottom Transport must be rejected.
    using DAW::mobile::floatingMixerFits;
    using DAW::mobile::PanelBounds;
    static_assert(floatingMixerFits(mixerPortrait, mixerPortrait.window));
    constexpr PanelBounds manuallyPlaced{
        mixerPortrait.safe.x, mixerPortrait.safe.y,
        mixerPortrait.minimumWidth, mixerPortrait.minimumHeight };
    static_assert(floatingMixerFits(mixerPortrait, manuallyPlaced));
    static_assert(!floatingMixerFits(mixerPortrait,
        { manuallyPlaced.x, 0, manuallyPlaced.width, manuallyPlaced.height }));
    static_assert(!floatingMixerFits(mixerPortrait,
        { manuallyPlaced.x, 852 - 144, manuallyPlaced.width, manuallyPlaced.height }));
    static_assert(!floatingMixerFits(mixerPortrait,
        { -500, 80, manuallyPlaced.width, manuallyPlaced.height }));
    static_assert(!floatingMixerFits(mixerPortrait,
        { 390, 80, manuallyPlaced.width, manuallyPlaced.height }));
    static_assert(!floatingMixerFits(mixerPortrait,
        { manuallyPlaced.x, manuallyPlaced.y, 0, manuallyPlaced.height }));
    static_assert(!floatingMixerFits(mixerPortrait,
        { manuallyPlaced.x, manuallyPlaced.y, manuallyPlaced.width - 1, manuallyPlaced.height }));
    static_assert(!floatingMixerFits(mixerPortrait,
        { std::numeric_limits<int>::max(), std::numeric_limits<int>::max(),
          std::numeric_limits<int>::max(), std::numeric_limits<int>::max() }));
    static_assert(!floatingMixerFits(mixerTiny, { 0, 0, 200, 180 }));
    static_assert(floatingMixerFits(mixerLandscape, mixerLandscape.window));
    for (int w : {1, 2, 20, 245, 360, 393, 852})
        for (int h : {1, 10, 96, 144, 220, 300, 852})
            for (int top : {0, 48, 1000})
                for (int bottom : {0, 96, 144, 1000})
                {
                    const auto fit = fitFloatingMixer(w, h, top, bottom);
                    if (fit.safe.width <= 0 || fit.safe.height <= 0)
                    {
                        assert(fit.window.width == 0 && fit.window.height == 0);
                        continue;
                    }
                    assert(inside(fit.safe, w, h));
                    assert(inside(fit.window, w, h));
                    assert(fit.window.x >= fit.safe.x && fit.window.y >= fit.safe.y);
                    assert(fit.window.x + fit.window.width <= fit.safe.x + fit.safe.width);
                    assert(fit.window.y + fit.window.height <= fit.safe.y + fit.safe.height);
                    assert(fit.minimumWidth <= fit.window.width);
                    assert(fit.minimumHeight <= fit.window.height);
                }
}
