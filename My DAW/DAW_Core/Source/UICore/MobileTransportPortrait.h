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
    PortraitTransportBounds key{}, tempo{}, clock{}, automation{};
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

/** Compact phone deck: six transport actions in the lower row, while the
 *  upper row reserves FX, Browser, floating Mixer and Sends first.
 *  No audio state, routing, or desktop/iPad geometry is involved.
 */
constexpr PortraitTransportLayout compactPhoneTransportLayout(int w, int h) noexcept
{
    PortraitTransportLayout out{};
    if (w < 20 || h < 8) return out;
    constexpr int margin = 2, gap = 3;
    const int usable = w - 2 * margin;
    const int rowH = (h - 3 * margin) / 2;
    const int actionY = 2 * margin + rowH;
    const int actionW = (usable - 5 * gap) / 6;
    for (int i = 0; i < 6; ++i)
    {
        const int inset = actionW > 2 ? 1 : 0;
        out.transport[i] = { margin + i * (actionW + gap) + inset,
                             actionY, actionW - 2 * inset, rowH };
    }
    // 44-unit shortcuts at widths >=189; below that degrade without overflow.
    const int slot = usable >= 185 ? 44 : (usable - 3 * gap) / 4;
    const int controlsW = 4 * slot + 3 * gap;
    const int controlsX = margin + usable - controlsW;
    out.fx = { controlsX, margin, slot, rowH };
    out.browser = { controlsX + slot + gap, margin, slot, rowH };
    out.mixer = { controlsX + 2 * (slot + gap), margin, slot, rowH };
    out.sends = { controlsX + 3 * (slot + gap), margin, slot, rowH };

    const int readoutRight = controlsX - gap;
    int cursor = margin;
    auto take = [&](int wanted, int minimum) -> PortraitTransportBounds
    {
        const int available = readoutRight - cursor;
        if (available < minimum) return {};
        const int width = available < wanted ? available : wanted;
        const PortraitTransportBounds r{ cursor, margin, width, rowH };
        cursor += width + gap;
        return r;
    };
    // Clock is essential; other readouts appear only with usable hit areas.
    out.clock = take(130, 54);
    out.tempo = take(85, 54);
    out.key = take(95, 54);
    out.automation = take(72, 44);
    out.click = take(132, 44);
    return out;
}
}
