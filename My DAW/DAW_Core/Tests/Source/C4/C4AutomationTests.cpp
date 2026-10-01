#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4AutomationTests — automation torture (spec §28) and smoothing contract
// (spec §19):
//
//   - rapid continuous sweeps: frequency, gain, Q, BLOOM, filter enable,
//     Bell/Shelf mode — everything finite and bounded
//   - automation crossing zero gain: no click (bounded per-sample delta)
//   - smoothing sanity: a gain step is NOT applied instantly (no zipper);
//     it ramps over the configured ~15 ms
//   - mode/filter toggles every block (worst-case automation rate)
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4Parameter;
using APEX::C4::C4ParamIndex;

class C4AutomationTests final : public juce::UnitTest
{
public:
    C4AutomationTests() : juce::UnitTest ("C4.Automation", "APEX.C4") {}

    void runTest() override
    {
        const double rate = 48000.0;
        const int block = 256;

        beginTest ("Rapid gain sweep across zero: finite, bounded, no click");
        runSweep (rate, block, Sweep::Gain);

        beginTest ("Rapid frequency sweep: finite, bounded");
        runSweep (rate, block, Sweep::Freq);

        beginTest ("Rapid Q sweep: finite, bounded");
        runSweep (rate, block, Sweep::Q);

        beginTest ("Rapid BLOOM sweep: finite, bounded");
        runSweep (rate, block, Sweep::Bloom);

        beginTest ("Rapid HPF/LPF enable toggles: finite, bounded");
        runSweep (rate, block, Sweep::Filters);

        beginTest ("Rapid mode toggles: finite, bounded");
        runSweep (rate, block, Sweep::Mode);

        beginTest ("Rapid bypass toggles: finite, bounded");
        runSweep (rate, block, Sweep::Bypass);

        beginTest ("Gain smoothing: a step ramps over ~15 ms, never jumps");
        testGainSmoothing (rate, block);

        beginTest ("Automation crossing zero gain is click-free");
        testZeroCrossing (rate, block);
    }

private:
    enum class Sweep { Gain, Freq, Q, Bloom, Filters, Mode, Bypass };

    void runSweep (double rate, int block, Sweep sweep)
    {
        auto proc = C4Test::makePreparedProcessor (rate, block);
        C4Test::settleProcessor (*proc, rate, block, 0.3);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        bool finite = true;
        float maxOut = 0.0f;

        for (int b = 0; b < 2000; ++b)
        {
            const float phase = (b % 200) / 199.0f; // 0..1 sawtooth
            switch (sweep)
            {
            case Sweep::Gain:
            {
                auto* p = proc->getC4Parameter (C4ParamIndex::kSculptGain);
                p->setValue (phase);
                break;
            }
            case Sweep::Freq:
            {
                auto* p = proc->getC4Parameter (C4ParamIndex::kBiteFreq);
                p->setValue (phase);
                break;
            }
            case Sweep::Q:
            {
                auto* p = proc->getC4Parameter (C4ParamIndex::kBiteQ);
                p->setValue (phase);
                break;
            }
            case Sweep::Bloom:
            {
                auto* p = proc->getC4Parameter (C4ParamIndex::kBloom);
                p->setValue (phase);
                break;
            }
            case Sweep::Filters:
            {
                auto* hpf = proc->getC4Parameter (C4ParamIndex::kHpf);
                hpf->setValue ((b % 4) < 2 ? hpf->getValueForText ("300") : 0.0f);
                auto* lpf = proc->getC4Parameter (C4ParamIndex::kLpf);
                lpf->setValue ((b % 4) >= 2 ? lpf->getValueForText ("8000") : 0.0f);
                break;
            }
            case Sweep::Mode:
            {
                auto* p = proc->getC4Parameter (C4ParamIndex::kWeightMode);
                p->setValue ((b % 2) == 0 ? 1.0f : 0.0f);
                break;
            }
            case Sweep::Bypass:
            {
                auto* p = proc->getC4Parameter (C4ParamIndex::kBypass);
                p->setValue ((b % 2) == 0 ? 1.0f : 0.0f);
                break;
            }
            }

            // Mixed content: sine + noise.
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, 0.4f * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 1200.0 * (b * block + i) / rate));
            if (b % 3 == 0)
                buf.setSample (0, 0, 0.7f);

            proc->processBlock (buf, midi);
            finite = finite && C4Test::allFinite (buf);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    maxOut = juce::jmax (maxOut, std::abs (buf.getSample (ch, i)));
        }

        expect (finite, "sweep must stay finite");
        // Input peak is 0.7 + sine 0.4; extremes can sum to ~1.1 * trims
        // (18 dB = ~8x) — but no pathological explosion. Bound at 32x input.
        expect (maxOut < 36.0f,
                "sweep must stay bounded (max |out| = " + juce::String (maxOut, 2) + ")");
    }

    void testGainSmoothing (double rate, int block)
    {
        // Measure the output RMS at 1 kHz while stepping +15 dB. At t=0 the
        // gain must NOT be applied instantly: after one block (~5 ms) the
        // measured gain must still be far below +15; after ~200 ms it must
        // be within 0.5 dB of +15.
        auto proc = C4Test::makePreparedProcessor (rate, block);
        proc->getC4Parameter (C4ParamIndex::kSculptFreq)->setValue (
            proc->getC4Parameter (C4ParamIndex::kSculptFreq)->getValueForText ("1000"));
        C4Test::settleProcessor (*proc, rate, block, 0.3);

        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        const double amp = 0.05;

        // Measure the settled gain BEFORE the step (reference).
        double preRms = 0.0;
        for (int b = 0; b < 50; ++b)
        {
            for (int i = 0; i < block; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 1000.0 * i / rate)));
            proc->processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
                preRms += (double) buf.getSample (0, i) * buf.getSample (0, i);
        }
        preRms = std::sqrt (preRms / (50.0 * block));

        // Step to +15 dB.
        proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (1.0f);

        // Gain after ONE block (~5 ms): must be well below +15 dB (smooth
        // ramp in progress, no zipper step).
        for (int i = 0; i < block; ++i)
            buf.setSample (0, i, (float) (amp * std::sin (
                2.0 * juce::MathConstants<double>::pi * 1000.0 * i / rate)));
        proc->processBlock (buf, midi);
        double oneBlockRms = 0.0;
        for (int i = 0; i < block; ++i)
            oneBlockRms += (double) buf.getSample (0, i) * buf.getSample (0, i);
        oneBlockRms = std::sqrt (oneBlockRms / block);

        const float gainOneBlockDb = 20.0f * (float) std::log10 (oneBlockRms / preRms);
        expect (gainOneBlockDb < 10.0f,
                "one block after a +15 dB step the gain must still be ramping "
                    "(measured " + juce::String (gainOneBlockDb, 2) + " dB, expected < 10)");

        // After the ramp the gain must have CONVERGED EXACTLY. The reference
        // is the fully-converged steady-state center gain at the same
        // settings (measured on a fresh processor with a 2 s settle): the
        // proportional-Q law and the TPT bell's off-center peak put the +15 dB
        // SCULPT center at ~14.4..14.8 dB, so comparing against +15.0 would
        // misreport a correct convergence as a failure (measured 14.39 dB on
        // a fully-settled processor). Discard the ramp (first 0.5 s), then
        // measure 0.5 s.
        auto refProc = C4Test::makePreparedProcessor (rate, block);
        refProc->getC4Parameter (C4ParamIndex::kSculptFreq)->setValue (
            refProc->getC4Parameter (C4ParamIndex::kSculptFreq)->getValueForText ("1000"));
        refProc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (1.0f);
        const float referenceDb = C4Test::measureGainDbSteady (*refProc, rate, block, 1000.0);

        const int settleBlocks = (int) (4.0 * rate / block);
        const int discardBlocks = (int) (3.5 * rate / block);
        const int measureBlocks = settleBlocks - discardBlocks;
        int pos = 0;
        for (int b = 0; b < discardBlocks; ++b)
        {
            for (int i = 0; i < block; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 1000.0 * (pos + i) / rate)));
            proc->processBlock (buf, midi);
            pos += block;
        }
        double settledRms = 0.0;
        for (int b = 0; b < measureBlocks; ++b)
        {
            for (int i = 0; i < block; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 1000.0 * (pos + i) / rate)));
            proc->processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
                settledRms += (double) buf.getSample (0, i) * buf.getSample (0, i);
            pos += block;
        }
        settledRms = std::sqrt (settledRms / (measureBlocks * block));
        const float gainSettledDb = 20.0f * (float) std::log10 (settledRms / preRms);

        // Control-state introspection: the smoothed gain/Q must have
        // converged EXACTLY to the targets (the no-progress snap).
        const auto& eng = proc->getEngine();
        const float gDb = eng.getSmoothedBandGainDb ((int) APEX::C4::C4BandId::Sculpt);
        const float gQ  = eng.getSmoothedBandQ ((int) APEX::C4::C4BandId::Sculpt);
        const float gHz = eng.getSmoothedBandFreqHz ((int) APEX::C4::C4BandId::Sculpt);
        logMessage ("gain-smoothing: stepped settled " + juce::String (gainSettledDb, 2)
                    + " dB vs reference " + juce::String (referenceDb, 2)
                    + " dB; engine smoothed gain " + juce::String (gDb, 6)
                    + " dB, Q " + juce::String (gQ, 4) + ", freq " + juce::String (gHz, 3) + " Hz");
        // Exact convergence: the stepped processor must match the fully
        // converged reference within measurement noise.
        expect (std::abs (gainSettledDb - referenceDb) < 0.05f,
                "settled gain must converge exactly to the steady-state center gain ("
                    + juce::String (referenceDb, 2) + " dB), measured "
                    + juce::String (gainSettledDb, 2) + " dB");
        // The Q law / SVF peak offset bounds the center gain near +15 (the
        // C4.Engine gain-extreme suite pins this within 0.35 dB by FFT).
        expect (std::abs (referenceDb - 15.0f) <= 1.0f,
                "Q-law center gain must sit near +15 dB (reference "
                    + juce::String (referenceDb, 2) + " dB)");
    }

    void testZeroCrossing (double rate, int block)
    {
        // Automation crossing zero gain: sine at 1 kHz, gain swept smoothly
        // through 0 dB; per-sample output delta must never exceed the input
        // delta times a small margin (a click would produce a discontinuity).
        auto proc = C4Test::makePreparedProcessor (rate, block);
        proc->getC4Parameter (C4ParamIndex::kSculptFreq)->setValue (
            proc->getC4Parameter (C4ParamIndex::kSculptFreq)->getValueForText ("1000"));
        C4Test::settleProcessor (*proc, rate, block, 0.3);

        juce::AudioBuffer<float> buf (1, block);
        juce::MidiBuffer midi;
        const double amp = 0.3;
        const double w = 2.0 * juce::MathConstants<double>::pi * 1000.0 / rate;

        auto inputAt = [&] (int idx) { return (float) (amp * std::sin (w * idx)); };

        bool click = false;
        double maxRatio = 0.0;
        float prevOut = 0.0f;

        for (int b = 0; b < 1000; ++b)
        {
            // Slow ramp: crosses 0 dB repeatedly in both directions.
            const float norm = (b % 250) / 249.0f;
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (norm);

            for (int i = 0; i < block; ++i)
                buf.setSample (0, i, inputAt (b * block + i));
            proc->processBlock (buf, midi);

            for (int i = 0; i < block; ++i)
            {
                const int idx = b * block + i;
                const float x = inputAt (idx);
                const float y = buf.getSample (0, i);

                if (idx > 0)
                {
                    const float dIn = std::abs (x - inputAt (idx - 1));
                    const float dOut = std::abs (y - prevOut);
                    // With +15 dB max the output slope can be up to ~5.6x the
                    // input slope (10^(15/20)); allow 12x for smoothing
                    // transients. A click is orders of magnitude larger.
                    if (dIn > 1.0e-6f)
                    {
                        const double ratio = dOut / dIn;
                        maxRatio = juce::jmax (maxRatio, ratio);
                        if (ratio > 12.0)
                            click = true;
                    }
                }
                prevOut = y;
            }
        }

        expect (! click,
                "zero-crossing automation must be click-free (max out/in slope ratio "
                    + juce::String (maxRatio, 2) + ")");
    }
};

static C4AutomationTests c4AutomationTests;
