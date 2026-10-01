#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4SampleRateTests — mandatory validation at 44.1/48/88.2/96/176.4/192 kHz
// (spec §18). Never assume behavior validated at 48 kHz works elsewhere:
//
//   - neutral path is bit-identical at every rate
//   - band response at the same musical settings is rate-consistent
//   - HPF/LPF cutoffs track their control values at every rate
//   - OPEN bell at 20 kHz behaves sanely even at 44.1 kHz (near Nyquist)
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;

class C4SampleRateTests final : public juce::UnitTest
{
public:
    C4SampleRateTests() : juce::UnitTest ("C4.SampleRates", "APEX.C4") {}

    void runTest() override
    {
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        const int block = 512;

        beginTest ("Neutral path bit-identical at every supported sample rate");
        for (double rate : rates)
        {
            auto proc = C4Test::makePreparedProcessor (rate, block);
            juce::AudioBuffer<float> in (2, block);
            C4Test::fillDeterministic (in, 0xC4C4u + (juce::uint32) rate);
            auto out = in;
            proc->processBlock (out, juce::MidiBuffer());

            double maxDiff = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    maxDiff = juce::jmax (maxDiff, (double) std::abs (
                        out.getSample (ch, i) - in.getSample (ch, i)));
            expect (maxDiff == 0.0,
                    "neutral bit-identical at " + juce::String (rate)
                        + " Hz (max diff " + juce::String (maxDiff, 12) + ")");
        }

        beginTest ("Band response consistency across sample rates (SCULPT +6 @ 1 kHz)");
        for (double rate : rates)
        {
            auto proc = C4Test::makePreparedProcessor (rate, block);
            proc->getC4Parameter (C4ParamIndex::kSculptFreq)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptFreq)->getValueForText ("1000"));
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText ("6.0"));
            const float g = C4Test::measureGainDb (*proc, rate, block, 1000.0);
            expect (std::abs (g - 6.0f) < 0.3f,
                    "SCULPT +6 @1k at " + juce::String (rate) + ": " + juce::String (g, 2));
        }

        beginTest ("HPF 100 Hz cutoff consistent across sample rates");
        for (double rate : rates)
        {
            auto proc = C4Test::makePreparedProcessor (rate, block);
            proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
                proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("100"));
            const float g = C4Test::measureGainDbSteady (*proc, rate, block, 100.0);
            expect (std::abs (g - (-3.01f)) < 0.8f,
                    "HPF 100 Hz at " + juce::String (rate) + ": " + juce::String (g, 2));
        }

        beginTest ("LPF 10 kHz cutoff consistent across sample rates");
        for (double rate : rates)
        {
            auto proc = C4Test::makePreparedProcessor (rate, block);
            proc->getC4Parameter (C4ParamIndex::kLpf)->setValue (
                proc->getC4Parameter (C4ParamIndex::kLpf)->getValueForText ("10000"));
            const float g = C4Test::measureGainDb (*proc, rate, block, 10000.0);
            expect (std::abs (g - (-3.01f)) < 0.8f,
                    "LPF 10 kHz at " + juce::String (rate) + ": " + juce::String (g, 2));
        }

        beginTest ("OPEN bell at 20 kHz near Nyquist (44.1/48 kHz): finite, sane");
        for (double rate : { 44100.0, 48000.0 })
        {
            auto proc = C4Test::makePreparedProcessor (rate, block);
            proc->getC4Parameter (C4ParamIndex::kOpenFreq)->setValue (1.0f); // 20 kHz max
            proc->getC4Parameter (C4ParamIndex::kOpenGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kOpenGain)->getValueForText ("12.0"));

            // Response at 15 kHz must be finite and show a real boost. The
            // bilinear warp compresses the 20 kHz bell's skirt near Nyquist
            // (measured ~+3 dB at 15 kHz at 44.1 kHz) — finite, stable and
            // musically usable; the dedicated HALO mapper (Phase 2) owns the
            // perceptual high-frequency behavior.
            const float g = C4Test::measureGainDb (*proc, rate, block, 15000.0);
            expect (std::isfinite (g) && g > 1.5f,
                    "OPEN 20 kHz bell at " + juce::String (rate) + ": "
                        + juce::String (g, 2) + " dB at 15 kHz");
        }

        beginTest ("OPEN 20 kHz bell at high rates: response near +12 at 18 kHz");
        for (double rate : { 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            auto proc = C4Test::makePreparedProcessor (rate, block);
            proc->getC4Parameter (C4ParamIndex::kOpenFreq)->setValue (1.0f);
            proc->getC4Parameter (C4ParamIndex::kOpenGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kOpenGain)->getValueForText ("12.0"));
            const float g = C4Test::measureGainDb (*proc, rate, block, 18000.0);
            expect (std::abs (g - 12.0f) < 0.8f,
                    "OPEN +12 @20k at " + juce::String (rate) + ": "
                        + juce::String (g, 2) + " dB @18k");
        }
    }
};

static C4SampleRateTests c4SampleRateTests;
