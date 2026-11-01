#include "../../Source/UICore/MobilePanelGeometry.h"
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

}
