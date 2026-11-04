#include "../../Source/UICore/MobileTransportPortrait.h"
#include <cassert>
#include <cstdlib>
#include <limits>
// Portable CI may compile with NDEBUG: never compile away runtime sweeps.
// Keep the test executable meaningful in both Debug and Release builds.
#undef assert
#define assert(condition) do { if (!(condition)) std::abort(); } while (false)
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
        // The bottom shortcuts must not cover any of the six transport actions.
        // Prior tests only compared controls within their own row.
        if (bottom[i].width > 0 && bottom[i].height > 0)
            for (int j = 0; j < 6; ++j)
                if (r.transport[j].width > 0 && r.transport[j].height > 0
                    && !separated(bottom[i], r.transport[j])) return false;
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
// Invalid OS dimensions must produce an empty, non-interactive layout.
// The normal bounds validator intentionally requires nonnegative viewports.
constexpr bool emptyLayout(DAW::mobile::PortraitTransportLayout r)
{
    const PortraitTransportBounds controls[] = {
        r.transport[0], r.transport[1], r.transport[2],
        r.transport[3], r.transport[4], r.transport[5],
        r.click, r.fx, r.browser, r.mixer, r.sends,
        r.clock, r.tempo, r.key, r.automation
    };
    for (const auto control : controls)
        if (control.x || control.y || control.width || control.height)
            return false;
    return true;
}
static_assert(emptyLayout(portraitTransportLayout(-1, 144)));
static_assert(emptyLayout(portraitTransportLayout(360, -1)));
static_assert(emptyLayout(portraitTransportLayout(std::numeric_limits<int>::min(), 144)));
static_assert(emptyLayout(portraitTransportLayout(360, std::numeric_limits<int>::min())));
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
static_assert(emptyLayout(DAW::mobile::compactPhoneTransportLayout(-1, 96)));
static_assert(emptyLayout(DAW::mobile::compactPhoneTransportLayout(360, -1)));
static_assert(emptyLayout(DAW::mobile::compactPhoneTransportLayout(std::numeric_limits<int>::min(), 96)));
static_assert(emptyLayout(DAW::mobile::compactPhoneTransportLayout(360, std::numeric_limits<int>::min())));
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

// Compact readouts must never cover phone-only Mixer/Sends shortcuts.
// Optional readouts are suppressed when the viewport is too narrow; if
// visible, each must keep its minimum intended width and original order.
constexpr bool orderedReadouts(int w, int h)
{
    const auto r = DAW::mobile::compactPhoneTransportLayout(w, h);
    const PortraitTransportBounds readouts[] = {
        r.clock, r.tempo, r.key, r.automation, r.click
    };
    const int minimums[] = { 54, 54, 54, 44, 44 };
    for (int i = 0; i < 5; ++i)
    {
        const auto a = readouts[i];
        if (a.width == 0 && a.height == 0) continue;
        if (a.width < minimums[i] || a.height <= 0) return false;
        if (!inside(a, w, h)) return false;
        if (!separated(a, r.fx) || !separated(a, r.browser)
            || !separated(a, r.mixer) || !separated(a, r.sends))
            return false;
        for (int j = 0; j < i; ++j)
            if (readouts[j].width && readouts[j].x >= a.x)
                return false;
    }
    return true;
}
static_assert(orderedReadouts(189, 94));
static_assert(orderedReadouts(320, 112));
static_assert(orderedReadouts(360, 96));
static_assert(orderedReadouts(852, 180));
static_assert(orderedReadouts(1024, 256));

// Portrait deck regression: rotating to a wide viewport must preserve the
// six action targets and both phone-only floating Mixer / Sends shortcuts.
// A bounds-only check would miss controls shrinking below touch size.
constexpr bool portraitTouchTargets(int w, int h)
{
    const auto r = portraitTransportLayout(w, h);
    if (!valid(w, h)) return false;
    for (const auto action : r.transport)
        if (action.width < 44 || action.height < 44) return false;
    return r.fx.width >= 44 && r.fx.height >= 44
        && r.browser.width >= 44 && r.browser.height >= 44
        && r.mixer.width >= 44 && r.mixer.height >= 44
        && r.sends.width >= 44 && r.sends.height >= 44
        && r.mixer.x < r.sends.x;
}
static_assert(portraitTouchTargets(320, 144));
static_assert(portraitTouchTargets(360, 144));
static_assert(portraitTouchTargets(393, 144));
static_assert(portraitTouchTargets(852, 180));
// A 112-unit deck has three rows and cannot provide 44-unit-high targets.
static_assert(!portraitTouchTargets(360, 112));

// Rotation and Android split-screen resize boundaries: validate every pixel
// around the portrait layout's 12/24-unit empty-layout guards, and the
// minimum practical 320px phone width. Coarse 7x9 sweeps skip these edges.
constexpr bool portraitActionsOrdered(int w, int h)
{
    const auto r = portraitTransportLayout(w, h);
    for (int i = 1; i < 6; ++i)
        if (r.transport[i - 1].x + r.transport[i - 1].width > r.transport[i].x)
            return false;
    return r.click.y == r.sends.y && r.fx.x < r.browser.x
        && r.browser.x < r.mixer.x && r.mixer.x < r.sends.x;
}
static_assert(portraitActionsOrdered(320, 144));
static_assert(portraitActionsOrdered(393, 144));
static_assert(portraitActionsOrdered(852, 180));


// Adversarial tap dispatch: the centre of every visible target must hit
// exactly that target, never a neighbouring transport or shortcut. This
// catches accidental touch interception despite otherwise valid bounds.
constexpr bool contains(PortraitTransportBounds r, int x, int y)
{
    return r.width > 0 && r.height > 0
        && x >= r.x && y >= r.y
        && static_cast<long long>(x) < static_cast<long long>(r.x) + r.width
        && static_cast<long long>(y) < static_cast<long long>(r.y) + r.height;
}
constexpr bool uniqueHitCentres(DAW::mobile::PortraitTransportLayout r)
{
    const PortraitTransportBounds targets[] = {
        r.transport[0], r.transport[1], r.transport[2],
        r.transport[3], r.transport[4], r.transport[5],
        r.fx, r.browser, r.mixer, r.sends, r.click
    };
    for (int i = 0; i < 11; ++i)
    {
        const auto a = targets[i];
        if (a.width <= 0 || a.height <= 0) continue;
        const int cx = a.x + a.width / 2;
        const int cy = a.y + a.height / 2;
        if (!contains(a, cx, cy)) return false;
        for (int j = 0; j < 11; ++j)
            if (i != j && contains(targets[j], cx, cy)) return false;
    }
    return true;
}
// Probe actual adjacent controls at every corner and edge, not only centres.
// This catches one-pixel overlaps that centre-only dispatch checks can miss.
constexpr bool exclusiveHitEdges(DAW::mobile::PortraitTransportLayout r)
{
    const PortraitTransportBounds targets[] = {
        r.transport[0], r.transport[1], r.transport[2],
        r.transport[3], r.transport[4], r.transport[5],
        r.fx, r.browser, r.mixer, r.sends, r.click
    };
    for (int i = 0; i < 11; ++i)
    {
        const auto a = targets[i];
        if (a.width <= 0 || a.height <= 0) continue;
        const int xs[] = { a.x, a.x + a.width / 2, a.x + a.width - 1 };
        const int ys[] = { a.y, a.y + a.height / 2, a.y + a.height - 1 };
        for (const int x : xs)
            for (const int y : ys)
            {
                if (!contains(a, x, y)) return false;
                for (int j = 0; j < 11; ++j)
                    if (i != j && contains(targets[j], x, y)) return false;
            }
    }
    return true;
}
static_assert(exclusiveHitEdges(portraitTransportLayout(320, 144)));
static_assert(exclusiveHitEdges(DAW::mobile::compactPhoneTransportLayout(320, 112)));
static_assert(exclusiveHitEdges(DAW::mobile::compactPhoneTransportLayout(852, 180)));

// Half-open touch bounds: a tap on the right/bottom border belongs to the
// neighbouring target (or none), never both. Include zero-sized and large
// coordinates to catch off-by-one and overflow regressions in hit testing.
constexpr bool hitEdgeSemantics()
{
    constexpr PortraitTransportBounds a{ 10, 20, 44, 44 };
    constexpr PortraitTransportBounds right{ 54, 20, 44, 44 };
    constexpr PortraitTransportBounds below{ 10, 64, 44, 44 };
    constexpr PortraitTransportBounds empty{ 10, 20, 0, 44 };
    constexpr PortraitTransportBounds far{ std::numeric_limits<int>::max() - 44,
                                            10, 44, 44 };
    return contains(a, 10, 20) && contains(a, 53, 63)
        && !contains(a, 9, 20) && !contains(a, 10, 19)
        && !contains(a, 54, 20) && !contains(a, 10, 64)
        && contains(right, 54, 20) && contains(below, 10, 64)
        && !contains(empty, 10, 20)
        && contains(far, std::numeric_limits<int>::max() - 1, 20)
        && !contains(far, std::numeric_limits<int>::max(), 20);
}
static_assert(hitEdgeSemantics());

static_assert(uniqueHitCentres(portraitTransportLayout(320, 144)));
static_assert(uniqueHitCentres(portraitTransportLayout(852, 180)));
static_assert(uniqueHitCentres(DAW::mobile::compactPhoneTransportLayout(320, 112)));
static_assert(uniqueHitCentres(DAW::mobile::compactPhoneTransportLayout(852, 180)));

int main()
{
    for (int w = 10; w <= 32; ++w)
        for (int h = 22; h <= 36; ++h)
            assert(valid(w, h));
    for (int w = 308; w <= 332; ++w)
        for (int h = 132; h <= 152; ++h)
        {
            assert(valid(w, h));
            assert(portraitActionsOrdered(w, h));
            // Pixel-by-pixel resize must not create edge-only touch overlaps.
            assert(exclusiveHitEdges(portraitTransportLayout(w, h)));
        }
    // Exhaustively cross the 189-unit compact shortcut threshold during
    // narrow split-screen resizing; the coarse sweep can skip this boundary.
    // Probe the minimum compact-deck height and the 189-unit shortcut
    // threshold, including the zero-layout guard immediately below h=8.
    for (int w = 18; w <= 24; ++w)
        for (int h = 6; h <= 12; ++h)
            assert(validCompact(w, h));
    for (int w = 180; w <= 200; ++w)
        for (int h = 90; h <= 100; ++h)
        {
            assert(validCompact(w, h));
            // Shortcut widths change at 189 logical units. Probe actual
            // tap ownership at every size, not just non-overlapping bounds.
            assert(exclusiveHitEdges(DAW::mobile::compactPhoneTransportLayout(w, h)));
            assert(uniqueHitCentres(DAW::mobile::compactPhoneTransportLayout(w, h)));
            assert(orderedReadouts(w, h));
        }
    // Adversarial rotation/split-screen sequences: check every intermediate
    // viewport, not only the portrait and landscape endpoints. The layout
    // is pure geometry and must be deterministic when returning to portrait.
    constexpr int widths[] = { 360, 393, 852, 393, 320, 852, 360 };
    constexpr int heights[] = { 144, 144, 180, 96, 112, 180, 144 };
    const auto initialPortrait = portraitTransportLayout(widths[0], heights[0]);
    const auto initialCompact = DAW::mobile::compactPhoneTransportLayout(widths[0], heights[0]);
    for (int i = 0; i < 7; ++i)
    {
        assert(valid(widths[i], heights[i]));
        assert(validCompact(widths[i], heights[i]));
        assert(orderedReadouts(widths[i], heights[i]));
        if (heights[i] >= 144)
            assert(portraitTouchTargets(widths[i], heights[i]));
        if (widths[i] >= 320 && heights[i] >= 96)
            assert(orderedActions(widths[i], heights[i]));
    }
    // Every intermediate orientation must retain uniquely dispatchable targets.
    for (int i = 0; i < 7; ++i)
    {
        assert(uniqueHitCentres(portraitTransportLayout(widths[i], heights[i])));
        assert(exclusiveHitEdges(portraitTransportLayout(widths[i], heights[i])));
        assert(uniqueHitCentres(DAW::mobile::compactPhoneTransportLayout(widths[i], heights[i])));
        assert(exclusiveHitEdges(DAW::mobile::compactPhoneTransportLayout(widths[i], heights[i])));
    }
    const auto returnedPortrait = portraitTransportLayout(widths[6], heights[6]);
    const auto returnedCompact = DAW::mobile::compactPhoneTransportLayout(widths[6], heights[6]);
    // A rotation round-trip must restore the complete hit geometry, not
    // merely x/width: stale y/height can silently redirect taps after resize.
    const auto sameRect = [](PortraitTransportBounds a, PortraitTransportBounds b) {
        return a.x == b.x && a.y == b.y
            && a.width == b.width && a.height == b.height;
    };
    for (int i = 0; i < 6; ++i)
    {
        assert(sameRect(initialPortrait.transport[i], returnedPortrait.transport[i]));
        assert(sameRect(initialCompact.transport[i], returnedCompact.transport[i]));
    }
    assert(sameRect(initialPortrait.fx, returnedPortrait.fx));
    assert(sameRect(initialPortrait.browser, returnedPortrait.browser));
    assert(sameRect(initialPortrait.mixer, returnedPortrait.mixer));
    assert(sameRect(initialPortrait.sends, returnedPortrait.sends));
    assert(sameRect(initialPortrait.click, returnedPortrait.click));
    assert(sameRect(initialCompact.fx, returnedCompact.fx));
    assert(sameRect(initialCompact.browser, returnedCompact.browser));
    assert(sameRect(initialCompact.mixer, returnedCompact.mixer));
    assert(sameRect(initialCompact.sends, returnedCompact.sends));
    assert(sameRect(initialCompact.click, returnedCompact.click));
    for (int i = 0; i < 6; ++i)
    {
        assert(initialPortrait.transport[i].x == returnedPortrait.transport[i].x);
        assert(initialPortrait.transport[i].width == returnedPortrait.transport[i].width);
        assert(initialCompact.transport[i].y == returnedCompact.transport[i].y);
    }
    assert(initialPortrait.mixer.x == returnedPortrait.mixer.x);
    assert(initialPortrait.sends.x == returnedPortrait.sends.x);
    assert(initialCompact.mixer.x == returnedCompact.mixer.x);
    assert(initialCompact.sends.x == returnedCompact.sends.x);
    for (int w = 0; w < 900; w += 7)
        for (int h = 0; h < 500; h += 9)
        {
            assert(valid(w, h));
            assert(validCompact(w, h));
            assert(orderedReadouts(w, h));
            assert(uniqueHitCentres(portraitTransportLayout(w, h)));
            assert(uniqueHitCentres(DAW::mobile::compactPhoneTransportLayout(w, h)));
        }
}
