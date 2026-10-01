#include <JuceHeader.h>

// Include GridLodPolicy — it's defined in ArrangementZoomCore.h which is
// in the Builds directory, reached via the same include path used by the
// ArrangementEditor sources.
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementZoomCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipWaveformCacheCore.h"

/** GridLodPolicy unit tests (2026-07-30).

    Verifies that GridLodPolicy correctly suppresses beat lines and skips
    bar lines at extreme zoom-out.  The policy is a pure stateless function
    of pixelsPerBeat and pixelsPerBar — no BPM or time-signature assumptions
    are baked in.

    Threshold docstring (copied from ArrangementZoomCore.h):
      - beat lines drawn only when pxPerBeat >= 6.0
      - bar step = ceil(24 / pxPerBar), rounded to 1,2,4,8,16,32
*/
class GridLodPolicyTests final : public juce::UnitTest
{
public:
    GridLodPolicyTests()
        : juce::UnitTest("grid.lod-policy.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        using ArrangementEditor::GridLodPolicy;

        // ===============================================================
        // EXTREME ZOOM-OUT: 5 px/sec, 126 BPM (as required)
        // ===============================================================
        // At 126 BPM:        beat = 60.0 / 126 ≈ 0.476190 s
        //                    bar  = 4 * beat ≈ 1.904762 s
        // At 5 px/sec:       pxPerBeat = 5 * 0.476190 ≈ 2.3810
        //                    pxPerBar  = 5 * 1.904762 ≈ 9.5238
        beginTest("extreme zoom-out: 5 px/sec, 126 BPM beats suppressed, barStep>1");
        {
            const double beatDuration = 60.0 / 126.0;     // ≈ 0.476190
            const double barDuration  = beatDuration * 4.0; // ≈ 1.904762
            const double pps = 5.0;
            const double pxPerBeat = beatDuration * pps;   // ≈ 2.381
            const double pxPerBar  = barDuration  * pps;   // ≈ 9.524

            const bool beatsOff = GridLodPolicy::suppressBeats(pxPerBeat);
            const int  barStep  = GridLodPolicy::barStep(pxPerBar);

            expect(beatsOff,
                   "Beats must be suppressed: pxPerBeat="
                   + juce::String(pxPerBeat, 3) + " < 6.0");
            expect(barStep > 1,
                   "barStep must be >1: pxPerBar="
                   + juce::String(pxPerBar, 3)
                   + ", barStep=" + juce::String(barStep));
            // ceil(24/9.524) = 3 → rounded to 4 (next power of two)
            expectEquals(barStep, 4);
        }

        // ===============================================================
        // NORMAL ZOOM: 100 px/sec, 120 BPM
        // ===============================================================
        beginTest("normal zoom: 100 px/sec, 120 BPM — full grid (beats + every bar)");
        {
            const double beatDuration = 60.0 / 120.0; // 0.5
            const double barDuration  = beatDuration * 4.0; // 2.0
            const double pps = 100.0;
            const double pxPerBeat = beatDuration * pps; // 50.0
            const double pxPerBar  = barDuration  * pps; // 200.0

            const bool beats = GridLodPolicy::suppressBeats(pxPerBeat);
            const int  barStep = GridLodPolicy::barStep(pxPerBar);

            expect(!beats, "Beats must NOT be suppressed at normal zoom");
            expectEquals(barStep, 1);
        }

        // ===============================================================
        // BOUNDARY: exactly at threshold (pxPerBeat = 6.0)
        // ===============================================================
        beginTest("threshold boundary: pxPerBeat == 6.0 — beats visible");
        {
            // suppressBeats uses pxPerBeat < 6.0 (strict less-than),
            // so exactly 6.0 should NOT suppress.
            const double pxPerBeat = 6.0;
            const bool   beats = GridLodPolicy::suppressBeats(pxPerBeat);
            expect(!beats, "pxPerBeat=6.0 must NOT suppress (threshold is <, not <=)");
        }

        beginTest("threshold boundary: pxPerBeat == 5.999 — beats suppressed");
        {
            const double pxPerBeat = 5.999;
            const bool   beats = GridLodPolicy::suppressBeats(pxPerBeat);
            expect(beats, "pxPerBeat=5.999 must suppress beats");
        }

        // ===============================================================
        // BAR STEP: verify rounded-to-powers-of-two
        // ===============================================================
        beginTest("barStep rounding: pxPerBar=23 → ceil(24/23)=2 → barStep=2");
        {
            expectEquals(GridLodPolicy::barStep(23.0), 2);
        }

        beginTest("barStep rounding: pxPerBar=12 → ceil(24/12)=2 → barStep=2");
        {
            expectEquals(GridLodPolicy::barStep(12.0), 2);
        }

        beginTest("barStep rounding: pxPerBar=6.0 → ceil(24/6)=4 → barStep=4");
        {
            expectEquals(GridLodPolicy::barStep(6.0), 4);
        }

        beginTest("barStep rounding: pxPerBar=3.0 → ceil(24/3)=8 → barStep=8");
        {
            expectEquals(GridLodPolicy::barStep(3.0), 8);
        }

        beginTest("barStep rounding: pxPerBar=1.5 → ceil(24/1.5)=16 → barStep=16");
        {
            expectEquals(GridLodPolicy::barStep(1.5), 16);
        }

        beginTest("barStep rounding: pxPerBar=0.7 → ceil(24/0.7)=35 → rounded 32 → barStep=32");
        {
            expectEquals(GridLodPolicy::barStep(0.7), 32);
        }

        beginTest("barStep: pxPerBar=0 (degenerate) returns 1");
        {
            expectEquals(GridLodPolicy::barStep(0.0), 1);
        }

        beginTest("barStep: negative pxPerBar returns 1");
        {
            expectEquals(GridLodPolicy::barStep(-1.0), 1);
        }

        // ===============================================================
        // SNAP PRESERVATION: verify at pxPerBeat=1.5 (extremely zoomed out)
        // ===============================================================
        beginTest("extreme zoom: pxPerBeat=1.5 — beats suppressed");
        {
            const bool beats = GridLodPolicy::suppressBeats(1.5);
            expect(beats, "beats must be suppressed at pxPerBeat=1.5");
        }

        beginTest("barStep at wide zoom: pxPerBar=30.0 -> ceil(24/30)=1 -> 1");
        {
            expectEquals(GridLodPolicy::barStep(30.0), 1,
                         "ceil(24/30)=1 therefore barStep must be 1");
        }

        // ===============================================================
        // Consistency: barStep always >= 1 and in {1,2,4,8,16,32}
        // ===============================================================
        beginTest("barStep always returns a power-of-two in [1..32]");
        {
            const int valid[] = {1, 2, 4, 8, 16, 32};
            for (double pxBar = 0.1; pxBar <= 10000.0; pxBar *= 1.3)
            {
                const int s = GridLodPolicy::barStep(pxBar);
                bool found = false;
                for (int v : valid)
                    if (s == v) { found = true; break; }
                expect(found,
                       juce::String("barStep(") + juce::String(pxBar, 1)
                       + ") = " + juce::String(s) + " is not a valid LOD step");
            }
        }

        using ArrangementEditor::ArrangementClipLodPolicy;

        beginTest("clip LOD: normal clip keeps waveform, name, detail, fades, gain and pattern layers");
        {
            constexpr int width = 160;
            constexpr int height = 80;
            expect(ArrangementClipLodPolicy::drawWaveform(width, height));
            expect(ArrangementClipLodPolicy::drawPatternGrid(width, height));
            expect(ArrangementClipLodPolicy::drawClipName(width, height));
            expect(ArrangementClipLodPolicy::drawSecondaryDetails(width, height));
            expect(ArrangementClipLodPolicy::drawFades(width, height));
            expect(ArrangementClipLodPolicy::drawGainLine(width, height));
        }

        beginTest("clip LOD: ultra-tiny clip suppresses expensive layers but policy leaves body/selection to renderer");
        {
            constexpr int width = 4;
            constexpr int height = 20;
            expect(!ArrangementClipLodPolicy::drawWaveform(width, height));
            expect(!ArrangementClipLodPolicy::drawPatternGrid(width, height));
            expect(!ArrangementClipLodPolicy::drawClipName(width, height));
            expect(!ArrangementClipLodPolicy::drawSecondaryDetails(width, height));
            expect(!ArrangementClipLodPolicy::drawFades(width, height));
            expect(!ArrangementClipLodPolicy::drawGainLine(width, height));
        }

        beginTest("clip LOD: threshold transitions are stable at exact boundaries");
        {
            expect(!ArrangementClipLodPolicy::drawWaveform(7, 28));
            expect(ArrangementClipLodPolicy::drawWaveform(8, 28));
            expect(!ArrangementClipLodPolicy::drawClipName(24, 17));
            expect(ArrangementClipLodPolicy::drawClipName(24, 18));
            expect(!ArrangementClipLodPolicy::drawSecondaryDetails(35, 28));
            expect(ArrangementClipLodPolicy::drawSecondaryDetails(36, 28));
            expect(!ArrangementClipLodPolicy::drawFades(17, 24));
            expect(ArrangementClipLodPolicy::drawFades(18, 24));
        }

        beginTest("clip culling: horizontal and vertical viewport edges include overscan only");
        {
            const juce::Rectangle<int> viewport(100, 200, 640, 480);
            const int trackHeight = 80;

            expect(ArrangementClipLodPolicy::intersectsViewport(
                {120, 240, 20, 20}, viewport, trackHeight));
            expect(!ArrangementClipLodPolicy::intersectsViewport(
                {40, 240, 20, 20}, viewport, trackHeight));
            expect(ArrangementClipLodPolicy::intersectsViewport(
                {68, 240, 1, 20}, viewport, trackHeight));
            expect(!ArrangementClipLodPolicy::intersectsViewport(
                {120, 95, 20, 20}, viewport, trackHeight));
            expect(ArrangementClipLodPolicy::intersectsViewport(
                {120, 120, 20, 20}, viewport, trackHeight));
            expect(!ArrangementClipLodPolicy::intersectsViewport(
                {120, 961, 20, 20}, viewport, trackHeight));
        }

        beginTest("waveform cache: deferred data stays idle until eligible activation");
        {
            ArrangementEditor::ClipWaveformCacheCore cache;
            expect(!cache.isThreadRunning());

            juce::AudioBuffer<float> processed(1, 16);
            processed.clear();
            cache.requestPeaksFromBuffer(processed, 64, false);

            expect(cache.hasPendingBuffer());
            expect(!cache.isThreadRunning());

            cache.requestPeaksUpdate();
            expect(cache.isThreadRunning());
        }
    }
};

static GridLodPolicyTests gridLodPolicyTests;
