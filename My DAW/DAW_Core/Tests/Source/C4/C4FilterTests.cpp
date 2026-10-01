#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4FilterTests — HPF (3rd-order Butterworth, 18 dB/oct) and LPF
// (2nd-order Butterworth, 12 dB/oct) contract:
//
//   - cutoff accuracy (-3.01 dB at fc for both filters)
//   - slope verification (18 dB/oct HPF, 12 dB/oct LPF)
//   - monotonic response
//   - extreme cutoffs (20 Hz HPF at 44.1 kHz; 24 kHz LPF at 44.1 kHz)
//   - sample-rate consistency across all six supported rates
//
// Expected values — DIGITAL bilinear Butterworth (not the unwarped analog
// values): the TPT-SVF realization is the bilinear transform of the analog
// prototype, so the response at digital frequency f is
//
//   LPF n=2: |H(f)|^2 = 1 / (1 + r^4),   r = tan(pi*f/fs) / tan(pi*fc/fs)
//   HPF n=3: |H(f)|^2 = r^6 / (1 + r^6), r = tan(pi*fc/fs) / tan(pi*f/fs)
//
// At 48 kHz, fc = 4 kHz: |H(2fc)| = -13.53 dB and |H(4fc)| = -32.44 dB
// (the analog 12.30/24.10 dB values do NOT hold digitally — the tan warp
// steepens the digital response far above fc).
//
// The 24 kHz LPF control at 44.1 kHz is beyond Nyquist; the Phase 1 clamp
// (0.49 * fs, TEMPORARY — see the Phase 2 C4HaloShelfMapper plan) makes it
// behave as ~21.6 kHz. Because tan(pi*21609/44100) ~ 221, the bilinear map
// compresses the response: the filter is essentially TRANSPARENT across the
// whole in-band region (measured ~0.00 dB at 1/15/20 kHz). This test pins
// that documented temporary behavior; Phase 2 replaces the high-frequency
// mapping with C4HaloShelfMapper.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4Parameter;
using APEX::C4::C4ParamIndex;

class C4FilterTests final : public juce::UnitTest
{
public:
    C4FilterTests() : juce::UnitTest ("C4.Filters", "APEX.C4") {}

    void runTest() override
    {
        testHpfCutoffAndSlope();
        testLpfCutoffAndSlope();
        testHpfMonotonic();
        testLpfMonotonic();
        testHpfExtremes();
        testLpfExtremes();
        testSampleRateConsistency();
    }

private:
    static void setHpf (C4Processor& proc, float hz)
    {
        auto* p = proc.getC4Parameter (C4ParamIndex::kHpf);
        p->setValue (p->getValueForText (juce::String (hz)));
    }

    static void setLpf (C4Processor& proc, float hz)
    {
        auto* p = proc.getC4Parameter (C4ParamIndex::kLpf);
        p->setValue (p->getValueForText (juce::String (hz)));
    }

    static void setHpfOff (C4Processor& proc)
    {
        proc.getC4Parameter (C4ParamIndex::kHpf)->setValue (0.0f);
    }

    static void setLpfOff (C4Processor& proc)
    {
        proc.getC4Parameter (C4ParamIndex::kLpf)->setValue (0.0f);
    }

    void testHpfCutoffAndSlope()
    {
        const double rate = 48000.0;
        const int block = 512;
        const float fc = 100.0f;

        beginTest ("HPF 100 Hz: -3 dB at fc, 18 dB/oct slope");

        auto proc = C4Test::makePreparedProcessor (rate, block);
        setHpf (*proc, fc);

        // Steady-sine at fc and fc/2 (100/50 Hz: FFT bins too coarse).
        const float gFc = C4Test::measureGainDbSteady (*proc, rate, block, fc);
        const float gFc2 = C4Test::measureGainDbSteady (*proc, rate, block, fc / 2.0f);
        // Impulse-FFT at fc/4 = 25 Hz would be bin 8.5 — use steady sine.
        const float gFc4 = C4Test::measureGainDbSteady (*proc, rate, block, fc / 4.0f);

        expect (std::abs (gFc - (-3.01f)) < 0.6f,
                "|H(fc)|: expected -3.01 dB, measured " + juce::String (gFc, 2));
        expect (std::abs (gFc2 - (-18.13f)) < 1.0f,
                "|H(fc/2)|: expected -18.13 dB, measured " + juce::String (gFc2, 2));
        expect (std::abs (gFc4 - (-36.12f)) < 1.5f,
                "|H(fc/4)|: expected -36.12 dB, measured " + juce::String (gFc4, 2));
    }

    void testLpfCutoffAndSlope()
    {
        const double rate = 48000.0;
        const int block = 512;
        const float fc = 4000.0f;

        beginTest ("LPF 4 kHz: -3 dB at fc, 12 dB/oct slope (bilinear)");

        auto proc = C4Test::makePreparedProcessor (rate, block);
        setLpf (*proc, fc);

        const float gFc = C4Test::measureGainDb (*proc, rate, block, fc);
        const float g2Fc = C4Test::measureGainDb (*proc, rate, block, fc * 2.0f);
        const float g4Fc = C4Test::measureGainDb (*proc, rate, block, fc * 4.0f);

        // Digital bilinear Butterworth expectations (see the file header).
        const auto bilinearLpfDb = [] (double f, double fcut, double fs)
        {
            const double r = std::tan (juce::MathConstants<double>::pi * f / fs)
                           / std::tan (juce::MathConstants<double>::pi * fcut / fs);
            return (float) (-10.0 * std::log10 (1.0 + std::pow (r, 4.0)));
        };

        expect (std::abs (gFc - (-3.01f)) < 0.6f,
                "|H(fc)|: expected -3.01 dB, measured " + juce::String (gFc, 2));
        expect (std::abs (g2Fc - bilinearLpfDb (fc * 2.0, fc, rate)) < 0.6f,
                "|H(2fc)|: expected " + juce::String (bilinearLpfDb (fc * 2.0, fc, rate), 2)
                    + " dB (bilinear), measured " + juce::String (g2Fc, 2));
        expect (std::abs (g4Fc - bilinearLpfDb (fc * 4.0, fc, rate)) < 0.8f,
                "|H(4fc)|: expected " + juce::String (bilinearLpfDb (fc * 4.0, fc, rate), 2)
                    + " dB (bilinear), measured " + juce::String (g4Fc, 2));
    }

    void testHpfMonotonic()
    {
        const double rate = 48000.0;
        const int block = 512;

        beginTest ("HPF monotonic response (no resonant bump)");
        auto proc = C4Test::makePreparedProcessor (rate, block);
        setHpf (*proc, 200.0f);

        // Response must be strictly increasing with frequency: sweep a few
        // points below fc and verify ordering, and verify the passband
        // converges to 0 dB without overshoot above fc.
        const float g25 = C4Test::measureGainDbSteady (*proc, rate, block, 25.0);
        const float g50 = C4Test::measureGainDbSteady (*proc, rate, block, 50.0);
        const float g100 = C4Test::measureGainDbSteady (*proc, rate, block, 100.0);
        const float g200 = C4Test::measureGainDbSteady (*proc, rate, block, 200.0);
        const float g400 = C4Test::measureGainDbSteady (*proc, rate, block, 400.0);
        const float g1k = C4Test::measureGainDb (*proc, rate, block, 1000.0);

        expect (g25 < g50 && g50 < g100 && g100 < g200 && g200 < g400,
                "HPF must be monotonic below/at fc: "
                    + juce::String (g25, 1) + " < " + juce::String (g50, 1) + " < "
                    + juce::String (g100, 1) + " < " + juce::String (g200, 1) + " < "
                    + juce::String (g400, 1));
        expect (std::abs (g400) < 0.25f && std::abs (g1k) < 0.15f,
                "passband must converge to unity without bump: g400="
                    + juce::String (g400, 2) + " g1k=" + juce::String (g1k, 2));
    }

    void testLpfMonotonic()
    {
        const double rate = 48000.0;
        const int block = 512;

        beginTest ("LPF monotonic response (no unnecessary resonance)");
        auto proc = C4Test::makePreparedProcessor (rate, block);
        setLpf (*proc, 8000.0f);

        const float g1k = C4Test::measureGainDb (*proc, rate, block, 1000.0);
        const float g4k = C4Test::measureGainDb (*proc, rate, block, 4000.0);
        const float g8k = C4Test::measureGainDb (*proc, rate, block, 8000.0);
        const float g16k = C4Test::measureGainDb (*proc, rate, block, 16000.0);

        expect (std::abs (g1k) < 0.15f, "passband unity at 1 kHz: " + juce::String (g1k, 2));
        expect (g1k > g4k && g4k > g8k && g8k > g16k,
                "LPF must be strictly monotonic: "
                    + juce::String (g1k, 1) + " > " + juce::String (g4k, 1) + " > "
                    + juce::String (g8k, 1) + " > " + juce::String (g16k, 1));
    }

    void testHpfExtremes()
    {
        const double rate = 44100.0;
        const int block = 512;

        beginTest ("HPF extreme low cutoff 20 Hz at 44.1 kHz");
        auto proc = C4Test::makePreparedProcessor (rate, block);
        setHpf (*proc, 20.0f);
        const float g20 = C4Test::measureGainDbSteady (*proc, rate, block, 20.0);
        const float g10 = C4Test::measureGainDbSteady (*proc, rate, block, 10.0);
        expect (std::abs (g20 - (-3.01f)) < 0.8f,
                "|H(20Hz)|: expected -3.01, measured " + juce::String (g20, 2));
        expect (g10 < g20 - 12.0f, "slope below fc: " + juce::String (g10, 1));

        beginTest ("HPF extreme high cutoff 1.5 kHz at 44.1 kHz");
        auto proc2 = C4Test::makePreparedProcessor (rate, block);
        setHpf (*proc2, 1500.0f);
        const float g1500 = C4Test::measureGainDb (*proc2, rate, block, 1500.0);
        const float g750 = C4Test::measureGainDb (*proc2, rate, block, 750.0);
        expect (std::abs (g1500 - (-3.01f)) < 0.8f,
                "|H(1.5k)|: expected -3.01, measured " + juce::String (g1500, 2));
        expect (std::abs (g750 - (-18.13f)) < 1.2f,
                "|H(750)|: expected -18.13, measured " + juce::String (g750, 2));
    }

    void testLpfExtremes()
    {
        beginTest ("LPF 24 kHz at 44.1 kHz: clamped TEMPORARY behavior (~21.6 kHz)");
        const double rate = 44100.0;
        const int block = 512;
        auto proc = C4Test::makePreparedProcessor (rate, block);
        setLpf (*proc, 24000.0f);

        // The Phase 1 Nyquist clamp (0.49 * fs = 21609 Hz) must keep the
        // response finite and smooth. Because tan(pi*21609/44100) ~ 221, the
        // bilinear map compresses the whole in-band region onto the analog
        // passband: the filter is essentially TRANSPARENT (documented
        // TEMPORARY behavior — measured ~0.00 dB at 1/15/20 kHz). Phase 2's
        // C4HaloShelfMapper replaces this high-frequency mapping.
        const float g1k = C4Test::measureGainDb (*proc, rate, block, 1000.0);
        const float g15k = C4Test::measureGainDb (*proc, rate, block, 15000.0);
        const float g20k = C4Test::measureGainDb (*proc, rate, block, 20000.0);

        expect (std::abs (g1k) < 0.2f, "flat at 1 kHz: " + juce::String (g1k, 2));
        expect (std::abs (g15k) < 0.3f,
                "15 kHz under clamped 24 kHz LPF: near-flat (TEMPORARY clamp), measured "
                    + juce::String (g15k, 2));
        expect (std::abs (g20k) < 0.3f,
                "20 kHz under clamped 24 kHz LPF: near-flat (TEMPORARY clamp), measured "
                    + juce::String (g20k, 2));
    }

    void testSampleRateConsistency()
    {
        beginTest ("HPF/LPF cutoff accuracy across all six supported sample rates");
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        const int block = 512;

        for (double rate : rates)
        {
            // HPF at 100 Hz.
            {
                auto proc = C4Test::makePreparedProcessor (rate, block);
                setHpf (*proc, 100.0f);
                const float gFc = C4Test::measureGainDbSteady (*proc, rate, block, 100.0);
                expect (std::abs (gFc - (-3.01f)) < 0.8f,
                        juce::String (rate) + " Hz HPF 100 Hz: "
                            + juce::String (gFc, 2) + " dB (expected -3.01)");
            }
            // LPF at 10 kHz (safe at every rate).
            {
                auto proc = C4Test::makePreparedProcessor (rate, block);
                setLpf (*proc, 10000.0f);
                const float gFc = C4Test::measureGainDb (*proc, rate, block, 10000.0);
                expect (std::abs (gFc - (-3.01f)) < 0.8f,
                        juce::String (rate) + " Hz LPF 10 kHz: "
                            + juce::String (gFc, 2) + " dB (expected -3.01)");
            }
        }
    }
};

static C4FilterTests c4FilterTests;
