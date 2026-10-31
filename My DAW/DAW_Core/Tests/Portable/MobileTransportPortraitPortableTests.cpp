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
    return a.x + a.width <= b.x || b.x + b.width <= a.x
        || a.y + a.height <= b.y || b.y + b.height <= a.y;
}
constexpr bool valid(int w, int h)
{
    const auto r = portraitTransportLayout(w, h);
    for (int i = 0; i < 6; ++i)
    {
        if (!inside(r.transport[i], w, h)) return false;
        if (i > 0 && !separated(r.transport[i - 1], r.transport[i])) return false;
    }
    const PortraitTransportBounds bottom[5] = {
        r.click, r.fx, r.browser, r.mixer, r.sends
    };
    for (int i = 0; i < 5; ++i)
    {
        if (!inside(bottom[i], w, h)) return false;
        if (i > 0 && !separated(bottom[i - 1], bottom[i])) return false;
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

int main()
{
    for (int w = 0; w < 900; w += 7)
        for (int h = 0; h < 500; h += 9)
            assert(valid(w, h));
}
