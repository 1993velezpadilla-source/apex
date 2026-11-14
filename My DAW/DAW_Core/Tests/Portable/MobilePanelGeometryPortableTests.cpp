#include "../../Source/UICore/MobilePanelGeometry.h"
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
    // Phone Mixer chrome: reject malformed touch coordinates and preserve
    // exclusive boundaries after rotation. No ghost minimize/maximize hit.
    for (int titleHeight : { 1, 31, 32, 44, 96 })
    {
        const auto hit = [titleHeight](float x, float y) {
            return DAW::mobile::phoneMixerLeftChromeButtonAt(
                x, y, 14, 12, 8, titleHeight);
        };
        if (hit(29.999f, 0.0f) != 1 || hit(30.0f, 0.0f) != 2
            || hit(60.0f, 0.0f) != 0)
            return 256;
        if (hit(20.0f, static_cast<float>(titleHeight)) != 0
            || hit(40.0f, -0.001f) != 0)
            return 257;
        constexpr float nan = std::numeric_limits<float>::quiet_NaN();
        constexpr float inf = std::numeric_limits<float>::infinity();
        if (hit(nan, 1.0f) != 0 || hit(inf, 1.0f) != 0
            || hit(-inf, 1.0f) != 0 || hit(20.0f, nan) != 0
            || hit(20.0f, inf) != 0 || hit(20.0f, -inf) != 0)
            return 258;
    }
    // Exhaustive compact Mixer toolbar hit-map across portrait resize widths.
    // Every visible control must remain fully tappable, non-overlapping, and
    // within the 32-high title region; hidden actions must not leave hit zones.
    for (int width = 0; width <= 320; ++width)
        for (int height : { 0, 31, 32, 33, 48, 64 })
        {
            const auto layout = DAW::mobile::phoneMixerToolbarLayout(width, height);
            if (layout.count < 0 || layout.count > 4)
                return 251;
            if (height < 32 && layout.count != 0)
                return 252;
            for (int i = 0; i < layout.count; ++i)
            {
                const auto bounds = layout.buttons[i].bounds;
                if (!inside(bounds, width, height)
                    || bounds.width != 44 || bounds.height != 32)
                    return 253;
                if (i > 0)
                {
                    const auto prev = layout.buttons[i - 1].bounds;
                    if (static_cast<long long>(prev.x) + prev.width + 4 > bounds.x)
                        return 254;
                }
            }
            if (layout.count > 0 && layout.count < 3
                && layout.buttons[layout.count - 1].action != DAW::mobile::phoneMixerOverflow)
                return 255;
        }
    // Toolbar breakpoints must preserve action identity across rotation and
    // split-screen resizing. A hidden FX/Reset action must not silently become
    // another button; the overflow control must remain the last visible one.
    for (int width : { 45, 46, 93, 94, 141, 142, 393, 852 })
    {
        const auto layout = DAW::mobile::phoneMixerToolbarLayout(width, 32);
        const int expectedCount = width < 46 ? 0 : width < 94 ? 1
                                : width < 142 ? 2 : 3;
        if (layout.count != expectedCount)
            return 259;
        for (int i = 0; i < layout.count; ++i)
        {
            const auto expectedAction = expectedCount == 3
                ? (i == 0 ? DAW::mobile::phoneMixerBrowser
                   : i == 1 ? DAW::mobile::phoneMixerFXChain
                            : DAW::mobile::phoneMixerReset)
                : (i == layout.count - 1 ? DAW::mobile::phoneMixerOverflow
                                         : DAW::mobile::phoneMixerBrowser);
            if (layout.buttons[i].action != expectedAction)
                return 260;
        }
    }
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
    // A zero-area viewport has no actionable hit region. Reject a panel
    // displaced by one pixel on either edge, including INT_MAX dimensions.
    static_assert(inside({ 0, 0, 0, 0 }, 0, 0));
    static_assert(!inside({ 1, 0, 0, 0 }, 0, 0));
    static_assert(!inside({ 0, 1, 0, 0 }, 0, 0));
    static_assert(inside({ std::numeric_limits<int>::max() - 1, 0, 1, 1 },
                         std::numeric_limits<int>::max(), 1));
    static_assert(!inside({ std::numeric_limits<int>::max() - 1, 0, 2, 1 },
                          std::numeric_limits<int>::max(), 1));
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
        // Must execute in Release/NDEBUG too: duplicate and out-of-order
        // releases must not launch a phone panel from the wrong source.
        if (taps.release(second, 2, 0, 0))
            return 48;
        if (taps.release(second, 2, 0, 0))
            return 49;
        if (taps.release(first, 1, 0, 0))
            return 50;
        taps.begin(3, second);
        if (!taps.release(second, 3, 0, 0))
            return 51;
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
            if (taps.release(source, 1, 0, 0))
                return 45;
        taps.begin(2, 1000);
        if (taps.release(1000, 2, 0, 0))
            return 46;
        taps.invalidate();
        taps.begin(2, 1000);
        if (!taps.release(1000, 2, 0, 0))
            return 47;
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
    // Keep runtime viewport chrome checks active in Release/NDEBUG builds.
    if (DAW::mobile::timelineViewportChrome(true).showCornerZoom)
        return 78;
    if (!DAW::mobile::timelineViewportChrome(false).showCornerZoom)
        return 79;

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
    if (!(zoomCarry.x == -0.8 && zoomCarry.y == 0.6))
        return 80;
    zoomCarry.observeContentScale(100.0, 60.0); // unchanged zoom retains carry
    if (!(zoomCarry.x == -0.8 && zoomCarry.y == 0.6))
        return 81;
    // At minimum content width or integer rounding, zoom changes but size
    // stays identical: the old fractional wheel remainder must be discarded.
    zoomCarry.observeContentScale(125.0, 60.0);
    if (!(zoomCarry.x == 0.0 && zoomCarry.y == 0.0))
        return 82;
    zoomCarry.x = 0.7;
    zoomCarry.y = -0.3;
    zoomCarry.observeContentScale(125.0, 70.0); // physical lane zoom
    if (!(zoomCarry.x == 0.0 && zoomCarry.y == 0.0))
        return 83;
    zoomCarry.x = -0.8;
    zoomCarry.y = 0.6;
    zoomCarry.observeContentSize(4000, 1200); // horizontal zoom
    if (!(zoomCarry.x == 0.0 && zoomCarry.y == 0.0))
        return 84;
    zoomCarry.y = -0.4;
    zoomCarry.observeContentSize(4000, 1800); // lane height/vertical zoom
    if (!(zoomCarry.x == 0.0 && zoomCarry.y == 0.0))
        return 85;
    zoomCarry.x = 0.5;
    zoomCarry.reset(); // rotation or presentation change
    if (!(zoomCarry.x == 0.0 && zoomCarry.y == 0.0))
        return 86;
    zoomCarry.observeContentSize(4000, 1800);
    if (!(zoomCarry.x == 0.0 && zoomCarry.y == 0.0))
        return 87;

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

    // Release-active adversarial Arrangement wheel sweep: fractional carry
    // must remain bounded after rapid portrait/landscape viewport changes.
    // This checks both directions, zero-width views and stale restored carry.
    for (int span : { -1, 0, 1, 393, 852, 1200 })
        for (float delta : { -2.0f, -0.001f, 0.0f, 0.001f, 2.0f })
            for (double carry : { -2.0, -0.9, 0.0, 0.9, 2.0 })
            {
                const auto motion = DAW::mobile::timelinePhoneWheelPixelMotion(
                    span, delta, carry);
                if (!(motion.remainder > -1.0 && motion.remainder < 1.0))
                    return 240;
                if (span <= 0 && (motion.pixels != 0 || motion.remainder != 0.0))
                    return 241;
                if (span > 0 && delta > 0.0f && motion.pixels > 0)
                    return 242;
                if (span > 0 && delta < 0.0f && motion.pixels < 0)
                    return 243;
            }

    // Rotation/resize invariant: scrolling to either Arrangement boundary
    // must stay clamped even when restored positions and wheel steps are
    // corrupted. Exercise this at runtime so Release/NDEBUG also checks it.
    for (int content : { 0, 1, 393, 852, 2000 })
        for (int viewport : { 0, 1, 393, 852, 2000 })
            for (int restored : { -1000000, -1, 0, 1, 1000000 })
                for (int step : { -1000000, -1, 0, 1, 1000000 })
                {
                    const int result = DAW::mobile::timelineWheelClampedPosition(
                        restored, content, viewport, step);
                    const int maximum = content > viewport ? content - viewport : 0;
                    if (result < 0 || result > maximum)
                        return 244;
                    if (content <= viewport && result != 0)
                        return 245;
                }

    // A wheel gesture must be monotonic relative to the clamped restored
    // position, even when rotation changes the visible Arrangement width.
    // Zero wheel motion must not move the restored viewport unexpectedly.
    for (int content : { 0, 1, 393, 852, 2000 })
        for (int viewport : { 0, 1, 393, 852, 2000 })
            for (int restored : { -1000000, -1, 0, 1, 1000000 })
            {
                const int baseline = DAW::mobile::timelineWheelClampedPosition(
                    restored, content, viewport, 0);
                if (DAW::mobile::timelineWheelClampedPosition(
                        baseline, content, viewport, 0) != baseline)
                    return 246;
                for (int step : { -1000000, -1, 1, 1000000 })
                {
                    const int moved = DAW::mobile::timelineWheelClampedPosition(
                        restored, content, viewport, step);
                    if ((step < 0 && moved > baseline)
                        || (step > 0 && moved < baseline))
                        return 247;
                }
            }

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
                    // Keep Arrangement wheel bounds checked in Release/NDEBUG.
                    if (result < 0)
                        return 60;
                    if (result > (content > view ? content - view : 0))
                        return 61;
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
        if (timelinePhonePlaybackExtentSeconds(30.0,
                static_cast<double>(tick) / 20.0) != 30.0)
            return 88;
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
    if (refreshes > 2) // Previously ~600 rebuilds after 28s.
        return 89;
    if (phoneExtent < 60.0)
        return 90;

    // Regression: closing Sends or the floating Mixer must not resurrect
    // desktop draggable launchers over the phone Arrangement. The desktop
    // restore affordance remains available for iPad/Windows/macOS.
    using DAW::mobile::desktopRestoreBubbleVisible;
    static_assert(!desktopRestoreBubbleVisible(true));
    static_assert(desktopRestoreBubbleVisible(false));
    if (desktopRestoreBubbleVisible(true)
        || !desktopRestoreBubbleVisible(false))
        return 91;
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
            // Keep close-button hit-area checks active in Release/NDEBUG.
            if (!inside(target, w, h))
                return 62;
            if (target.width != (w < 44 ? w : 44))
                return 63;
            if (target.height != (h < 44 ? h : 44))
                return 64;
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
        if (hit != (x < 0 || x >= 60 ? 0 : x < 30 ? 1 : 2))
            return 74;
    }
    DAW::mobile::PhoneChromeTapState mixerChromeTap;
    mixerChromeTap.begin(101); // minimize
    if (mixerChromeTap.release(102, 0, 0)) // release on maximize
        return 75;
    mixerChromeTap.begin(102);
    mixerChromeTap.move(12, 0);
    mixerChromeTap.move(0, 0); // a swipe returning to the orb stays cancelled
    if (mixerChromeTap.release(102, 0, 0))
        return 76;
    mixerChromeTap.begin(102);
    if (!mixerChromeTap.release(102, 0, 0)) // stationary tap
        return 77;

    // The phone's Mixer/Sends/FX/Browser launchers must not cross-activate
    // when two fingers overlap, even if one returns to its starting point.
    DAW::mobile::PhoneTransportTapState phoneLauncherTap;
    phoneLauncherTap.begin(1, 7);
    if (!phoneLauncherTap.release(7, 1, 0, 0)) return 92; // stationary one-finger tap
    phoneLauncherTap.begin(1, 7);
    phoneLauncherTap.move(7, 20, 0);
    phoneLauncherTap.move(7, 0, 0);
    if (phoneLauncherTap.release(7, 1, 0, 0)) return 93; // swipe and return
    phoneLauncherTap.begin(2, 7);
    phoneLauncherTap.begin(3, 8); // second finger cancels both
    if (phoneLauncherTap.release(8, 3, 0, 0)) return 94;
    if (phoneLauncherTap.release(7, 2, 0, 0)) return 95;
    phoneLauncherTap.begin(4, 7);
    if (phoneLauncherTap.release(8, 4, 0, 0)) return 96; // wrong pointer
    if (phoneLauncherTap.release(7, 4, 0, 0)) return 97; // cannot re-arm after foreign up
    phoneLauncherTap.begin(4, 7);
    phoneLauncherTap.move(8, 0, 0); // foreign drag cancels
    // Release/NDEBUG must still execute release() to clear gesture state.
    if (phoneLauncherTap.release(7, 4, 0, 0))
        return 112;
    phoneLauncherTap.begin(4, 7);
    if (phoneLauncherTap.release(7, 3, 0, 0)) // wrong target
        return 113;
    phoneLauncherTap.begin(4, 7);
    phoneLauncherTap.invalidate(); // rotation or resize
    if (phoneLauncherTap.release(7, 4, 0, 0))
        return 114;
    // A non-launcher touch also occupies the Transport: a second finger
    // must not turn a previously ineligible gesture into a panel launch.
    phoneLauncherTap.begin(0, 7);
    phoneLauncherTap.begin(1, 8);
    // Release must consume each finger even when NDEBUG disables assert().
    if (phoneLauncherTap.release(8, 1, 0, 0)) return 118;
    if (phoneLauncherTap.release(7, 0, 0, 0)) return 119;
    phoneLauncherTap.begin(1, 7);
    if (!phoneLauncherTap.release(7, 1, 0, 0)) return 120;

    // A cancelled two-finger gesture stays blocked until BOTH fingers lift.
    // Evaluate releases in Release/NDEBUG as well as Debug.
    phoneLauncherTap.begin(1, 7);
    phoneLauncherTap.begin(2, 8);
    if (phoneLauncherTap.release(7, 1, 0, 0)) return 121;
    phoneLauncherTap.begin(3, 9); // third finger cannot re-arm
    if (phoneLauncherTap.release(9, 3, 0, 0)) return 122;
    if (phoneLauncherTap.release(8, 2, 0, 0)) return 123;
    phoneLauncherTap.begin(4, 7);
    if (!phoneLauncherTap.release(7, 4, 0, 0)) return 124;

    // Nine or ten contacts can coexist on Android. If only eight are
    // tracked, the state resets while the ninth is still down and an
    // unrelated new finger can accidentally open a floating panel.
    for (int source = 0; source < 10; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    // Execute every release in Release/NDEBUG: assert() would skip the
    // transition and invalidate this multi-contact overflow regression.
    if (phoneLauncherTap.activeCount != 10) return 125;
    for (int source = 0; source < 8; ++source)
        if (phoneLauncherTap.release(source, 1, 0, 0)) return 126;
    if (phoneLauncherTap.activeCount != 2) return 127;
    phoneLauncherTap.begin(2, 12); // Must not re-arm while 8 and 9 remain.
    if (phoneLauncherTap.release(12, 2, 0, 0)) return 128;
    if (phoneLauncherTap.release(8, 1, 0, 0)) return 129;
    if (phoneLauncherTap.release(9, 1, 0, 0)) return 130;
    phoneLauncherTap.begin(4, 7);
    if (!phoneLauncherTap.release(7, 4, 0, 0)) return 131;

    // A 17th touch exceeds the fixed source-ID array. Releasing the first
    // sixteen must NOT re-arm a launcher while the overflow touch remains.
    for (int source = 0; source < 17; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    // Keep overflow and release transitions executable under NDEBUG.
    if (phoneLauncherTap.activeCount != 16) return 132;
    if (phoneLauncherTap.overflowCount != 1) return 133;
    for (int source = 0; source < 16; ++source)
        if (phoneLauncherTap.release(source, 1, 0, 0)) return 134;
    if (phoneLauncherTap.activeCount != 0) return 135;
    if (phoneLauncherTap.overflowCount != 1) return 136;
    phoneLauncherTap.begin(2, 30);
    if (phoneLauncherTap.release(30, 2, 0, 0)) return 137;
    if (phoneLauncherTap.release(16, 0, 0, 0)) return 138;
    if (phoneLauncherTap.overflowCount != 0) return 139;
    phoneLauncherTap.begin(3, 7);
    if (!phoneLauncherTap.release(7, 3, 0, 0)) return 140;

    // Foreign and duplicate releases must not consume an overflow ID.
    for (int source = 0; source < 18; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    // Overflow and duplicate releases must execute in NDEBUG builds too.
    if (phoneLauncherTap.overflowCount != 2) return 141;
    if (phoneLauncherTap.release(999, 0, 0, 0)) return 142;
    if (phoneLauncherTap.overflowCount != 2) return 143;
    if (phoneLauncherTap.release(16, 0, 0, 0)) return 144;
    if (phoneLauncherTap.overflowCount != 1) return 145;
    if (phoneLauncherTap.release(16, 0, 0, 0)) return 146; // duplicate
    if (phoneLauncherTap.overflowCount != 1) return 147;
    for (int source = 0; source < 16; ++source)
        if (phoneLauncherTap.release(source, 0, 0, 0)) return 148;
    phoneLauncherTap.begin(2, 50);
    if (phoneLauncherTap.release(50, 2, 0, 0)) return 149;
    if (phoneLauncherTap.release(17, 0, 0, 0)) return 150;
    phoneLauncherTap.begin(3, 7);
    if (!phoneLauncherTap.release(7, 3, 0, 0)) return 151;

    // Beyond 48 contacts there is no room for another source ID. Keep all
    // phone panel launchers disabled until an explicit cancel/rotation reset;
    // unknown releases must not accidentally clear that safety latch.
    for (int source = 0; source < 49; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    // Execute overflow-state checks in Release/NDEBUG too.
    if (phoneLauncherTap.activeCount != 16) return 152;
    if (phoneLauncherTap.overflowCount != 32) return 153;
    if (!phoneLauncherTap.untrackedOverflow) return 154;
    for (int source = 0; source < 49; ++source)
        if (phoneLauncherTap.release(source, 1, 0, 0)) return 155;
    if (!phoneLauncherTap.untrackedOverflow) return 156;
    if (phoneLauncherTap.release(999, 1, 0, 0)) return 157;
    phoneLauncherTap.begin(2, 77);
    if (phoneLauncherTap.release(77, 2, 0, 0)) return 158;
    phoneLauncherTap.invalidate(); // OS cancellation or orientation reset
    if (phoneLauncherTap.untrackedOverflow) return 159;
    phoneLauncherTap.begin(3, 7);
    if (!phoneLauncherTap.release(7, 3, 0, 0)) return 160;

    // Interleaved primary/overflow releases must not lose pointer identity.
    // These state transitions must execute even in Release/NDEBUG builds.
    // Reusing a freed primary slot while overflow fingers remain down must
    // never re-arm a floating-panel launcher.
    for (int source = 0; source < 18; ++source)
        phoneLauncherTap.begin(source == 0 ? 1 : 0, source);
    if (phoneLauncherTap.activeCount != 16) return 161;
    if (phoneLauncherTap.overflowCount != 2) return 162;
    if (phoneLauncherTap.release(3, 1, 0, 0)) return 163;
    phoneLauncherTap.begin(2, 100); // reuse the freed primary slot
    if (phoneLauncherTap.activeCount != 16) return 164;
    if (phoneLauncherTap.overflowCount != 2) return 165;
    if (phoneLauncherTap.release(999, 2, 0, 0)) return 166; // unknown release
    if (phoneLauncherTap.overflowCount != 2) return 167;
    if (phoneLauncherTap.release(17, 2, 0, 0)) return 168;
    if (phoneLauncherTap.overflowCount != 1) return 169;
    if (phoneLauncherTap.release(17, 2, 0, 0)) return 170; // duplicate release
    if (phoneLauncherTap.overflowCount != 1) return 171;
    for (int source = 0; source < 18; ++source)
        if (source != 3 && source != 17)
            if (phoneLauncherTap.release(source, 1, 0, 0)) return 172;
    if (phoneLauncherTap.release(100, 2, 0, 0)) return 173;
    if (phoneLauncherTap.activeCount != 0) return 174;
    if (phoneLauncherTap.overflowCount != 0) return 175;
    phoneLauncherTap.begin(2, 100);
    if (!phoneLauncherTap.release(100, 2, 0, 0)) return 176;

    // A cancelled swipe cannot be re-armed by another finger.
    phoneLauncherTap.begin(1, 7);
    phoneLauncherTap.move(7, 20, 0);
    phoneLauncherTap.begin(2, 8);
    if (phoneLauncherTap.release(7, 1, 0, 0)) return 177;
    if (phoneLauncherTap.release(8, 2, 0, 0)) return 178;


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
            // Preserve these Mixer header bounds checks in Release/NDEBUG.
            if (!inside(energy, DAW::mobile::maxInt(0, w), 32))
                return 98;
            if (energy.x < DAW::mobile::minInt(60, w))
                return 99;
            if (static_cast<long long>(energy.x) + energy.width >
                DAW::mobile::maxInt(energy.x, toolbarStart))
                return 100;
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
                // Keep paint clipping checks active in Release/NDEBUG.
                if (!inside(clip, DAW::mobile::maxInt(0, w),
                                  DAW::mobile::maxInt(0, h)))
                    return 101;
                if (clip.x != DAW::mobile::clampInt(
                        start, 0, DAW::mobile::maxInt(0, w)))
                    return 102;
                if (static_cast<long long>(clip.x) + clip.width
                    != DAW::mobile::maxInt(0, w))
                    return 103;
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
    // Release/NDEBUG must exercise every Mixer toolbar geometry check.
    for (int w = 0; w <= 500; ++w)
        for (int h : {0, 20, 28, 31, 32, 48})
        {
            const auto layout = phoneMixerToolbarLayout(w, h);
            if (layout.count < 0 || layout.count > 3)
                return 104;
            for (int i = 0; i < layout.count; ++i)
            {
                const auto b = layout.buttons[i];
                if (!inside(b.bounds, w, h))
                    return 105;
                if (b.bounds.width != 44 || b.bounds.height != 32)
                    return 106;
                if (b.bounds.y < 0 || static_cast<long long>(b.bounds.y) + b.bounds.height > h)
                    return 107;
                if (i > 0)
                {
                    const auto previous = layout.buttons[i - 1].bounds;
                    if (static_cast<long long>(previous.x) + previous.width >= b.bounds.x)
                        return 108;
                }
            }
            if (layout.count > 0 && layout.count < 3)
                if (layout.buttons[layout.count - 1].action != phoneMixerOverflow)
                    return 109;
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
                    // Keep rotation/inset geometry checks active under NDEBUG.
                    if (!inside(fitted, w, h))
                        return 115;
                    if (fitted.height > 0)
                    {
                        if (fitted.y < safeTop)
                            return 116;
                        if (static_cast<long long>(fitted.y) + fitted.height > h - safeBottom)
                            return 117;
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
                if (!inside(fitted, w, h))
                    return 216;
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
                if (!inside(fitted, w, h))
                    return 217;
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
            if (!inside(fitted, w, h))
                return 218;
            if (fitted.height > 0)
                if (static_cast<long long>(fitted.y) + fitted.height
                    > h - DAW::mobile::clampInt(96, 0,
                        h - DAW::mobile::clampInt(50, 0, h)))
                    return 219;
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
            // Execute the full swipe/tap grid in Release/NDEBUG as well.
            if (pluginBrowserRowTapActivates(true, dx, dy, true) != expected)
                return 110;
            if (pluginBrowserRowTapActivates(true, dx, dy, false))
                return 111;
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
    if (phoneRowTapEligible) return 201;
    if (pluginBrowserRowTapActivates(true, 0, 0, phoneRowTapEligible)) return 202;
    bool phoneButtonTapEligible = true;
    phoneButtonTapEligible = phoneTapGestureRemainsEligible(phoneButtonTapEligible, 0, 30);
    phoneButtonTapEligible = phoneTapGestureRemainsEligible(phoneButtonTapEligible, 0, 0);
    if (phoneButtonTapEligible) return 203;
    if (phoneButtonTapEligible && DAW::mobile::phoneChromeButtonTapActivates(1, 1, 0, 0)) return 204;
    // A short stationary touch remains eligible.
    bool shortPhoneTap = phoneTapGestureRemainsEligible(true, 4, 3);
    shortPhoneTap = phoneTapGestureRemainsEligible(shortPhoneTap, 0, 0);
    if (!shortPhoneTap) return 205;
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
            if (phoneChromeButtonTapActivates(1, 1, dx, dy) != tap) return 206;
            if (phoneChromeButtonTapActivates(1, 2, dx, dy)) return 207;
        }
    // Regression: a phone rotation during a pressed chrome control must
    // never close/minimize/maximize the newly repositioned floating window.
    using DAW::mobile::PhoneChromeTapState;
    PhoneChromeTapState chromeTap;
    if (chromeTap.release(1, 0, 0)) return 69; // No press.
    chromeTap.begin(1);
    if (!chromeTap.release(1, 0, 0)) return 70; // Stationary close.
    if (chromeTap.release(1, 0, 0)) return 71; // Single activation.
    chromeTap.begin(2);
    chromeTap.move(3, 4);
    if (!chromeTap.release(2, 3, 4)) return 72; // Short movement.
    chromeTap.begin(3);
    chromeTap.move(12, 0);
    chromeTap.move(0, 0);
    if (chromeTap.release(3, 0, 0)) return 73; // Swipe and return.
    chromeTap.begin(1);
    chromeTap.invalidate(); // Parent resized on rotation.
    if (chromeTap.release(1, 0, 0)) return 179;
    chromeTap.begin(3);
    if (chromeTap.release(1, 0, 0)) return 180; // Different button.
    chromeTap.begin(2);
    chromeTap.move(std::numeric_limits<int>::min(),
                   std::numeric_limits<int>::max());
    if (chromeTap.release(2, 0, 0)) return 181; // Extreme drag.
    chromeTap.begin(3);
    if (!chromeTap.release(3, 0, 0)) return 182; // Fresh press after rotation.

    // MixerWindow's phone toolbar uses IDs + 1 (Settings is ID 0), so
    // pressing Settings cannot be confused with an absent touch. All
    // toolbar actions must wait for release; a swipe/rotation cancels them.
    PhoneChromeTapState mixerToolbar;
    if (mixerToolbar.release(1, 0, 0)) return 188;
    mixerToolbar.begin(1); // Settings (toolbar ID 0 + 1).
    if (!mixerToolbar.release(1, 0, 0)) return 189;
    if (mixerToolbar.release(1, 0, 0)) return 190; // Exactly one action.
    mixerToolbar.begin(8); // Overflow.
    if (mixerToolbar.release(7, 0, 0)) return 191; // Different action.
    mixerToolbar.begin(6); // Bubblegum.
    mixerToolbar.move(16, 0);
    mixerToolbar.move(0, 0);
    if (mixerToolbar.release(6, 0, 0)) return 192; // Drag out and back.
    mixerToolbar.begin(2);
    mixerToolbar.invalidate(); // Mixer rotated or resized.
    if (mixerToolbar.release(2, 0, 0)) return 193;
    mixerToolbar.begin(8);
    if (mixerToolbar.release(0, 0, 0)) return 194; // Lift outside.
    mixerToolbar.begin(8);
    mixerToolbar.move(4, 3);
    if (!mixerToolbar.release(8, 4, 3)) return 195; // Legitimate short tap.

    // Left Mixer minimize/maximize use distinct IDs above toolbar IDs.
    // Swipe-out-and-back, releasing over the other button, and rotation
    // must all suppress the action; a stationary release fires once.
    PhoneChromeTapState mixerLeftChrome;
    mixerLeftChrome.begin(101); // Minimize.
    if (mixerLeftChrome.release(102, 0, 0)) return 196; // Released on maximize.
    mixerLeftChrome.begin(102);
    mixerLeftChrome.move(0, 12);
    mixerLeftChrome.move(0, 0);
    if (mixerLeftChrome.release(102, 0, 0)) return 197;
    mixerLeftChrome.begin(101);
    mixerLeftChrome.invalidate(); // Orientation changed.
    if (mixerLeftChrome.release(101, 0, 0)) return 198;
    mixerLeftChrome.begin(102);
    if (!mixerLeftChrome.release(102, 0, 0)) return 199;
    if (mixerLeftChrome.release(102, 0, 0)) return 200;

    // Bubblegum Sends uses this same state for its phone close orb. A
    // scroll/drag that returns to the orb must not close the Sends panel;
    // rotation or safe-inset changes invalidate an outstanding close press.
    PhoneChromeTapState sendsClose;
    sendsClose.begin(1);
    sendsClose.move(0, 9);
    sendsClose.move(0, 0);
    if (sendsClose.release(1, 0, 0))
        return 183;
    sendsClose.begin(1);
    if (sendsClose.release(0, 0, 0)) // Finger lifted outside orb.
        return 184;
    sendsClose.begin(1);
    sendsClose.invalidate(); // Parent resized or safe insets changed.
    if (sendsClose.release(1, 0, 0))
        return 185;
    sendsClose.begin(1);
    sendsClose.move(6, 4);
    if (!sendsClose.release(1, 6, 4)) // Fresh stationary phone tap.
        return 186;
    if (sendsClose.release(1, 0, 0)) // No duplicate close.
        return 187;

    // Phone Transport panel launchers must not open Mixer/Sends/FX/Browser
    // on swipe, on release over a different launcher, or after a drag that
    // returns to its origin. Desktop launchers are intentionally unchanged.
    using DAW::mobile::phoneTransportLauncherTapActivates;
    for (int launcher = 1; launcher <= 4; ++launcher)
    {
        if (!phoneTransportLauncherTapActivates(launcher, launcher, 0, 0)) return 208;
        if (!phoneTransportLauncherTapActivates(launcher, launcher, 6, 5)) return 209;
        if (phoneTransportLauncherTapActivates(launcher, launcher, 9, 0)) return 210;
        if (phoneTransportLauncherTapActivates(launcher, launcher, 8, 8)) return 211;
        if (phoneTransportLauncherTapActivates(launcher, 0, 0, 0)) return 212;
        if (phoneTransportLauncherTapActivates(0, launcher, 0, 0)) return 213;
        if (phoneTransportLauncherTapActivates(launcher, launcher % 4 + 1, 0, 0)) return 214;
        bool eligible = DAW::mobile::phoneTapGestureRemainsEligible(true, 20, 0);
        eligible = DAW::mobile::phoneTapGestureRemainsEligible(eligible, 0, 0);
        if (eligible || (eligible && phoneTransportLauncherTapActivates(
            launcher, launcher, 0, 0))) return 215;
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
            if (chromeTitleDoubleClickMaximizes(phone, overButton)
                != (!phone && !overButton))
                return 220;
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
                if (listWidth < 0 || listWidth > width)
                    return 221;
                if (width > 0 && height > 0 && rows * 44 <= height)
                    if (listWidth != width)
                        return 222;
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
        if (side < 0 || side > 44 || 3 * side > w)
            return 223;
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
                    if (!inside(fit, w, h))
                        return 224;
                    if (fit.height > 0)
                    {
                        if (fit.y < safeTop)
                            return 225;
                        if (static_cast<long long>(fit.y) + fit.height > h - safeBottom)
                            return 226;
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
                        if (!(fit.width == 0 && fit.height == 0))
                            return 227;
                        continue;
                    }
                    const int reservedTop = DAW::mobile::clampInt(top, 0, h);
                    const int reservedBottom = DAW::mobile::clampInt(bottom, 0, h - reservedTop);
                    if (!(inside(fit, w, h)))
                        return 228;
                    if (!(fit.y >= reservedTop))
                        return 229;
                    if (!(fit.y + fit.height <= h - reservedBottom))
                        return 230;
                    if (!(fit.width <= 340 && fit.height <= 560))
                        return 231;
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
        if (!(width >= 0 && width <= 220))
            return 232;
        if (!(width < workspace || workspace <= 1))
            return 233;
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
                        if (fit.window.width != 0 || fit.window.height != 0)
                            return 201;
                        continue;
                    }
                    if (!inside(fit.safe, w, h)) return 202;
                    if (!inside(fit.window, w, h)) return 203;
                    if (fit.window.x < fit.safe.x || fit.window.y < fit.safe.y) return 204;
                    if (static_cast<long long>(fit.window.x) + fit.window.width > static_cast<long long>(fit.safe.x) + fit.safe.width) return 205;
                    if (static_cast<long long>(fit.window.y) + fit.window.height > static_cast<long long>(fit.safe.y) + fit.safe.height) return 206;
                    if (fit.minimumWidth > fit.window.width) return 207;
                    if (fit.minimumHeight > fit.window.height) return 208;
                    // Arbitrary out-of-bounds restored positions must
                    // recover to a usable window without losing a fitting size.
                    for (int offset : { -1000000, -1, 0, 1000000 })
                    {
                        const auto moved = constrainFloatingMixerPlacement(
                            fit, { offset, -offset, fit.window.width, fit.window.height });
                        if (!floatingMixerFits(fit, moved)) return 209;
                        if (moved.width != fit.window.width
                            || moved.height != fit.window.height) return 210;
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
                        if (!(fit.window.width == 0 && fit.window.height == 0))
                            return 234;
                        continue;
                    }
                    if (!(inside(fit.safe, w, h)))
                        return 235;
                    if (!(inside(fit.window, w, h)))
                        return 236;
                    if (!(fit.window.y >= fit.safe.y))
                        return 237;
                    if (!(fit.window.y + fit.window.height <= fit.safe.y + fit.safe.height))
                        return 238;
                    if (!(phoneClipPropertiesFits(fit, fit.window)))
                        return 239;
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
            // Keep this restored-plugin-browser rotation sweep active with NDEBUG.
            if (placed.width != 340 || placed.height != 138)
                return 65;
            if (placed.x < 0 || placed.y < 50)
                return 66;
            if (static_cast<long long>(placed.x) + placed.width > 852)
                return 67;
            if (static_cast<long long>(placed.y) + placed.height > 204)
                return 68;
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
                // Keep multi-panel rotation checks active in Release/NDEBUG.
                if (browser.x < 0 || browser.y < top)
                    return 52;
                if (static_cast<long long>(browser.y) + browser.height > h - bottom)
                    return 53;
                if (!inside(browser, w, h))
                    return 54;
                if (!inside(sends, w, h))
                    return 55;
                if (!inside(clip.window, w, h))
                    return 56;
                if (sends.height > 0
                    && (sends.y < top
                        || static_cast<long long>(sends.y) + sends.height > h - bottom))
                    return 57;
                if (mixer.safe.width > 0 && mixer.safe.height > 0
                    && !floatingMixerFits(mixer, mixer.window))
                    return 58;
                if (clip.safe.width > 0 && clip.safe.height > 0
                    && !phoneClipPropertiesFits(clip, clip.window))
                    return 59;
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

    // Three floating-window chrome controls share a narrow phone title bar.
    // Sweep every width around the 132-unit full-size threshold: rounding
    // must never let their combined hit regions exceed the panel.
    for (int width = 0; width <= 200; ++width)
    {
        const int control = DAW::mobile::phoneChromeControlSize(width);
        if (control < 0 || control > 44)
            return 261;
        if (3LL * control > width)
            return 262;
        if (width >= 132 && control != 44)
            return 263;
        if (width < 3 && control != 0)
            return 264;
    }

    // A one-pixel portrait resize must never make floating-window chrome
    // controls shrink or jump backward. This protects drag/close hit regions
    // while Android system insets change incrementally.
    int previousChromeSize = 0;
    for (int width = 0; width <= 200; ++width)
    {
        const int size = DAW::mobile::phoneChromeControlSize(width);
        if (size < previousChromeSize)
            return 265;
        if (width >= 132 && size != previousChromeSize && width > 132)
            return 266;
        previousChromeSize = size;
    }

    // FX row and Transport launcher share the same circular tap threshold.
    // Exhaust the +/-9 boundary after a portrait/landscape transition:
    // diagonal drags outside radius 8 must never activate a plugin.
    for (int dx = -9; dx <= 9; ++dx)
        for (int dy = -9; dy <= 9; ++dy)
        {
            const bool expected = dx * dx + dy * dy <= 64;
            if (DAW::mobile::pluginBrowserRowTapActivates(true, dx, dy, true)
                != expected)
                return 267;
            if (DAW::mobile::phoneTransportLauncherTapActivates(1, 1, dx, dy)
                != expected)
                return 268;
            if (DAW::mobile::pluginBrowserRowTapActivates(true, dx, dy, false))
                return 269;
        }
    // A swipe that leaves the circle and returns to its origin is not a tap.
    // This guards against accidental plugin loading after scrolling.
    for (int dx : { -9, 9 })
        for (int dy : { -9, 9 })
        {
            bool eligible = DAW::mobile::phoneTapGestureRemainsEligible(true, dx, dy);
            eligible = DAW::mobile::phoneTapGestureRemainsEligible(eligible, 0, 0);
            if (eligible)
                return 270;
        }

    // Rotation and split-screen can reduce the floating Mixer's title zone
    // below its 32-unit touch target. Never expose a partially clipped
    // button; once the zone recovers, every action must fit completely.
    for (int zoneWidth : { 0, 45, 46, 93, 94, 141, 142, 393, 852 })
        for (int zoneHeight : { 0, 1, 31, 32, 33, 48, 96 })
        {
            const auto layout = DAW::mobile::phoneMixerToolbarLayout(
                zoneWidth, zoneHeight);
            if ((zoneWidth < 46 || zoneHeight < 32) && layout.count != 0)
                return 248;
            for (int index = 0; index < layout.count; ++index)
            {
                const auto button = layout.buttons[index].bounds;
                if (!inside(button, zoneWidth, zoneHeight))
                    return 249;
                if (button.width != 44 || button.height != 32)
                    return 250;
            }
        }
    // Track-list/Arrangement split during one-unit portrait resizes:
    // reserve a positive timeline whenever the workspace can display both.
    // A restored oversized track width must never consume the Arrangement.
    for (int workspace = 0; workspace <= 512; ++workspace)
        for (int requested : { -1, 0, 1, 108, 256, 512, 1000000 })
        {
            const int track = DAW::mobile::phoneTrackListWidth(requested, workspace);
            if (track < 0 || (track > requested && requested >= 0))
                return 271;
            if (workspace <= 1 || requested <= 0)
            {
                if (track != 0)
                    return 272;
            }
            else if (track >= workspace || track <= 0)
                return 273;
        }
    // Restored track widths must not grow as a narrow split view shrinks.
    for (int requested : { 1, 108, 256, 1000000 })
    {
        int previous = 0;
        for (int workspace = 0; workspace <= 512; ++workspace)
        {
            const int track = DAW::mobile::phoneTrackListWidth(requested, workspace);
            if (track < previous)
                return 274;
            previous = track;
        }
    }

    // A corrupted/restored Track List width can be INT_MAX while Android
    // reports a transient one-unit viewport during rotation. The timeline
    // must remain reachable and the result must never overflow signed int.
    for (int workspace : { -1, 0, 1, 2, 3, 108, 393, 852,
                           std::numeric_limits<int>::max() })
        for (int requested : { std::numeric_limits<int>::min(), -1, 0, 1,
                               108, std::numeric_limits<int>::max() })
        {
            const int track = DAW::mobile::phoneTrackListWidth(requested, workspace);
            if (track < 0 || (requested > 0 && track > requested))
                return 275;
            if (workspace <= 1 || requested <= 0)
            {
                if (track != 0)
                    return 276;
            }
            else if (track <= 0 || track >= workspace)
                return 277;
        }
    // A restored enormous Track List must not hide the timeline on the
    // largest representable viewport (avoid overflowing workspace - track).
    const int hugeWorkspace = std::numeric_limits<int>::max();
    const int hugeTrack = DAW::mobile::phoneTrackListWidth(hugeWorkspace,
                                                            hugeWorkspace);
    if (static_cast<long long>(hugeWorkspace) - hugeTrack < 1)
        return 278;


    // Android can briefly report extreme window bounds during orientation
    // changes. The floating Mixer toolbar must not overflow or create
    // phantom controls when its available zone is malformed.
    for (int zoneWidth : { std::numeric_limits<int>::min(), -1, 0, 1,
                           45, 46, 93, 94, 141, 142,
                           std::numeric_limits<int>::max() })
        for (int zoneHeight : { std::numeric_limits<int>::min(), -1, 0,
                                31, 32, 33, std::numeric_limits<int>::max() })
        {
            const auto layout = DAW::mobile::phoneMixerToolbarLayout(
                zoneWidth, zoneHeight);
            if (layout.count < 0 || layout.count > 4)
                return 279;
            if ((zoneWidth < 46 || zoneHeight < 32) && layout.count != 0)
                return 280;
            for (int index = 0; index < layout.count; ++index)
            {
                const auto bounds = layout.buttons[index].bounds;
                if (!inside(bounds, zoneWidth, zoneHeight))
                    return 281;
                if (bounds.width != 44 || bounds.height != 32)
                    return 282;
            }
        }


    // Restored Track List widths can change one unit at a time while the
    // Arrangement is live. Increasing the requested width must never shrink
    // the visible list, overflow the viewport, or erase the timeline.
    for (int workspace : { 0, 1, 2, 3, 46, 108, 393, 852 })
    {
        int previousTrack = 0;
        for (int requested = 0; requested <= 512; ++requested)
        {
            const int track = DAW::mobile::phoneTrackListWidth(requested, workspace);
            if (track < previousTrack)
                return 287;
            if (track < 0 || track > requested)
                return 288;
            if (workspace > 1 && requested > 0 && track >= workspace)
                return 289;
            previousTrack = track;
        }
    }

    // Rotating a phone must not change Mixer toolbar actions merely because
    // the title region becomes taller. Buttons stay vertically centered and
    // the horizontal hit map remains identical across title heights.
    for (int width : { 0, 45, 46, 93, 94, 141, 142, 393, 852 })
    {
        const auto baseline = DAW::mobile::phoneMixerToolbarLayout(width, 32);
        for (int height : { 32, 33, 47, 48, 96, 852 })
        {
            const auto layout = DAW::mobile::phoneMixerToolbarLayout(width, height);
            if (layout.count != baseline.count)
                return 283;
            for (int index = 0; index < layout.count; ++index)
            {
                const auto bounds = layout.buttons[index].bounds;
                const auto original = baseline.buttons[index];
                if (layout.buttons[index].action != original.action)
                    return 284;
                if (bounds.x != original.bounds.x
                    || bounds.width != original.bounds.width)
                    return 285;
                if (bounds.y != (height - bounds.height) / 2
                    || !inside(bounds, width, height))
                    return 286;
            }
        }
    }

    // FX browser scrollbar: resizing by one logical unit must never
    // increase the usable row width, then decrease it again. The sole
    // transition is from a reserved scrollbar to the full row width.
    // Check exact-fit boundaries and malformed row counts in Release too.
    for (int rows : { -1, 0, 1, 2, 3, 44, 1000000 })
        for (int rowHeight : { -1, 0, 1, 22, 44, 1000000 })
        {
            int previousWidth = 0;
            for (int viewportHeight = 0; viewportHeight <= 256; ++viewportHeight)
            {
                const int usable = DAW::mobile::pluginBrowserListWidth(
                    393, viewportHeight, rows, rowHeight, 12);
                if (usable < 0 || usable > 393)
                    return 290;
                if (viewportHeight > 0 && usable < previousWidth)
                    return 291;
                if (viewportHeight > 0 && rows > 0 && rowHeight > 0)
                {
                    const bool overflow = rows > viewportHeight / rowHeight;
                    if (usable != (overflow ? 381 : 393))
                        return 292;
                }
                previousWidth = usable;
            }
        }

    // FX browser scrollbar geometry must remain bounded when Android
    // supplies malformed dimensions or a corrupted saved scrollbar width.
    // An exact-fit list must not reserve a scrollbar.
    for (int width : { std::numeric_limits<int>::min(), -1, 0, 1,
                       11, 12, 44, 393, std::numeric_limits<int>::max() })
        for (int height : { -1, 0, 1, 43, 44, 45,
                            std::numeric_limits<int>::max() })
            for (int thickness : { std::numeric_limits<int>::min(), -1, 0,
                                   1, 12, 393, std::numeric_limits<int>::max() })
            {
                const int usable = DAW::mobile::pluginBrowserListWidth(
                    width, height, 1, 44, thickness);
                if (usable < 0 || (width >= 0 && usable > width))
                    return 293;
                if (width <= 0 || height <= 0)
                {
                    if (usable != 0)
                        return 294;
                }
                else if (height >= 44 && usable != width)
                    return 295;
                else if (height < 44)
                {
                    const int reserved = thickness <= 0 ? 0
                        : (thickness > width ? width : thickness);
                    if (usable != width - reserved)
                        return 296;
                }
            }

    // A split-screen resize changes the FX browser width independently of
    // its row count. The scrollbar must consume only its fixed thickness,
    // never produce a shrinking hit target as the viewport grows.
    for (int height : { 1, 43, 44, 45, 393 })
    {
        int previous = 0;
        for (int width = 0; width <= 512; ++width)
        {
            const int usable = DAW::mobile::pluginBrowserListWidth(
                width, height, 2, 44, 12);
            if (usable < previous || usable > width)
                return 297;
            const int reserved = height < 88 ? (width < 12 ? width : 12) : 0;
            if (usable != width - reserved)
                return 298;
            previous = usable;
        }
    }


    // FX browser: adding plugin rows cannot increase the touch width.
    // At the exact overflow threshold the scrollbar appears once, and
    // further additions must leave the hit target unchanged.
    for (int width : { 1, 11, 12, 44, 393 })
        for (int height : { 1, 43, 44, 45, 88, 393 })
        {
            int previous = width;
            for (int rows = 0; rows <= 32; ++rows)
            {
                const int usable = DAW::mobile::pluginBrowserListWidth(
                    width, height, rows, 44, 12);
                if (usable < 0 || usable > previous)
                    return 299;
                const bool overflow = rows > height / 44;
                const int reserved = overflow ? (width < 12 ? width : 12) : 0;
                if (usable != width - reserved)
                    return 300;
                previous = usable;
            }
        }


    // FX row-height changes (font scaling and portrait/landscape) must
    // never widen a touch target as rows become taller. Exact fit is not
    // overflow, and malformed nonpositive heights must not reserve space.
    for (int width : { 1, 11, 12, 44, 393 })
        for (int viewport : { 1, 43, 44, 88, 393 })
            for (int rows : { -1, 0, 1, 2, 9, 1000000 })
            {
                int previous = width;
                for (int rowHeight = 0; rowHeight <= 128; ++rowHeight)
                {
                    const int usable = DAW::mobile::pluginBrowserListWidth(
                        width, viewport, rows, rowHeight, 12);
                    if (usable < 0 || usable > width || usable > previous)
                        return 301;
                    const bool overflow = rows > 0 && rowHeight > 0
                        && rows > viewport / rowHeight;
                    const int reserved = overflow ? (width < 12 ? width : 12) : 0;
                    if (usable != width - reserved)
                        return 302;
                    previous = usable;
                }
            }

    // FX Browser placement after phone rotation: corrupted saved origins
    // must be clamped to the safe workspace without changing panel size.
    // A maximized browser must ignore its stale dragged coordinates.
    for (int width : { 1, 2, 44, 320, 393, 852 })
        for (int height : { 1, 44, 128, 480, 852 })
            for (int origin : { std::numeric_limits<int>::min(), -1000,
                                0, 1000, std::numeric_limits<int>::max() })
            {
                const auto expected = DAW::mobile::fitPhonePluginBrowser(
                    width, height, 44, 64);
                const DAW::mobile::PanelBounds restored{
                    origin, origin, 340, 560 };
                const auto fitted = DAW::mobile::fitPhonePluginBrowserPlacement(
                    width, height, 44, 64, restored);
                if (fitted.width != expected.width
                    || fitted.height != expected.height)
                    return 303;
                if (fitted.width > 0 && fitted.height > 0
                    && !inside(fitted, width, height))
                    return 304;
                const auto maximized = DAW::mobile::fitPhonePluginBrowserPlacement(
                    width, height, 44, 64, restored, true);
                const auto maxExpected = DAW::mobile::fitPhonePluginBrowser(
                    width, height, 44, 64, true);
                if (maximized.x != maxExpected.x
                    || maximized.y != maxExpected.y
                    || maximized.width != maxExpected.width
                    || maximized.height != maxExpected.height)
                    return 305;
            }
    // Repeated layout passes after orientation settle must be idempotent.
    for (int width : { 320, 393, 852 })
        for (int height : { 480, 852 })
        {
            const DAW::mobile::PanelBounds stale{ 1000000, -1000000, 340, 560 };
            const auto once = DAW::mobile::fitPhonePluginBrowserPlacement(
                width, height, 44, 64, stale);
            const auto twice = DAW::mobile::fitPhonePluginBrowserPlacement(
                width, height, 44, 64, once);
            if (once.x != twice.x || once.y != twice.y
                || once.width != twice.width || once.height != twice.height)
                return 306;
        }

    // A rotated FX browser must never cover the phone header or persistent
    // Transport. Verify safe-area containment, not merely viewport bounds.
    for (int width : { 1, 44, 320, 393, 852 })
        for (int height : { 1, 44, 128, 480, 852 })
            for (int top : { -1, 0, 44, 1000 })
                for (int bottom : { -1, 0, 64, 1000 })
                {
                    const DAW::mobile::PanelBounds stale{
                        std::numeric_limits<int>::max(),
                        std::numeric_limits<int>::min(), 340, 560 };
                    const auto fitted = DAW::mobile::fitPhonePluginBrowserPlacement(
                        width, height, top, bottom, stale);
                    const int safeTop = DAW::mobile::clampInt(top, 0, height);
                    const int safeBottom = DAW::mobile::clampInt(
                        bottom, 0, height - safeTop);
                    if (fitted.width > 0 && fitted.height > 0)
                    {
                        if (fitted.y < safeTop)
                            return 307;
                        if (static_cast<long long>(fitted.y) + fitted.height
                            > height - safeBottom)
                            return 308;
                        if (!inside(fitted, width, height))
                            return 309;
                    }
                }
    // Restored FX browser dimensions can be zero or corrupt after process
    // recreation. They must fall back to responsive placement, not retain a
    // stale origin; valid sizes should settle after a second layout pass.
    for (int width : { 1, 44, 320, 393, 852 })
        for (int height : { 1, 44, 128, 480, 852 })
            for (int size : { std::numeric_limits<int>::min(), -1, 0,
                              1, 340, std::numeric_limits<int>::max() })
            {
                const DAW::mobile::PanelBounds restored{
                    std::numeric_limits<int>::max(),
                    std::numeric_limits<int>::min(), size, size };
                const auto fitted = DAW::mobile::fitPhonePluginBrowserPlacement(
                    width, height, 44, 64, restored);
                const auto fallback = DAW::mobile::fitPhonePluginBrowser(
                    width, height, 44, 64);
                if (size <= 0 && (fitted.x != fallback.x
                    || fitted.y != fallback.y || fitted.width != fallback.width
                    || fitted.height != fallback.height))
                    return 310;
                if (fitted.width > 0 && fitted.height > 0
                    && !inside(fitted, width, height))
                    return 311;
                const auto settled = DAW::mobile::fitPhonePluginBrowserPlacement(
                    width, height, 44, 64, fitted);
                if (settled.x != fitted.x || settled.y != fitted.y
                    || settled.width != fitted.width
                    || settled.height != fitted.height)
                    return 312;
            }

    // Phone FX browser: safe-area changes from immersive mode or rotation
    // must never leak a maximized panel into the system/Transport regions.
    // Maximization must also be independent of a saved dragged origin.
    for (int width : { 1, 44, 320, 393, 852 })
        for (int height : { 1, 44, 128, 480, 852 })
            for (int top : { 0, 24, 44, 1000 })
                for (int bottom : { 0, 24, 64, 1000 })
                {
                    const DAW::mobile::PanelBounds stale{
                        std::numeric_limits<int>::min(),
                        std::numeric_limits<int>::max(), 1, 1 };
                    const auto maximized = DAW::mobile::fitPhonePluginBrowserPlacement(
                        width, height, top, bottom, stale, true);
                    const auto expected = DAW::mobile::fitPhonePluginBrowser(
                        width, height, top, bottom, true);
                    if (maximized.x != expected.x || maximized.y != expected.y
                        || maximized.width != expected.width
                        || maximized.height != expected.height)
                        return 313;
                    if (maximized.width > 0 && maximized.height > 0)
                    {
                        const int safeTop = DAW::mobile::clampInt(top, 0, height);
                        const int safeBottom = DAW::mobile::clampInt(
                            bottom, 0, height - safeTop);
                        if (!inside(maximized, width, height)
                            || maximized.y < safeTop
                            || static_cast<long long>(maximized.y) + maximized.height
                                > height - safeBottom)
                            return 314;
                    }
                }


    // Phone-only Sends safe-area changes must not alter the desktop/iPad
    // layout. Conversely, phone panels must stay clear of both reservations
    // even if a stale drag anchor lies far outside the rotated viewport.
    for (int width : { 1, 44, 320, 393, 852 })
        for (int height : { 1, 96, 300, 852 })
            for (int anchor : { std::numeric_limits<int>::min(), -1,
                                0, 1000, std::numeric_limits<int>::max() })
        {
            const auto desktop = fitBubblegumSends(
                width, height, anchor, anchor, false);
            for (int top : { -1, 0, 50, 1000 })
                for (int bottom : { -1, 0, 96, 1000 })
                {
                    const auto unchanged = fitBubblegumSends(
                        width, height, anchor, anchor, false, top, bottom);
                    if (unchanged.x != desktop.x || unchanged.y != desktop.y
                        || unchanged.width != desktop.width
                        || unchanged.height != desktop.height)
                        return 315;

                    const auto phone = fitBubblegumSends(
                        width, height, anchor, anchor, true, top, bottom);
                    const int safeTop = DAW::mobile::clampInt(top, 0, height);
                    const int safeBottom = DAW::mobile::clampInt(
                        bottom, 0, height - safeTop);
                    if (phone.width > 0 && phone.height > 0
                        && (!inside(phone, width, height)
                            || phone.y < safeTop
                            || static_cast<long long>(phone.y) + phone.height
                                > height - safeBottom))
                        return 316;
                }
        }


    // Phone Arrangement track-list width must never consume the last timeline
    // unit after a fold/unfold, rotation, or tiny split-screen resize.
    // Desktop/iPad layout does not use this phone-only helper.
    for (int workspace : { std::numeric_limits<int>::min(), -1, 0, 1,
                           2, 44, 108, 320, 393, 852,
                           std::numeric_limits<int>::max() })
        for (int requested : { std::numeric_limits<int>::min(), -1, 0,
                               1, 44, 108, 320, 852,
                               std::numeric_limits<int>::max() })
        {
            const int width = DAW::mobile::phoneTrackListWidth(
                requested, workspace);
            if (width < 0 || width > (requested > 0 ? requested : 0))
                return 317;
            if (workspace <= 1 ? width != 0
                : (width > workspace - 1))
                return 318;
            if (requested > 0 && workspace > 1 && width == 0)
                return 319;
        }

    // Phone Arrangement: expanding the available workspace must never shrink
    // the track-list hit region, nor consume the final timeline unit.
    // Check transitions across the 108-unit minimum and quarter-width rule.
    for (int requested : { 1, 44, 108, 320, 852,
                           std::numeric_limits<int>::max() })
    {
        int previous = 0;
        for (int workspace = 0; workspace <= 2048; ++workspace)
        {
            const int current = DAW::mobile::phoneTrackListWidth(
                requested, workspace);
            if (current < previous)
                return 320;
            if (current < 0 || current > requested
                || (workspace > 1 && current > workspace - 1))
                return 321;
            if (workspace <= 1 && current != 0)
                return 322;
            previous = current;
        }
    }


    // Phone floating Mixer: restored sizes and coordinates can be corrupt
    // after rotation/process recreation. Reject invalid sizes, preserve
    // manually sized valid windows, and stabilize after one layout pass.
    for (int viewportW : { 1, 44, 320, 393, 852 })
        for (int viewportH : { 1, 96, 300, 852 })
            for (int restoredSize : { std::numeric_limits<int>::min(),
                                      -1, 0, 1, 200, 500,
                                      std::numeric_limits<int>::max() })
            {
                const auto fit = DAW::mobile::fitFloatingMixer(
                    viewportW, viewportH, 44, 64);
                const DAW::mobile::PanelBounds stale{
                    std::numeric_limits<int>::max(),
                    std::numeric_limits<int>::min(),
                    restoredSize, restoredSize };
                const auto once = DAW::mobile::constrainFloatingMixerPlacement(
                    fit, stale);
                if (fit.safe.width <= 0 || fit.safe.height <= 0)
                {
                    if (once.width != 0 || once.height != 0)
                        return 323;
                    continue;
                }
                if (!DAW::mobile::floatingMixerFits(fit, once))
                    return 324;
                const bool validSize = restoredSize >= fit.minimumWidth
                    && restoredSize >= fit.minimumHeight
                    && restoredSize <= fit.safe.width
                    && restoredSize <= fit.safe.height;
                if (validSize && (once.width != restoredSize
                    || once.height != restoredSize))
                    return 325;
                const auto twice = DAW::mobile::constrainFloatingMixerPlacement(
                    fit, once);
                if (twice.x != once.x || twice.y != once.y
                    || twice.width != once.width || twice.height != once.height)
                    return 326;
            }

    // Phone Mixer: immersive/fullscreen inset changes and extreme restored
    // drag origins must not place the floating window over header/Transport.
    // Keep these runtime checks active in Release/NDEBUG.
    for (int viewportW : { 1, 44, 320, 393, 852, 2048 })
        for (int viewportH : { 1, 44, 96, 300, 852 })
            for (int top : { -1, 0, 24, 44, 1000,
                             std::numeric_limits<int>::max() })
                for (int bottom : { -1, 0, 24, 64, 1000,
                                    std::numeric_limits<int>::max() })
                {
                    const auto fit = DAW::mobile::fitFloatingMixer(
                        viewportW, viewportH, top, bottom);
                    const auto restored = DAW::mobile::constrainFloatingMixerPlacement(
                        fit, { std::numeric_limits<int>::min(),
                               std::numeric_limits<int>::max(), 200, 180 });
                    if (fit.safe.width <= 0 || fit.safe.height <= 0)
                    {
                        if (restored.width != 0 || restored.height != 0)
                            return 327;
                        continue;
                    }
                    if (!DAW::mobile::floatingMixerFits(fit, restored))
                        return 328;
                    const int safeTop = DAW::mobile::clampInt(top, 0, viewportH);
                    const int safeBottom = DAW::mobile::clampInt(
                        bottom, 0, viewportH - safeTop);
                    if (restored.y < safeTop
                        || static_cast<long long>(restored.y) + restored.height
                            > viewportH - safeBottom)
                        return 329;
                }
    // Phone Quick Send must never restore the desktop 260-high Mixer minimum:
    // that recovery would overlap the persistent Transport in landscape.
    // Conversely, desktop/iPad retain the historical recovery behavior.
    for (int currentHeight : { -1, 0, 1, 64, 179, 180, 259, 260, 261, 852 })
        for (int desktopMinimum : { 0, 1, 180, 260, 1000 })
        {
            if (DAW::mobile::quickSendNeedsDesktopMixerMinimum(
                    true, currentHeight, desktopMinimum))
                return 330;
            if (DAW::mobile::quickSendNeedsDesktopMixerMinimum(
                    false, currentHeight, desktopMinimum)
                != (currentHeight < desktopMinimum))
                return 331;
        }

    // A phone landscape viewport shorter than desktop's 260 minimum must
    // still constrain Quick Send's floating Mixer above its Transport.
    for (int width : { 320, 393, 852, 1280 })
        for (int height : { 96, 180, 220, 259 })
        {
            const auto fit = DAW::mobile::fitFloatingMixer(
                width, height, 44, 64);
            const auto window = DAW::mobile::constrainFloatingMixerPlacement(
                fit, { 0, 0, 800, 260 });
            if (fit.safe.width > 0 && fit.safe.height > 0
                && (!DAW::mobile::floatingMixerFits(fit, window)
                    || window.height > fit.safe.height))
                return 332;
        }


    // Clip FX host: immersive inset changes must keep the opening window
    // inside the usable phone region, even on near-zero split-screen sizes.
    for (int viewportW : { 1, 44, 180, 393, 852 })
        for (int viewportH : { 1, 44, 120, 480, 852 })
            for (int top : { -1, 0, 44, 1000 })
                for (int bottom : { -1, 0, 64, 1000 })
                {
                    const auto fit = DAW::mobile::fitPhoneClipProperties(
                        viewportW, viewportH, top, bottom);
                    if (fit.safe.width <= 0 || fit.safe.height <= 0)
                    {
                        if (fit.window.width != 0 || fit.window.height != 0)
                            return 333;
                        continue;
                    }
                    if (!DAW::mobile::phoneClipPropertiesFits(fit, fit.window))
                        return 334;
                    const int safeTop = DAW::mobile::clampInt(top, 0, viewportH);
                    const int safeBottom = DAW::mobile::clampInt(
                        bottom, 0, viewportH - safeTop);
                    if (fit.window.y < safeTop
                        || static_cast<long long>(fit.window.y) + fit.window.height
                            > viewportH - safeBottom)
                        return 335;
                }

    // Phone FX Browser: a stale drag position after portrait/landscape rotation
    // must be clamped, and a second layout pass must not shift the window.
    // Invalid restored sizes fall back to the same safe default.
    for (int width : { 1, 44, 180, 393, 852 })
        for (int height : { 1, 44, 120, 480, 852 })
            for (int top : { -1, 0, 44, 1000 })
                for (int bottom : { -1, 0, 64, 1000 })
            {
                const auto fitted = DAW::mobile::fitPhonePluginBrowser(
                    width, height, top, bottom);
                const auto once = DAW::mobile::fitPhonePluginBrowserPlacement(
                    width, height, top, bottom,
                    { std::numeric_limits<int>::max(),
                      std::numeric_limits<int>::min(), 340, 560 });
                if (!inside(once, width, height))
                    return 336;
                if (once.width != fitted.width || once.height != fitted.height)
                    return 337;
                const auto twice = DAW::mobile::fitPhonePluginBrowserPlacement(
                    width, height, top, bottom, once);
                if (once.x != twice.x || once.y != twice.y
                    || once.width != twice.width || once.height != twice.height)
                    return 338;
            }
    // A phone Transport gesture interrupted by orientation/fullscreen resize
    // must not open a stale floating Mixer/Sends/FX/Browser launcher. Reset
    // also clears a blocked multi-touch gesture before the next fresh tap.
    for (int launcher : { 1, 2, 3, 4 })
        for (int source : { 0, 7, std::numeric_limits<int>::max() })
        {
            DAW::mobile::PhoneTransportTapState taps;
            taps.begin(launcher, source);
            taps.invalidate(); // parent resized/rotated before mouseUp
            if (taps.release(source, launcher, 0, 0))
                return 339;
            taps.begin(launcher, source);
            taps.begin(launcher, source + (source == std::numeric_limits<int>::max() ? -1 : 1));
            taps.invalidate(); // multi-touch cancellation on recreation
            if (taps.release(source, launcher, 0, 0))
                return 340;
            taps.begin(launcher, source);
            if (!taps.release(source, launcher, 0, 0))
                return 341;
        }

    // Transport launcher must not fire on a release over a different
    // launcher, a foreign touch source, or a drag across the panel row.
    // All four phone launchers share this gesture policy.
    for (int launcher : { 1, 2, 3, 4 })
    {
        const int other = launcher == 4 ? 1 : launcher + 1;
        DAW::mobile::PhoneTransportTapState taps;
        taps.begin(launcher, 7);
        if (taps.release(7, other, 0, 0))
            return 342;
        taps.begin(launcher, 7);
        if (taps.release(8, launcher, 0, 0)
            || taps.release(7, launcher, 0, 0))
            return 343;
        taps.begin(launcher, 7);
        taps.move(7, 1000, 1000);
        if (taps.release(7, launcher, 1000, 1000))
            return 344;
        taps.begin(launcher, 7);
        if (!taps.release(7, launcher, 0, 0))
            return 345;
    }

    // Transport: overflow touch contacts must not re-arm a launcher while
    // any other finger remains down, even beyond the inline 16-source array.
    for (int count : { 2, 16, 17, 32, 48, 49, 64 })
    {
        DAW::mobile::PhoneTransportTapState taps;
        for (int source = 0; source < count; ++source)
            taps.begin(1, source);
        if (taps.release(0, 1, 0, 0))
            return 346;
        for (int source = count - 1; source >= 1; --source)
            if (taps.release(source, 1, 0, 0))
                return 347;
        // Untracked overflow deliberately requires a lifecycle reset.
        taps.invalidate();
        taps.begin(1, 0);
        if (!taps.release(0, 1, 0, 0))
            return 348;
    }


    // Repeated mouseDown from the same JUCE touch source is not a second
    // valid tap; an interrupted gesture cannot reopen Sends or Mixer.
    for (int launcher : { 1, 2, 3, 4 })
    {
        DAW::mobile::PhoneTransportTapState taps;
        taps.begin(launcher, 7);
        taps.begin(launcher, 7);
        if (taps.release(7, launcher, 0, 0))
            return 349;
        taps.begin(launcher, 7);
        if (!taps.release(7, launcher, 0, 0))
            return 350;

        // Once drag exceeds the tap slop, returning to origin must not
        // resurrect the original press after a layout/rotation gesture.
        taps.begin(launcher, 7);
        taps.move(7, 1000, 1000);
        taps.move(7, 0, 0);
        if (taps.release(7, launcher, 0, 0))
            return 351;
    }

    // A foreign touch move during a pending phone launcher tap must cancel
    // the gesture, not let a later release open Mixer/Sends/FX/Browser.
    // A fresh contact must still work after all pending fingers are gone.
    for (int launcher : { 1, 2, 3, 4 })
    {
        DAW::mobile::PhoneTransportTapState taps;
        taps.begin(launcher, 11);
        taps.move(12, 0, 0);
        if (taps.release(11, launcher, 0, 0))
            return 352;
        taps.begin(launcher, 12);
        if (!taps.release(12, launcher, 0, 0))
            return 353;

        // Reusing a touch ID on another launcher before mouseUp must not
        // change the active target or re-arm a cancelled gesture.
        taps.begin(launcher, 11);
        taps.begin(launcher == 4 ? 1 : launcher + 1, 11);
        if (taps.release(11, launcher, 0, 0))
            return 354;
    }

    // Phone launcher hit-slop boundary: movement at exactly eight logical
    // units is a tap; diagonal movement beyond radius eight is a drag.
    // Every launcher uses the same circular threshold, regardless of
    // orientation or the selected floating panel.
    for (int launcher : { 1, 2, 3, 4 })
    {
        for (int source : { 0, 7, 15 })
        {
            DAW::mobile::PhoneTransportTapState taps;
            taps.begin(launcher, source);
            taps.move(source, 8, 0);
            if (!taps.release(source, launcher, 8, 0))
                return 355;

            taps.begin(launcher, source);
            taps.move(source, 7, 5); // sqrt(74) > 8-unit tap radius
            if (taps.release(source, launcher, 0, 0))
                return 356;

            // A cancelled diagonal drag must not poison a fresh tap.
            taps.begin(launcher, source);
            taps.move(source, 0, -8);
            if (!taps.release(source, launcher, 0, -8))
                return 357;
        }
    }

    // Adversarial gesture coordinates: avoid treating an axis-aligned
    // boundary plus one orthogonal unit as an eligible panel launcher tap.
    // Extreme signed displacements must never overflow into a valid tap.
    for (int launcher : { 1, 2, 3, 4 })
    {
        for (int source : { 0, 7, 15 })
        {
            DAW::mobile::PhoneTransportTapState taps;
            taps.begin(launcher, source);
            taps.move(source, 8, 1);
            if (taps.release(source, launcher, 0, 0))
                return 358;

            taps.begin(launcher, source);
            taps.move(source, std::numeric_limits<int>::min(),
                      std::numeric_limits<int>::max());
            if (taps.release(source, launcher, 0, 0))
                return 359;

            // Rotation/recreation must discard a pending press, and a
            // subsequent fresh tap must remain operational.
            taps.begin(launcher, source);
            taps.invalidate();
            if (taps.release(source, launcher, 0, 0))
                return 360;
            taps.begin(launcher, source);
            if (!taps.release(source, launcher, 0, 0))
                return 361;
        }
    }

    // Arrangement: restored scroll offsets and wheel steps may originate
    // from different screen orientations. Clamp using a wide intermediate
    // instead of overflowing when a saved position is near INT_MIN/MAX.
    for (int content : { 0, 1, 320, 2048, std::numeric_limits<int>::max() })
        for (int viewport : { 0, 1, 320, 2048, std::numeric_limits<int>::max() })
            for (int current : { std::numeric_limits<int>::min(), -1, 0, 1,
                                 std::numeric_limits<int>::max() })
                for (int step : { std::numeric_limits<int>::min(), -1, 0, 1,
                                  std::numeric_limits<int>::max() })
                {
                    const int actual = DAW::mobile::timelineWheelClampedPosition(
                        current, content, viewport, step);
                    const long long limit = content > viewport && viewport > 0
                        ? static_cast<long long>(content) - viewport : 0;
                    const long long target = static_cast<long long>(current) + step;
                    const int expected = static_cast<int>(
                        target < 0 ? 0 : target > limit ? limit : target);
                    if (actual != expected)
                        return 362;
                }


    // Maximized phone FX Browser must ignore stale floating coordinates
    // after orientation or immersive-inset changes. Re-layout must remain
    // stable and never restore a desktop-sized window over the Transport.
    for (int width : { 1, 44, 180, 393, 852 })
        for (int height : { 1, 44, 120, 480, 852 })
            for (int top : { -1, 0, 44, 1000 })
                for (int bottom : { -1, 0, 64, 1000 })
                {
                    const auto expected = DAW::mobile::fitPhonePluginBrowser(
                        width, height, top, bottom, true);
                    const auto restored = DAW::mobile::fitPhonePluginBrowserPlacement(
                        width, height, top, bottom,
                        { std::numeric_limits<int>::min(),
                          std::numeric_limits<int>::max(), 340, 560 }, true);
                    if (restored.x != expected.x || restored.y != expected.y
                        || restored.width != expected.width
                        || restored.height != expected.height)
                        return 363;
                    if (!inside(restored, width, height))
                        return 364;
                    const auto again = DAW::mobile::fitPhonePluginBrowserPlacement(
                        width, height, top, bottom, restored, true);
                    if (again.x != restored.x || again.y != restored.y
                        || again.width != restored.width
                        || again.height != restored.height)
                        return 365;
                }


    // Floating (non-maximized) phone FX Browser: a restored desktop
    // position must clamp to the current safe workspace after rotation.
    // Reapplying the same layout must not cause position drift.
    for (int width : { 1, 44, 180, 393, 852 })
        for (int height : { 1, 44, 120, 480, 852 })
            for (int top : { -1, 0, 44, 1000 })
                for (int bottom : { -1, 0, 64, 1000 })
                {
                    const auto fitted = DAW::mobile::fitPhonePluginBrowser(
                        width, height, top, bottom);
                    const auto restored = DAW::mobile::fitPhonePluginBrowserPlacement(
                        width, height, top, bottom,
                        { std::numeric_limits<int>::max(),
                          std::numeric_limits<int>::min(), 340, 560 });
                    if (!inside(restored, width, height))
                        return 366;
                    if (restored.width != fitted.width
                        || restored.height != fitted.height)
                        return 367;
                    const auto again = DAW::mobile::fitPhonePluginBrowserPlacement(
                        width, height, top, bottom, restored);
                    if (again.x != restored.x || again.y != restored.y
                        || again.width != restored.width
                        || again.height != restored.height)
                        return 368;
                }

    // The floating FX Browser must remain below the phone header and
    // above the persistent Transport, not merely inside the viewport.
    // Negative/oversized insets are normalized independently per axis.
    for (int width : { 1, 44, 180, 393, 852 })
        for (int height : { 1, 44, 120, 480, 852 })
            for (int top : { -1, 0, 44, 1000 })
                for (int bottom : { -1, 0, 64, 1000 })
                {
                    const int safeTop = top < 0 ? 0 : top > height ? height : top;
                    const int remaining = height - safeTop;
                    const int safeBottom = bottom < 0 ? 0
                        : bottom > remaining ? remaining : bottom;
                    for (bool maximized : { false, true })
                    {
                        const auto bounds = DAW::mobile::fitPhonePluginBrowserPlacement(
                            width, height, top, bottom,
                            { 999999, -999999, 340, 560 }, maximized);
                        if (bounds.width <= 0 || bounds.height <= 0)
                            continue;
                        if (bounds.y < safeTop
                            || static_cast<long long>(bounds.y) + bounds.height
                                > static_cast<long long>(height) - safeBottom)
                            return 369;
                        if (bounds.x < 0
                            || static_cast<long long>(bounds.x) + bounds.width > width)
                            return 370;
                    }
                }

    // Phone Track List: after orientation or split-view changes, never
    // consume the entire Arrangement width or create a negative pane.
    for (int workspace : { -1, 0, 1, 2, 44, 108, 320, 393, 852,
                           std::numeric_limits<int>::max() })
        for (int requested : { std::numeric_limits<int>::min(), -1, 0, 1,
                               107, 108, 320, 9999,
                               std::numeric_limits<int>::max() })
        {
            const int actual = DAW::mobile::phoneTrackListWidth(
                requested, workspace);
            if (actual < 0 || actual > (requested > 0 ? requested : 0))
                return 371;
            if (workspace <= 1 || requested <= 0)
            {
                if (actual != 0)
                    return 372;
            }
            else if (actual >= workspace || actual > (workspace / 4 > 108
                                                   ? workspace / 4 : 108))
                return 373;
        }

}
