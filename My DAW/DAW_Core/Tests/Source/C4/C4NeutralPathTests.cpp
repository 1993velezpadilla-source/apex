#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4NeutralPathTests — the SACRED NEUTRAL PATH (spec §6, audit D).
//
// With every band at 0 dB, trims unity, HPF/LPF OFF, BLOOM 0 and Auto Gain
// OFF, C4 must enter the bit-identical fast path. This suite proves it with
// actual sample comparisons and reports the maximum absolute sample
// difference (target: EXACTLY 0.0 — true bit identity, not a tolerance).
//
// Matrix: mono/stereo x {silence, impulse, deterministic random, full-scale
// sine} x block sizes {1, 32, 128, 333, 512, 1024, 4096} x all six sample
// rates, plus:
//   - state save/reload at neutral stays neutral
//   - neutral reached after previously non-neutral settings settle
//   - HPF/LPF settle-OFF, mode flips, bypass engage/disengage at 0 dB
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;

class C4NeutralPathTests final : public juce::UnitTest
{
public:
    C4NeutralPathTests() : juce::UnitTest ("C4.NeutralPath", "APEX.C4") {}

    void runTest() override
    {
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        const int blocks[] = { 1, 32, 128, 333, 512, 1024, 4096 };

        beginTest ("Neutral path: bit-identical across the full matrix");

        double maxDiff = 0.0;
        juce::String worstCase;

        for (double rate : rates)
        {
            for (int block : blocks)
            {
                for (int channels : { 1, 2 })
                {
                    for (int signal = 0; signal < 4; ++signal)
                    {
                        const double diff = measureNeutralDiff (rate, block, channels, signal);
                        if (diff > maxDiff)
                        {
                            maxDiff = diff;
                            worstCase = juce::String (rate) + " Hz, block "
                                      + juce::String (block) + ", ch " + juce::String (channels)
                                      + ", signal " + juce::String (signal);
                        }
                    }
                }
            }
        }

        // TRUE BIT IDENTITY: the fast path must be exactly zero difference.
        // The neutral fast path leaves the buffer untouched, so the diff is
        // exactly 0.0 by construction — no tolerance, no weakening.
        expect (maxDiff == 0.0,
                "neutral must be BIT-IDENTICAL everywhere; max abs diff = "
                    + juce::String (maxDiff, 12) + " at " + worstCase);

        testStateRoundTripNeutral();
        testNeutralAfterNonNeutral();
        testSettleOffTransitions();
        testNeutralGate();
    }

private:
    /** One neutral-path measurement: process `signal` through a fresh
        processor and return the max |out - in|. */
    static double measureNeutralDiff (double rate, int block, int channels, int signal)
    {
        auto proc = C4Test::makePreparedProcessor (rate, block);

        juce::AudioBuffer<float> in (channels, block);
        juce::AudioBuffer<float> out (channels, block);
        juce::MidiBuffer midi;

        switch (signal)
        {
        case 0: // silence
            in.clear();
            break;
        case 1: // impulse
            in.clear();
            in.setSample (0, 0, 1.0f);
            break;
        case 2: // deterministic random
            C4Test::fillDeterministic (in, 0xA9E12026u);
            break;
        default: // full-scale-safe sine (0.5 peak)
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < block; ++i)
                    in.setSample (ch, i, 0.5f * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 997.0 * i / rate));
            break;
        }

        out = in;
        proc->processBlock (out, midi);

        double maxDiff = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float d = std::abs (out.getSample (ch, i) - in.getSample (ch, i));
                if ((double) d > maxDiff)
                    maxDiff = (double) d;
            }
        return maxDiff;
    }

    void testStateRoundTripNeutral()
    {
        beginTest ("State save/reload at neutral stays neutral (bit-identical)");
        const double rate = 48000.0;
        const int block = 512;

        auto proc = C4Test::makePreparedProcessor (rate, block);
        juce::MemoryBlock state;
        proc->getStateInformation (state);

        auto restored = C4Test::makePreparedProcessor (rate, block);
        restored->setStateInformation (state.getData(), (int) state.getSize());

        juce::AudioBuffer<float> in (2, block);
        C4Test::fillDeterministic (in, 0xC4C4C4C4u);
        auto out = in;
        restored->processBlock (out, juce::MidiBuffer());

        double maxDiff = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                maxDiff = juce::jmax (maxDiff, (double) std::abs (
                    out.getSample (ch, i) - in.getSample (ch, i)));
        expect (maxDiff == 0.0, "restored neutral state must be bit-identical");
    }

    void testNeutralAfterNonNeutral()
    {
        beginTest ("Neutral reached after non-neutral settings settle: bit-identical");
        const double rate = 48000.0;
        const int block = 512;

        auto proc = C4Test::makePreparedProcessor (rate, block);

        // Drive the processor hard first.
        proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("12.0"));
        proc->getC4Parameter (C4ParamIndex::kBiteGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kBiteGain)->getValueForText ("-9.0"));
        proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
            proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("80"));
        juce::AudioBuffer<float> buf (2, block);
        C4Test::fillDeterministic (buf, 0x5555u);
        juce::MidiBuffer midi;
        for (int b = 0; b < 100; ++b)
            proc->processBlock (buf, midi);

        // Back to full neutral (0 dB is the NORMALIZED default, not 0).
        proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightGain)->getDefaultValue());
        proc->getC4Parameter (C4ParamIndex::kBiteGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kBiteGain)->getDefaultValue());
        proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (0.0f);

        // Settle: every smoother/crossfade must converge (1 s is far beyond
        // the 25 ms worst-case time constant).
        for (int b = 0; b < (int) (rate / block) + 64; ++b)
            proc->processBlock (buf, midi);

        const auto original = buf;
        proc->processBlock (buf, midi);

        double maxDiff = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                maxDiff = juce::jmax (maxDiff, (double) std::abs (
                    buf.getSample (ch, i) - original.getSample (ch, i)));
        expect (maxDiff == 0.0,
                "settled-from-non-neutral must be bit-identical, max diff = "
                    + juce::String (maxDiff, 12));
    }

    void testSettleOffTransitions()
    {
        beginTest ("HPF/LPF settle-OFF, mode flips and bypass at 0 dB: bit-identical");
        const double rate = 48000.0;
        const int block = 512;

        auto proc = C4Test::makePreparedProcessor (rate, block);
        juce::AudioBuffer<float> buf (2, block);
        C4Test::fillDeterministic (buf, 0x7777u);
        juce::MidiBuffer midi;
        const int settleBlocks = (int) (rate / block) + 64; // ~1.7 s

        // Engage a non-neutral state, let it settle, return to full neutral,
        // settle again, then verify the final block is bit-identical.
        auto engageThenNeutral = [&] (const std::function<void()>& apply,
                                      const std::function<void()>& clear)
        {
            apply();
            for (int b = 0; b < settleBlocks; ++b)
                proc->processBlock (buf, midi);
            clear();
            for (int b = 0; b < settleBlocks; ++b)
                proc->processBlock (buf, midi);
            const auto original = buf;
            proc->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (buf.getSample (ch, i) != original.getSample (ch, i))
                        return false;
            return true;
        };

        // HPF engaged then disengaged at 0 dB.
        expect (engageThenNeutral (
                    [&] { proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
                        proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("120")); },
                    [&] { proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (0.0f); }),
                "HPF on->off settle must return to bit-identity");
        // LPF engaged then disengaged.
        expect (engageThenNeutral (
                    [&] { proc->getC4Parameter (C4ParamIndex::kLpf)->setValue (
                        proc->getC4Parameter (C4ParamIndex::kLpf)->getValueForText ("12000")); },
                    [&] { proc->getC4Parameter (C4ParamIndex::kLpf)->setValue (0.0f); }),
                "LPF on->off settle must return to bit-identity");
        // Mode flip at 0 dB.
        expect (engageThenNeutral (
                    [&] { proc->getC4Parameter (C4ParamIndex::kWeightMode)->setValue (1.0f); },
                    [&] { proc->getC4Parameter (C4ParamIndex::kWeightMode)->setValue (0.0f); }),
                "mode flip at 0 dB settle must return to bit-identity");
        // Bypass engage then disengage.
        expect (engageThenNeutral (
                    [&] { proc->getC4Parameter (C4ParamIndex::kBypass)->setValue (1.0f); },
                    [&] { proc->getC4Parameter (C4ParamIndex::kBypass)->setValue (0.0f); }),
                "bypass on->off settle must return to bit-identity");
    }

    void testNeutralGate()
    {
        beginTest ("Engine neutral gate state transitions");
        auto proc = C4Test::makePreparedProcessor (48000.0, 128);
        juce::AudioBuffer<float> buf (1, 128);
        juce::MidiBuffer midi;

        // Freshly prepared: gate is true.
        expect (proc->getEngine().isSettledNeutral(), "fresh engine must be neutral");

        // Any non-neutral target closes the gate.
        proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("3.0"));
        proc->processBlock (buf, midi);
        expect (! proc->getEngine().isSettledNeutral(), "boosted band must close the gate");

        // Returning to neutral re-opens it after settling (0 dB is the
        // normalized default, not 0).
        proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightGain)->getDefaultValue());
        for (int b = 0; b < 1000; ++b)
            proc->processBlock (buf, midi);
        expect (proc->getEngine().isSettledNeutral(), "settled neutral must re-open the gate");
    }
};

static C4NeutralPathTests c4NeutralPathTests;
