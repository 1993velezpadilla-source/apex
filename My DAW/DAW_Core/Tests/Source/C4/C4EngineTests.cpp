#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4EngineTests — the C4 curve contract (Phase 1 linear foundation):
//
//   - center-frequency accuracy for every band
//   - gain accuracy at the center / extremes
//   - Q behavior (user Q meaningful; narrower Q narrows the skirt)
//   - gain-dependent geometry (proportional-Q law, exact engine-level checks)
//   - boost/cut asymmetry (cuts more focused in variant B/C)
//   - shelf behavior (WEIGHT low shelf, OPEN high shelf)
//   - mode-transition continuity (bell<->shelf, silent at 0 dB)
//   - response continuity / monotonicity
//   - frequency-law round trips
//
// The engine is fully linear in Phase 1: settled impulse-FFT magnitude equals
// the true transfer response. Low-frequency shelf checks use the steady-sine
// measurement (window-truncation immunity).
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4Parameter;
using APEX::C4::C4TuningProfile;
using APEX::C4::C4EngineCore;
using APEX::C4::kNumBands;
using APEX::C4::C4BandId;
using APEX::C4::C4ParamIndex;
using APEX::C4::makeProfileVariantA;
using APEX::C4::makeProfileVariantB;
using APEX::C4::makeProfileVariantC;

class C4EngineTests final : public juce::UnitTest
{
public:
    C4EngineTests() : juce::UnitTest ("C4.Engine", "APEX.C4") {}

    void runTest() override
    {
        testBandCenterGainAccuracy();
        testGainExtremes();
        testSculptQBehavior();
        testProportionalGeometryLaw();
        testBoostCutAsymmetry();
        testWeightShelf();
        testOpenShelf();
        testModeTransitionContinuity();
        testResponseContinuity();
        testFrequencyLawRoundTrip();
    }

private:
    static C4ParamIndex freqIndexFor (C4BandId band)
    {
        switch (band)
        {
        case C4BandId::Weight: return C4ParamIndex::kWeightFreq;
        case C4BandId::Sculpt: return C4ParamIndex::kSculptFreq;
        case C4BandId::Bite:   return C4ParamIndex::kBiteFreq;
        default:               return C4ParamIndex::kOpenFreq;
        }
    }

    static C4ParamIndex gainIndexFor (C4BandId band)
    {
        switch (band)
        {
        case C4BandId::Weight: return C4ParamIndex::kWeightGain;
        case C4BandId::Sculpt: return C4ParamIndex::kSculptGain;
        case C4BandId::Bite:   return C4ParamIndex::kBiteGain;
        default:               return C4ParamIndex::kOpenGain;
        }
    }

    /** Set one band's freq/gain and return the smoothed center gain (dB). */
    static float centerGainDb (C4Processor& proc, C4BandId band,
                               float f0, float gainDb, double sampleRate,
                               int blockSize)
    {
        proc.getC4Parameter (freqIndexFor (band))->setValue (
            proc.getC4Parameter (freqIndexFor (band))->getValueForText (juce::String (f0)));
        proc.getC4Parameter (gainIndexFor (band))->setValue (
            proc.getC4Parameter (gainIndexFor (band))->getValueForText (juce::String (gainDb, 1)));
        return C4Test::measureGainDb (proc, sampleRate, blockSize, f0);
    }

    void testBandCenterGainAccuracy()
    {
        const double rate = 48000.0;
        const int block = 512;

        struct Case { C4BandId band; float f0; float gain; float tol; };
        const Case cases[] =
        {
            { C4BandId::Weight, 100.0f,  +6.0f, 0.25f },
            { C4BandId::Weight, 300.0f,  -6.0f, 0.25f },
            { C4BandId::Sculpt, 1000.0f, +6.0f, 0.25f },
            { C4BandId::Sculpt, 800.0f,  -6.0f, 0.25f },
            { C4BandId::Bite,   4000.0f, +6.0f, 0.25f },
            { C4BandId::Bite,   2000.0f, -6.0f, 0.25f },
            { C4BandId::Open,   10000.0f,+6.0f, 0.25f },
            { C4BandId::Open,   5000.0f, -6.0f, 0.25f },
        };

        for (const auto& c : cases)
        {
            beginTest ("Center gain accuracy: "
                       + juce::String ((int) c.band) + " @ "
                       + juce::String (c.f0) + " Hz, "
                       + juce::String (c.gain, 1) + " dB");
            auto proc = C4Test::makePreparedProcessor (rate, block);
            const float measured = centerGainDb (*proc, c.band, c.f0, c.gain, rate, block);
            expect (std::abs (measured - c.gain) <= c.tol,
                    "f0=" + juce::String (c.f0) + " expected " + juce::String (c.gain, 2)
                        + " dB, measured " + juce::String (measured, 2) + " dB");
        }
    }

    void testGainExtremes()
    {
        const double rate = 48000.0;
        const int block = 512;
        const float gains[] = { 15.0f, -15.0f, 12.0f, -12.0f };

        for (float g : gains)
        {
            beginTest ("Gain extreme " + juce::String (g, 1) + " dB at center (SCULPT 1 kHz)");
            auto proc = C4Test::makePreparedProcessor (rate, block);
            const float measured = centerGainDb (*proc, C4BandId::Sculpt, 1000.0f, g, rate, block);
            // The TPT bell peak sits slightly off the nominal center at high
            // gains (documented SVF property); 0.35 dB covers it.
            expect (std::abs (measured - g) <= 0.35f,
                    "expected " + juce::String (g, 2) + " dB, measured "
                        + juce::String (measured, 2) + " dB");
        }
    }

    void testSculptQBehavior()
    {
        const double rate = 48000.0;
        const int block = 512;

        beginTest ("SCULPT Q: center gain is Q-independent, skirt narrows with Q");
        auto procLowQ = C4Test::makePreparedProcessor (rate, block);
        auto procHighQ = C4Test::makePreparedProcessor (rate, block);

        for (auto* proc : { procLowQ.get(), procHighQ.get() })
        {
            proc->getC4Parameter (C4ParamIndex::kSculptFreq)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptFreq)->getValueForText ("1000"));
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText ("6.0"));
        }
        procLowQ->getC4Parameter (C4ParamIndex::kSculptQ)->setValue (
            procLowQ->getC4Parameter (C4ParamIndex::kSculptQ)->getValueForText ("0.5"));
        procHighQ->getC4Parameter (C4ParamIndex::kSculptQ)->setValue (
            procHighQ->getC4Parameter (C4ParamIndex::kSculptQ)->getValueForText ("2.0"));

        const float centerLow = C4Test::measureGainDb (*procLowQ, rate, block, 1000.0);
        const float centerHigh = C4Test::measureGainDb (*procHighQ, rate, block, 1000.0);
        const float octLow = C4Test::measureGainDb (*procLowQ, rate, block, 2000.0);
        const float octHigh = C4Test::measureGainDb (*procHighQ, rate, block, 2000.0);

        expect (std::abs (centerLow - 6.0f) < 0.25f, "Q=0.5 center " + juce::String (centerLow, 2));
        expect (std::abs (centerHigh - 6.0f) < 0.25f, "Q=2.0 center " + juce::String (centerHigh, 2));

        const float attenLow = centerLow - octLow;
        const float attenHigh = centerHigh - octHigh;
        expect (attenHigh > attenLow + 3.0f,
                "skirt should narrow with Q: atten(Q=2)=" + juce::String (attenHigh, 2)
                    + " dB, atten(Q=0.5)=" + juce::String (attenLow, 2) + " dB");
    }

    /** Settle an engine with silence and read the smoothed effective Q. */
    static float settledQ (C4EngineCore& engine, int band, double rate, int block)
    {
        juce::AudioBuffer<float> buf (1, block);
        float* chans[1] = { buf.getWritePointer (0) };
        const int total = (int) (rate * 0.3);
        int done = 0;
        while (done < total)
        {
            // Explicit zero-fill: the engine writes raw pointers, which
            // defeat JUCE's flagged clear() (documented contract — the
            // growing-IR root cause).
            C4Test::clearBufferExplicit (buf);
            engine.processBlock (chans, 1, block);
            done += block;
        }
        return engine.getSmoothedBandQ (band);
    }

    void testProportionalGeometryLaw()
    {
        const double rate = 48000.0;
        const int block = 128;
        const C4TuningProfile profile = makeProfileVariantB();

        C4EngineCore engine;
        engine.prepare (rate, block, 1, profile);

        // SCULPT (user Q 1.0): Q_eff = 0.8 * (1 + 0.9 * |g|/15)
        engine.setBandGainTargetDb ((int) C4BandId::Sculpt, 3.0f);
        const float q3 = settledQ (engine, (int) C4BandId::Sculpt, rate, block);
        const float expectedQ3 = 0.8f * (1.0f + 0.9f * (3.0f / 15.0f));
        expect (std::abs (q3 - expectedQ3) < 0.01f,
                "SCULPT +3 Q_eff: expected " + juce::String (expectedQ3, 4)
                    + " measured " + juce::String (q3, 4));

        engine.setBandGainTargetDb ((int) C4BandId::Sculpt, 12.0f);
        const float q12 = settledQ (engine, (int) C4BandId::Sculpt, rate, block);
        const float expectedQ12 = 0.8f * (1.0f + 0.9f * (12.0f / 15.0f));
        expect (std::abs (q12 - expectedQ12) < 0.01f,
                "SCULPT +12 Q_eff: expected " + juce::String (expectedQ12, 4)
                    + " measured " + juce::String (q12, 4));
        expect (q12 > q3, "larger boost must produce a more focused band");

        // WEIGHT (profile base Q 0.75, drive 0.6): no user Q.
        engine.setBandGainTargetDb ((int) C4BandId::Sculpt, 0.0f);
        engine.setBandGainTargetDb ((int) C4BandId::Weight, 12.0f);
        const float w12 = settledQ (engine, (int) C4BandId::Weight, rate, block);
        const float expectedW12 = 0.75f * (1.0f + 0.6f * (12.0f / 15.0f));
        expect (std::abs (w12 - expectedW12) < 0.01f,
                "WEIGHT +12 Q_eff: expected " + juce::String (expectedW12, 4)
                    + " measured " + juce::String (w12, 4));

        // BITE (variant B drive 0.25): nearly constant Q (musical, not
        // surgical) — the drive must be small.
        engine.setBandGainTargetDb ((int) C4BandId::Weight, 0.0f);
        engine.setBandGainTargetDb ((int) C4BandId::Bite, 12.0f);
        const float b12 = settledQ (engine, (int) C4BandId::Bite, rate, block);
        const float expectedB12 = 1.0f * (1.0f + 0.25f * (12.0f / 15.0f));
        expect (std::abs (b12 - expectedB12) < 0.01f,
                "BITE +12 Q_eff: expected " + juce::String (expectedB12, 4)
                    + " measured " + juce::String (b12, 4));
        expect (b12 < 1.3f, "BITE must never become surgical at +12 dB");
    }

    void testBoostCutAsymmetry()
    {
        const double rate = 48000.0;
        const int block = 128;
        const C4TuningProfile profile = makeProfileVariantB();

        beginTest ("Boost/cut asymmetry: cuts more focused (SCULPT variant B)");
        C4EngineCore engine;
        engine.prepare (rate, block, 1, profile);

        engine.setBandGainTargetDb ((int) C4BandId::Sculpt, 12.0f);
        const float qBoost = settledQ (engine, (int) C4BandId::Sculpt, rate, block);

        C4EngineCore engine2;
        engine2.prepare (rate, block, 1, profile);
        engine2.setBandGainTargetDb ((int) C4BandId::Sculpt, -12.0f);
        const float qCut = settledQ (engine2, (int) C4BandId::Sculpt, rate, block);

        expect (qCut > qBoost,
                "cuts must be more controlled/focused than boosts: qCut="
                    + juce::String (qCut, 4) + " qBoost=" + juce::String (qBoost, 4));

        // Variant A is constant Q: no asymmetry at all.
        C4EngineCore engineA;
        engineA.prepare (rate, block, 1, makeProfileVariantA());
        engineA.setBandGainTargetDb ((int) C4BandId::Sculpt, 12.0f);
        const float qBoostA = settledQ (engineA, (int) C4BandId::Sculpt, rate, block);
        expect (std::abs (qBoostA - 0.8f) < 0.01f,
                "variant A must be constant Q (measured " + juce::String (qBoostA, 4) + ")");
    }

    void testWeightShelf()
    {
        const double rate = 96000.0; // headroom above the shelf band
        const int block = 512;

        beginTest ("WEIGHT low shelf: plateau below f0, unity above");
        auto proc = C4Test::makePreparedProcessor (rate, block);
        proc->getC4Parameter (C4ParamIndex::kWeightFreq)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightFreq)->getValueForText ("100"));
        proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("6.0"));
        proc->getC4Parameter (C4ParamIndex::kWeightMode)->setValue (1.0f);

        // Steady-sine at 25 Hz (plateau side) and 5 kHz (unity side).
        const float lowSide = C4Test::measureGainDbSteady (*proc, rate, block, 25.0);
        const float highSide = C4Test::measureGainDb (*proc, rate, block, 5000.0);

        expect (std::abs (lowSide - 6.0f) < 0.6f,
                "shelf plateau at 25 Hz: expected ~+6, measured " + juce::String (lowSide, 2));
        expect (std::abs (highSide) < 0.4f,
                "shelf unity at 5 kHz: expected ~0, measured " + juce::String (highSide, 2));
    }

    void testOpenShelf()
    {
        const double rate = 96000.0;
        const int block = 512;

        beginTest ("OPEN high shelf (Phase 1 plain shelf): plateau above f0, unity below");
        auto proc = C4Test::makePreparedProcessor (rate, block);
        proc->getC4Parameter (C4ParamIndex::kOpenFreq)->setValue (
            proc->getC4Parameter (C4ParamIndex::kOpenFreq)->getValueForText ("10000"));
        proc->getC4Parameter (C4ParamIndex::kOpenGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kOpenGain)->getValueForText ("6.0"));
        proc->getC4Parameter (C4ParamIndex::kOpenMode)->setValue (1.0f);

        // The plateau of a 10 kHz shelf is reached only asymptotically: the
        // 1st-order shelf sits at ~5.3 dB at 2xf0 (measured 5.36 at 20 kHz)
        // and converges to +6 dB several octaves up. Probe the plateau at
        // 4xf0 (40 kHz, below the 48 kHz Nyquist) where the response has
        // converged to within ~0.2 dB.
        const float highSide = C4Test::measureGainDb (*proc, rate, block, 40000.0);
        const float lowSide = C4Test::measureGainDb (*proc, rate, block, 100.0);

        expect (std::abs (highSide - 6.0f) < 0.6f,
                "shelf plateau at 40 kHz: expected ~+6, measured " + juce::String (highSide, 2));
        expect (std::abs (lowSide) < 0.4f,
                "shelf unity at 100 Hz: expected ~0, measured " + juce::String (lowSide, 2));
    }

    void testModeTransitionContinuity()
    {
        const double rate = 48000.0;
        const int block = 256;

        beginTest ("Bell<->Shelf transition: finite, click-safe, converges to the pure shape");

        // WEIGHT carries the Bell/Shelf mode in Phase 1. Reference: settled
        // pure shelf response measured at the 25 Hz plateau (steady sine:
        // 25 Hz is far below f0 where the bell reads ~0 dB and the shelf
        // reads ~+6 dB).
        auto refProc = C4Test::makePreparedProcessor (rate, block);
        refProc->getC4Parameter (C4ParamIndex::kWeightFreq)->setValue (
            refProc->getC4Parameter (C4ParamIndex::kWeightFreq)->getValueForText ("100"));
        refProc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            refProc->getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("6.0"));
        refProc->getC4Parameter (C4ParamIndex::kWeightMode)->setValue (1.0f);
        const float refShelf = C4Test::measureGainDbSteady (*refProc, rate, block, 25.0);

        // Same processor flips mode mid-stream while audio runs.
        auto proc = C4Test::makePreparedProcessor (rate, block);
        proc->getC4Parameter (C4ParamIndex::kWeightFreq)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightFreq)->getValueForText ("100"));
        proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("6.0"));

        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        bool finite = true;
        for (int b = 0; b < 200; ++b)
        {
            C4Test::fillDeterministic (buf, 0x1234u + (juce::uint32) b);
            if (b == 60)
                proc->getC4Parameter (C4ParamIndex::kWeightMode)->setValue (1.0f); // flip mid-stream
            proc->processBlock (buf, midi);
            finite = finite && C4Test::allFinite (buf);
        }
        expect (finite, "mode flip must stay finite");

        const float settled = C4Test::measureGainDbSteady (*proc, rate, block, 25.0);
        expect (std::abs (settled - refShelf) < 0.05f,
                "settled response must equal the pure shelf: ref="
                    + juce::String (refShelf, 3) + " measured=" + juce::String (settled, 3));

        // Mode switch at 0 dB must be silent: output bit-identical to input.
        beginTest ("Mode switch at 0 dB: bit-identical (silent)");
        auto silent = C4Test::makePreparedProcessor (rate, block);
        C4Test::fillDeterministic (buf, 0x99u);
        const auto original = buf;
        silent->getC4Parameter (C4ParamIndex::kWeightMode)->setValue (1.0f);
        silent->processBlock (buf, midi);
        bool identical = true;
        for (int i = 0; i < block; ++i)
            identical = identical && buf.getSample (0, i) == original.getSample (0, i);
        expect (identical, "mode switch at 0 dB must be bit-identical");
    }

    void testResponseContinuity()
    {
        const double rate = 48000.0;
        const int block = 512;

        beginTest ("Bell response continuity and monotonicity (SCULPT +6, Q 1)");
        auto proc = C4Test::makePreparedProcessor (rate, block);
        proc->getC4Parameter (C4ParamIndex::kSculptFreq)->setValue (
            proc->getC4Parameter (C4ParamIndex::kSculptFreq)->getValueForText ("1000"));
        proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText ("6.0"));

        const float gF0 = C4Test::measureGainDb (*proc, rate, block, 1000.0);
        const float gClose = C4Test::measureGainDb (*proc, rate, block, 1050.0);
        const float gOct = C4Test::measureGainDb (*proc, rate, block, 2000.0);
        const float g2Oct = C4Test::measureGainDb (*proc, rate, block, 4000.0);

        expect (std::abs (gF0 - gClose) < 0.6f,
                "response must be locally continuous (|g(f0)-g(1.05 f0)|="
                    + juce::String (std::abs (gF0 - gClose), 3) + ")");
        expect (gF0 > gOct && gOct > g2Oct,
                "bell must be monotonic on the upper skirt");
    }

    void testFrequencyLawRoundTrip()
    {
        beginTest ("Frequency law round trips (norm <-> Hz, per band)");
        auto proc = C4Test::makePreparedProcessor (48000.0, 128);

        struct Case { C4BandId band; float hz; };
        const Case cases[] =
        {
            { C4BandId::Weight, 30.0f },  { C4BandId::Weight, 100.0f },  { C4BandId::Weight, 450.0f },
            { C4BandId::Sculpt, 120.0f }, { C4BandId::Sculpt, 1000.0f }, { C4BandId::Sculpt, 2500.0f },
            { C4BandId::Bite,   400.0f }, { C4BandId::Bite,   3000.0f }, { C4BandId::Bite,   9000.0f },
            { C4BandId::Open,   1500.0f },{ C4BandId::Open,   10000.0f },{ C4BandId::Open,   20000.0f },
        };

        for (const auto& c : cases)
        {
            auto* p = proc->getC4Parameter (freqIndexFor (c.band));
            const float norm = p->getValueForText (juce::String (c.hz));
            // Parse the unit from the TEXT, never from the requested Hz: the
            // float round trip can land a hair below 1000.0 (e.g. 999.99994),
            // which formats as "1000 Hz" — treating that as kHz would scale
            // it by 1000 (the historical 1e+06 failure).
            const juce::String text = p->getText (norm, 1);
            const float parsed = text.getFloatValue();
            const float hz = text.contains ("kHz") ? parsed * 1000.0f : parsed;
            expect (std::abs (hz - c.hz) <= 1.0f,
                    juce::String ((int) c.band) + " " + juce::String (c.hz)
                        + " Hz -> " + juce::String (hz) + " Hz");
        }
    }
};

static C4EngineTests c4EngineTests;
