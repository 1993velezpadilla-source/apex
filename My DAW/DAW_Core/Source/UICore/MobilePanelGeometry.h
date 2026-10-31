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

/** Keep the floating FX browser's title, close control and scrollable list
 *  between the phone header and persistent bottom Transport. Desktop/iPad
 *  keep their existing 340x560 browser placement.
 */
constexpr PanelBounds fitPhonePluginBrowser(int viewportW, int viewportH,
                                            int reservedTop, int reservedBottom) noexcept
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
    return { margin, top + margin, minInt(340, safeW), minInt(560, safeH) };
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

}
