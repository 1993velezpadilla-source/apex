#pragma once

// JUCE-independent portrait phone transport geometry, in component logical units.
// The six transport buttons get their own row; the two quick-access buttons
// share the bottom row with Click, floating Mixer and Bubblegum Sends.
namespace DAW::mobile
{
struct PortraitTransportBounds { int x, y, width, height; };
struct PortraitTransportLayout
{
    PortraitTransportBounds transport[6]{};
    PortraitTransportBounds fx{}, browser{}, mixer{}, sends{}, click{};
};

constexpr PortraitTransportLayout portraitTransportLayout(int viewportW, int viewportH) noexcept
{
    PortraitTransportLayout out{};
    if (viewportW <= 12 || viewportH <= 24)
        return out;

    constexpr int margin = 6;
    constexpr int verticalMargin = 3; // 44-unit touch rows at a 144-unit deck.
    constexpr int gap = 3;
    const int usableW = viewportW - 2 * margin;
    const int rowH = (viewportH - 4 * verticalMargin) / 3;
    const int actionsY = 2 * verticalMargin + rowH;
    const int bottomY = 3 * verticalMargin + 2 * rowH;

    const int actionGap = usableW / 7 < gap ? usableW / 7 : gap;
    const int actionW = (usableW - 5 * actionGap) / 6;
    for (int i = 0; i < 6; ++i)
    {
        const int inset = actionW > 2 ? 1 : 0;
        out.transport[i] = { margin + i * (actionW + actionGap) + inset,
                             actionsY, actionW - 2 * inset, rowH };
    }

    const int bottomGap = usableW / 5 < gap ? usableW / 5 : gap;
    const int controlsW = usableW - 4 * bottomGap;
    const int minActionW = controlsW / 4 < 44 ? controlsW / 4 : 44;
    const int extra = controlsW - 4 * minActionW;
    const int clickW = extra < 132 ? extra : 132;
    const int mixerExtra = (extra - clickW) / 2 < 36 ? (extra - clickW) / 2 : 36;
    const int mixerW = minActionW + mixerExtra;

    int x = margin;
    out.click = { x, bottomY, clickW, rowH };
    x += clickW + bottomGap;
    out.fx = { x, bottomY, minActionW, rowH };
    x += minActionW + bottomGap;
    out.browser = { x, bottomY, minActionW, rowH };
    x += minActionW + bottomGap;
    out.mixer = { x, bottomY, mixerW, rowH };
    x += mixerW + bottomGap;
    out.sends = { x, bottomY, mixerW, rowH };
    return out;
}
}
