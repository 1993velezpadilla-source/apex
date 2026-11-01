#pragma once

// JUCE-independent panel sizing logic (owning component logical units).
namespace DAW::mobile
{
struct PanelBounds { int x, y, width, height; };

// The bottom phone Transport owns Mixer/Sends launch actions. Desktop-only
// restore bubbles must stay hidden after closing a floating phone panel.
constexpr bool desktopRestoreBubbleVisible(bool phonePresentation) noexcept
{
    return !phonePresentation;
}

constexpr int minInt(int a, int b) noexcept { return a < b ? a : b; }
constexpr int maxInt(int a, int b) noexcept { return a > b ? a : b; }
constexpr int clampInt(int value, int lower, int upper) noexcept
{
    return minInt(maxInt(value, lower), upper);
}
// Anchors may originate from drag gestures, restored windows or external
// display coordinates. Do not subtract panel sizes in signed 32-bit space.
constexpr int clampAnchorOffset(long long offset, int lower, int upper) noexcept
{
    return offset < lower ? lower : offset > upper ? upper : static_cast<int>(offset);
}
// Expand the 30-unit painted close orb to a 44-unit phone touch target.
// Clamp within the panel for tiny split-screen viewports; desktop keeps
// its original painted hit bounds.
constexpr PanelBounds phoneCloseTouchBounds(int panelW, int panelH) noexcept
{
    if (panelW <= 0 || panelH <= 0)
        return { 0, 0, 0, 0 };
    const int w = minInt(44, panelW);
    const int h = minInt(44, panelH);
    return { clampInt(panelW - 25 - w / 2, 0, panelW - w),
             clampInt(25 - h / 2, 0, panelH - h), w, h };
}

// MixerWindow hides its close light, leaving minimize/maximize at the two
// left-side JUCE chrome centres. On phones, extend the adjacent touch zones
// into unused title-bar space without moving the original 12x12 artwork.
// With the 12/8/14 chrome geometry these are two non-overlapping 30x32
// targets (formerly 20x32). The midpoint is the exclusive boundary: a
// release cannot activate a different orb by landing in an overlap.
// Return 1/2 for minimize/maximize; zero allows title dragging.
// The caller supplies FloatingWindowBase's constants to avoid drift.
constexpr int phoneMixerLeftChromeButtonAt(float x, float y,
                                           int padLeft, int buttonSize,
                                           int buttonGap, int titleHeight) noexcept
{
    if (buttonSize <= 0 || buttonGap < 0 || titleHeight <= 0
        || y < 0.0f || y >= static_cast<float>(titleHeight))
        return 0;
    const float firstCentre = static_cast<float>(padLeft)
                            + static_cast<float>(buttonSize) * 0.5f;
    const float separation = static_cast<float>(buttonSize + buttonGap);
    const float boundary = firstCentre + separation * 0.5f;
    const float touchWidth = separation * 1.5f;
    const float firstLeft = boundary > touchWidth ? boundary - touchWidth : 0.0f;
    if (x >= firstLeft && x < boundary)
        return 1;
    return x >= boundary && x < boundary + touchWidth ? 2 : 0;
}

// Keep the phone mixer's original left traffic lights and right toolbar
// unobscured by the cyan/violet header energy overlay. The overlay is a
// decoration, never an input surface; preserve the full original hit zones.
// Desktop/iPad deliberately retain the existing full-width energy treatment.
constexpr PanelBounds phoneMixerHeaderEnergyRegion(int windowWidth,
                                                   int leftChromeEnd,
                                                   int toolbarStart,
                                                   int titleHeight) noexcept
{
    const int width = maxInt(0, windowWidth);
    const int left = clampInt(leftChromeEnd, 0, width);
    const int right = clampInt(toolbarStart, left, width);
    return { left, 0, right - left, maxInt(0, titleHeight) };
}

// Clip the phone toolbar's painted hover/active glows to its own title-bar
// zone. The 32-high touch targets may paint a 2-unit glow; without a clip,
// the glow bleeds into the Mixer content and can cover the title artwork.
// Desktop/iPad do not use this clipping policy.
constexpr PanelBounds phoneMixerToolbarPaintClip(int windowWidth,
                                                 int titleHeight,
                                                 int toolbarStart) noexcept
{
    const int width = maxInt(0, windowWidth);
    const int left = clampInt(toolbarStart, 0, width);
    return { left, 0, width - left, maxInt(0, titleHeight) };
}

// Phone Mixer title bar is 32 units tall. Use the full available height for
// 44-wide, full 32-high touch targets; a true 44-high target needs a
// separate chrome redesign. Only wired phone actions are exposed: Browser, FX Chain and Reset.
// Bubblegum Sends is launched from the phone Transport, never the Send Matrix.
enum PhoneMixerToolbarAction
{
    phoneMixerBrowser = 0, phoneMixerFXChain, phoneMixerReset, phoneMixerOverflow
};
struct PhoneMixerToolbarButton
{
    PanelBounds bounds{};
    PhoneMixerToolbarAction action = phoneMixerOverflow;
};
struct PhoneMixerToolbarLayout
{
    PhoneMixerToolbarButton buttons[4]{};
    int count = 0;
};
constexpr PhoneMixerToolbarLayout phoneMixerToolbarLayout(int zoneW, int zoneH) noexcept
{
    PhoneMixerToolbarLayout out{};
    // A 1-unit edge inset preserves all three 44-wide actions in a 142-unit
    // phone toolbar zone (common ~390-wide portrait Mixer), without shrinking
    // touch targets or reducing the 4-unit separation between actions.
    constexpr int pad = 1, gap = 4, targetW = 44, targetH = 32;
    if (zoneW < targetW + 2 * pad || zoneH < targetH)
        return out;
    const int available = zoneW - 2 * pad;
    int visible = 3;
    if (3 * targetW + 2 * gap > available)
    {
        visible = 0;
        for (int n = 1; n <= 3; ++n)
            if ((n + 1) * targetW + n * gap <= available)
                visible = n;
    }
    const int y = (zoneH - targetH) / 2;
    int x = pad;
    for (int i = 0; i < visible; ++i)
    {
        const auto action = i == 0 ? phoneMixerBrowser
                          : i == 1 ? phoneMixerFXChain : phoneMixerReset;
        out.buttons[out.count++] = { { x, y, targetW, targetH }, action };
        x += targetW + gap;
    }
    if (visible < 3)
        out.buttons[out.count++] = { { x, y, targetW, targetH }, phoneMixerOverflow };
    return out;
}

constexpr PanelBounds fitBubblegumSends(int viewportW, int viewportH,
                                       int anchorX, int anchorY, bool phone,
                                       int reservedTop = 0, int reservedBottom = 0) noexcept
{
    if (viewportW <= 0 || viewportH <= 0)
        return { 0, 0, 0, 0 };

    // The phone's APEX header and persistent bottom Transport are not
    // available to the Sends panel. Desktop callers keep zero reservations.
    const int top = phone ? clampInt(reservedTop, 0, viewportH) : 0;
    const int bottom = phone ? clampInt(reservedBottom, 0, viewportH - top) : 0;
    const int margin = minInt(8, minInt(viewportW, viewportH) / 4);
    const int availableW = maxInt(0, viewportW - 2 * margin);
    const int availableH = maxInt(0, viewportH - top - bottom - 2 * margin);
    const int w = minInt(phone ? 360 : 260, availableW);
    const int h = minInt(phone ? 420 : 380, availableH);
    if (w <= 0 || h <= 0)
        return { 0, 0, 0, 0 };

    return { clampAnchorOffset(static_cast<long long>(anchorX) - w - 16,
                               margin, viewportW - margin - w),
             clampAnchorOffset(static_cast<long long>(anchorY) - h + 20,
                               top + margin, viewportH - bottom - margin - h),
             w, h };
}

/** A phone's floating mixer must stay above its bottom Transport and below
 *  the header. Keep the safe rectangle separate from the preferred window:
 *  rotation can preserve a manually sized window while it still fits.
 *  All coordinates are logical component units, independent of JUCE/DSP.
 */
struct FloatingMixerFit
{
    PanelBounds safe{}, window{};
    int minimumWidth = 0, minimumHeight = 0;
};

constexpr FloatingMixerFit fitFloatingMixer(int viewportW, int viewportH,
                                            int reservedTop, int reservedBottom) noexcept
{
    if (viewportW <= 0 || viewportH <= 0)
        return {};

    const int insetX = minInt(8, viewportW / 4);
    const int insetY = minInt(6, viewportH / 4);
    const int top = clampInt(reservedTop, 0, viewportH);
    const int bottom = clampInt(reservedBottom, 0, viewportH - top);
    const int safeW = viewportW - 2 * insetX;
    const int safeH = viewportH - top - bottom - 2 * insetY;
    if (safeW <= 0 || safeH <= 0)
        return {};

    // Use 64-bit intermediate arithmetic for extreme restored display sizes.
    const int wantedW = static_cast<int>((static_cast<long long>(safeW) * 88 + 50) / 100);
    const int wantedH = static_cast<int>((static_cast<long long>(safeH) * 86 + 50) / 100);
    const int width = minInt(safeW, maxInt(200, wantedW));
    const int height = minInt(safeH, maxInt(180, wantedH));
    const PanelBounds safe{ insetX, top + insetY, safeW, safeH };
    const PanelBounds window{ safe.x + (safeW - width) / 2,
                              safe.y + (safeH - height) / 2, width, height };
    return { safe, window, minInt(200, safeW), minInt(180, safeH) };
}

/** Clip Properties hosts clip FX on phones. Keep its title/close chrome
 *  between the APEX header and the persistent bottom Transport on rotation.
 *  Desktop and iPad retain the existing 420x360 opening behavior.
 */
struct PhoneClipPropertiesFit
{
    PanelBounds safe{}, window{};
    int minimumWidth = 0, minimumHeight = 0;
};

constexpr PhoneClipPropertiesFit fitPhoneClipProperties(
    int viewportW, int viewportH, int reservedTop, int reservedBottom) noexcept
{
    if (viewportW <= 0 || viewportH <= 0)
        return {};
    const int top = clampInt(reservedTop, 0, viewportH);
    const int bottom = clampInt(reservedBottom, 0, viewportH - top);
    const int margin = minInt(8, minInt(viewportW, viewportH) / 4);
    const int safeW = viewportW - 2 * margin;
    const int safeH = viewportH - top - bottom - 2 * margin;
    if (safeW <= 0 || safeH <= 0)
        return {};

    const int width = minInt(420, safeW);
    const int height = minInt(360, safeH);
    const PanelBounds safe{ margin, top + margin, safeW, safeH };
    const PanelBounds window{ safe.x + (safeW - width) / 2,
                              safe.y + (safeH - height) / 2, width, height };
    return { safe, window, minInt(180, safeW), minInt(120, safeH) };
}

constexpr bool phoneClipPropertiesFits(const PhoneClipPropertiesFit& fit,
                                       PanelBounds current) noexcept
{
    if (fit.safe.width <= 0 || fit.safe.height <= 0
        || current.width < fit.minimumWidth || current.height < fit.minimumHeight
        || current.width <= 0 || current.height <= 0)
        return false;
    return current.x >= fit.safe.x && current.y >= fit.safe.y
        && static_cast<long long>(current.x) + current.width
            <= static_cast<long long>(fit.safe.x) + fit.safe.width
        && static_cast<long long>(current.y) + current.height
            <= static_cast<long long>(fit.safe.y) + fit.safe.height;
}

// Phone FX Browser rows/search must be finger-sized; the decorative desktop
// manufacturer summary header can be removed to preserve one list row in
// short landscape viewports. Desktop/iPad retain their exact original sizes.
constexpr int pluginBrowserHeaderHeight(bool phone) noexcept { return phone ? 0 : 28; }
constexpr int pluginBrowserSearchSlotHeight(bool phone) noexcept { return phone ? 48 : 26; }
constexpr int pluginBrowserRowHeight(bool phone) noexcept { return phone ? 44 : 22; }
constexpr int pluginBrowserChromeHeight(bool phone) noexcept { return phone ? 44 : 28; }
// Calculate the FX list width from *current* content overflow, rather than
// the scrollbar visibility from the previous layout pass. A stale scrollbar
// state can cover the right edge of phone plugin rows after rotation/filtering.
constexpr int pluginBrowserListWidth(int viewportW, int viewportH,
                                     int rowCount, int rowHeight,
                                     int scrollbarThickness) noexcept
{
    if (viewportW <= 0 || viewportH <= 0)
        return 0;
    const bool needsScrollbar = rowCount > 0 && rowHeight > 0
        && rowCount > viewportH / rowHeight;
    const int reserved = needsScrollbar
        ? clampInt(scrollbarThickness, 0, viewportW) : 0;
    return viewportW - reserved;
}

// A phone swipe that scrolls the FX list must never load a plugin on
// mouseUp. JUCE delivers mouseUp to the row that received mouseDown even
// when the finger has moved; desktop mouse selection stays unchanged.
// Limit each axis before squaring to avoid overflow for malformed events.
constexpr bool pluginBrowserRowTapActivates(bool phone, int dragX, int dragY,
                                            bool releasedInside) noexcept
{
    if (!phone)
        return true;
    if (!releasedInside || dragX < -8 || dragX > 8 || dragY < -8 || dragY > 8)
        return false;
    return dragX * dragX + dragY * dragY <= 64;
}

// Remember the full phone gesture, not just its final displacement. A finger
// can travel far and return to the pressed row/button before mouseUp.
// Once outside the 8-unit tap radius, the gesture can never activate.
constexpr bool phoneTapGestureRemainsEligible(bool eligible, int dragX, int dragY) noexcept
{
    return eligible && pluginBrowserRowTapActivates(true, dragX, dragY, true);
}

// A phone window control must not close/minimize/maximize during a swipe,
// or when the finger is released over a different title-bar button. This
// shares the FX row's bounded 8-unit tap threshold (no overflow on extremes).
constexpr bool phoneChromeButtonTapActivates(int pressedButton, int releasedButton,
                                             int dragX, int dragY) noexcept
{
    return pressedButton != 0 && pressedButton == releasedButton
        && pluginBrowserRowTapActivates(true, dragX, dragY, true);
}

// Preserve a phone chrome press across movement, but cancel it on rotation
// or any parent resize before the release can hit newly positioned controls.
struct PhoneChromeTapState
{
    int pressed = 0;
    bool eligible = false;
    constexpr void begin(int button) noexcept
    {
        pressed = button;
        eligible = button != 0;
    }
    constexpr void move(int dx, int dy) noexcept
    {
        eligible = phoneTapGestureRemainsEligible(eligible, dx, dy);
    }
    constexpr void invalidate() noexcept
    {
        pressed = 0;
        eligible = false;
    }
    constexpr bool release(int released, int dx, int dy) noexcept
    {
        const bool activate = eligible
            && phoneChromeButtonTapActivates(pressed, released, dx, dy);
        invalidate();
        return activate;
    }
};

// Four phone Transport launchers (Mixer, Sends, FX, Browser) must
// activate only after a stationary release on the same launcher. A gesture
// cancelled during mouseDrag stays cancelled even if it returns to origin.
constexpr bool phoneTransportLauncherTapActivates(int pressed, int released,
                                                  int dragX, int dragY) noexcept
{
    return phoneChromeButtonTapActivates(pressed, released, dragX, dragY);
}

// A second touch cannot replace a pending floating-panel launcher tap.
// Bind the entire gesture to the initiating JUCE MouseInputSource index.
struct PhoneTransportTapState
{
    int pressed = 0;
    int sourceIndex = -1;
    bool eligible = false;
    // Keep tracking fingers after cancellation: a second touch must not
    // re-arm a launcher while either finger remains on the Transport.
    int activeSources[8]{};
    int activeCount = 0;
    bool blocked = false;

    constexpr void invalidate() noexcept
    {
        pressed = 0;
        sourceIndex = -1;
        eligible = false;
        activeCount = 0;
        blocked = false;
    }
    constexpr void begin(int button, int source) noexcept
    {
        for (int i = 0; i < activeCount; ++i)
            if (activeSources[i] == source)
            {
                blocked = true;
                eligible = false;
                return;
            }
        if (activeCount == 8)
        {
            blocked = true;
            eligible = false;
            return;
        }
        activeSources[activeCount++] = source;
        if (activeCount != 1 || blocked)
        {
            blocked = true;
            eligible = false;
            return;
        }
        pressed = button;
        sourceIndex = source;
        eligible = button != 0;
    }
    constexpr void move(int source, int dx, int dy) noexcept
    {
        if (activeCount != 1 || source != sourceIndex)
        {
            if (activeCount > 0)
            {
                blocked = true;
                eligible = false;
            }
            return;
        }
        eligible = phoneTapGestureRemainsEligible(eligible, dx, dy);
    }
    constexpr bool release(int source, int button, int dx, int dy) noexcept
    {
        const bool activate = activeCount == 1 && !blocked && eligible
            && source == sourceIndex
            && phoneTransportLauncherTapActivates(pressed, button, dx, dy);
        bool matched = false;
        for (int i = 0; i < activeCount; ++i)
            if (activeSources[i] == source)
            {
                activeSources[i] = activeSources[--activeCount];
                matched = true;
                break;
            }
        // A foreign mouseUp must cancel, not leave the original finger
        // eligible to open a panel on its subsequent release.
        if (!matched && activeCount > 0)
        {
            blocked = true;
            eligible = false;
        }
        if (activeCount == 0)
            invalidate();
        else
        {
            blocked = true;
            eligible = false;
            pressed = 0;
            sourceIndex = -1;
        }
        return activate;
    }
};
// Phones already expose a 44-unit maximize button. A second finger tap on
// the title must not toggle maximize while JUCE is still processing the
// second mouseDown/drag; desktop double-click-to-maximize remains available.
constexpr bool chromeTitleDoubleClickMaximizes(bool phonePresentation,
                                                bool hitWindowButton) noexcept
{
    return !phonePresentation && !hitWindowButton;
}

// In impossibly narrow split views three controls share the available width.
constexpr int phoneChromeControlSize(int panelWidth) noexcept
{
    return panelWidth > 0 ? minInt(44, panelWidth / 3) : 0;
}

/** Keep the floating FX browser's title, close control and scrollable list
 *  between the phone header and persistent bottom Transport. Desktop/iPad
 *  keep their existing 340x560 browser placement.
 */
constexpr PanelBounds fitPhonePluginBrowser(int viewportW, int viewportH,
                                            int reservedTop, int reservedBottom,
                                            bool maximized = false) noexcept
{
    if (viewportW <= 0 || viewportH <= 0)
        return { 0, 0, 0, 0 };
    const int top = clampInt(reservedTop, 0, viewportH);
    const int bottom = clampInt(reservedBottom, 0, viewportH - top);
    const int margin = minInt(8, minInt(viewportW, viewportH) / 4);
    const int safeW = viewportW - 2 * margin;
    const int safeH = viewportH - top - bottom - 2 * margin;
    if (safeW <= 0 || safeH <= 0)
        return { 0, 0, 0, 0 };
    return { margin, top + margin,
             maximized ? safeW : minInt(340, safeW),
             maximized ? safeH : minInt(560, safeH) };
}

/** Preserve a manually dragged phone FX Browser position while keeping its
 *  title and list within the header/Transport safe workspace. Rotation may
 *  shrink the browser; maximized windows always fill the safe rectangle.
 */
constexpr PanelBounds fitPhonePluginBrowserPlacement(
    int viewportW, int viewportH, int reservedTop, int reservedBottom,
    PanelBounds current, bool maximized = false) noexcept
{
    const auto fitted = fitPhonePluginBrowser(
        viewportW, viewportH, reservedTop, reservedBottom, maximized);
    if (maximized || fitted.width <= 0 || fitted.height <= 0
        || current.width <= 0 || current.height <= 0)
        return fitted;

    const int top = clampInt(reservedTop, 0, viewportH);
    const int bottom = clampInt(reservedBottom, 0, viewportH - top);
    const int margin = fitted.x;
    return { clampInt(current.x, margin, viewportW - margin - fitted.width),
             clampInt(current.y, top + margin,
                      viewportH - bottom - margin - fitted.height),
             fitted.width, fitted.height };
}

/** Keep a phone's Track List reachable without stealing the Arrangement.
 *  iPhone and Android use this; iPad/desktop keep their stored track width.
 *  Reserve at least one logical unit for the timeline in tiny split views.
 */
constexpr int phoneTrackListWidth(int requestedWidth, int workspaceWidth) noexcept
{
    if (requestedWidth <= 0 || workspaceWidth <= 1)
        return 0;
    const int preferred = maxInt(108, workspaceWidth / 4);
    return minInt(requestedWidth, minInt(preferred, workspaceWidth - 1));
}

/** Check a restored or dragged Mixer window against the safe phone area.
 *  Long-long edges avoid signed overflow from corrupt/restored coordinates.
 *  Reject collapsed windows even if their origin lies inside the viewport.
 */
constexpr bool floatingMixerFits(const FloatingMixerFit& fit, PanelBounds current) noexcept
{
    if (fit.safe.width <= 0 || fit.safe.height <= 0
        || current.width < fit.minimumWidth || current.height < fit.minimumHeight
        || current.width <= 0 || current.height <= 0)
        return false;
    return current.x >= fit.safe.x && current.y >= fit.safe.y
        && static_cast<long long>(current.x) + current.width
            <= static_cast<long long>(fit.safe.x) + fit.safe.width
        && static_cast<long long>(current.y) + current.height
            <= static_cast<long long>(fit.safe.y) + fit.safe.height;
}

// Preserve a manually sized floating phone Mixer when only its position
// escaped the safe workspace (drag release, rotation or changed insets).
// A size that cannot fit falls back to the responsive default. All edges
// remain inside the fit's safe area, including extreme restored coordinates.
constexpr PanelBounds constrainFloatingMixerPlacement(
    const FloatingMixerFit& fit, PanelBounds current) noexcept
{
    if (fit.safe.width <= 0 || fit.safe.height <= 0
        || current.width < fit.minimumWidth || current.height < fit.minimumHeight
        || current.width > fit.safe.width || current.height > fit.safe.height)
        return fit.window;

    return { clampInt(current.x, fit.safe.x,
                      fit.safe.x + fit.safe.width - current.width),
             clampInt(current.y, fit.safe.y,
                      fit.safe.y + fit.safe.height - current.height),
             current.width, current.height };
}

// Desktop Quick Send historically expands a collapsed Mixer to 800x260.
// A phone in landscape can have less than 260 units above its Transport;
// applying that desktop recovery would push the floating Mixer off-screen.
constexpr bool quickSendNeedsDesktopMixerMinimum(bool phonePresentation,
                                                  int currentHeight,
                                                  int desktopMinimumHeight) noexcept
{
    return !phonePresentation && currentHeight < desktopMinimumHeight;
}

}
