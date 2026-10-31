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

// Existing desktop projects may include nested folder buses. Keep their
// hierarchy perceptible without allowing 14-unit * depth to shrink phone
// record controls below their touch target size.
constexpr int phoneTrackIndentWidth(int depth) noexcept
{
    return trackMin(2, trackMax(0, depth)) * 5;
}

// The folder toggle is in the title strip, safely ABOVE M/S/Record.
// A mid-row chevron would steal touches from Arm on a 180-unit phone lane.
constexpr TrackControlRect phoneFolderChevronBounds(
    int colourStripLeft, int rowWidth, int rowHeight) noexcept
{
    if (rowHeight < 36 || rowWidth < 48) return {};
    const int x = colourStripLeft + 11;
    return x + 26 <= rowWidth ? TrackControlRect{x, 7, 26, 26}
                              : TrackControlRect{};
}

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

    if (isMaster)
    {
        // MASTER keeps its name/eye presentation and only has Mute/Solo.
        // Put both below the title/eye area, not into the tiny row top.
        const int masterH = trackMin(44, trackMax(0, primaryRowHeight - 58));
        const int masterY = primaryRowHeight - masterH - 4;
        out.mute = { x0, masterY, w, masterH };
        out.solo = { x1, masterY, w, masterH };
        return out;
    }
    out.mute = { x0, y0, w, h };
    out.solo = { x1, y0, w, h };

    out.monitor = { x2, y0, w, h };
    out.arm = { x0, y1, w, h };
    out.automation = { x1, y1, w, h };
    if (showInput)
        out.input = { x2, y1, w, h };
    if (showTarget)
        // Automation parameter names are longer than single-letter toggles.
        // Use the third row as a full-width selector instead of another tiny
        // 50-unit cell.
        out.target = { x0, y2, contentWidth, h };
    return out;
}
} // namespace DAW::mobile
