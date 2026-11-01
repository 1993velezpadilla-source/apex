#pragma once

// JUCE-independent panel sizing logic (owning component logical units).
namespace DAW::mobile
{
struct PanelBounds { int x, y, width, height; };

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
                                       int anchorX, int anchorY, bool phone) noexcept
{
    if (viewportW <= 0 || viewportH <= 0)
        return { 0, 0, 0, 0 };

    const int margin = minInt(8, minInt(viewportW, viewportH) / 4);
    const int availableW = maxInt(0, viewportW - 2 * margin);
    const int availableH = maxInt(0, viewportH - 2 * margin);
    const int w = minInt(phone ? 360 : 260, availableW);
    const int h = minInt(phone ? 420 : 380, availableH);
    if (w <= 0 || h <= 0)
        return { 0, 0, 0, 0 };

    return { clampAnchorOffset(static_cast<long long>(anchorX) - w - 16,
                               margin, viewportW - margin - w),
             clampAnchorOffset(static_cast<long long>(anchorY) - h + 20,
                               margin, viewportH - margin - h),
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

/** Keep a phone's Track List reachable without stealing the Arrangement.
 *  iPhone and Android use this; iPad/desktop keep their stored track width.
 *  Reserve at least one logical unit for the timeline in tiny split views.
 */
constexpr int phoneTrackListWidth(int requestedWidth, int workspaceWidth) noexcept
{
    if (requestedWidth <= 0 || workspaceWidth <= 1)
        return 0;
    // Portrait: allow an actual three-column touch grid while preserving
    // approximately half of the screen for waveform/timeline interaction.
    // Landscape: keep the original user width, with a 220-unit touch floor.
    // Minuscule split-screen widths degrade symmetrically, never overflow.
    if (workspaceWidth <= 600)
    {
        // BandLab-like split: compact track identity/status (~1/3 viewport),
        // while the actual waveforms remain the dominant interaction canvas.
        // Advanced track controls are accessible through the 44-unit ellipsis
        // inspector; expanding the header by drag is still possible.
        const int preferred = minInt(workspaceWidth - 1,
            minInt(128, maxInt(84, workspaceWidth / 3)));
        const int maxAllowed = minInt(workspaceWidth - 1,
            static_cast<int>((static_cast<long long>(workspaceWidth) * 60) / 100));
        const int base = minInt(preferred, maxAllowed);
        const int extra = maxInt(0, requestedWidth - 220);
        return base + minInt(extra, maxInt(0, maxAllowed - base));
    }
    return minInt(maxInt(requestedWidth, 220), minInt(260, workspaceWidth - 1));
}

/** BandLab-style phone arranger rail.
 *  Collapses the track-name header to a fixed icon strip after horizontal
 *  movement away from the beginning. Hysteresis prevents a rapid open/close
 *  loop from touch scroll jitter. Pixel widths are JUCE logical coordinates.
 *  The actual Arrangement horizontal offset is NEVER modified here.
 */
constexpr bool shouldCollapsePhoneTrackRail(bool wasCollapsed,
                                             int timelineScrollX) noexcept
{
    // Only a deliberate scroll collapses the name rail; returning to song
    // start restores it. Between thresholds keep the current state.
    return wasCollapsed ? timelineScrollX > 10 : timelineScrollX >= 48;
}

/** Reversible phone-rail animation curve (0..1), no JUCE state or timers.
 *  Smoothstep starts/ends at zero velocity and is safe after dropped frames.
 */
constexpr float smoothPhoneRailProgress(float t) noexcept
{
    const float x = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return x * x * (3.0f - 2.0f * x);
}

constexpr int phoneTrackRailWidth(int requestedWidth, int workspaceWidth,
                                  bool isCollapsed) noexcept
{
    const int full = phoneTrackListWidth(requestedWidth, workspaceWidth);
    return isCollapsed ? minInt(full, 48) : full;
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
