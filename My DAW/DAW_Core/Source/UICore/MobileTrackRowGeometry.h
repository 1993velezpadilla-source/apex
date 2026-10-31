#pragma once

// JUCE-free geometry for the APEX phone Track List. All rectangles are
// *local to the TrackRow*, in logical component units. Desktop and iPad do
// not use this layout. Never force five buttons plus Input onto one row.
namespace DAW::mobile
{
struct TrackControlRect
{
    int x = 0, y = 0, width = 0, height = 0;
    constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
};

struct PhoneTrackControlLayout
{
    TrackControlRect mute{}, solo{}, monitor{}, arm{}, automation{}, input{}, target{};
};

constexpr int trackMin(int a, int b) noexcept { return a < b ? a : b; }
constexpr int trackMax(int a, int b) noexcept { return a > b ? a : b; }

constexpr PhoneTrackControlLayout phoneTrackControlLayout(
    int contentX, int contentWidth, int primaryRowHeight,
    bool isMaster, bool showInput, bool showTarget) noexcept
{
    PhoneTrackControlLayout out{};
    if (contentWidth <= 0 || primaryRowHeight <= 0)
        return out;

    // Three independent columns: M/S/Monitor; Arm/Automation/Input; optional
    // Automation Target on the final row. Every actionable cell is in-bounds.
    const int gapX = trackMin(6, contentWidth / 12);
    const int w = trackMax(0, (contentWidth - 2 * gapX) / 3);
    const int firstY = trackMin(41, primaryRowHeight / 4);
    const int maxButtonH = trackMax(0, (primaryRowHeight - firstY) / 3);
    const int h = trackMin(44, maxButtonH);
    if (w == 0 || h == 0)
        return out;
    const int gapY = trackMin(6, trackMax(0, (primaryRowHeight - firstY - 3 * h) / 2));

    const int x0 = contentX;
    const int x1 = x0 + w + gapX;
    const int x2 = x1 + w + gapX;
    const int y0 = firstY;
    const int y1 = y0 + h + gapY;
    const int y2 = y1 + h + gapY;

    out.mute = { x0, y0, w, h };
    out.solo = { x1, y0, w, h };
    if (isMaster)
        return out;

    out.monitor = { x2, y0, w, h };
    out.arm = { x0, y1, w, h };
    out.automation = { x1, y1, w, h };
    if (showInput)
        out.input = { x2, y1, w, h };
    if (showTarget)
        out.target = { x0, y2, w, h };
    return out;
}
} // namespace DAW::mobile
