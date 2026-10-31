#include "../../Source/UICore/MobilePanelGeometry.h"
#include <cassert>
#include <initializer_list>
using DAW::mobile::fitBubblegumSends;
constexpr bool inside(DAW::mobile::PanelBounds p, int w, int h)
{
    return p.x >= 0 && p.y >= 0 && p.x + p.width <= w && p.y + p.height <= h;
}
int main()
{
    constexpr auto portrait = fitBubblegumSends(393, 852, 360, 70, true);
    static_assert(portrait.width == 360 && portrait.height == 420
                  && inside(portrait, 393, 852));
    constexpr auto landscape = fitBubblegumSends(852, 300, 820, 60, true);
    static_assert(landscape.width == 360 && landscape.height == 284
                  && inside(landscape, 852, 300));
    constexpr auto tiny = fitBubblegumSends(245, 220, 220, 200, true);
    static_assert(tiny.width == 229 && tiny.height == 204
                  && inside(tiny, 245, 220));
    constexpr auto desktop = fitBubblegumSends(1800, 900, 450, 490, false);
    static_assert(desktop.width == 260 && desktop.height == 380
                  && inside(desktop, 1800, 900));
    // A panel dragged beyond any phone edge must have a valid on-screen
    // recovery position. The UI calls this fitter when the drag ends.
    constexpr auto offLeft = fitBubblegumSends(393, 852, -1000, -1000, true);
    static_assert(offLeft.x == 8 && offLeft.y == 8
                  && inside(offLeft, 393, 852));
    constexpr auto offRight = fitBubblegumSends(393, 852, 20000, 20000, true);
    static_assert(offRight.x == 25 && offRight.y == 424
                  && inside(offRight, 393, 852));
    for (int w : {1, 2, 10, 160, 245, 393, 852})
        for (int h : {1, 2, 10, 120, 220, 300, 852})
            for (int anchor : {-20000, -100, 0, 100, 20000})
            {
                const auto fitted = fitBubblegumSends(w, h, anchor, anchor, true);
                assert(inside(fitted, w, h));
            }
    constexpr auto zero = fitBubblegumSends(0, 0, 0, 0, true);
    static_assert(zero.width == 0 && zero.height == 0);
    for (int w = 1; w <= 900; w += 9)
        for (int h = 1; h <= 900; h += 11)
            for (bool isPhone : {false, true})
            {
                const auto fitted = fitBubblegumSends(w, h, w + 50, -100, isPhone);
                assert(inside(fitted, w, h));
            }
}
