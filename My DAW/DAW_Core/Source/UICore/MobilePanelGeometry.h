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
}
