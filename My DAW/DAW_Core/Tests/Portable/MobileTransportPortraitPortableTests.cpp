#include "../../Source/UICore/MobileTransportPortrait.h"
#include <cassert>
#include <limits>
using DAW::mobile::PortraitTransportBounds;
using DAW::mobile::portraitTransportLayout;

constexpr bool inside(PortraitTransportBounds r, int w, int h)
{
    return r.x >= 0 && r.y >= 0 && r.width >= 0 && r.height >= 0
        && static_cast<long long>(r.x) + r.width <= w
        && static_cast<long long>(r.y) + r.height <= h;
}
constexpr bool separated(PortraitTransportBounds a, PortraitTransportBounds b)
{
    return static_cast<long long>(a.x) + a.width <= b.x
        || static_cast<long long>(b.x) + b.width <= a.x
        || static_cast<long long>(a.y) + a.height <= b.y
        || static_cast<long long>(b.y) + b.height <= a.y;
}
constexpr bool valid(int w, int h)
{
    const auto r = portraitTransportLayout(w, h);
    for (int i = 0; i < 6; ++i)
    {
        if (!inside(r.transport[i], w, h)) return false;
        for (int j = 0; j < i; ++j)
            if (!separated(r.transport[j], r.transport[i])) return false;
    }
    const PortraitTransportBounds bottom[5] = {
        r.click, r.fx, r.browser, r.mixer, r.sends
    };
    for (int i = 0; i < 5; ++i)
    {
        if (!inside(bottom[i], w, h)) return false;
        for (int j = 0; j < i; ++j)
            if (!separated(bottom[j], bottom[i])) return false;
    }
    return true;
}
static_assert(valid(320, 144));
static_assert(valid(360, 112));
static_assert(valid(360, 144));
static_assert(valid(393, 144));
static_assert(valid(245, 144));
static_assert(valid(852, 300));
static_assert(valid(13, 25));
static_assert(valid(1, 1));
static_assert(valid(0, 0));
static_assert(valid(std::numeric_limits<int>::max(),
                    std::numeric_limits<int>::max()));

constexpr auto phone = portraitTransportLayout(360, 144);
static_assert(phone.transport[0].width >= 44 && phone.transport[5].width >= 44);
static_assert(phone.fx.width == 44 && phone.browser.width == 44);
static_assert(phone.mixer.width >= 44 && phone.sends.width >= 44);
static_assert(phone.click.width == 132);
// At the 144-unit portrait deck, all primary actions must have >=44
// logical-unit vertical hit areas, without overlapping the top readouts.
static_assert(phone.transport[0].height >= 44 && phone.transport[5].height >= 44);
static_assert(phone.fx.height >= 44 && phone.browser.height >= 44);
static_assert(phone.mixer.height >= 44 && phone.sends.height >= 44);
static_assert(phone.click.height >= 44);
static_assert(phone.transport[0].y >= 6 + (144 - 4 * 6) / 3);
static_assert(phone.sends.y + phone.sends.height <= 144);
static_assert(portraitTransportLayout(320, 144).transport[0].width >= 44);
// A shorter deck cannot provide the same 44-unit height in three rows.
static_assert(portraitTransportLayout(360, 112).transport[0].height < 44);


constexpr bool validCompact(int w, int h)
{
    const auto r = DAW::mobile::compactPhoneTransportLayout(w, h);
    for (int i = 0; i < 6; ++i)
    {
        if (!inside(r.transport[i], w, h)) return false;
        for (int j = 0; j < i; ++j)
            if (!separated(r.transport[j], r.transport[i])) return false;
    }
    const PortraitTransportBounds top[] = {
        r.clock, r.tempo, r.key, r.automation, r.click,
        r.fx, r.browser, r.mixer, r.sends
    };
    for (int i = 0; i < 9; ++i)
    {
        if (!inside(top[i], w, h)) return false;
        if (top[i].width == 0 || top[i].height == 0) continue;
        for (int j = 0; j < i; ++j)
            if (top[j].width && top[j].height
                && !separated(top[j], top[i])) return false;
        for (int j = 0; j < 6; ++j)
            if (!separated(top[i], r.transport[j])) return false;
    }
    return true;
}
static_assert(validCompact(360, 96));
static_assert(validCompact(320, 112));
static_assert(validCompact(480, 96));
static_assert(validCompact(245, 96));
static_assert(validCompact(20, 8));
static_assert(validCompact(0, 0));
static_assert(validCompact(std::numeric_limits<int>::max(),
                           std::numeric_limits<int>::max()));
constexpr auto compact360 = DAW::mobile::compactPhoneTransportLayout(360, 96);
static_assert(compact360.transport[0].width >= 44
              && compact360.transport[5].width >= 44);
static_assert(compact360.transport[0].height >= 44
              && compact360.transport[5].height >= 44);
static_assert(compact360.fx.width >= 44 && compact360.browser.width >= 44);
static_assert(compact360.mixer.width >= 44 && compact360.sends.width >= 44);
static_assert(compact360.mixer.height >= 44 && compact360.sends.height >= 44);
static_assert(DAW::mobile::compactPhoneTransportLayout(320, 112).transport[0].height >= 44);
static_assert(DAW::mobile::compactPhoneTransportLayout(480, 96).sends.width >= 44);
// Landscape and rotation regression: all compact controls remain inside the
// viewport even when the OS supplies a taller-than-usual transport deck.
static_assert(validCompact(393, 144));
static_assert(validCompact(852, 180));
static_assert(validCompact(1024, 256));
static_assert(validCompact(189, 94));
static_assert(validCompact(188, 94));
static_assert(validCompact(320, 94));
static_assert(validCompact(360, 94));
// At the 189-unit compact breakpoint the four shortcuts must not shrink
// below the intended 44-unit hit width. Just below it they must remain
// bounded rather than overlapping the clock/readouts.
constexpr auto compactAtThreshold = DAW::mobile::compactPhoneTransportLayout(189, 94);
constexpr auto compactBelowThreshold = DAW::mobile::compactPhoneTransportLayout(188, 94);
static_assert(compactAtThreshold.fx.width == 44
              && compactAtThreshold.browser.width == 44
              && compactAtThreshold.mixer.width == 44
              && compactAtThreshold.sends.width == 44);
static_assert(compactBelowThreshold.fx.width < 44
              && compactBelowThreshold.sends.width < 44);
static_assert(compactAtThreshold.transport[0].height >= 44
              && compactBelowThreshold.transport[0].height >= 44);
// Landscape rotation must preserve the six transport hit targets and
// phone-only floating Mixer / Sends shortcuts at accessible dimensions.
constexpr auto rotated = DAW::mobile::compactPhoneTransportLayout(852, 180);
static_assert(rotated.transport[0].width >= 44
              && rotated.transport[5].width >= 44
              && rotated.transport[0].height >= 44);
static_assert(rotated.mixer.width >= 44 && rotated.mixer.height >= 44);
static_assert(rotated.sends.width >= 44 && rotated.sends.height >= 44);

// Realistic phone rotation must preserve semantic order as well as bounds.
// A non-overlapping but permuted transport would still be a broken UI.
constexpr bool orderedActions(int w, int h)
{
    const auto r = DAW::mobile::compactPhoneTransportLayout(w, h);
    for (int i = 0; i < 6; ++i)
    {
        if (r.transport[i].width < 44 || r.transport[i].height < 44)
            return false;
        if (i && r.transport[i - 1].x + r.transport[i - 1].width > r.transport[i].x)
            return false;
    }
    return r.fx.width >= 44 && r.browser.width >= 44
        && r.mixer.width >= 44 && r.sends.width >= 44
        && r.fx.x < r.browser.x && r.browser.x < r.mixer.x
        && r.mixer.x < r.sends.x
        && r.fx.y == r.sends.y
        && r.transport[0].y >= r.sends.y + r.sends.height;
}
static_assert(orderedActions(320, 112));
static_assert(orderedActions(360, 96));
static_assert(orderedActions(852, 180));

int main()
{
    // Exhaustively cross the 189-unit compact shortcut threshold during
    // narrow split-screen resizing; the coarse sweep can skip this boundary.
    // Probe the minimum compact-deck height and the 189-unit shortcut
    // threshold, including the zero-layout guard immediately below h=8.
    for (int w = 18; w <= 24; ++w)
        for (int h = 6; h <= 12; ++h)
            assert(validCompact(w, h));
    for (int w = 180; w <= 200; ++w)
        for (int h = 90; h <= 100; ++h)
            assert(validCompact(w, h));
    for (int w = 0; w < 900; w += 7)
        for (int h = 0; h < 500; h += 9)
        {
            assert(valid(w, h));
            assert(validCompact(w, h));
        }
}
