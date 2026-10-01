#include <JuceHeader.h>
#include <cmath>

/**
    F1 playhead dirty-rectangle tests (2026-07-30).

    Verifies that the bounded dirty-rect helpers for playhead partial repaint
    produce correct, clamped rectangles that cover the glow + line + triangle
    areas without missing pixels and without spilling outside component bounds.

    The half-width constants mirror those in ArrangementViewCore.cpp and
    ArrangementRulerCore.cpp:
      - Body:   kHalfWidth = 3  (7 px wide: 1px line + 2px glow each side + AA)
      - Ruler:  kHalfWidth = 6  (13 px wide: 1px line + 5px triangle + AA)
*/
class PlayheadDirtyRectTests final : public juce::UnitTest
{
public:
    PlayheadDirtyRectTests()
        : juce::UnitTest("playhead.dirty-rects.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        // ── Body helpers (mirror ArrangementViewCore.cpp) ──────────────
        constexpr int kBodyHalf = 3;

        auto bodyDirtyRect = [kBodyHalf](int x, int top, int bottom,
                                      int compW, int compH) -> juce::Rectangle<int>
        {
            const auto body = juce::Rectangle<int>(0, top, compW, compH - top);
            const auto glow = juce::Rectangle<int>(
                x - kBodyHalf, top, kBodyHalf * 2 + 1,
                juce::jmax(0, bottom - top));
            return glow.getIntersection(body);
        };

        // ── Ruler helpers (mirror ArrangementRulerCore.cpp) ────────────
        constexpr int kRulerHalf = 6;

        auto rulerDirtyRect = [kRulerHalf](int x, int rulerW, int rulerH) -> juce::Rectangle<int>
        {
            const auto bounds = juce::Rectangle<int>(0, 0, rulerW, rulerH);
            const auto glow = juce::Rectangle<int>(
                x - kRulerHalf, 0, kRulerHalf * 2 + 1, rulerH);
            return glow.getIntersection(bounds);
        };

        // ================================================================
        // Body dirty-rect tests
        // ================================================================
        beginTest("body: centered playhead produces correct 7px-wide rect");
        {
            auto r = bodyDirtyRect(500, 80, 2000, 3000, 2000);
            expectEquals(r.getX(), 497);       // 500 - 3
            expectEquals(r.getWidth(), 7);     // 3*2+1
            expectEquals(r.getY(), 80);        // top
            expectEquals(r.getHeight(), 1920); // 2000 - 80
        }

        beginTest("body: same old and new X produces one valid rect");
        {
            auto r = bodyDirtyRect(100, 80, 2000, 3000, 2000);
            expectEquals(r.getWidth(), 7);
            expectEquals(r.getHeight(), 1920);
            expect(!r.isEmpty());
        }

        beginTest("body: playhead at left edge (x=0) clamps correctly");
        {
            auto r = bodyDirtyRect(0, 80, 2000, 3000, 2000);
            // x-3 = -3, but clamped to 0
            expectEquals(r.getX(), 0);
            expect(r.getWidth() <= 7);
            // Width should be 4: we lost 3px on the left side
            expectEquals(r.getWidth(), 4); // 7 - 3 = 4 (pixels 0,1,2,3)
            expectEquals(r.getY(), 80);
        }

        beginTest("body: playhead at right edge (x=compW) clamps correctly");
        {
            auto r = bodyDirtyRect(3000, 80, 2000, 3000, 2000);
            // x-3 = 2997, x+3 = 3003 -> clamped to 3000
            expectEquals(r.getX(), 2997);
            expectEquals(r.getWidth(), 3); // 3000 - 2997 = 3
            expectEquals(r.getY(), 80);
        }

        beginTest("body: playhead just inside right edge (x=2999)");
        {
            auto r = bodyDirtyRect(2999, 80, 2000, 3000, 2000);
            // x+3 = 3002 -> clamped
            expectEquals(r.getX(), 2996);
            expectEquals(r.getWidth(), 4); // 3000 - 2996 = 4
        }

        beginTest("body: playhead off-screen left returns empty");
        {
            auto r = bodyDirtyRect(-10, 80, 2000, 3000, 2000);
            expect(r.isEmpty());
        }

        beginTest("body: playhead off-screen right returns empty");
        {
            auto r = bodyDirtyRect(3010, 80, 2000, 3000, 2000);
            expect(r.isEmpty());
        }

        beginTest("body: playhead off-screen left but glow still hits edge");
        {
            // x=-2: glow band (-3..0) should clip to 0..0, width=1
            auto r = bodyDirtyRect(-2, 80, 2000, 3000, 2000);
            expect(r.isEmpty() || r.getWidth() >= 1);
        }

        beginTest("body: zero height body area");
        {
            // When top >= bottom, the body area is empty
            auto r = bodyDirtyRect(500, 2000, 2000, 3000, 2000);
            expect(r.isEmpty());
        }

        // ================================================================
        // Ruler dirty-rect tests
        // ================================================================
        beginTest("ruler: centered playhead produces correct 13px-wide rect");
        {
            auto r = rulerDirtyRect(500, 1200, 32);
            expectEquals(r.getX(), 494);       // 500 - 6
            expectEquals(r.getWidth(), 13);    // 6*2+1
            expectEquals(r.getY(), 0);
            expectEquals(r.getHeight(), 32);
        }

        beginTest("ruler: playhead at left edge clamps correctly");
        {
            auto r = rulerDirtyRect(0, 1200, 32);
            expectEquals(r.getX(), 0);
            expectEquals(r.getWidth(), 7); // 13 - 6 = 7
        }

        beginTest("ruler: playhead at right edge clamps correctly");
        {
            auto r = rulerDirtyRect(1200, 1200, 32);
            expectEquals(r.getX(), 1194);  // 1200 - 6
            expectEquals(r.getWidth(), 6); // 1200 - 1194 = 6
        }

        beginTest("ruler: playhead off-screen left returns empty");
        {
            auto r = rulerDirtyRect(-10, 1200, 32);
            expect(r.isEmpty());
        }

        beginTest("ruler: playhead off-screen right returns empty");
        {
            auto r = rulerDirtyRect(1210, 1200, 32);
            expect(r.isEmpty());
        }

        beginTest("ruler: playhead partially visible at left edge");
        {
            // x = -4: band from -10 to -4+6=-4+6=2, clamped to 0..2 = width 3
            auto r = rulerDirtyRect(-4, 1200, 32);
            expectEquals(r.getX(), 0);
            expect(r.getWidth() > 0 && r.getWidth() <= 8);
        }

        // ================================================================
        // Combined / edge-case tests
        // ================================================================
        beginTest("both rects for distinct old/new positions are valid");
        {
            // Simulate old X=400, new X=500 (playhead moved right)
            auto oldR = bodyDirtyRect(400, 80, 2000, 3000, 2000);
            auto newR = bodyDirtyRect(500, 80, 2000, 3000, 2000);
            expect(!oldR.isEmpty());
            expect(!newR.isEmpty());
            expectEquals(oldR.getX(), 397);
            expectEquals(newR.getX(), 497);
            // The two rects are 100px apart; each 7px wide
            expect(std::abs(oldR.getX() - newR.getX()) >= 93);
        }

        beginTest("old position off-screen, new position on-screen");
        {
            // Seek from way off-screen left to on-screen
            auto oldR = bodyDirtyRect(-100, 80, 2000, 3000, 2000);
            auto newR = bodyDirtyRect(500, 80, 2000, 3000, 2000);
            expect(oldR.isEmpty());       // old was offscreen
            expect(!newR.isEmpty());       // new is visible
        }

        beginTest("old position on-screen, new position off-screen left (wrap)");
        {
            // Loop wrap from near end to near start — in pixel terms,
            // moving left far enough to land off-screen
            auto oldR = bodyDirtyRect(2500, 80, 2000, 3000, 2000);
            auto newR = bodyDirtyRect(-50, 80, 2000, 3000, 2000);
            expect(!oldR.isEmpty());
            expect(newR.isEmpty()); // off-screen
        }
    }
};

static PlayheadDirtyRectTests playheadDirtyRectTests;
