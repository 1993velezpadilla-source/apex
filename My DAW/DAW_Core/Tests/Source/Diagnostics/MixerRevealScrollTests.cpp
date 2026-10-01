#include <JuceHeader.h>

// Include the production mixer panel that owns the pure reveal math.
#include "../../Source/UICore/MixerPanel.h"

//==============================================================================
/**
    @test    mixer.reveal-scroll.v1
    @verify  DAW::MixerPanel::computeRevealScrollX implements the keyboard
             navigation reveal rule used by MainComponent::scrollMixerViewportToTrack:

             - strip fully visible  -> scroll position unchanged
             - strip off right      -> minimum positive scroll to reveal
             - strip off left       -> minimum negative scroll to reveal
             - first track          -> clamp at 0
             - last track           -> never exceed maximum scroll extent
             - repeated navigation  -> follows selection, no oscillation/recentering

    The helper is static and side-effect free; the MainComponent integration
    (mixer-open guard, mixer-focus guard) is verified by code inspection and
    manual GUI validation.
*/
class MixerRevealScrollTests : public juce::UnitTest
{
public:
    MixerRevealScrollTests()
        : juce::UnitTest ("mixer.reveal-scroll.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::MixerPanel;

        // Strip layout mirrors MixerPanel: strip i starts at i*(kStripW+kStripGap).
        constexpr int kStripW   = DAW::MixerPanel::kStripW;   // 80
        constexpr int kStripGap = DAW::MixerPanel::kStripGap; // 2
        constexpr int kPitch    = kStripW + kStripGap;        // 82
        constexpr int kViewW    = 600;

        auto stripX = [kPitch](int i) { return i * kPitch; };

        beginTest ("Fully visible strip leaves scroll position unchanged");
        {
            // Strip 0 at the origin, viewport at 0.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (0), kStripW, kViewW, 0, 1000),
                          0, "first strip visible at scroll 0");
            // Strip 5 fully inside the viewport.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (5), kStripW, kViewW, 0, 1000),
                          0, "mid strip fully visible -> no scroll");
            // Mid-range scroll position, strip fully inside.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (4), kStripW, kViewW, 300, 1000),
                          300, "visible strip at scroll 300 -> unchanged");
            // Strip exactly flush with the right edge is fully visible.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (6), kStripW, kViewW, 0, 1000),
                          0, "strip flush with right edge -> no scroll");
        }

        beginTest ("Strip off-screen to the right scrolls the minimum positive delta");
        {
            // Strip 7: x=574, right edge 654 > 600 -> reveal 54.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (7), kStripW, kViewW, 0, 1000),
                          54, "strip 7 revealed with minimum delta");
            // Strip 10: x=820 -> reveal 300.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (10), kStripW, kViewW, 0, 1000),
                          300, "strip 10 revealed with minimum delta");
            // Partially visible strip (right edge clipped) -> reveal the clipped part only.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (1), kStripW, 100, 0, 1000),
                          62, "partially clipped strip revealed by the clipped amount");
        }

        beginTest ("Pinned-Master reveal uses the full preferred strip width");
        {
            constexpr int masterW = 170;
            constexpr int viewportW = 662;
            constexpr int safeW = viewportW - masterW - DAW::MixerPanel::kStripGap;
            constexpr int partialX = 530;
            constexpr int currentX = 82;
            constexpr int clippedW = 42;

            // At this position the Master hard wall leaves only 42 rendered
            // pixels of the strip.  That clipped width must not be used as
            // proof that the full 80-pixel strip is visible.
            expectEquals (MixerPanel::computeRevealScrollX (partialX, clippedW, safeW,
                                                             currentX, 1000),
                          currentX,
                          "clipped render width alone would falsely pass");
            expectEquals (MixerPanel::computeRevealScrollX (partialX, kStripW, safeW,
                                                             currentX, 1000),
                          120,
                          "full preferred width reveals the strip before Master");
            expect (partialX >= 120 && partialX + kStripW <= 120 + safeW,
                    "full preferred strip width fits after the minimum reveal");
        }

        beginTest ("Strip off-screen to the left scrolls the minimum negative delta");
        {
            // Strip 3 (x=246) with viewport scrolled to 500 -> reveal 246.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (3), kStripW, kViewW, 500, 1000),
                          246, "strip 3 revealed from the left");
            // Strip 7 (x=574) with viewport scrolled to 600 -> reveal 574.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (7), kStripW, kViewW, 600, 1000),
                          574, "strip 7 revealed from the left");
        }

        beginTest ("First track clamps at scroll 0");
        {
            expectEquals (MixerPanel::computeRevealScrollX (stripX (0), kStripW, kViewW, 500, 1000),
                          0, "first strip clamps to 0");
            expectEquals (MixerPanel::computeRevealScrollX (stripX (0), kStripW, kViewW, 0, 1000),
                          0, "first strip at scroll 0 stays 0");
        }

        beginTest ("Last track never exceeds the maximum scroll extent");
        {
            // Strip 20: x=1640, right edge 1720 -> naive reveal 1120 > maxX 1000.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (20), kStripW, kViewW, 900, 1000),
                          1000, "reveal clamps to maximum scroll extent");
            // Degenerate maxX (content not wider than viewport) -> 0.
            expectEquals (MixerPanel::computeRevealScrollX (stripX (20), kStripW, kViewW, 0, 0),
                          0, "degenerate maxX clamps to 0");
        }

        beginTest ("Repeated keyboard navigation follows selection without oscillation");
        {
            // Simulate stepping down the strips: each reveal is monotonic and
            // never re-centers (a re-centering implementation would jump back
            // toward the middle and oscillate).
            int curX = 0;
            const int prevX = curX;
            for (int i = 7; i <= 12; ++i)
            {
                const int newX = MixerPanel::computeRevealScrollX (stripX (i), kStripW, kViewW, curX, 2000);
                expect (newX >= curX, "downward navigation never scrolls backwards");
                expect (newX <= stripX (i) + kStripW - kViewW, "reveal never overshoots the strip");
                curX = newX;
            }
            expect (curX > prevX, "navigation actually advanced the viewport");

            // Stepping back up: monotonic decrease, no oscillation. Every
            // strip 7..12 is ALREADY fully visible at the current scroll
            // position, so the viewport must NOT scroll back (the requirement
            // is "do not scroll when already visible" — not "return to 0").
            for (int i = 12; i >= 7; --i)
            {
                const int newX = MixerPanel::computeRevealScrollX (stripX (i), kStripW, kViewW, curX, 2000);
                expect (newX <= curX, "upward navigation never scrolls forwards");
                curX = newX;
            }
            // Final scroll equals the reveal of strip 12 (12*82 + 80 - 600 = 464):
            // strip 7 (x=574) is fully visible there, so no backward scroll.
            expectEquals (curX, stripX (12) + kStripW - kViewW,
                          "already-visible strips cause no backward scroll");
        }

        beginTest ("Reveal is idempotent once the strip is visible");
        {
            // After revealing strip 7 (54), calling again with the same state
            // must not move the viewport (no jitter on repeated keys).
            const int revealed = MixerPanel::computeRevealScrollX (stripX (7), kStripW, kViewW, 0, 1000);
            expectEquals (MixerPanel::computeRevealScrollX (stripX (7), kStripW, kViewW, revealed, 1000),
                          revealed, "second call with visible strip is a no-op");
        }
    }
};

static MixerRevealScrollTests mixerRevealScrollTests;
