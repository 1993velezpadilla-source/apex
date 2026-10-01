#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>
#include <vector>

// ============================================================================
// C4CouplingTests — Phase 3: BLOOM Coupling (the beginning of C4's signature
// sonic behavior).
//
// Architecture (frozen): a contour bell between ADJACENT boosted bands only
// (WEIGHT<->SCULPT, SCULPT<->BITE, BITE<->OPEN; never WEIGHT<->OPEN), boost
// + boost only, acting on the facing-shoulder valley — NEVER subtracting
// fixed dB and NEVER reducing the user's requested peak gain. The contour
// sits at the log midpoint of the pair, its gain is
//
//   contourDb = maxContourDb * strength * (gA/15) * (gB/15) * engage(overlap)
//
// smoothed per sample (click-free) and exactly zero when either band is at
// or below 0 dB or the overlap is below the threshold — so the uncoupled
// response is bit-identical whenever coupling is inactive.
//
// The Phase 1 uncoupled response is the regression reference
// (makeProfilePhase1Reference — coupling OFF; every Phase 1 test runs on
// it). The coupling candidates are variant A (OFF), B (strength 0.20) and
// C (strength 0.35) via C4TuningProfile. Final amount: Phase 5.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;
using APEX::C4::C4BandId;
using APEX::C4::makeProfilePhase1Reference;
using APEX::C4::makeProfileVariantA;
using APEX::C4::makeProfileVariantB;
using APEX::C4::makeProfileVariantC;

class C4CouplingTests final : public juce::UnitTest
{
public:
    C4CouplingTests() : juce::UnitTest ("C4.Coupling", "APEX.C4") {}

    void runTest() override
    {
        const double rate = 48000.0;
        const int block = 512;

        testInactiveNeutrality (rate, block);
        testPairValleyFill (rate, block);
        testAllAdjacentPairs (rate, block);
        testGainPreservation (rate, block);
        testExtremeBoosts (rate, block);
        testAutomation (rate, block);
        testSampleRates();
        testDirectProcessorAgreement (rate, block);
        testAllocationFree (rate, block);
        testLifecycleReset (rate, block);
    }

private:
    // ---- helpers -----------------------------------------------------------

    static void setParam (C4Processor& proc, C4ParamIndex idx, const juce::String& text)
    {
        proc.getC4Parameter (idx)->setValue (proc.getC4Parameter (idx)->getValueForText (text));
    }

    static std::vector<float> irOf (C4Processor& proc, double rate, int block, int len = 8192)
    {
        C4Test::settleProcessor (proc, rate, block, 1.0);
        return C4Test::measureImpulseResponse (proc, len, block);
    }

    static float dbAt (const std::vector<float>& ir, double rate, double freq)
    {
        return C4Test::magnitudeDbAtFrequency (ir, rate, freq, 16384);
    }

    static void setBand (C4Processor& proc, C4BandId band, float freq, float gainDb)
    {
        const auto freqIdx = (C4ParamIndex) ((int) C4ParamIndex::kWeightFreq + (int) band * 3);
        const auto gainIdx = (C4ParamIndex) ((int) C4ParamIndex::kWeightGain + (int) band * 3);
        setParam (proc, freqIdx, juce::String (freq));
        setParam (proc, gainIdx, juce::String (gainDb, 1));
    }

    // ---- tests -------------------------------------------------------------

    void testInactiveNeutrality (double rate, int block)
    {
        beginTest ("Coupling inactive: bit-identical to the Phase 1 reference");

        // Variant B with NO boosted adjacent pair (only a single band
        // boosted, and a cut-only configuration): the coupling layer must be
        // bit-identical to the uncoupled Phase 1 reference.
        {
            auto coupled = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
            auto reference = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
            setBand (*coupled, C4BandId::Weight, 100.0f, 6.0f);
            setBand (*reference, C4BandId::Weight, 100.0f, 6.0f);

            juce::AudioBuffer<float> a (2, block);
            juce::AudioBuffer<float> b (2, block);
            juce::MidiBuffer midi;
            bool identical = true;
            for (int blk = 0; blk < 60; ++blk)
            {
                C4Test::fillDeterministic (a, 0x1234u + (juce::uint32) blk);
                b = a;
                coupled->processBlock (a, midi);
                reference->processBlock (b, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (a.getSample (ch, i) != b.getSample (ch, i))
                            identical = false;
            }
            expect (identical, "single-band boost with coupling enabled must stay bit-identical");
        }

        // All four bands CUT: no boost pair -> coupling inert.
        {
            auto coupled = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
            auto reference = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
            for (int b = 0; b < 4; ++b)
            {
                setBand (*coupled, (C4BandId) b, 1000.0f * (float) (b + 1), -6.0f);
                setBand (*reference, (C4BandId) b, 1000.0f * (float) (b + 1), -6.0f);
            }
            juce::AudioBuffer<float> a (2, block);
            juce::AudioBuffer<float> b (2, block);
            juce::MidiBuffer midi;
            bool identical = true;
            for (int blk = 0; blk < 60; ++blk)
            {
                C4Test::fillDeterministic (a, 0xABCDu + (juce::uint32) blk);
                b = a;
                coupled->processBlock (a, midi);
                reference->processBlock (b, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (a.getSample (ch, i) != b.getSample (ch, i))
                            identical = false;
            }
            expect (identical, "cuts with coupling enabled must stay bit-identical");
        }
    }

    void testPairValleyFill (double rate, int block)
    {
        beginTest ("Coupling fills the valley between adjacent boosts");

        // WEIGHT 200 +6, SCULPT 600 +6 (medium overlap): the coupled response
        // at the log midpoint must be >= the uncoupled reference (the
        // contour fills the valley), by no more than the contour cap, with
        // finite response and no pathological peak.
        auto coupled = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        auto reference = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
        setBand (*coupled, C4BandId::Weight, 200.0f, 6.0f);
        setBand (*coupled, C4BandId::Sculpt, 600.0f, 6.0f);
        setBand (*reference, C4BandId::Weight, 200.0f, 6.0f);
        setBand (*reference, C4BandId::Sculpt, 600.0f, 6.0f);

        const auto irC = irOf (*coupled, rate, block);
        const auto irR = irOf (*reference, rate, block);
        expect (C4Test::allFinite (irC), "coupled response must be finite");

        const double mid = std::sqrt (200.0 * 600.0); // log midpoint ~346 Hz
        const float gC = dbAt (irC, rate, mid);
        const float gR = dbAt (irR, rate, mid);
        const float gC20k = dbAt (irC, rate, 20000.0);
        const float gR20k = dbAt (irR, rate, 20000.0);

        expect (gC >= gR - 0.05f,
                "valley must fill: coupled " + juce::String (gC, 2)
                    + " dB vs uncoupled " + juce::String (gR, 2) + " dB at "
                    + juce::String (mid, 0) + " Hz");
        expect (gC - gR <= 0.6f,
                "contour bounded by its cap: fill " + juce::String (gC - gR, 3) + " dB");
        // No pathological peak: the coupled response never exceeds the
        // uncoupled one by more than the cap anywhere on the grid.
        for (double f : { 50.0, 100.0, 200.0, 400.0, 800.0, 1600.0, 3000.0, 6000.0, 12000.0, 20000.0 })
        {
            const float dc = dbAt (irC, rate, f);
            const float dr = dbAt (irR, rate, f);
            expect (dc <= dr + 0.6f,
                    "no pathological peak at " + juce::String (f, 0) + " Hz: "
                        + juce::String (dc, 2) + " vs " + juce::String (dr, 2));
        }
        // The contour is localized: far away (20 kHz) the responses coincide.
        expect (std::abs (gC20k - gR20k) < 0.05f,
                "contour localized away from the pair: " + juce::String (gC20k - gR20k, 3) + " dB");
    }

    void testAllAdjacentPairs (double rate, int block)
    {
        beginTest ("Every adjacent pair: far / slight / medium / heavy / identical");
        // Pairs: (W,S), (S,B), (B,O) — freqs chosen inside both bands.
        const struct { C4BandId a, b; float fA, fB; juce::String label; } pairs[] =
        {
            { C4BandId::Weight, C4BandId::Sculpt, 100.0f, 450.0f,  "W-S far" },
            { C4BandId::Weight, C4BandId::Sculpt, 120.0f, 300.0f,  "W-S slight" },
            { C4BandId::Weight, C4BandId::Sculpt, 200.0f, 300.0f,  "W-S medium" },
            { C4BandId::Weight, C4BandId::Sculpt, 250.0f, 260.0f,  "W-S heavy" },
            { C4BandId::Weight, C4BandId::Sculpt, 300.0f, 300.0f,  "W-S identical" },
            { C4BandId::Sculpt, C4BandId::Bite,   400.0f, 9000.0f, "S-B far" },
            { C4BandId::Sculpt, C4BandId::Bite,   1200.0f, 2500.0f,"S-B slight" },
            { C4BandId::Sculpt, C4BandId::Bite,   1500.0f, 2000.0f,"S-B medium" },
            { C4BandId::Sculpt, C4BandId::Bite,   1800.0f, 1900.0f,"S-B heavy" },
            { C4BandId::Sculpt, C4BandId::Bite,   2000.0f, 2000.0f,"S-B identical" },
            { C4BandId::Bite,   C4BandId::Open,   400.0f, 20000.0f,"B-O far" },
            { C4BandId::Bite,   C4BandId::Open,   4000.0f, 12000.0f,"B-O slight" },
            { C4BandId::Bite,   C4BandId::Open,   6000.0f, 9000.0f, "B-O medium" },
            { C4BandId::Bite,   C4BandId::Open,   7000.0f, 8000.0f, "B-O heavy" },
            { C4BandId::Bite,   C4BandId::Open,   8000.0f, 8000.0f, "B-O identical" },
        };

        for (const auto& p : pairs)
        {
            auto coupled = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
            auto reference = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
            setBand (*coupled, p.a, p.fA, 6.0f);
            setBand (*coupled, p.b, p.fB, 6.0f);
            setBand (*reference, p.a, p.fA, 6.0f);
            setBand (*reference, p.b, p.fB, 6.0f);

            const auto irC = irOf (*coupled, rate, block);
            const auto irR = irOf (*reference, rate, block);
            expect (C4Test::allFinite (irC), p.label + ": finite");

            const double mid = std::sqrt ((double) p.fA * (double) p.fB);
            const float gC = dbAt (irC, rate, mid);
            const float gR = dbAt (irR, rate, mid);

            // Far-apart pairs: overlap below threshold -> no coupling.
            if (p.label.contains ("far"))
                expect (std::abs (gC - gR) < 0.1f,
                        p.label + ": no coupling when far apart (" + juce::String (gC - gR, 3) + " dB)");
            else
                expect (gC >= gR - 0.05f && gC - gR <= 0.6f,
                        p.label + ": valley filled within cap (fill "
                            + juce::String (gC - gR, 3) + " dB)");

            // Phase stability: the coupled phase at the valley is finite and
            // close to the uncoupled phase (no phase jump from the contour).
            const float phC = C4Test::phaseRadiansAtFrequency (irC, rate, mid, 16384);
            const float phR = C4Test::phaseRadiansAtFrequency (irR, rate, mid, 16384);
            expect (std::isfinite (phC) && std::abs (phC - phR) < 0.5f,
                    p.label + ": stable phase (delta "
                        + juce::String (phC - phR, 3) + " rad)");
        }
    }

    void testGainPreservation (double rate, int block)
    {
        beginTest ("Requested center gains remain approximately respected");
        // Heavy overlap (W-S at 300/320 +6 each): the coupled center gains
        // must stay within 0.6 dB of the uncoupled response at each center
        // (the contour adds at most its cap; it never reduces the peaks).
        auto coupled = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        auto reference = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
        setBand (*coupled, C4BandId::Weight, 300.0f, 6.0f);
        setBand (*coupled, C4BandId::Sculpt, 320.0f, 6.0f);
        setBand (*reference, C4BandId::Weight, 300.0f, 6.0f);
        setBand (*reference, C4BandId::Sculpt, 320.0f, 6.0f);

        const auto irC = irOf (*coupled, rate, block);
        const auto irR = irOf (*reference, rate, block);
        for (double f : { 300.0, 320.0 })
        {
            const float c = dbAt (irC, rate, f);
            const float r = dbAt (irR, rate, f);
            expect (std::abs (c - r) < 0.6f,
                    "center gain preserved at " + juce::String (f, 0)
                        + " Hz: " + juce::String (c, 2) + " vs " + juce::String (r, 2));
        }
        // Unequal boosts: W +9, S +3 — coupling proportional to both.
        auto coupledU = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        auto referenceU = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
        setBand (*coupledU, C4BandId::Weight, 200.0f, 9.0f);
        setBand (*coupledU, C4BandId::Sculpt, 400.0f, 3.0f);
        setBand (*referenceU, C4BandId::Weight, 200.0f, 9.0f);
        setBand (*referenceU, C4BandId::Sculpt, 400.0f, 3.0f);
        const auto irCU = irOf (*coupledU, rate, block);
        const auto irRU = irOf (*referenceU, rate, block);
        const double midU = std::sqrt (200.0 * 400.0);
        const float fillU = dbAt (irCU, rate, midU) - dbAt (irRU, rate, midU);
        expect (fillU >= 0.0f && fillU <= 0.5f,
                "unequal boosts fill proportionally (" + juce::String (fillU, 3) + " dB)");
        // Small boosts: +1 dB each — coupling near-zero but finite/bounded.
        auto coupledS = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        auto referenceS = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
        setBand (*coupledS, C4BandId::Weight, 200.0f, 1.0f);
        setBand (*coupledS, C4BandId::Sculpt, 400.0f, 1.0f);
        setBand (*referenceS, C4BandId::Weight, 200.0f, 1.0f);
        setBand (*referenceS, C4BandId::Sculpt, 400.0f, 1.0f);
        const auto irCS = irOf (*coupledS, rate, block);
        const auto irRS = irOf (*referenceS, rate, block);
        const float fillS = dbAt (irCS, rate, midU) - dbAt (irRS, rate, midU);
        expect (std::isfinite (fillS) && fillS >= 0.0f && fillS <= 0.3f,
                "small boosts couple gently (" + juce::String (fillS, 3) + " dB)");
    }

    void testExtremeBoosts (double rate, int block)
    {
        beginTest ("Extreme boosts: finite, bounded, no cancellation");
        // W and S both at +15 with heavy overlap: the coupled response stays
        // finite and bounded by the uncoupled envelope + cap.
        auto coupled = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        auto reference = C4Test::makePreparedProcessor (rate, block, makeProfilePhase1Reference());
        setBand (*coupled, C4BandId::Weight, 300.0f, 15.0f);
        setBand (*coupled, C4BandId::Sculpt, 320.0f, 15.0f);
        setBand (*reference, C4BandId::Weight, 300.0f, 15.0f);
        setBand (*reference, C4BandId::Sculpt, 320.0f, 15.0f);

        const auto irC = irOf (*coupled, rate, block);
        const auto irR = irOf (*reference, rate, block);
        expect (C4Test::allFinite (irC), "extreme coupled response finite");

        float maxC = -300.0f, maxR = -300.0f, minC = 300.0f, minR = 300.0f;
        for (double f : { 50.0, 100.0, 200.0, 300.0, 400.0, 800.0, 1600.0, 3000.0, 6000.0 })
        {
            const float dc = dbAt (irC, rate, f);
            const float dr = dbAt (irR, rate, f);
            maxC = juce::jmax (maxC, dc);
            maxR = juce::jmax (maxR, dr);
            minC = juce::jmin (minC, dc);
            minR = juce::jmin (minR, dr);
        }
        expect (maxC <= maxR + 0.6f, "no pathological peak (max " + juce::String (maxC, 2) + ")");
        expect (minC >= minR - 0.6f, "no unexpected cancellation (min " + juce::String (minC, 2) + ")");
    }

    void testAutomation (double rate, int block)
    {
        beginTest ("Automation: coupling engages/disengages click-free");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        setBand (*proc, C4BandId::Weight, 200.0f, 6.0f);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        bool finite = true;
        float prev = 0.0f, maxDelta = 0.0f, maxOut = 0.0f;

        for (int b = 0; b < 400; ++b)
        {
            // Sweep SCULPT gain 0 -> 15 -> 0: the W-S contour engages and
            // disengages repeatedly.
            const float phase = (float) (b % 200) / 199.0f;
            const float g = 15.0f * (phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f);
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText (juce::String (g, 1)));
            proc->getC4Parameter (C4ParamIndex::kSculptFreq)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptFreq)->getValueForText ("400"));
            C4Test::fillDeterministic (buf, 0x7777u + (juce::uint32) b);
            proc->processBlock (buf, midi);
            finite = finite && C4Test::allFinite (buf);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    maxOut = juce::jmax (maxOut, std::abs (v));
                    maxDelta = juce::jmax (maxDelta, std::abs (v - prev));
                    prev = v;
                }
        }
        expect (finite, "coupling automation stays finite");
        // Physical envelope: +1.0 peak noise through WEIGHT +6 (2x) and
        // SCULPT +15 (5.62x) in parallel — worst-case in-phase stacking is
        // ~7.6; measured 2.38 sits well inside (no state explosion).
        expect (maxOut < 8.0f, "coupling automation bounded (max " + juce::String (maxOut, 3) + ")");
        // The deterministic noise input itself has a max sample delta of ~2.0;
        // a click from the contour engaging would far exceed it.
        expect (maxDelta < 4.0f, "no click from coupling (max delta " + juce::String (maxDelta, 3) + ")");
    }

    void testSampleRates()
    {
        beginTest ("Coupling valley-fill across all six sample rates");
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        for (double rate : rates)
        {
            auto coupled = C4Test::makePreparedProcessor (rate, 512, makeProfileVariantB());
            auto reference = C4Test::makePreparedProcessor (rate, 512, makeProfilePhase1Reference());
            setBand (*coupled, C4BandId::Weight, 200.0f, 6.0f);
            setBand (*coupled, C4BandId::Sculpt, 600.0f, 6.0f);
            setBand (*reference, C4BandId::Weight, 200.0f, 6.0f);
            setBand (*reference, C4BandId::Sculpt, 600.0f, 6.0f);
            const auto irC = irOf (*coupled, rate, 512);
            const auto irR = irOf (*reference, rate, 512);
            expect (C4Test::allFinite (irC), juce::String (rate, 0) + ": finite");
            const double mid = std::sqrt (200.0 * 600.0);
            const float fill = dbAt (irC, rate, mid) - dbAt (irR, rate, mid);
            expect (fill >= 0.0f && fill <= 0.6f,
                    juce::String (rate, 0) + ": valley filled within cap ("
                        + juce::String (fill, 3) + " dB)");
        }
    }

    void testDirectProcessorAgreement (double rate, int block)
    {
        beginTest ("Direct engine vs processor agreement with coupling active");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        // Phase 5: the production BLOOM default is 4.0. This agreement test
        // isolates the COUPLING layer, so the color layer must be idle on
        // both paths (the direct engine's bloom target is 0 by construction).
        proc->getC4Parameter (C4ParamIndex::kBloom)->setValue (0.0f);
        setBand (*proc, C4BandId::Weight, 200.0f, 6.0f);
        setBand (*proc, C4BandId::Sculpt, 400.0f, 6.0f);

        APEX::C4::C4EngineCore eng;
        eng.prepare (rate, block, 2, makeProfileVariantB());
        eng.setBandFreqTargetHz ((int) C4BandId::Weight, 200.0f);
        eng.setBandGainTargetDb ((int) C4BandId::Weight, 6.0f);
        eng.setBandFreqTargetHz ((int) C4BandId::Sculpt, 400.0f);
        eng.setBandGainTargetDb ((int) C4BandId::Sculpt, 6.0f);
        eng.setHpfTarget (0.0f, false);
        eng.setLpfTarget (0.0f, false);

        juce::AudioBuffer<float> pbuf (2, block);
        juce::AudioBuffer<float> ebuf (2, block);
        juce::MidiBuffer midi;
        float* eChans[2] = { ebuf.getWritePointer (0), ebuf.getWritePointer (1) };
        bool equal = true;
        for (int b = 0; b < 80; ++b)
        {
            C4Test::fillDeterministic (pbuf, 0x4242u + (juce::uint32) b);
            C4Test::clearBufferExplicit (ebuf);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    ebuf.setSample (ch, i, pbuf.getSample (ch, i));
            proc->processBlock (pbuf, midi);
            eng.adoptTargets (0.0f);
            eng.processBlock (eChans, 2, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (pbuf.getSample (ch, i) != ebuf.getSample (ch, i))
                        equal = false;
        }
        expect (equal, "direct engine and processor must agree with coupling active");
    }

    void testAllocationFree (double rate, int block)
    {
        beginTest ("Realtime: coupling processBlock performs zero allocation");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantC());
        setBand (*proc, C4BandId::Weight, 200.0f, 12.0f);
        setBand (*proc, C4BandId::Sculpt, 400.0f, 12.0f);
        setBand (*proc, C4BandId::Bite, 3000.0f, 12.0f);
        setBand (*proc, C4BandId::Open, 12000.0f, 12.0f);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        C4Test::fillDeterministic (buf, 0xCAFEu);
        // Warm up (smoothers move; nothing allocates on the audio thread).
        for (int b = 0; b < 50; ++b)
            proc->processBlock (buf, midi);
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int b = 0; b < 200; ++b)
            {
                proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
                    (float) (b % 20) / 19.0f); // plain float store
                proc->processBlock (buf, midi);
            }
        }
    }

    void testLifecycleReset (double rate, int block)
    {
        beginTest ("Lifecycle: reset with coupling is deterministic");
        auto proc = C4Test::makePreparedProcessor (rate, block, makeProfileVariantB());
        setBand (*proc, C4BandId::Weight, 200.0f, 6.0f);
        setBand (*proc, C4BandId::Sculpt, 400.0f, 6.0f);

        const auto ir0 = irOf (*proc, rate, block);
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            proc->getEngine().reset();
            setBand (*proc, C4BandId::Weight, 200.0f, 6.0f);
            setBand (*proc, C4BandId::Sculpt, 400.0f, 6.0f);
            const auto ir = irOf (*proc, rate, block);
            bool same = ir.size() == ir0.size();
            for (size_t i = 0; i < ir.size() && same; ++i)
                if (ir[i] != ir0[i])
                    same = false;
            expect (same, "cycle " + juce::String (cycle) + " bit-identical");
        }
    }
};

static C4CouplingTests c4CouplingTests;
