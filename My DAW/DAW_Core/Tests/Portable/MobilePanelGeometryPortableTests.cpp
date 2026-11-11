#include "../../Source/UICore/MobilePanelGeometry.h"
#include <cassert>
#include <initializer_list>
#include <limits>
using DAW::mobile::fitBubblegumSends;
using DAW::mobile::phoneCloseTouchBounds;
constexpr bool inside(DAW::mobile::PanelBounds p, int w, int h)
{
    return p.x >= 0 && p.y >= 0 && p.width >= 0 && p.height >= 0
        && static_cast<long long>(p.x) + p.width <= w
        && static_cast<long long>(p.y) + p.height <= h;
}
int main()
{
    // Platform policy gate: phone Arrangement hides desktop scroll/zoom chrome
    // while retaining both scroll axes; iPad/PC/Mac keep desktop chrome.
    // The wheel event must be consumed only once on phones.
    constexpr auto phoneChrome = DAW::mobile::timelineViewportChrome(true);
    constexpr auto desktopChrome = DAW::mobile::timelineViewportChrome(false);
    static_assert(!phoneChrome.showVerticalBar && !phoneChrome.showHorizontalBar
                  && phoneChrome.allowVerticalScrollWithoutBar
                  && phoneChrome.allowHorizontalScrollWithoutBar
                  && !phoneChrome.showCornerZoom);
    static_assert(desktopChrome.showVerticalBar && desktopChrome.showHorizontalBar
                  && !desktopChrome.allowVerticalScrollWithoutBar
                  && !desktopChrome.allowHorizontalScrollWithoutBar
                  && desktopChrome.showCornerZoom);
    static_assert(!DAW::mobile::timelineWheelDelegatesToViewport(true));
    static_assert(DAW::mobile::timelineWheelDelegatesToViewport(false));
    static_assert(!DAW::mobile::desktopRestoreBubbleVisible(true));
    static_assert(DAW::mobile::desktopRestoreBubbleVisible(false));

    // The test oracle itself must reject overflowed restored window edges.
    // A 32-bit addition here could wrap and falsely accept an offscreen panel.
    static_assert(!inside({ std::numeric_limits<int>::max(), 0, 20, 20 },
                          std::numeric_limits<int>::max(), 100));
    static_assert(!inside({ 0, std::numeric_limits<int>::max(), 20, 20 },
                          100, std::numeric_limits<int>::max()));
    static_assert(!inside({ 0, 0, -1, 20 }, 100, 100));
    static_assert(inside({ 0, 0, 100, 100 }, 100, 100));
    // Android source IDs are opaque signed integers, not small touch-slot
    // indices. Extreme IDs and a duplicate release must never arm another
    // floating-panel launcher while the original finger is still down.
    for (int first : { std::numeric_limits<int>::min(),
                       std::numeric_limits<int>::max() })
    {
        DAW::mobile::PhoneTransportTapState taps;
        const int second = first == std::numeric_limits<int>::min()
            ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();
        taps.begin(1, first);
        taps.begin(2, second);
        assert(!taps.release(second, 2, 0, 0));
        assert(!taps.release(second, 2, 0, 0));
        assert(!taps.release(first, 1, 0, 0));
        taps.begin(3, second);
        assert(taps.release(second, 3, 0, 0));
    }
    // More simultaneous touch sources than the fixed + overflow arrays can
    // represent must fail closed. A late mouseUp must not launch a panel,
    // even after the tracked contacts have lifted; explicit reset recovers.
    {
        DAW::mobile::PhoneTransportTapState taps;
        constexpr int contacts =
            DAW::mobile::PhoneTransportTapState::maxTrackedSources
            + DAW::mobile::PhoneTransportTapState::maxOverflowSources + 1;
        for (int source = 0; source < contacts; ++source)
            taps.begin(1, source);
        for (int source = contacts - 1; source >= 0; --source)
            assert(!taps.release(source, 1, 0, 0));
        taps.begin(2, 1000);
        assert(!taps.release(1000, 2, 0, 0));
        taps.invalidate();
        taps.begin(2, 1000);
        assert(taps.release(1000, 2, 0, 0));
    }
    // Boundary regression for compact phone Mixer toolbar: a one-unit
    // resize must not expose an unreachable action or remove Overflow.
    // These exact thresholds matter when portrait split-screen changes width.
    for (int width : { 0, 45, 46, 93, 94, 141, 142, 143 })
    {
        const auto layout = DAW::mobile::phoneMixerToolbarLayout(width, 32);
        const int expectedCount = width < 46 ? 0 : width < 94 ? 1
                                : width < 142 ? 2 : 3;
        if (layout.count != expectedCount)
            return 30;
        if (layout.count > 0)
        {
            const auto lastAction = layout.buttons[layout.count - 1].action;
            if (lastAction != (width < 142 ? DAW::mobile::phoneMixerOverflow
                                           : DAW::mobile::phoneMixerReset))
                return 31;
        }
    }
    // Portrait Mixer toolbar must never expose two overlapping hit regions,
    // including its overflow replacement on narrow phones.
    for (int width = 0; width <= 220; ++width)
    {
        const auto layout = DAW::mobile::phoneMixerToolbarLayout(width, 32);
        if (layout.count > 0)
        {
            const auto last = layout.buttons[layout.count - 1].bounds;
            if (last.x < 0 || last.x + last.width > width)
                return 1;
        }
        for (int i = 0; i < layout.count; ++i)
        {
            const auto a = layout.buttons[i].bounds;
            if (a.width != 44 || a.height != 32 || a.x < 0)
                return 2;
            for (int j = i + 1; j < layout.count; ++j)
            {
                const auto b = layout.buttons[j].bounds;
                if (a.x + a.width > b.x)
                    return 3;
            }
        }
        const auto shortLayout = DAW::mobile::phoneMixerToolbarLayout(width, 31);
        if (shortLayout.count != 0)
            return 4;
    }
    // Restored/corrupt viewport dimensions can reach the geometry helper
    // before JUCE has completed a rotation. Keep action counts and bounds
    // valid even at signed integer extremes (also in Release builds).
    constexpr auto extremeToolbar = DAW::mobile::phoneMixerToolbarLayout(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
    static_assert(extremeToolbar.count == 3);
    static_assert(extremeToolbar.buttons[2].action == DAW::mobile::phoneMixerReset);
    static_assert(static_cast<long long>(extremeToolbar.buttons[2].bounds.y)
                  + extremeToolbar.buttons[2].bounds.height
                  <= std::numeric_limits<int>::max());
    static_assert(DAW::mobile::phoneMixerToolbarLayout(-1, 32).count == 0);
    static_assert(DAW::mobile::phoneMixerToolbarLayout(142, -1).count == 0);

    // Rotation and title-bar resizing can increase toolbar height without
    // changing its width. Every hit rectangle must remain vertically bounded,
    // and the action set must not depend on spare vertical space.
    for (int width = 0; width <= 220; ++width)
        for (int height : { 0, 1, 31, 32, 33, 44, 64, 96 })
        {
            const auto layout = DAW::mobile::phoneMixerToolbarLayout(width, height);
            const auto baseline = DAW::mobile::phoneMixerToolbarLayout(width, 32);
            if (layout.count != (height < 32 ? 0 : baseline.count))
                return 5;
            for (int i = 0; i < layout.count; ++i)
            {
                const auto b = layout.buttons[i].bounds;
                if (b.y < 0 || b.height != 32 || b.y + b.height > height
                    || b.x < 0 || b.x + b.width > width
                    || layout.buttons[i].action != baseline.buttons[i].action)
                    return 6;
            }
        }
    // Adversarial hit-target geometry across rotation-sized title bars: a
    // taller title must centre every target, preserve 1-unit edge insets,
    // and leave at least the documented 4-unit gap between touch targets.
    // Check action identity too: overflow must be last, never duplicated.
    for (int width = 0; width <= 220; ++width)
        for (int height : { 32, 33, 44, 63, 96 })
        {
            const auto layout = DAW::mobile::phoneMixerToolbarLayout(width, height);
            for (int i = 0; i < layout.count; ++i)
            {
                const auto button = layout.buttons[i];
                if (button.bounds.x < 1
                    || button.bounds.y != (height - 32) / 2
                    || button.bounds.x + button.bounds.width > width - 1)
                    return 7;
                if (i > 0
                    && button.bounds.x - (layout.buttons[i - 1].bounds.x
                                          + layout.buttons[i - 1].bounds.width) < 4)
                    return 8;
                const auto expected = layout.count == 3
                    ? (i == 0 ? DAW::mobile::phoneMixerBrowser
                       : i == 1 ? DAW::mobile::phoneMixerFXChain
                                : DAW::mobile::phoneMixerReset)
                    : (i == layout.count - 1 ? DAW::mobile::phoneMixerOverflow
                       : DAW::mobile::phoneMixerBrowser);
                if (button.action != expected)
                    return 9;
            }
        }
    // Landscape / unfolded phone widths: resizing must not introduce a fourth
    // action, lose Reset, or place a target beyond the available toolbar.
    // Earlier sweeps ended at 220, below common landscape toolbar widths.
    for (int width = 221; width <= 1200; width += 7)
        for (int height : { 32, 44, 64 })
        {
            const auto layout = DAW::mobile::phoneMixerToolbarLayout(width, height);
            if (layout.count != 3)
                return 10;
            for (int i = 0; i < layout.count; ++i)
            {
                const auto b = layout.buttons[i].bounds;
                if (b.x < 1 || b.y < 0 || b.width != 44 || b.height != 32
                    || b.x + b.width > width - 1 || b.y + b.height > height)
                    return 11;
                const auto expected = i == 0 ? DAW::mobile::phoneMixerBrowser
                    : i == 1 ? DAW::mobile::phoneMixerFXChain
                             : DAW::mobile::phoneMixerReset;
                if (layout.buttons[i].action != expected)
                    return 12;
                // A wide toolbar must keep the same 4-unit touch separation
                // and centre its targets after a portrait/landscape resize.
                if (b.y != (height - 32) / 2
                    || (i > 0 && b.x - (layout.buttons[i - 1].bounds.x
                                        + layout.buttons[i - 1].bounds.width) < 4))
                    return 13;
            }
        }
    // The stepped sweep above skips its upper endpoint: test it explicitly.
    static_assert(DAW::mobile::phoneMixerToolbarLayout(1200, 32).count == 3);
    // Exact narrow-width transitions: never silently drop FX/Reset without
    // offering Overflow, and never expose an action outside the title bar.
    // These compile-time checks remain active in Release/NDEBUG builds.
    constexpr auto tooNarrow = DAW::mobile::phoneMixerToolbarLayout(45, 32);
    constexpr auto firstFit = DAW::mobile::phoneMixerToolbarLayout(46, 32);
    constexpr auto twoFit = DAW::mobile::phoneMixerToolbarLayout(94, 32);
    constexpr auto justShort = DAW::mobile::phoneMixerToolbarLayout(141, 32);
    constexpr auto allFit = DAW::mobile::phoneMixerToolbarLayout(142, 32);
    static_assert(tooNarrow.count == 0);
    static_assert(firstFit.count == 1
                  && firstFit.buttons[0].action == DAW::mobile::phoneMixerOverflow);
    static_assert(twoFit.count == 2
                  && twoFit.buttons[0].action == DAW::mobile::phoneMixerBrowser
                  && twoFit.buttons[1].action == DAW::mobile::phoneMixerOverflow);
    // At 141 units, three 44-wide targets plus two 4-unit gaps and
    // two 1-unit insets require 142 units: keep Browser + Overflow.
    static_assert(justShort.count == 2
                  && justShort.buttons[0].action == DAW::mobile::phoneMixerBrowser
                  && justShort.buttons[1].action == DAW::mobile::phoneMixerOverflow);
    static_assert(allFit.count == 3
                  && allFit.buttons[0].action == DAW::mobile::phoneMixerBrowser
                  && allFit.buttons[1].action == DAW::mobile::phoneMixerFXChain
                  && allFit.buttons[2].action == DAW::mobile::phoneMixerReset);
    static_assert(allFit.buttons[2].bounds.x + allFit.buttons[2].bounds.width <= 142);
    static_assert(DAW::mobile::phoneMixerToolbarLayout(142, 31).count == 0);
    // Verify both sides of each action-set threshold after rotation/resizing.
    // These checks also execute in Release, unlike runtime assert().
    static_assert(DAW::mobile::phoneMixerToolbarLayout(0, 32).count == 0);
    static_assert(DAW::mobile::phoneMixerToolbarLayout(93, 32).count == 1);
    static_assert(DAW::mobile::phoneMixerToolbarLayout(95, 32).count == 2);
    static_assert(DAW::mobile::phoneMixerToolbarLayout(140, 32).count == 2);
    static_assert(DAW::mobile::phoneMixerToolbarLayout(143, 32).count == 3);
    static_assert(DAW::mobile::phoneMixerToolbarLayout(220, 32).count == 3);


    // Rotation/restore regression: a 32-high Mixer toolbar must not expose
    // partially clipped controls, even at extreme width or height.
    // Verify the exact action transition at every narrow threshold.
    for (int w = 0; w <= 160; ++w)
    {
        const auto layout = DAW::mobile::phoneMixerToolbarLayout(w, 32);
        const int expected = w < 46 ? 0 : w < 94 ? 1 : w < 142 ? 2 : 3;
        if (layout.count != expected)
            return 13;
        for (int i = 0; i < layout.count; ++i)
        {
            const auto& b = layout.buttons[i].bounds;
            if (b.x < 0 || b.y != 0 || b.width != 44 || b.height != 32
                || static_cast<long long>(b.x) + b.width > w)
                return 14;
        }
    }
    // In short split-screen title bars, zero buttons are valid; a partial
    // button must never be made clickable.
    for (int h = 0; h < 32; ++h)
        if (DAW::mobile::phoneMixerToolbarLayout(142, h).count != 0)
            return 15;

    // Adversarial toolbar hit geometry: across rotation/split-screen widths,
    // no two Mixer actions may claim the same touch point. The overflow
    // action must be the last visible target, never a Send Matrix launcher.
    for (int w = 0; w <= 600; ++w)
    {
        const auto layout = DAW::mobile::phoneMixerToolbarLayout(w, 32);
        if (layout.count < 0 || layout.count > 3)
            return 16;
        for (int i = 0; i < layout.count; ++i)
        {
            const auto& button = layout.buttons[i];
            if (!inside(button.bounds, w, 32))
                return 17;
            if (button.action == DAW::mobile::phoneMixerOverflow
                && i != layout.count - 1)
                return 18;
            for (int j = i + 1; j < layout.count; ++j)
            {
                const auto& other = layout.buttons[j].bounds;
                const long long right = static_cast<long long>(button.bounds.x)
                                      + button.bounds.width;
                const long long otherRight = static_cast<long long>(other.x)
                                           + other.width;
                if (button.bounds.x < otherRight && other.x < right
                    && button.bounds.y < other.y + other.height
                    && other.y < button.bounds.y + button.bounds.height)
                    return 19;
            }
        }
    }

    // Portrait and landscape phones hide desktop scrollbars/corner zoom
    // without disabling setViewPosition() navigation; iPad/PC/Mac retain it.
    // Reuse the phoneChrome/desktopChrome constants declared above.
    static_assert(!phoneChrome.showVerticalBar && !phoneChrome.showHorizontalBar);
    static_assert(phoneChrome.allowVerticalScrollWithoutBar
                  && phoneChrome.allowHorizontalScrollWithoutBar);
    static_assert(!phoneChrome.showCornerZoom);
    static_assert(desktopChrome.showVerticalBar && desktopChrome.showHorizontalBar);
    static_assert(!desktopChrome.allowVerticalScrollWithoutBar
                  && !desktopChrome.allowHorizontalScrollWithoutBar);
    static_assert(desktopChrome.showCornerZoom);
    assert(!DAW::mobile::timelineViewportChrome(true).showCornerZoom);
    assert(DAW::mobile::timelineViewportChrome(false).showCornerZoom);

    // Phone Arrangement must consume each wheel gesture once, even with
    // hidden scrollbars; desktop/iPad keep their original JUCE wheel path.
    using DAW::mobile::timelineWheelDelegatesToViewport;
    using DAW::mobile::timelineWheelClampedPosition;
    using DAW::mobile::timelinePhoneWheelIntent;
    // Horizontal trackpad gestures must not be lost without Shift; a
    // Shift+vertical wheel must also pan horizontally on external keyboards.
    static_assert(timelinePhoneWheelIntent(false, 0.75f, 0.0f).horizontal);
    static_assert(timelinePhoneWheelIntent(false, 0.75f, 0.0f).delta == 0.75f);
    static_assert(!timelinePhoneWheelIntent(false, 0.0f, -0.5f).horizontal);
    static_assert(timelinePhoneWheelIntent(false, 0.0f, -0.5f).delta == -0.5f);
    static_assert(timelinePhoneWheelIntent(true, 0.0f, -0.5f).horizontal);
    static_assert(timelinePhoneWheelIntent(true, 0.0f, -0.5f).delta == -0.5f);
    static_assert(timelinePhoneWheelIntent(false, 0.9f, 0.2f).horizontal);
    static_assert(!timelinePhoneWheelIntent(false, 0.2f, 0.9f).horizontal);
    static_assert(timelinePhoneWheelIntent(false, 0.2f, 0.9f).delta == 0.9f);
    static_assert(timelinePhoneWheelIntent(true, 0.2f, 0.9f).delta == 0.2f);
    static_assert(!timelinePhoneWheelIntent(false, 0.0f, 0.0f).horizontal);
    // Regression: extreme/invalid touchpad wheel values cannot create
    // runaway viewport-relative jumps or hide a valid other-axis gesture.
    static_assert(DAW::mobile::timelinePhoneBoundWheelDelta(2.5f) == 1.0f);
    static_assert(DAW::mobile::timelinePhoneBoundWheelDelta(-2.5f) == -1.0f);
    static_assert(DAW::mobile::timelinePhoneBoundWheelDelta(0.25f) == 0.25f);
    static_assert(DAW::mobile::timelinePhoneBoundWheelDelta(
        std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    static_assert(timelinePhoneWheelIntent(false, 50.0f, 0.0f).delta == 1.0f);
    static_assert(timelinePhoneWheelIntent(false, 50.0f, 20.0f).horizontal);
    static_assert(timelinePhoneWheelIntent(false, 50.0f, 20.0f).delta == 1.0f);
    static_assert(!timelinePhoneWheelIntent(false, 20.0f, 50.0f).horizontal);
    static_assert(timelinePhoneWheelIntent(false, 20.0f, 50.0f).delta == 1.0f);
    static_assert(!timelinePhoneWheelIntent(false,
        std::numeric_limits<float>::infinity(), 20.0f).horizontal);
    static_assert(timelinePhoneWheelIntent(false,
        std::numeric_limits<float>::infinity(), 20.0f).delta == 1.0f);
    static_assert(timelinePhoneWheelIntent(false, 0.0f, -50.0f).delta == -1.0f);
    static_assert(timelinePhoneWheelIntent(true, 0.0f, -50.0f).delta == -1.0f);
    static_assert(timelinePhoneWheelIntent(false,
        std::numeric_limits<float>::quiet_NaN(), 0.4f).delta == 0.4f);
    // Infinite wheel samples are invalid, not full-speed pan commands.
    static_assert(DAW::mobile::timelinePhoneBoundWheelDelta(
        std::numeric_limits<float>::infinity()) == 0.0f);
    static_assert(DAW::mobile::timelinePhoneBoundWheelDelta(
        -std::numeric_limits<float>::infinity()) == 0.0f);
    static_assert(timelinePhoneWheelIntent(false,
        std::numeric_limits<float>::infinity(), 0.0f).delta == 0.0f);
    static_assert(timelinePhoneWheelIntent(false, 0.0f,
        -std::numeric_limits<float>::infinity()).delta == 0.0f);
    static_assert(timelinePhoneWheelIntent(true,
        std::numeric_limits<float>::infinity(), 0.4f).delta == 0.4f);

    // Zooming changes the viewed component size even when the viewport itself
    // is unchanged. Do not carry subpixel wheel movement across that zoom.
    DAW::mobile::TimelinePhoneWheelCarryState zoomCarry;
    zoomCarry.observeContentSize(2000, 1200);
    zoomCarry.observeContentScale(100.0, 60.0);
    zoomCarry.x = -0.8;
    zoomCarry.y = 0.6;
    zoomCarry.observeContentSize(2000, 1200); // no zoom: retain fine motion
    assert(zoomCarry.x == -0.8 && zoomCarry.y == 0.6);
    zoomCarry.observeContentScale(100.0, 60.0); // unchanged zoom retains carry
    assert(zoomCarry.x == -0.8 && zoomCarry.y == 0.6);
    // At minimum content width or integer rounding, zoom changes but size
    // stays identical: the old fractional wheel remainder must be discarded.
    zoomCarry.observeContentScale(125.0, 60.0);
    assert(zoomCarry.x == 0.0 && zoomCarry.y == 0.0);
    zoomCarry.x = 0.7;
    zoomCarry.y = -0.3;
    zoomCarry.observeContentScale(125.0, 70.0); // physical lane zoom
    assert(zoomCarry.x == 0.0 && zoomCarry.y == 0.0);
    zoomCarry.x = -0.8;
    zoomCarry.y = 0.6;
    zoomCarry.observeContentSize(4000, 1200); // horizontal zoom
    assert(zoomCarry.x == 0.0 && zoomCarry.y == 0.0);
    zoomCarry.y = -0.4;
    zoomCarry.observeContentSize(4000, 1800); // lane height/vertical zoom
    assert(zoomCarry.x == 0.0 && zoomCarry.y == 0.0);
    zoomCarry.x = 0.5;
    zoomCarry.reset(); // rotation or presentation change
    assert(zoomCarry.x == 0.0 && zoomCarry.y == 0.0);
    zoomCarry.observeContentSize(4000, 1800);
    assert(zoomCarry.x == 0.0 && zoomCarry.y == 0.0);

    // A stream of high-resolution wheel ticks must move eventually instead
    // of each fractional pixel being rounded to zero and discarded.
    constexpr auto fine1 = DAW::mobile::timelinePhoneWheelPixelMotion(400, 0.001f, 0.0);
    constexpr auto fine2 = DAW::mobile::timelinePhoneWheelPixelMotion(400, 0.001f, fine1.remainder);
    constexpr auto fine3 = DAW::mobile::timelinePhoneWheelPixelMotion(400, 0.001f, fine2.remainder);
    constexpr auto fine4 = DAW::mobile::timelinePhoneWheelPixelMotion(400, 0.001f, fine3.remainder);
    static_assert(fine1.pixels == 0 && fine2.pixels == 0 && fine3.pixels == 0);
    static_assert(fine4.pixels == -1);
    static_assert(fine4.remainder < 0.0 && fine4.remainder > -1.0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(400, -0.01f, 0.0).pixels == 2);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(0, 1.0f, 0.9).pixels == 0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(0, 1.0f, 0.9).remainder == 0.0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(400, 0.0f, 0.5).remainder == 0.5);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, std::numeric_limits<float>::infinity(), 0.0).pixels == 0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, -std::numeric_limits<float>::infinity(), 0.0).pixels == 0);
    // Corrupt wheel carry must not overflow integer conversion or move the
    // viewport after a rotation/restore. Valid fractional carry is preserved.
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.0f, std::numeric_limits<double>::quiet_NaN()).pixels == 0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.0f, std::numeric_limits<double>::quiet_NaN()).remainder == 0.0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.0f, std::numeric_limits<double>::infinity()).remainder == 0.0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.0f, -std::numeric_limits<double>::infinity()).remainder == 0.0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.0f, std::numeric_limits<double>::max()).pixels == 0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.0f, 1.0).remainder == 0.0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.0f, -1.0).remainder == 0.0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.01f, std::numeric_limits<double>::max()).pixels == -2);
    // A direction reversal must not first consume old subpixel carry.
    // Without the reset this positive fine tick would remain negative.
    constexpr auto reversed = DAW::mobile::timelinePhoneWheelPixelMotion(
        400, -0.001f, -0.9);
    static_assert(reversed.pixels == 0 && reversed.remainder > 0.0);
    constexpr auto reversedAgain = DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.001f, 0.9);
    static_assert(reversedAgain.pixels == 0 && reversedAgain.remainder < 0.0);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        400, 0.0f, -0.5).remainder == -0.5);
    static_assert(DAW::mobile::timelinePhoneWheelPixelMotion(
        std::numeric_limits<int>::max(), 1.0f, 0.0).pixels < 0);

    // Edge-clamped subpixel gestures should be discarded immediately,
    // including content smaller than its viewport and invalid deltas.
    using DAW::mobile::timelinePhoneWheelBlockedAtEdge;
    static_assert(timelinePhoneWheelBlockedAtEdge(0, 2000, 400, 0.001f));
    static_assert(!timelinePhoneWheelBlockedAtEdge(0, 2000, 400, -0.001f));
    static_assert(timelinePhoneWheelBlockedAtEdge(1600, 2000, 400, -0.001f));
    static_assert(!timelinePhoneWheelBlockedAtEdge(1600, 2000, 400, 0.001f));
    static_assert(!timelinePhoneWheelBlockedAtEdge(200, 2000, 400, 0.001f));
    static_assert(timelinePhoneWheelBlockedAtEdge(0, 200, 400, 0.001f));
    static_assert(timelinePhoneWheelBlockedAtEdge(0, 0, 400, 0.001f));
    static_assert(!timelinePhoneWheelBlockedAtEdge(0, 2000, 400, 0.0f));
    static_assert(!timelinePhoneWheelBlockedAtEdge(0, 2000, 400,
        std::numeric_limits<float>::quiet_NaN()));

    static_assert(!timelineWheelDelegatesToViewport(true));
    static_assert(timelineWheelDelegatesToViewport(false));
    static_assert(timelineWheelClampedPosition(200, 2000, 400, 120) == 320);
    static_assert(timelineWheelClampedPosition(200, 2000, 400, -300) == 0);
    static_assert(timelineWheelClampedPosition(1500, 2000, 400, 300) == 1600);
    static_assert(timelineWheelClampedPosition(400, 400, 400, 50) == 0);
    static_assert(timelineWheelClampedPosition(400, 400, 0, 50) == 0);
    static_assert(timelineWheelClampedPosition(400, 0, 200, 50) == 0);
    static_assert(timelineWheelClampedPosition(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(),
        400, std::numeric_limits<int>::max()) == std::numeric_limits<int>::max() - 400);
    static_assert(timelineWheelClampedPosition(
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
        400, std::numeric_limits<int>::min()) == 0);
    for (int content : { 1, 200, 400, 2000 })
        for (int view : { 1, 200, 400, 1000 })
            for (int current : { -1000000, 0, 150, 1000000 })
                for (int step : { -1000000, -1, 0, 1, 1000000 })
                {
                    const int result = timelineWheelClampedPosition(
                        current, content, view, step);
                    assert(result >= 0);
                    assert(result <= (content > view ? content - view : 0));
                }

    // Once playback approaches the end of the Arrangement, extending the
    // phone scroll range by only 2s would rebuild clip bounds every tick.
    // A 30s phone runway amortizes those costly GUI updates; long projects
    // still retain their full clip/loop extent.
    using DAW::mobile::timelinePhonePlaybackExtentSeconds;
    static_assert(timelinePhonePlaybackExtentSeconds(30.0, 28.1) == 58.1);
    // Invalid restored transport positions must not make the viewport
    // infinite/NaN or grow the Arrangement on every transport tick.
    static_assert(timelinePhonePlaybackExtentSeconds(30.0, -1.0) == 30.0);
    static_assert(timelinePhonePlaybackExtentSeconds(30.0,
        std::numeric_limits<double>::quiet_NaN()) == 30.0);
    static_assert(timelinePhonePlaybackExtentSeconds(30.0,
        std::numeric_limits<double>::infinity()) == 30.0);
    static_assert(timelinePhonePlaybackExtentSeconds(30.0,
        std::numeric_limits<double>::max()) == 30.0);

    static_assert(timelinePhonePlaybackExtentSeconds(200.0, 28.1) == 200.0);
    static_assert(timelinePhonePlaybackExtentSeconds(30.0, 0.0) == 30.0);
    static_assert(timelinePhonePlaybackExtentSeconds(30.0, 1.0) == 30.0);
    static_assert(timelinePhonePlaybackExtentSeconds(30.0, 27.9) == 30.0);
    static_assert(timelinePhonePlaybackExtentSeconds(30.0, 28.0) == 30.0);
    static_assert(timelinePhonePlaybackExtentSeconds(200.0, 198.0) == 200.0);
    static_assert(timelinePhonePlaybackExtentSeconds(200.0, 199.0) == 229.0);
    // Repeated unrelated phone layout refreshes during the first 28s must
    // not extend a 30s Arrangement or trigger a resize on each refresh.
    for (int tick = 0; tick <= 560; ++tick) // 0..28s at 20 Hz
        assert(timelinePhonePlaybackExtentSeconds(30.0,
                   static_cast<double>(tick) / 20.0) == 30.0);
    double phoneExtent = 30.0;
    int refreshes = 0;
    for (int tick = 0; tick < 1200; ++tick) // 60s at 20 transport ticks/s
    {
        const double playhead = static_cast<double>(tick) / 20.0;
        if (playhead + 2.0 > phoneExtent)
        {
            phoneExtent = timelinePhonePlaybackExtentSeconds(30.0, playhead);
            ++refreshes;
        }
    }
    assert(refreshes <= 2); // Previously ~600 rebuilds after 28s.
    assert(phoneExtent >= 60.0);

    // Regression: closing Sends or the floating Mixer must not resurrect
    // desktop draggable launchers over the phone Arrangement. The desktop
    // restore affordance remains available for iPad/Windows/macOS.
    using DAW::mobile::desktopRestoreBubbleVisible;
    static_assert(!desktopRestoreBubbleVisible(true));
    static_assert(desktopRestoreBubbleVisible(false));
    assert(!desktopRestoreBubbleVisible(true));
    assert(desktopRestoreBubbleVisible(false));
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

    // The Mixer hides the close traffic light. Expand the two 12x12 orbs
    // to adjacent 30x32 phone targets without moving the APEX title artwork.
    // The boundary at x=30 belongs ONLY to maximize, never both controls.
    // Desktop retains its original painted 12x12 hit regions.
    using DAW::mobile::phoneMixerLeftChromeButtonAt;
    static_assert(phoneMixerLeftChromeButtonAt(0.f, 0.f, 14, 12, 8, 32) == 1);
    static_assert(phoneMixerLeftChromeButtonAt(29.99f, 31.99f, 14, 12, 8, 32) == 1);
    static_assert(phoneMixerLeftChromeButtonAt(30.f, 16.f, 14, 12, 8, 32) == 2);
    static_assert(phoneMixerLeftChromeButtonAt(59.99f, 31.f, 14, 12, 8, 32) == 2);
    static_assert(phoneMixerLeftChromeButtonAt(-0.01f, 16.f, 14, 12, 8, 32) == 0);
    static_assert(phoneMixerLeftChromeButtonAt(60.f, 16.f, 14, 12, 8, 32) == 0);
    static_assert(phoneMixerLeftChromeButtonAt(20.f, -1.f, 14, 12, 8, 32) == 0);
    static_assert(phoneMixerLeftChromeButtonAt(40.f, 32.f, 14, 12, 8, 32) == 0);
    static_assert(phoneMixerLeftChromeButtonAt(20.f, 16.f, 14, 12, 8, 0) == 0);
    for (int x = -1; x <= 61; ++x)
    {
        const int hit = phoneMixerLeftChromeButtonAt(
            static_cast<float>(x), 16.f, 14, 12, 8, 32);
        assert(hit == (x < 0 || x >= 60 ? 0 : x < 30 ? 1 : 2));
    }
    DAW::mobile::PhoneChromeTapState mixerChromeTap;
    mixerChromeTap.begin(101); // minimize
    assert(!mixerChromeTap.release(102, 0, 0)); // release on maximize
    mixerChromeTap.begin(102);
    mixerChromeTap.move(12, 0);
    mixerChromeTap.move(0, 0); // a swipe returning to the orb stays cancelled
    assert(!mixerChromeTap.release(102, 0, 0));
    mixerChromeTap.begin(102);
    assert(mixerChromeTap.release(102, 0, 0)); // stationary tap

    // The phone's Mixer/Sends/FX/Browser launchers must not cross-activate
    // when two fingers overlap, even if one returns to its starting point.
    DAW::mobile::PhoneTransportTapState phoneLauncherTap;
    phoneLauncherTap.begin(1, 7);
    assert(phoneLauncherTap.release(7, 1, 0, 0)); // stationary one-finger tap
    phoneLauncherTap.begin(1, 7);
    phoneLauncherTap.move(7, 20, 0);
    phoneLauncherTap.move(7, 0, 0);
    assert(!phoneLauncherTap.release(7, 1, 0, 0)); // swipe and return
    phoneLauncherTap.begin(2, 7);
    phoneLauncherTap.begin(3, 8); // second finger cancels both
    assert(!phoneLauncherTap.release(8, 3, 0, 0));
    assert(!phoneLauncherTap.release(7, 2, 0, 0));
    phoneLauncherTap.begin(4, 7);
    assert(!phoneLauncherTap.release(8, 4, 0, 0)); // wrong pointer
    assert(!phoneLauncherTap.release(7, 4, 0, 0)); // cannot re-arm after foreign up
    phoneLauncherTap.begin(4, 7);
    phoneLauncherTap.move(8, 0, 0); // foreign drag cancels
    assert(!phoneLauncherTap.release(7, 4, 0, 0));
    phoneLauncherTap.begin(4, 7);
    assert(!phoneLauncherTap.release(7, 3, 0, 0)); // wrong target
    phoneLauncherTap.begin(4, 7);
    phoneLauncherTap.invalidate(); // rotation or resize
    assert(!phoneLauncherTap.release(7, 4, 0, 0));
    // A non-launcher touch also occupies the Transport: a second finger
    // must not turn a previously ineligible gesture into a panel launch.
    phoneLauncherTap.begin(0, 7);
    phoneLauncherTap.begin(1, 8);
    assert(!phoneLauncherTap.release(8, 1, 0, 0));
    assert(!phoneLauncherTap.release(7, 0, 0, 0));
    phoneLauncherTap.begin(1, 7);
    assert(phoneLauncherTap.release(7, 1, 0, 0));

    // A cancelled two-finger gesture stays blocked until BOTH fingers lift.
    phoneLauncherTap.begin(1, 7);
    phoneLauncherTap.begin(2, 8);
    assert(!phoneLauncherTap.release(7, 1, 0, 0));
    phoneLauncherTap.begin(3, 9); // third finger cannot re-arm
    assert(!phoneLauncherTap.release(9, 3, 0, 0));
    assert(!phoneLauncherTap.release(8, 2, 0, 0));
    phoneLauncherTap.begin(4, 7);
    assert(phoneLauncherTap.release(7, 4, 0, 0));

    // Nine or ten contacts can coexist on Android. If only eight are
    // tracked, the state resets while the ninth is still down and an
    // unrelated new finger can accidentally open a floating panel.
    for (int source = 0; source < 10; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    assert(phoneLauncherTap.activeCount == 10);
    for (int source = 0; source < 8; ++source)
        assert(!phoneLauncherTap.release(source, 1, 0, 0));
    assert(phoneLauncherTap.activeCount == 2);
    phoneLauncherTap.begin(2, 12); // Must not re-arm while 8 and 9 remain.
    assert(!phoneLauncherTap.release(12, 2, 0, 0));
    assert(!phoneLauncherTap.release(8, 1, 0, 0));
    assert(!phoneLauncherTap.release(9, 1, 0, 0));
    phoneLauncherTap.begin(4, 7);
    assert(phoneLauncherTap.release(7, 4, 0, 0));

    // A 17th touch exceeds the fixed source-ID array. Releasing the first
    // sixteen must NOT re-arm a launcher while the overflow touch remains.
    for (int source = 0; source < 17; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    assert(phoneLauncherTap.activeCount == 16);
    assert(phoneLauncherTap.overflowCount == 1);
    for (int source = 0; source < 16; ++source)
        assert(!phoneLauncherTap.release(source, 1, 0, 0));
    assert(phoneLauncherTap.activeCount == 0);
    assert(phoneLauncherTap.overflowCount == 1);
    phoneLauncherTap.begin(2, 30);
    assert(!phoneLauncherTap.release(30, 2, 0, 0));
    assert(!phoneLauncherTap.release(16, 0, 0, 0));
    assert(phoneLauncherTap.overflowCount == 0);
    phoneLauncherTap.begin(3, 7);
    assert(phoneLauncherTap.release(7, 3, 0, 0));

    // Foreign and duplicate releases must not consume an overflow ID.
    for (int source = 0; source < 18; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    assert(phoneLauncherTap.overflowCount == 2);
    assert(!phoneLauncherTap.release(999, 0, 0, 0));
    assert(phoneLauncherTap.overflowCount == 2);
    assert(!phoneLauncherTap.release(16, 0, 0, 0));
    assert(phoneLauncherTap.overflowCount == 1);
    assert(!phoneLauncherTap.release(16, 0, 0, 0)); // duplicate
    assert(phoneLauncherTap.overflowCount == 1);
    for (int source = 0; source < 16; ++source)
        assert(!phoneLauncherTap.release(source, 0, 0, 0));
    phoneLauncherTap.begin(2, 50);
    assert(!phoneLauncherTap.release(50, 2, 0, 0));
    assert(!phoneLauncherTap.release(17, 0, 0, 0));
    phoneLauncherTap.begin(3, 7);
    assert(phoneLauncherTap.release(7, 3, 0, 0));

    // Beyond 48 contacts there is no room for another source ID. Keep all
    // phone panel launchers disabled until an explicit cancel/rotation reset;
    // unknown releases must not accidentally clear that safety latch.
    for (int source = 0; source < 49; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    assert(phoneLauncherTap.activeCount == 16);
    assert(phoneLauncherTap.overflowCount == 32);
    assert(phoneLauncherTap.untrackedOverflow);
    for (int source = 0; source < 49; ++source)
        assert(!phoneLauncherTap.release(source, 1, 0, 0));
    assert(phoneLauncherTap.untrackedOverflow);
    assert(!phoneLauncherTap.release(999, 1, 0, 0));
    phoneLauncherTap.begin(2, 77);
    assert(!phoneLauncherTap.release(77, 2, 0, 0));
    phoneLauncherTap.invalidate(); // OS cancellation or orientation reset
    assert(!phoneLauncherTap.untrackedOverflow);
    phoneLauncherTap.begin(3, 7);
    assert(phoneLauncherTap.release(7, 3, 0, 0));

    // Interleaved primary/overflow releases must not lose pointer identity.
    // Reusing a freed primary slot while overflow fingers remain down must
    // never re-arm a floating-panel launcher.
    for (int source = 0; source < 18; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    assert(phoneLauncherTap.activeCount == 16);
    assert(phoneLauncherTap.overflowCount == 2);
    assert(!phoneLauncherTap.release(3, 1, 0, 0));
    phoneLauncherTap.begin(2, 100); // reuse the freed primary slot
    assert(phoneLauncherTap.activeCount == 16);
    assert(phoneLauncherTap.overflowCount == 2);
    assert(!phoneLauncherTap.release(999, 2, 0, 0)); // unknown release
    assert(phoneLauncherTap.overflowCount == 2);
    assert(!phoneLauncherTap.release(17, 2, 0, 0));
    assert(phoneLauncherTap.overflowCount == 1);
    assert(!phoneLauncherTap.release(17, 2, 0, 0)); // duplicate release
    assert(phoneLauncherTap.overflowCount == 1);
    for (int source = 0; source < 18; ++source)
        if (source != 3 && source != 17)
            assert(!phoneLauncherTap.release(source, 1, 0, 0));
    assert(!phoneLauncherTap.release(100, 2, 0, 0));
    assert(phoneLauncherTap.activeCount == 0);
    assert(phoneLauncherTap.overflowCount == 0);
    phoneLauncherTap.begin(2, 100);
    assert(phoneLauncherTap.release(100, 2, 0, 0));

    // A cancelled swipe cannot be re-armed by another finger.
    phoneLauncherTap.begin(1, 7);
    phoneLauncherTap.move(7, 20, 0);
    phoneLauncherTap.begin(2, 8);
    assert(!phoneLauncherTap.release(7, 1, 0, 0));
    assert(!phoneLauncherTap.release(8, 2, 0, 0));


    // Header energy must not overpaint either the original left chrome or
    // the phone toolbar. Desktop/iPad keep the original full-width rendering.
    using DAW::mobile::phoneMixerHeaderEnergyRegion;
    constexpr auto energyPortrait = phoneMixerHeaderEnergyRegion(390, 60, 237, 32);
    static_assert(energyPortrait.x == 60 && energyPortrait.y == 0
                  && energyPortrait.width == 177 && energyPortrait.height == 32);
    constexpr auto energyLandscape = phoneMixerHeaderEnergyRegion(852, 60, 622, 32);
    static_assert(energyLandscape.x == 60 && energyLandscape.width == 562);
    constexpr auto energyNarrow = phoneMixerHeaderEnergyRegion(40, 60, 24, 32);
    static_assert(energyNarrow.x == 40 && energyNarrow.width == 0);
    constexpr auto energyInvalid = phoneMixerHeaderEnergyRegion(-10, 60, 237, -1);
    static_assert(energyInvalid.width == 0 && energyInvalid.height == 0);
    for (int w : {0, 1, 40, 60, 200, 390, 852})
        for (int toolbarStart : {-100, 0, 24, 60, 142, 237, 622, 2000})
        {
            const auto energy = phoneMixerHeaderEnergyRegion(w, 60, toolbarStart, 32);
            assert(inside(energy, DAW::mobile::maxInt(0, w), 32));
            assert(energy.x >= DAW::mobile::minInt(60, w));
            assert(energy.x + energy.width <=
                   DAW::mobile::maxInt(energy.x, toolbarStart));
        }

    // The toolbar's hover and active glows extend 2 units beyond the
    // painted button. On phones, clip them to the right title-bar zone so
    // neither Mixer strips below y=32 nor the centred title can be painted.
    // This is deliberately phone-only; desktop/iPad keep existing chrome.
    using DAW::mobile::phoneMixerToolbarPaintClip;
    constexpr auto toolbarPaintPortrait = phoneMixerToolbarPaintClip(390, 32, 237);
    static_assert(toolbarPaintPortrait.x == 237 && toolbarPaintPortrait.y == 0
                  && toolbarPaintPortrait.width == 153
                  && toolbarPaintPortrait.height == 32);
    constexpr auto toolbarPaintLandscape = phoneMixerToolbarPaintClip(852, 32, 622);
    static_assert(toolbarPaintLandscape.x == 622
                  && toolbarPaintLandscape.width == 230);
    constexpr auto toolbarPaintTiny = phoneMixerToolbarPaintClip(20, 32, 237);
    static_assert(toolbarPaintTiny.x == 20 && toolbarPaintTiny.width == 0);
    constexpr auto toolbarPaintInvalid = phoneMixerToolbarPaintClip(-1, -1, -100);
    static_assert(toolbarPaintInvalid.x == 0 && toolbarPaintInvalid.width == 0
                  && toolbarPaintInvalid.height == 0);
    for (int w : {-1, 0, 1, 20, 390, 852})
        for (int h : {-1, 0, 1, 32, 44})
            for (int start : {-100, 0, 60, 237, 622, 2000})
            {
                const auto clip = phoneMixerToolbarPaintClip(w, h, start);
                assert(inside(clip, DAW::mobile::maxInt(0, w),
                                    DAW::mobile::maxInt(0, h)));
                assert(clip.x == DAW::mobile::clampInt(
                    start, 0, DAW::mobile::maxInt(0, w)));
                assert(clip.x + clip.width == DAW::mobile::maxInt(0, w));
            }

    // Mixer title-bar phone targets: the desktop 22x20 toolbar was too small.
    // The 32-high title bar caps phone targets at 44x32; fill that height
    // without spilling the pill into Mixer content. Overflow prevents
    // invisible/overlapping actions on narrow phones.
    using DAW::mobile::phoneMixerToolbarLayout;
    using DAW::mobile::phoneMixerBrowser;
    using DAW::mobile::phoneMixerFXChain;
    using DAW::mobile::phoneMixerReset;
    using DAW::mobile::phoneMixerOverflow;
    constexpr auto mixerToolbarPortrait = phoneMixerToolbarLayout(108, 32);
    static_assert(mixerToolbarPortrait.count == 2
                  && mixerToolbarPortrait.buttons[0].action == phoneMixerBrowser
                  && mixerToolbarPortrait.buttons[1].action == phoneMixerOverflow
                  && mixerToolbarPortrait.buttons[0].bounds.width == 44
                  && mixerToolbarPortrait.buttons[0].bounds.height == 32
                  && mixerToolbarPortrait.buttons[0].bounds.y == 0);
    constexpr auto mixerToolbarLandscape = phoneMixerToolbarLayout(144, 32);
    static_assert(mixerToolbarLandscape.count == 3
                  && mixerToolbarLandscape.buttons[0].action == phoneMixerBrowser
                  && mixerToolbarLandscape.buttons[1].action == phoneMixerFXChain
                  && mixerToolbarLandscape.buttons[2].action == phoneMixerReset);
    // A 390-unit portrait window yields ~143 units of right toolbar space.
    // Preserve Browser, FX Chain and Reset instead of needlessly overflowing.
    constexpr auto mixerToolbarNarrowPortrait = phoneMixerToolbarLayout(143, 32);
    static_assert(mixerToolbarNarrowPortrait.count == 3
                  && mixerToolbarNarrowPortrait.buttons[0].action == phoneMixerBrowser
                  && mixerToolbarNarrowPortrait.buttons[1].action == phoneMixerFXChain
                  && mixerToolbarNarrowPortrait.buttons[2].action == phoneMixerReset);
    static_assert(phoneMixerToolbarLayout(142, 32).count == 3);
    static_assert(phoneMixerToolbarLayout(141, 32).count == 2);
    static_assert(phoneMixerToolbarLayout(46, 32).count == 1
                  && phoneMixerToolbarLayout(46, 32).buttons[0].action == phoneMixerOverflow);
    static_assert(phoneMixerToolbarLayout(45, 32).count == 0);
    static_assert(phoneMixerToolbarLayout(108, 20).count == 0);
    static_assert(phoneMixerToolbarLayout(108, 31).count == 0);
    static_assert(phoneMixerToolbarLayout(108, 32).buttons[0].bounds.height == 32);
    for (int w = 0; w <= 500; ++w)
        for (int h : {0, 20, 28, 31, 32, 48})
        {
            const auto layout = phoneMixerToolbarLayout(w, h);
            assert(layout.count >= 0 && layout.count <= 3);
            for (int i = 0; i < layout.count; ++i)
            {
                const auto b = layout.buttons[i];
                assert(inside(b.bounds, w, h));
                assert(b.bounds.width == 44 && b.bounds.height == 32);
                assert(b.bounds.y >= 0 && b.bounds.y + b.bounds.height <= h);
                if (i > 0)
                {
                    const auto previous = layout.buttons[i - 1].bounds;
                    assert(previous.x + previous.width < b.bounds.x);
                }
            }
            if (layout.count > 0 && layout.count < 3)
                assert(layout.buttons[layout.count - 1].action == phoneMixerOverflow);
        }

    // A phone Sends window must never cover the APEX header or the bottom
    // Transport, even when opened near the bottom or dragged off-screen.
    constexpr auto sendsPortrait = fitBubblegumSends(393, 852, 360, 800, true, 50, 144);
    static_assert(sendsPortrait.y == 280 && sendsPortrait.height == 420
                  && sendsPortrait.y + sendsPortrait.height <= 852 - 144);
    constexpr auto sendsLandscape = fitBubblegumSends(852, 300, 820, 280, true, 50, 96);
    static_assert(sendsLandscape.y == 58 && sendsLandscape.height == 138
                  && sendsLandscape.y + sendsLandscape.height <= 300 - 96);
    constexpr auto sendsCollapsed = fitBubblegumSends(360, 96, 200, 80, true, 50, 46);
    static_assert(sendsCollapsed.width == 0 && sendsCollapsed.height == 0);
    constexpr auto sendsDesktopUnchanged = fitBubblegumSends(1800, 900, 450, 490, false, 50, 144);
    static_assert(sendsDesktopUnchanged.x == 174 && sendsDesktopUnchanged.y == 130
                  && sendsDesktopUnchanged.width == 260 && sendsDesktopUnchanged.height == 380);
    for (int w : {1, 20, 245, 393, 852})
        for (int h : {1, 96, 220, 300, 852})
            for (int top : {0, 50, 1000})
                for (int bottom : {0, 96, 144, 1000})
                {
                    const auto fitted = fitBubblegumSends(w, h,
                        std::numeric_limits<int>::max(),
                        std::numeric_limits<int>::min(), true, top, bottom);
                    const int safeTop = DAW::mobile::clampInt(top, 0, h);
                    const int safeBottom = DAW::mobile::clampInt(bottom, 0, h - safeTop);
                    assert(inside(fitted, w, h));
                    if (fitted.height > 0)
                    {
                        assert(fitted.y >= safeTop);
                        assert(static_cast<long long>(fitted.y) + fitted.height <= h - safeBottom);
                    }
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

    // FX browser used a desktop 340x560 rect at x=24 and a top offset
    // derived from desktop Transport. On narrow phones and short landscape
    // windows it could cover the bottom thumb Transport or exit the viewport.
    using DAW::mobile::fitPhonePluginBrowser;
    constexpr auto browserPortrait = fitPhonePluginBrowser(393, 852, 50, 144);
    static_assert(browserPortrait.x == 8 && browserPortrait.y == 58
                  && browserPortrait.width == 340 && browserPortrait.height == 560);
    static_assert(inside(browserPortrait, 393, 852));
    // Maximized phone FX Browser previously escaped the responsive branch:
    // FloatingPanelChrome maximized to the entire parent, covering Transport.
    // Its maximized state must fill only the safe phone workspace.
    constexpr auto browserMaxPortrait = fitPhonePluginBrowser(393, 852, 50, 144, true);
    static_assert(browserMaxPortrait.x == 8 && browserMaxPortrait.y == 58
                  && browserMaxPortrait.width == 377 && browserMaxPortrait.height == 642);
    static_assert(browserMaxPortrait.y + browserMaxPortrait.height <= 852 - 144);
    constexpr auto browserMaxLandscape = fitPhonePluginBrowser(852, 300, 50, 96, true);
    static_assert(browserMaxLandscape.x == 8 && browserMaxLandscape.y == 58
                  && browserMaxLandscape.width == 836 && browserMaxLandscape.height == 138);
    static_assert(browserMaxLandscape.y + browserMaxLandscape.height <= 300 - 96);
    // Dragging the phone FX Browser should not be undone by layout, but
    // releasing it beyond the viewport must keep its title and controls
    // accessible above the persistent bottom Transport.
    using DAW::mobile::fitPhonePluginBrowserPlacement;
    constexpr auto browserDragInside = fitPhonePluginBrowserPlacement(
        393, 852, 50, 144, { 20, 90, 340, 560 });
    static_assert(browserDragInside.x == 20 && browserDragInside.y == 90
                  && browserDragInside.width == 340 && browserDragInside.height == 560);
    constexpr auto browserDragOutside = fitPhonePluginBrowserPlacement(
        393, 852, 50, 144, { 400, 900, 340, 560 });
    static_assert(browserDragOutside.x == 45 && browserDragOutside.y == 140
                  && browserDragOutside.y + browserDragOutside.height <= 852 - 144);
    constexpr auto browserDragNegative = fitPhonePluginBrowserPlacement(
        393, 852, 50, 144, { -500, -500, 340, 560 });
    static_assert(browserDragNegative.x == 8 && browserDragNegative.y == 58);
    constexpr auto browserAfterRotation = fitPhonePluginBrowserPlacement(
        852, 300, 50, 96, browserDragInside);
    static_assert(browserAfterRotation.x == 20 && browserAfterRotation.y == 58
                  && browserAfterRotation.height == 138);
    constexpr auto browserMaxAfterDrag = fitPhonePluginBrowserPlacement(
        852, 300, 50, 96, browserDragOutside, true);
    static_assert(browserMaxAfterDrag.x == 8 && browserMaxAfterDrag.y == 58
                  && browserMaxAfterDrag.width == 836 && browserMaxAfterDrag.height == 138);
    constexpr auto browserFirstOpen = fitPhonePluginBrowserPlacement(
        393, 852, 50, 144, { 0, 0, 0, 0 });
    static_assert(browserFirstOpen.x == browserPortrait.x
                  && browserFirstOpen.y == browserPortrait.y);
    constexpr auto browserNoWorkspace = fitPhonePluginBrowserPlacement(
        360, 96, 50, 46, { 400, 900, 340, 560 });
    static_assert(browserNoWorkspace.width == 0 && browserNoWorkspace.height == 0);
    constexpr auto browserExtremeDrag = fitPhonePluginBrowserPlacement(
        393, 852, 50, 144,
        { std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), 340, 560 });
    static_assert(browserExtremeDrag.x == 45 && browserExtremeDrag.y == 58);
    for (int w : {1, 2, 20, 245, 393, 852})
        for (int h : {1, 50, 96, 300, 852})
        {
            const auto fitted = fitPhonePluginBrowserPlacement(
                w, h, 50, 96, { 100000, -100000, 340, 560 });
            assert(inside(fitted, w, h));
            if (fitted.height > 0)
                assert(fitted.y + fitted.height <= h - DAW::mobile::clampInt(
                    96, 0, h - DAW::mobile::clampInt(50, 0, h)));
        }
    // Browser rows were 22 high and the search field only 22 high. Phones
    // now use 44-unit row hit targets and a 44-unit search input; desktop
    // geometry is unchanged. The 138-high landscape window still shows
    // at least one full row beneath the 28-unit floating chrome.
    using DAW::mobile::pluginBrowserHeaderHeight;
    using DAW::mobile::pluginBrowserSearchSlotHeight;
    using DAW::mobile::pluginBrowserRowHeight;
    static_assert(pluginBrowserRowHeight(true) == 44);
    static_assert(pluginBrowserSearchSlotHeight(true) - 4 == 44);
    static_assert(pluginBrowserHeaderHeight(true) == 0);
    static_assert(pluginBrowserRowHeight(false) == 22);
    static_assert(pluginBrowserSearchSlotHeight(false) == 26);
    static_assert(pluginBrowserHeaderHeight(false) == 28);
    // A swipe over a plugin row must scroll the phone FX Browser, not
    // instantiate an effect on release. Stationary taps still activate.
    using DAW::mobile::pluginBrowserRowTapActivates;
    static_assert(pluginBrowserRowTapActivates(true, 0, 0, true));
    static_assert(pluginBrowserRowTapActivates(true, 8, 0, true));
    static_assert(pluginBrowserRowTapActivates(true, 6, 5, true));
    static_assert(!pluginBrowserRowTapActivates(true, 8, 8, true));
    static_assert(!pluginBrowserRowTapActivates(true, 0, 9, true));
    static_assert(!pluginBrowserRowTapActivates(true, 30, 0, true));
    static_assert(!pluginBrowserRowTapActivates(true, 0, 0, false));
    static_assert(!pluginBrowserRowTapActivates(true,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), true));
    static_assert(pluginBrowserRowTapActivates(false, 30, 0, true));
    static_assert(pluginBrowserRowTapActivates(false, 0, 0, false));
    for (int dx = -12; dx <= 12; ++dx)
        for (int dy = -12; dy <= 12; ++dy)
        {
            const bool expected = dx * dx + dy * dy <= 64;
            assert(pluginBrowserRowTapActivates(true, dx, dy, true) == expected);
            assert(!pluginBrowserRowTapActivates(true, dx, dy, false));
        }
    // Regression: swipe far away and return to the original row/control.
    // The final displacement is zero, but the gesture must stay cancelled.
    using DAW::mobile::phoneTapGestureRemainsEligible;
    static_assert(phoneTapGestureRemainsEligible(true, 0, 0));
    static_assert(phoneTapGestureRemainsEligible(true, 8, 0));
    static_assert(!phoneTapGestureRemainsEligible(true, 9, 0));
    static_assert(!phoneTapGestureRemainsEligible(false, 0, 0));
    static_assert(!phoneTapGestureRemainsEligible(true,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    bool phoneRowTapEligible = true;
    phoneRowTapEligible = phoneTapGestureRemainsEligible(phoneRowTapEligible, 24, 0);
    phoneRowTapEligible = phoneTapGestureRemainsEligible(phoneRowTapEligible, 0, 0);
    assert(!phoneRowTapEligible);
    assert(!pluginBrowserRowTapActivates(true, 0, 0, phoneRowTapEligible));
    bool phoneButtonTapEligible = true;
    phoneButtonTapEligible = phoneTapGestureRemainsEligible(phoneButtonTapEligible, 0, 30);
    phoneButtonTapEligible = phoneTapGestureRemainsEligible(phoneButtonTapEligible, 0, 0);
    assert(!phoneButtonTapEligible);
    assert(!(phoneButtonTapEligible && DAW::mobile::phoneChromeButtonTapActivates(1, 1, 0, 0)));
    // A short stationary touch remains eligible.
    bool shortPhoneTap = phoneTapGestureRemainsEligible(true, 4, 3);
    shortPhoneTap = phoneTapGestureRemainsEligible(shortPhoneTap, 0, 0);
    assert(shortPhoneTap);
    // Phone title-bar buttons must activate on a stationary release over
    // the same control, not on mouseDown or after swiping across controls.
    using DAW::mobile::phoneChromeButtonTapActivates;
    static_assert(phoneChromeButtonTapActivates(1, 1, 0, 0));
    static_assert(phoneChromeButtonTapActivates(2, 2, 8, 0));
    static_assert(!phoneChromeButtonTapActivates(0, 0, 0, 0));
    static_assert(!phoneChromeButtonTapActivates(1, 2, 0, 0));
    static_assert(!phoneChromeButtonTapActivates(3, 0, 0, 0));
    static_assert(!phoneChromeButtonTapActivates(1, 1, 8, 8));
    static_assert(!phoneChromeButtonTapActivates(1, 1, 9, 0));
    static_assert(!phoneChromeButtonTapActivates(1, 1,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    for (int dx = -12; dx <= 12; ++dx)
        for (int dy = -12; dy <= 12; ++dy)
        {
            const bool tap = dx * dx + dy * dy <= 64;
            assert(phoneChromeButtonTapActivates(1, 1, dx, dy) == tap);
            assert(!phoneChromeButtonTapActivates(1, 2, dx, dy));
        }
    // Regression: a phone rotation during a pressed chrome control must
    // never close/minimize/maximize the newly repositioned floating window.
    using DAW::mobile::PhoneChromeTapState;
    PhoneChromeTapState chromeTap;
    assert(!chromeTap.release(1, 0, 0)); // No press.
    chromeTap.begin(1);
    assert(chromeTap.release(1, 0, 0)); // Stationary close.
    assert(!chromeTap.release(1, 0, 0)); // Single activation.
    chromeTap.begin(2);
    chromeTap.move(3, 4);
    assert(chromeTap.release(2, 3, 4)); // Short movement.
    chromeTap.begin(3);
    chromeTap.move(12, 0);
    chromeTap.move(0, 0);
    assert(!chromeTap.release(3, 0, 0)); // Swipe and return.
    chromeTap.begin(1);
    chromeTap.invalidate(); // Parent resized on rotation.
    assert(!chromeTap.release(1, 0, 0));
    chromeTap.begin(3);
    assert(!chromeTap.release(1, 0, 0)); // Different button.
    chromeTap.begin(2);
    chromeTap.move(std::numeric_limits<int>::min(),
                   std::numeric_limits<int>::max());
    assert(!chromeTap.release(2, 0, 0)); // Extreme drag.
    chromeTap.begin(3);
    assert(chromeTap.release(3, 0, 0)); // Fresh press after rotation.

    // MixerWindow's phone toolbar uses IDs + 1 (Settings is ID 0), so
    // pressing Settings cannot be confused with an absent touch. All
    // toolbar actions must wait for release; a swipe/rotation cancels them.
    PhoneChromeTapState mixerToolbar;
    assert(!mixerToolbar.release(1, 0, 0));
    mixerToolbar.begin(1); // Settings (toolbar ID 0 + 1).
    assert(mixerToolbar.release(1, 0, 0));
    assert(!mixerToolbar.release(1, 0, 0)); // Exactly one action.
    mixerToolbar.begin(8); // Overflow.
    assert(!mixerToolbar.release(7, 0, 0)); // Different action.
    mixerToolbar.begin(6); // Bubblegum.
    mixerToolbar.move(16, 0);
    mixerToolbar.move(0, 0);
    assert(!mixerToolbar.release(6, 0, 0)); // Drag out and back.
    mixerToolbar.begin(2);
    mixerToolbar.invalidate(); // Mixer rotated or resized.
    assert(!mixerToolbar.release(2, 0, 0));
    mixerToolbar.begin(8);
    assert(!mixerToolbar.release(0, 0, 0)); // Lift outside.
    mixerToolbar.begin(8);
    mixerToolbar.move(4, 3);
    assert(mixerToolbar.release(8, 4, 3)); // Legitimate short tap.

    // Left Mixer minimize/maximize use distinct IDs above toolbar IDs.
    // Swipe-out-and-back, releasing over the other button, and rotation
    // must all suppress the action; a stationary release fires once.
    PhoneChromeTapState mixerLeftChrome;
    mixerLeftChrome.begin(101); // Minimize.
    assert(!mixerLeftChrome.release(102, 0, 0)); // Released on maximize.
    mixerLeftChrome.begin(102);
    mixerLeftChrome.move(0, 12);
    mixerLeftChrome.move(0, 0);
    assert(!mixerLeftChrome.release(102, 0, 0));
    mixerLeftChrome.begin(101);
    mixerLeftChrome.invalidate(); // Orientation changed.
    assert(!mixerLeftChrome.release(101, 0, 0));
    mixerLeftChrome.begin(102);
    assert(mixerLeftChrome.release(102, 0, 0));
    assert(!mixerLeftChrome.release(102, 0, 0));

    // Bubblegum Sends uses this same state for its phone close orb. A
    // scroll/drag that returns to the orb must not close the Sends panel;
    // rotation or safe-inset changes invalidate an outstanding close press.
    PhoneChromeTapState sendsClose;
    sendsClose.begin(1);
    sendsClose.move(0, 9);
    sendsClose.move(0, 0);
    assert(!sendsClose.release(1, 0, 0));
    sendsClose.begin(1);
    assert(!sendsClose.release(0, 0, 0)); // Finger lifted outside orb.
    sendsClose.begin(1);
    sendsClose.invalidate(); // Parent resized or safe insets changed.
    assert(!sendsClose.release(1, 0, 0));
    sendsClose.begin(1);
    sendsClose.move(6, 4);
    assert(sendsClose.release(1, 6, 4)); // Fresh stationary phone tap.
    assert(!sendsClose.release(1, 0, 0)); // No duplicate close.

    // Phone Transport panel launchers must not open Mixer/Sends/FX/Browser
    // on swipe, on release over a different launcher, or after a drag that
    // returns to its origin. Desktop launchers are intentionally unchanged.
    using DAW::mobile::phoneTransportLauncherTapActivates;
    for (int launcher = 1; launcher <= 4; ++launcher)
    {
        assert(phoneTransportLauncherTapActivates(launcher, launcher, 0, 0));
        assert(phoneTransportLauncherTapActivates(launcher, launcher, 6, 5));
        assert(!phoneTransportLauncherTapActivates(launcher, launcher, 9, 0));
        assert(!phoneTransportLauncherTapActivates(launcher, launcher, 8, 8));
        assert(!phoneTransportLauncherTapActivates(launcher, 0, 0, 0));
        assert(!phoneTransportLauncherTapActivates(0, launcher, 0, 0));
        assert(!phoneTransportLauncherTapActivates(launcher, launcher % 4 + 1, 0, 0));
        bool eligible = DAW::mobile::phoneTapGestureRemainsEligible(true, 20, 0);
        eligible = DAW::mobile::phoneTapGestureRemainsEligible(eligible, 0, 0);
        assert(!eligible && !(eligible && phoneTransportLauncherTapActivates(
            launcher, launcher, 0, 0)));
    }
    static_assert(phoneTransportLauncherTapActivates(1, 1, 0, 0));
    static_assert(phoneTransportLauncherTapActivates(4, 4, 8, 0));
    static_assert(!phoneTransportLauncherTapActivates(2, 3, 0, 0));
    static_assert(!phoneTransportLauncherTapActivates(3, 3, 9, 0));
    static_assert(!phoneTransportLauncherTapActivates(4, 4,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    // Double-tapping a phone title bar must not maximize unexpectedly or
    // overwrite saved floating bounds during the second touch gesture.
    // Desktop title-bar double-click retains its original behavior.
    using DAW::mobile::chromeTitleDoubleClickMaximizes;
    static_assert(!chromeTitleDoubleClickMaximizes(true, false));
    static_assert(!chromeTitleDoubleClickMaximizes(true, true));
    static_assert(chromeTitleDoubleClickMaximizes(false, false));
    static_assert(!chromeTitleDoubleClickMaximizes(false, true));
    for (bool phone : {false, true})
        for (bool overButton : {false, true})
            assert(chromeTitleDoubleClickMaximizes(phone, overButton)
                   == (!phone && !overButton));
    // Filtering and orientation change can show/hide the vertical scrollbar.
    // Reserve its actual thickness based on row overflow, not stale state.
    using DAW::mobile::pluginBrowserListWidth;
    static_assert(pluginBrowserListWidth(377, 46, 1, 44, 14) == 377);
    static_assert(pluginBrowserListWidth(377, 46, 2, 44, 14) == 363);
    static_assert(pluginBrowserListWidth(377, 44, 1, 44, 14) == 377);
    static_assert(pluginBrowserListWidth(377, 43, 1, 44, 14) == 363);
    static_assert(pluginBrowserListWidth(100, 50, 4, 22, 12) == 88);
    static_assert(pluginBrowserListWidth(100, 50, 0, 44, 12) == 100);
    static_assert(pluginBrowserListWidth(5, 50, 4, 44, 12) == 0);
    static_assert(pluginBrowserListWidth(0, 50, 4, 44, 12) == 0);
    static_assert(pluginBrowserListWidth(100, 0, 4, 44, 12) == 0);
    static_assert(pluginBrowserListWidth(100, 50, 4, 44, -10) == 100);
    for (int width : {0, 1, 5, 100, 377, 852})
        for (int height : {0, 1, 43, 44, 46, 200})
            for (int rows : {0, 1, 2, 50})
            {
                const int listWidth = pluginBrowserListWidth(width, height,
                                                             rows, 44, 14);
                assert(listWidth >= 0 && listWidth <= width);
                if (width > 0 && height > 0 && rows * 44 <= height)
                    assert(listWidth == width);
            }
    using DAW::mobile::pluginBrowserChromeHeight;
    using DAW::mobile::phoneChromeControlSize;
    static_assert(pluginBrowserChromeHeight(true) == 44);
    static_assert(pluginBrowserChromeHeight(false) == 28);
    static_assert(phoneChromeControlSize(393) == 44);
    static_assert(phoneChromeControlSize(245) == 44);
    static_assert(phoneChromeControlSize(100) == 33);
    static_assert(phoneChromeControlSize(2) == 0);
    static_assert(phoneChromeControlSize(0) == 0);
    for (int w : {0, 1, 2, 10, 100, 245, 393, 852})
    {
        const int side = phoneChromeControlSize(w);
        assert(side >= 0 && side <= 44 && 3 * side <= w);
    }
    static_assert(browserMaxLandscape.height - pluginBrowserChromeHeight(true)
                  - pluginBrowserHeaderHeight(true)
                  - pluginBrowserSearchSlotHeight(true) >= pluginBrowserRowHeight(true));
    constexpr auto browserMaxCollapsed = fitPhonePluginBrowser(360, 96, 50, 46, true);
    static_assert(browserMaxCollapsed.width == 0 && browserMaxCollapsed.height == 0);
    constexpr auto browserMaxExtreme = fitPhonePluginBrowser(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 50, 144, true);
    static_assert(inside(browserMaxExtreme,
                         std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
    for (int w : {1, 2, 20, 245, 320, 393, 852})
        for (int h : {1, 10, 96, 144, 220, 300, 852})
            for (int top : {0, 50, 1000})
                for (int bottom : {0, 96, 144, 1000})
                {
                    const auto fit = fitPhonePluginBrowser(w, h, top, bottom, true);
                    const int safeTop = DAW::mobile::clampInt(top, 0, h);
                    const int safeBottom = DAW::mobile::clampInt(bottom, 0, h - safeTop);
                    assert(inside(fit, w, h));
                    if (fit.height > 0)
                    {
                        assert(fit.y >= safeTop);
                        assert(fit.y + fit.height <= h - safeBottom);
                    }
                }
    constexpr auto browserNarrow = fitPhonePluginBrowser(320, 640, 50, 144);
    static_assert(browserNarrow.width == 304 && browserNarrow.height == 430
                  && inside(browserNarrow, 320, 640));
    constexpr auto browserLandscape = fitPhonePluginBrowser(852, 300, 50, 96);
    static_assert(browserLandscape.y == 58 && browserLandscape.height == 138
                  && browserLandscape.y + browserLandscape.height <= 300 - 96);
    constexpr auto browserSplit = fitPhonePluginBrowser(245, 220, 50, 96);
    static_assert(browserSplit.width == 229 && browserSplit.height == 58
                  && inside(browserSplit, 245, 220));
    constexpr auto browserCollapsed = fitPhonePluginBrowser(360, 96, 50, 46);
    static_assert(browserCollapsed.width == 0 && browserCollapsed.height == 0);
    constexpr auto browserExtreme = fitPhonePluginBrowser(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 50, 144);
    static_assert(inside(browserExtreme,
                         std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
    for (int w : {1, 2, 20, 245, 320, 393, 852})
        for (int h : {1, 10, 96, 144, 220, 300, 852})
            for (int top : {0, 50, 1000})
                for (int bottom : {0, 96, 144, 1000})
                {
                    const auto fit = fitPhonePluginBrowser(w, h, top, bottom);
                    if (fit.width == 0 || fit.height == 0)
                    {
                        assert(fit.width == 0 && fit.height == 0);
                        continue;
                    }
                    const int reservedTop = DAW::mobile::clampInt(top, 0, h);
                    const int reservedBottom = DAW::mobile::clampInt(bottom, 0, h - reservedTop);
                    assert(inside(fit, w, h));
                    assert(fit.y >= reservedTop);
                    assert(fit.y + fit.height <= h - reservedBottom);
                    assert(fit.width <= 340 && fit.height <= 560);
                }

    // iPhone previously used desktop's 220-unit Track List at 393 units,
    // leaving too little horizontal Arrangement space. Android already had
    // the compact policy; both phones now share the same bounded calculation.
    using DAW::mobile::phoneTrackListWidth;
    static_assert(phoneTrackListWidth(220, 393) == 108);
    static_assert(phoneTrackListWidth(220, 360) == 108);
    static_assert(phoneTrackListWidth(220, 852) == 213);
    static_assert(phoneTrackListWidth(80, 393) == 80);
    static_assert(phoneTrackListWidth(220, 100) == 99);
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
    // Quick Send must not force desktop's 260-high Mixer onto a short phone
    // landscape viewport. Desktop/iPad retain their legacy recovery.
    using DAW::mobile::quickSendNeedsDesktopMixerMinimum;
    static_assert(!quickSendNeedsDesktopMixerMinimum(
        true, mixerLandscape.window.height, 260));
    static_assert(!quickSendNeedsDesktopMixerMinimum(true, 0, 260));
    static_assert(quickSendNeedsDesktopMixerMinimum(
        false, mixerLandscape.window.height, 260));
    static_assert(quickSendNeedsDesktopMixerMinimum(false, 259, 260));
    static_assert(!quickSendNeedsDesktopMixerMinimum(false, 260, 260));
    static_assert(!quickSendNeedsDesktopMixerMinimum(false, 340, 260));
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
    // A phone drag/rotation should clamp only the escaped position, not
    // discard the user's manually selected Mixer width and height.
    using DAW::mobile::constrainFloatingMixerPlacement;
    constexpr PanelBounds movedOffscreen{ -500, 0, 210, 190 };
    constexpr auto recoveredMixer = constrainFloatingMixerPlacement(
        mixerPortrait, movedOffscreen);
    static_assert(recoveredMixer.x == mixerPortrait.safe.x
                  && recoveredMixer.y == mixerPortrait.safe.y
                  && recoveredMixer.width == 210 && recoveredMixer.height == 190);
    static_assert(floatingMixerFits(mixerPortrait, recoveredMixer));
    constexpr auto recoveredFarEdge = constrainFloatingMixerPlacement(
        mixerPortrait, { std::numeric_limits<int>::max(),
                         std::numeric_limits<int>::max(), 210, 190 });
    static_assert(recoveredFarEdge.x + recoveredFarEdge.width
                  == mixerPortrait.safe.x + mixerPortrait.safe.width);
    static_assert(recoveredFarEdge.y + recoveredFarEdge.height
                  == mixerPortrait.safe.y + mixerPortrait.safe.height);
    static_assert(floatingMixerFits(mixerPortrait, recoveredFarEdge));
    constexpr auto unchangedMixer = constrainFloatingMixerPlacement(
        mixerPortrait, manuallyPlaced);
    static_assert(unchangedMixer.x == manuallyPlaced.x
                  && unchangedMixer.y == manuallyPlaced.y
                  && unchangedMixer.width == manuallyPlaced.width
                  && unchangedMixer.height == manuallyPlaced.height);
    constexpr auto oversizedMixer = constrainFloatingMixerPlacement(
        mixerLandscape, mixerPortrait.window);
    static_assert(oversizedMixer.x == mixerLandscape.window.x
                  && oversizedMixer.y == mixerLandscape.window.y
                  && oversizedMixer.width == mixerLandscape.window.width
                  && oversizedMixer.height == mixerLandscape.window.height);
    static_assert(floatingMixerFits(mixerLandscape, oversizedMixer));
    static_assert(constrainFloatingMixerPlacement(
        mixerTiny, { 100, 100, 200, 180 }).width == 0);

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
                    // Arbitrary out-of-bounds restored positions must
                    // recover to a usable window without losing a fitting size.
                    for (int offset : { -1000000, -1, 0, 1000000 })
                    {
                        const auto moved = constrainFloatingMixerPlacement(
                            fit, { offset, -offset, fit.window.width, fit.window.height });
                        assert(floatingMixerFits(fit, moved));
                        assert(moved.width == fit.window.width
                               && moved.height == fit.window.height);
                    }
                }
    // Clip Properties contains clip-region FX. A desktop 420x360 dialog
    // overflows a 393-wide phone and covers Transport in short landscape.
    using DAW::mobile::fitPhoneClipProperties;
    using DAW::mobile::phoneClipPropertiesFits;
    constexpr auto clipPortrait = fitPhoneClipProperties(393, 852, 50, 144);
    static_assert(clipPortrait.window.width == 377
                  && clipPortrait.window.height == 360);
    static_assert(clipPortrait.window.y >= 50
                  && clipPortrait.window.y + clipPortrait.window.height <= 852 - 144);
    static_assert(phoneClipPropertiesFits(clipPortrait, clipPortrait.window));
    constexpr auto clipLandscape = fitPhoneClipProperties(852, 300, 50, 96);
    static_assert(clipLandscape.window.width == 420
                  && clipLandscape.window.height == 138);
    static_assert(clipLandscape.window.y >= 50
                  && clipLandscape.window.y + clipLandscape.window.height <= 300 - 96);
    static_assert(phoneClipPropertiesFits(clipLandscape, clipLandscape.window));
    static_assert(!phoneClipPropertiesFits(clipLandscape, clipPortrait.window));
    static_assert(!phoneClipPropertiesFits(clipPortrait,
        { 0, 0, 420, 360 }));
    static_assert(!phoneClipPropertiesFits(clipPortrait,
        { std::numeric_limits<int>::max(), 60, 420, 360 }));
    constexpr auto clipCollapsed = fitPhoneClipProperties(360, 96, 50, 46);
    static_assert(clipCollapsed.window.width == 0
                  && clipCollapsed.window.height == 0);
    constexpr auto clipExtreme = fitPhoneClipProperties(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 50, 144);
    static_assert(inside(clipExtreme.safe,
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
    static_assert(phoneClipPropertiesFits(clipExtreme, clipExtreme.window));
    for (int w : {1, 2, 20, 245, 360, 393, 852})
        for (int h : {1, 10, 96, 144, 220, 300, 852})
            for (int top : {0, 50, 1000})
                for (int bottom : {0, 96, 144, 1000})
                {
                    const auto fit = fitPhoneClipProperties(w, h, top, bottom);
                    if (fit.safe.width <= 0 || fit.safe.height <= 0)
                    {
                        assert(fit.window.width == 0 && fit.window.height == 0);
                        continue;
                    }
                    assert(inside(fit.safe, w, h));
                    assert(inside(fit.window, w, h));
                    assert(fit.window.y >= fit.safe.y);
                    assert(fit.window.y + fit.window.height <= fit.safe.y + fit.safe.height);
                    assert(phoneClipPropertiesFits(fit, fit.window));
                }

    // A restored FX browser may carry extreme coordinates from a different
    // display. Clamp both edges on rotation without losing its usable size.
    for (int x : { std::numeric_limits<int>::min(), -1000000, 0,
                   1000000, std::numeric_limits<int>::max() })
        for (int y : { std::numeric_limits<int>::min(), -1000000, 0,
                       1000000, std::numeric_limits<int>::max() })
        {
            const auto placed = DAW::mobile::fitPhonePluginBrowserPlacement(
                852, 300, 50, 96, { x, y, 340, 560 });
            assert(placed.width == 340 && placed.height == 138);
            assert(placed.x >= 0 && placed.y >= 50);
            assert(static_cast<long long>(placed.x) + placed.width <= 852);
            assert(static_cast<long long>(placed.y) + placed.height <= 204);
        }

    // Cross-panel rotation gauntlet: a portrait FX Browser and Mixer may both
    // remain open when the phone turns landscape. Their *individual* chrome
    // must remain above Transport; overlap between floating panels is allowed.
    constexpr auto browserBeforeRotation = DAW::mobile::fitPhonePluginBrowserPlacement(
        393, 852, 50, 144, { 8, 80, 340, 560 });
    constexpr auto concurrentBrowserAfterRotation = DAW::mobile::fitPhonePluginBrowserPlacement(
        852, 300, 50, 96, browserBeforeRotation);
    static_assert(concurrentBrowserAfterRotation.y >= 50
                  && concurrentBrowserAfterRotation.y + concurrentBrowserAfterRotation.height <= 300 - 96);
    static_assert(concurrentBrowserAfterRotation.width > 0 && concurrentBrowserAfterRotation.height > 0);
    static_assert(!floatingMixerFits(mixerLandscape, mixerPortrait.window));
    static_assert(floatingMixerFits(mixerLandscape, mixerLandscape.window));
    constexpr auto browserAfterReturn = DAW::mobile::fitPhonePluginBrowserPlacement(
        393, 852, 50, 144, concurrentBrowserAfterRotation);
    static_assert(browserAfterReturn.x >= 0 && browserAfterReturn.y >= 50);
    static_assert(browserAfterReturn.y + browserAfterReturn.height <= 852 - 144);

    // The same safe-area contract applies while Sends, FX Browser, Clip FX
    // and the floating Mixer coexist through portrait/landscape transitions.
    for (int w : {245, 393, 852})
        for (int h : {220, 300, 852})
            for (int bottom : {96, 144})
            {
                constexpr int top = 50;
                const auto mixer = fitFloatingMixer(w, h, top, bottom);
                const auto browser = DAW::mobile::fitPhonePluginBrowserPlacement(
                    w, h, top, bottom, { 1000000, -1000000, 340, 560 });
                const auto sends = fitBubblegumSends(
                    w, h, 1000000, -1000000, true, top, bottom);
                const auto clip = fitPhoneClipProperties(w, h, top, bottom);
                assert(browser.x >= 0 && browser.y >= top);
                assert(static_cast<long long>(browser.y) + browser.height <= h - bottom);
                assert(inside(browser, w, h));
                assert(inside(sends, w, h));
                assert(inside(clip.window, w, h));
                if (sends.height > 0)
                    assert(sends.y >= top
                           && static_cast<long long>(sends.y) + sends.height <= h - bottom);
                if (mixer.safe.width > 0 && mixer.safe.height > 0)
                    assert(floatingMixerFits(mixer, mixer.window));
                if (clip.safe.width > 0 && clip.safe.height > 0)
                    assert(phoneClipPropertiesFits(clip, clip.window));
            }


    // Release-mode regression: a restored Sends panel must never cover the
    // reserved phone header or bottom Transport after portrait/landscape.
    // Compile-time checks survive NDEBUG (unlike assert()).
    constexpr auto sendsPortraitEdge = fitBubblegumSends(
        393, 852, std::numeric_limits<int>::max(),
        std::numeric_limits<int>::min(), true, 50, 144);
    constexpr auto sendsLandscapeEdge = fitBubblegumSends(
        852, 300, std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(), true, 50, 96);
    constexpr auto sendsNoWorkspace = fitBubblegumSends(
        393, 144, 100, 100, true, 50, 94);
    static_assert(sendsPortraitEdge.width > 0 && sendsPortraitEdge.height > 0);
    static_assert(sendsPortraitEdge.y >= 50
                  && static_cast<long long>(sendsPortraitEdge.y) + sendsPortraitEdge.height <= 708);
    static_assert(sendsLandscapeEdge.width > 0 && sendsLandscapeEdge.height > 0);
    static_assert(sendsLandscapeEdge.y >= 50
                  && static_cast<long long>(sendsLandscapeEdge.y) + sendsLandscapeEdge.height <= 204);
    static_assert(sendsNoWorkspace.width == 0 && sendsNoWorkspace.height == 0);
    // Phone-only reserved-area policy: desktop/iPad Matrix geometry must
    // ignore phone header/Transport reservations, while phone Sends must
    // collapse rather than overlap them when split-screen leaves no room.
    constexpr auto desktopSendsBaseline = fitBubblegumSends(
        393, 300, 200, 180, false);
    constexpr auto desktopSendsWithPhoneInsets = fitBubblegumSends(
        393, 300, 200, 180, false, 250, 250);
    static_assert(desktopSendsBaseline.x == desktopSendsWithPhoneInsets.x
                  && desktopSendsBaseline.y == desktopSendsWithPhoneInsets.y
                  && desktopSendsBaseline.width == desktopSendsWithPhoneInsets.width
                  && desktopSendsBaseline.height == desktopSendsWithPhoneInsets.height);
    constexpr auto phoneSendsFullyReserved = fitBubblegumSends(
        393, 300, 200, 180, true, 250, 250);
    static_assert(phoneSendsFullyReserved.width == 0
                  && phoneSendsFullyReserved.height == 0);
    constexpr auto phoneSendsNegativeInsets = fitBubblegumSends(
        393, 300, 200, 180, true, -100, -100);
    constexpr auto phoneSendsZeroInsets = fitBubblegumSends(
        393, 300, 200, 180, true, 0, 0);
    static_assert(phoneSendsNegativeInsets.x == phoneSendsZeroInsets.x
                  && phoneSendsNegativeInsets.y == phoneSendsZeroInsets.y
                  && phoneSendsNegativeInsets.width == phoneSendsZeroInsets.width
                  && phoneSendsNegativeInsets.height == phoneSendsZeroInsets.height);

    // Split-window boundary: a single available logical unit must not leak
    // into the reserved header/Transport, and invalid viewports stay empty.
    // Compile-time checks also run when NDEBUG removes runtime assertions.
    constexpr auto sendsOneUnitWorkspace = fitBubblegumSends(
        393, 301, std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(), true, 150, 150);
    static_assert(sendsOneUnitWorkspace.width == 0
                  && sendsOneUnitWorkspace.height == 0);
    constexpr auto sendsInvalidWidth = fitBubblegumSends(
        -1, 852, 10, 10, true, 50, 144);
    constexpr auto sendsInvalidHeight = fitBubblegumSends(
        393, 0, 10, 10, true, 50, 144);
    static_assert(sendsInvalidWidth.width == 0 && sendsInvalidWidth.height == 0);
    static_assert(sendsInvalidHeight.width == 0 && sendsInvalidHeight.height == 0);

    // Rotation must also clamp the horizontal edge, including restored INT_MIN/MAX
    // positions; vertical-only checks miss off-screen Sends chrome.
    static_assert(sendsPortraitEdge.x >= 0
                  && static_cast<long long>(sendsPortraitEdge.x) + sendsPortraitEdge.width <= 393);
    static_assert(sendsLandscapeEdge.x >= 0
                  && static_cast<long long>(sendsLandscapeEdge.x) + sendsLandscapeEdge.width <= 852);


    // Adversarial FX list boundaries: one pixel of overflow must reserve the
    // scrollbar; exact-fit content must retain the full row hit width.
    // Extreme row counts/thicknesses must not overflow signed arithmetic.
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 131, 3, 44, 12) == 381);
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 132, 3, 44, 12) == 393);
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 133, 3, 44, 12) == 393);
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 1,
        std::numeric_limits<int>::max(), 44, 12) == 381);
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 1, 1, 44,
        std::numeric_limits<int>::max()) == 0);
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 1, -1, 44, 12) == 393);

    // Phone FX swipe-vs-tap boundary: a diagonal drag beyond the circular
    // 8-unit radius must not instantiate a plugin on release. Desktop mouse
    // activation keeps its existing behavior, even outside the row.
    static_assert(DAW::mobile::pluginBrowserRowTapActivates(true, 8, 0, true));
    static_assert(DAW::mobile::pluginBrowserRowTapActivates(true, 0, -8, true));
    static_assert(!DAW::mobile::pluginBrowserRowTapActivates(true, 8, 1, true));
    static_assert(!DAW::mobile::pluginBrowserRowTapActivates(true, 6, 6, true));
    static_assert(!DAW::mobile::pluginBrowserRowTapActivates(true, 0, 0, false));
    static_assert(DAW::mobile::pluginBrowserRowTapActivates(false, 100, 100, false));

    // Circular touch slop must be evaluated as a radius, not an 8x8 box.
    // A diagonal 5x6 gesture is still inside the radius (61 <= 64),
    // whereas 6x6 is outside (72 > 64). Both signs behave symmetrically.
    static_assert(DAW::mobile::pluginBrowserRowTapActivates(true, 5, 6, true));
    static_assert(DAW::mobile::pluginBrowserRowTapActivates(true, -5, -6, true));
    static_assert(!DAW::mobile::pluginBrowserRowTapActivates(true, 6, 6, true));
    static_assert(!DAW::mobile::pluginBrowserRowTapActivates(true, -6, -6, true));
    static_assert(!DAW::mobile::pluginBrowserRowTapActivates(true, 5, 6, false));


    // Phone FX browser: rotation must clamp an off-screen dragged window
    // without placing chrome over the persistent Transport or header.
    // Maximizing intentionally discards the old drag position.
    constexpr auto fxPortraitRestored = DAW::mobile::fitPhonePluginBrowserPlacement(
        393, 852, 50, 144, { 1000000, -1000000, 340, 560 });
    static_assert(fxPortraitRestored.x >= 0 && fxPortraitRestored.y >= 50);
    static_assert(fxPortraitRestored.x + fxPortraitRestored.width <= 393);
    static_assert(fxPortraitRestored.y + fxPortraitRestored.height <= 708);
    constexpr auto fxLandscapeRestored = DAW::mobile::fitPhonePluginBrowserPlacement(
        852, 300, 50, 96, fxPortraitRestored);
    static_assert(fxLandscapeRestored.x >= 0 && fxLandscapeRestored.y >= 50);
    static_assert(fxLandscapeRestored.x + fxLandscapeRestored.width <= 852);
    static_assert(fxLandscapeRestored.y + fxLandscapeRestored.height <= 204);
    constexpr auto fxLandscapeMaximized = DAW::mobile::fitPhonePluginBrowserPlacement(
        852, 300, 50, 96, { 1000000, -1000000, 340, 560 }, true);
    constexpr auto fxLandscapeDefaultMaximized = DAW::mobile::fitPhonePluginBrowser(
        852, 300, 50, 96, true);
    static_assert(fxLandscapeMaximized.x == fxLandscapeDefaultMaximized.x
                  && fxLandscapeMaximized.y == fxLandscapeDefaultMaximized.y
                  && fxLandscapeMaximized.width == fxLandscapeDefaultMaximized.width
                  && fxLandscapeMaximized.height == fxLandscapeDefaultMaximized.height);
    constexpr auto fxNoWorkspace = DAW::mobile::fitPhonePluginBrowserPlacement(
        393, 144, 50, 94, { 1000000, -1000000, 340, 560 });
    static_assert(fxNoWorkspace.width == 0 && fxNoWorkspace.height == 0);

    // Restored FX Browser coordinates may be corrupted at either signed-int
    // extreme. Clamp both axes through rotation without signed overflow or
    // allowing the title bar to cover the persistent phone Transport.
    constexpr auto fxIntMin = DAW::mobile::fitPhonePluginBrowserPlacement(
        393, 852, 50, 144,
        { std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), 340, 560 });
    constexpr auto fxIntMax = DAW::mobile::fitPhonePluginBrowserPlacement(
        852, 300, 50, 96,
        { std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 340, 560 });
    static_assert(fxIntMin.x >= 0 && fxIntMin.y >= 50);
    static_assert(static_cast<long long>(fxIntMin.x) + fxIntMin.width <= 393
                  && static_cast<long long>(fxIntMin.y) + fxIntMin.height <= 708);
    static_assert(fxIntMax.x >= 0 && fxIntMax.y >= 50);
    static_assert(static_cast<long long>(fxIntMax.x) + fxIntMax.width <= 852
                  && static_cast<long long>(fxIntMax.y) + fxIntMax.height <= 204);

    // FX list scrollbar transition after rotation or search filtering: the
    // final fully visible row must not lose width to a stale scrollbar.
    // Overflow begins only when one additional row exceeds the viewport.
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 132, 3, 44, 12) == 393);
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 132, 4, 44, 12) == 381);
    static_assert(DAW::mobile::pluginBrowserListWidth(852, 88, 2, 44, 12) == 852);
    static_assert(DAW::mobile::pluginBrowserListWidth(852, 88, 3, 44, 12) == 840);
    // A hidden/empty list and desktop-sized rows must not reserve scrollbar
    // space; malformed thickness cannot make a negative-width touch row.
    static_assert(DAW::mobile::pluginBrowserListWidth(100, 44, 0, 44, 12) == 100);
    static_assert(DAW::mobile::pluginBrowserListWidth(100, 44, 2, 44, -10) == 100);
    static_assert(DAW::mobile::pluginBrowserListWidth(100, 44, 2, 44, 200) == 0);
    static_assert(DAW::mobile::pluginBrowserListWidth(0, 44, 2, 44, 12) == 0);
    static_assert(DAW::mobile::pluginBrowserListWidth(100, 0, 2, 44, 12) == 0);

    // Adversarial FX list overflow: the scrollbar decision must avoid
    // multiplying row count by row height (signed overflow at INT_MAX).
    // Exactly fitting rows keep their full hit width; one extra row reserves
    // the scrollbar, including at the largest representable dimensions.
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 132, 3, 44, 12) == 393);
    static_assert(DAW::mobile::pluginBrowserListWidth(393, 131, 3, 44, 12) == 381);
    static_assert(DAW::mobile::pluginBrowserListWidth(
        393, std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(), 12) == 381);
    static_assert(DAW::mobile::pluginBrowserListWidth(
        393, std::numeric_limits<int>::max(), 1,
        std::numeric_limits<int>::max(), 12) == 393);

    // Tiny split-screen / rotated FX Browser: zero-sized workspaces must
    // never produce an interactive off-screen panel or a negative hit area.
    constexpr auto fxOnePixel = DAW::mobile::fitPhonePluginBrowser(1, 1, 0, 0);
    constexpr auto fxTwoPixels = DAW::mobile::fitPhonePluginBrowser(2, 2, 0, 0);
    constexpr auto fxFullyReserved = DAW::mobile::fitPhonePluginBrowser(
        393, 852, 852, 144);
    static_assert(fxOnePixel.width == 1 && fxOnePixel.height == 1);
    static_assert(fxTwoPixels.width == 2 && fxTwoPixels.height == 2);
    static_assert(fxFullyReserved.width == 0 && fxFullyReserved.height == 0);
    constexpr auto fxTinyRestored = DAW::mobile::fitPhonePluginBrowserPlacement(
        2, 2, 0, 0, { std::numeric_limits<int>::max(),
                       std::numeric_limits<int>::min(), 340, 560 });
    static_assert(fxTinyRestored.x == 0 && fxTinyRestored.y == 0);
    static_assert(fxTinyRestored.width == 2 && fxTinyRestored.height == 2);
    // Adversarial portrait/split-screen sweep: every restored FX browser
    // must remain inside the unreserved viewport after an inset change.
    for (int width : { 1, 2, 3, 8, 46, 94, 393, 852 })
        for (int height : { 1, 2, 8, 50, 144, 300, 852 })
            for (int top : { 0, 1, 50, 144 })
                for (int bottom : { 0, 1, 96, 144 })
                {
                    const auto panel = DAW::mobile::fitPhonePluginBrowserPlacement(
                        width, height, top, bottom,
                        { std::numeric_limits<int>::max(),
                          std::numeric_limits<int>::min(), 340, 560 });
                    // Keep the rotation sweep active in Release/NDEBUG CI.
                    if (!inside(panel, width, height))
                        return 40;
                    if (panel.width > 0 && panel.height > 0)
                    {
                        const int safeTop = DAW::mobile::clampInt(top, 0, height);
                        const int safeBottom = DAW::mobile::clampInt(
                            bottom, 0, height - safeTop);
                        if (panel.y < safeTop
                            || static_cast<long long>(panel.y) + panel.height
                                   > height - safeBottom)
                            return 41;
                    }
                }

    // Maximized FX browser must ignore stale restored drag coordinates after
    // portrait/landscape changes, including a fully reserved workspace.
    for (int width : { 1, 393, 852 })
        for (int height : { 1, 300, 852 })
            for (int top : { 0, 50, 852 })
            {
                const auto maximized = DAW::mobile::fitPhonePluginBrowserPlacement(
                    width, height, top, 96,
                    { std::numeric_limits<int>::min(),
                      std::numeric_limits<int>::max(), 340, 560 }, true);
                const auto expected = DAW::mobile::fitPhonePluginBrowser(
                    width, height, top, 96, true);
                // Runtime checks must also execute in Release/NDEBUG CI.
                if (maximized.x != expected.x || maximized.y != expected.y)
                    return 42;
                if (maximized.width != expected.width
                    || maximized.height != expected.height)
                    return 43;
                if (!inside(maximized, width, height))
                    return 44;
            }

}
