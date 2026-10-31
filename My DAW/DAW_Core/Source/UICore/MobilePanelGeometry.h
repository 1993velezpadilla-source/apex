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

    return { clampInt(anchorX - w - 16, margin, viewportW - margin - w),
             clampInt(anchorY - h + 20, margin, viewportH - margin - h),
             w, h };
}
}
