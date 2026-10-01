#include <JuceHeader.h>
#include "../../../Source/MeteringCore/LufsMeterCore.h"

/**
    Regression tests for the low-buffer hardening of DAW::LufsMeterCore.

    Background: the previous implementation appended every gated 400 ms block
    to an unbounded std::vector and re-ran the full BS.1770 two-pass gating
    over the entire session history on EVERY audio callback, on six meter
    instances on the master path. CPU per callback grew linearly with session
    length — a slow-motion deadline bomb at 64/128-sample buffers.

    The hardened implementation folds block energies into a fixed 0.1 LU
    histogram and recomputes the integrated value only when a new 400 ms
    block has actually arrived. These tests pin the required behaviour:
    bounded memory (allocation-free process), block-size independence,
    correct gating, and long-session stability.
*/
class LufsMeterCoreTests final : public juce::UnitTest
{
public:
    LufsMeterCoreTests() : juce::UnitTest ("metering.lufs-bounded-integrated.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("silence keeps integrated at -inf");
        {
            DAW::LufsMeterCore meter;
            meter.prepare (48000.0);
            std::vector<float> silence (512, 0.0f);
            for (int i = 0; i < 2000; ++i) // ~21 s of silence
                meter.process (silence.data(), 512);
            expect (! std::isfinite (meter.getIntegratedLufs()));
        }

        beginTest ("integrated is block-size independent");
        {
            // Identical 12 s 1 kHz sine rendered through 64/256/1024-sample
            // callbacks must produce the same integrated loudness.
            auto runMeter = [] (int blockSize)
            {
                DAW::LufsMeterCore meter;
                meter.prepare (48000.0);
                std::vector<float> buf ((size_t) blockSize);
                constexpr int total = 48000 * 12;
                int done = 0;
                uint64_t n = 0;
                while (done < total)
                {
                    const int chunk = juce::jmin (blockSize, total - done);
                    for (int i = 0; i < chunk; ++i)
                        buf[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                            * 1000.0 * (double) (n + (uint64_t) i) / 48000.0);
                    n += (uint64_t) chunk;
                    meter.process (buf.data(), chunk);
                    done += chunk;
                }
                return meter.getIntegratedLufs();
            };
            const float a = runMeter (64);
            const float b = runMeter (256);
            const float c = runMeter (1024);
            expectWithinAbsoluteError (a, b, 0.05f);
            expectWithinAbsoluteError (b, c, 0.05f);
            // Sine amp 0.5 ≈ -9.7 LUFS before K-weighting; +4 dB shelf pulls
            // it up a few dB. Wide sanity window only.
            expect (a > -12.0f && a < -3.0f);
        }

        beginTest ("relative gate excludes quiet sections");
        {
            // Meter fed 5 s loud + 25 s quiet (-46 dBFS: above the -70 LUFS
            // absolute gate, below the -10 LU relative gate) must agree with
            // the loud-only meter within 0.15 LU (0.1 LU histogram bin
            // quantisation at the gate boundary plus arithmetic noise).
            auto runMeter = [] (int loudSeconds, int quietSeconds)
            {
                DAW::LufsMeterCore meter;
                meter.prepare (48000.0);
                std::vector<float> buf (512);
                uint64_t n = 0;
                auto feed = [&] (int seconds, float amp)
                {
                    const int total = 48000 * seconds;
                    int done = 0;
                    while (done < total)
                    {
                        const int chunk = juce::jmin (512, total - done);
                        for (int i = 0; i < chunk; ++i)
                            buf[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                * 1000.0 * (double) (n + (uint64_t) i) / 48000.0);
                        n += (uint64_t) chunk;
                        meter.process (buf.data(), chunk);
                        done += chunk;
                    }
                };
                feed (loudSeconds, 0.5f);
                if (quietSeconds > 0)
                    feed (quietSeconds, 0.005f);
                return meter.getIntegratedLufs();
            };
            // 20 s loud dilutes the single loud->quiet transition 400 ms
            // block (which BS.1770 legitimately counts) below the tolerance.
            const float loudOnly      = runMeter (20, 0);
            const float loudThenQuiet = runMeter (20, 25);
            expectWithinAbsoluteError (loudThenQuiet, loudOnly, 0.15f);
        }

        beginTest ("process is allocation-free after prepare");
        {
            DAW::LufsMeterCore meter;
            meter.prepare (48000.0);
            std::vector<float> buf (8192, 0.25f);
            meter.process (buf.data(), 8192); // warm-up
            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int i = 0; i < 500; ++i)
                    meter.process (buf.data(), 64);
                for (int i = 0; i < 50; ++i)
                    meter.process (buf.data(), 8192);
            }
        }

        beginTest ("long session stays bounded and finite");
        {
            DAW::LufsMeterCore meter;
            meter.prepare (48000.0);
            std::vector<float> buf (512);
            uint64_t n = 0;
            // 120 s of program material (~300 gated 400 ms blocks; the old
            // implementation iterated the entire block history on every one
            // of these 11250 callbacks).
            for (int b = 0; b < (48000 * 120) / 512; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    buf[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                            * 440.0 * (double) (n + (uint64_t) i) / 48000.0);
                n += 512;
                meter.process (buf.data(), 512);
            }
            expect (std::isfinite (meter.getIntegratedLufs()));
            expect (meter.getIntegratedLufs() > -20.0f && meter.getIntegratedLufs() < 0.0f);
            expect (std::isfinite (meter.getMomentaryLufs()));
            expect (std::isfinite (meter.getShortTermLufs()));
        }
    }
};

/**
    K-weighting correctness across the professional rate set (2026-07-25).

    Before this fix, exact BS.1770 K-weighting existed only at 48 kHz; every
    other rate used RBJ approximations (1500 Hz/+4 dB shelf, 38 Hz/Q 0.5 HP)
    that are NOT the standard filters. Now: 48 kHz keeps the published
    coefficients verbatim; every other rate derives both biquads from the
    BS.1770 analog prototype (De Man refinement). The oracle in this suite
    independently re-derives the same prototype transform, and the
    sine-conformance case checks end-to-end loudness through the public API.
*/
class LufsKWeightingRateTests final : public juce::UnitTest
{
public:
    LufsKWeightingRateTests() : juce::UnitTest ("metering.lufs-kweighting-rates.v1", "APEX.Diagnostics") {}

    static void oracleShelf (double fs, double c[5])
    {
        // Independent oracle: bilinear transform (K = tan pre-warping) of the
        // BS.1770 analog prototype pre-filter (De Man refined constants).
        const double f0 = 1681.974450955533, gainDb = 3.99984385397, q = 0.7071752369554193;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, gainDb / 20.0);
        const double Vb = std::sqrt (Vh);
        const double A0 = 1.0 + K / q + K * K;
        c[0] = (Vh + Vb * K / q + K * K) / A0;
        c[1] = 2.0 * (K * K - Vh) / A0;
        c[2] = (Vh - Vb * K / q + K * K) / A0;
        c[3] = 2.0 * (K * K - 1.0) / A0;
        c[4] = (1.0 - K / q + K * K) / A0;
    }

    static void oracleHp (double fs, double c[5])
    {
        // Independent oracle: RLB high-pass, numerator pinned to [1,-2,1]
        // (published-style), denominator from the bilinear prototype.
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double A0 = 1.0 + K / q + K * K;
        c[0] = 1.0; c[1] = -2.0; c[2] = 1.0;
        c[3] = 2.0 * (K * K - 1.0) / A0;
        c[4] = (1.0 - K / q + K * K) / A0;
    }

    void runTest() override
    {
        beginTest ("48 kHz keeps the published BS.1770 coefficients verbatim");
        {
            double b0, b1, b2, a1, a2;
            DAW::LufsMeterCore::computeKWeightingCoefficients (48000.0, true, b0, b1, b2, a1, a2);
            expectWithinAbsoluteError (b0,  1.53512485958697, 1e-12);
            expectWithinAbsoluteError (b1, -2.69169618940638, 1e-12);
            expectWithinAbsoluteError (b2,  1.19839281085285, 1e-12);
            expectWithinAbsoluteError (a1, -1.69065929318241, 1e-12);
            expectWithinAbsoluteError (a2,  0.73248077421585, 1e-12);

            DAW::LufsMeterCore::computeKWeightingCoefficients (48000.0, false, b0, b1, b2, a1, a2);
            expectWithinAbsoluteError (b0,  1.0,               1e-12);
            expectWithinAbsoluteError (b1, -2.0,               1e-12);
            expectWithinAbsoluteError (b2,  1.0,               1e-12);
            expectWithinAbsoluteError (a1, -1.99004745483398, 1e-12);
            expectWithinAbsoluteError (a2,  0.99007225036621, 1e-12);
        }

        beginTest ("the prototype derivation reproduces the published 48k coefficients");
        {
            // Independent validation of the derivation itself: the analog-
            // prototype transform evaluated AT 48 kHz must land on the
            // standard's published constants (known result to ~1e-4).
            double c[5];
            oracleShelf (48000.0, c);
            expectWithinAbsoluteError (c[0],  1.53512485958697, 5e-4);
            expectWithinAbsoluteError (c[1], -2.69169618940638, 5e-4);
            expectWithinAbsoluteError (c[2],  1.19839281085285, 5e-4);
            expectWithinAbsoluteError (c[3], -1.69065929318241, 5e-4);
            expectWithinAbsoluteError (c[4],  0.73248077421585, 5e-4);

            oracleHp (48000.0, c);
            expectWithinAbsoluteError (c[0],  1.0,               5e-4);
            expectWithinAbsoluteError (c[1], -2.0,               5e-4);
            expectWithinAbsoluteError (c[2],  1.0,               5e-4);
            expectWithinAbsoluteError (c[3], -1.99004745483398, 5e-4);
            expectWithinAbsoluteError (c[4],  0.99007225036621, 5e-4);
        }

        beginTest ("derived coefficients match the oracle at all supported rates");
        {
            const double rates[] = { 32000.0, 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            for (const double fs : rates)
            {
                double oracle[5];
                double b0, b1, b2, a1, a2;

                oracleShelf (fs, oracle);
                DAW::LufsMeterCore::computeKWeightingCoefficients (fs, true, b0, b1, b2, a1, a2);
                expectWithinAbsoluteError (b0, oracle[0], 1e-9);
                expectWithinAbsoluteError (b1, oracle[1], 1e-9);
                expectWithinAbsoluteError (b2, oracle[2], 1e-9);
                expectWithinAbsoluteError (a1, oracle[3], 1e-9);
                expectWithinAbsoluteError (a2, oracle[4], 1e-9);

                oracleHp (fs, oracle);
                DAW::LufsMeterCore::computeKWeightingCoefficients (fs, false, b0, b1, b2, a1, a2);
                expectWithinAbsoluteError (b0, oracle[0], 1e-9);
                expectWithinAbsoluteError (b1, oracle[1], 1e-9);
                expectWithinAbsoluteError (b2, oracle[2], 1e-9);
                expectWithinAbsoluteError (a1, oracle[3], 1e-9);
                expectWithinAbsoluteError (a2, oracle[4], 1e-9);
            }
        }

        beginTest ("-23 dBFS (RMS) 1 kHz sine measures -23 LUFS at every supported rate");
        {
            const double rates[] = { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            const int blocks[] = { 32, 256 };
            for (const double fs : rates)
                for (const int block : blocks)
                {
                    DAW::LufsMeterCore meter;
                    meter.prepare (fs);

                    // BS.1770 test-signal convention: the reference sine's
                    // level is its RMS value. A sine of amplitude A has
                    // RMS A/sqrt(2), so RMS = -23 dBFS requires A = 10^(-23/20) * sqrt(2).
                    const double amplitude = std::pow (10.0, -23.0 / 20.0) * std::sqrt (2.0);
                    std::vector<float> data ((size_t) block);
                    juce::int64 phase = 0;

                    const int numBlocks = (int) (fs * 3.5 / block);
                    for (int b = 0; b < numBlocks; ++b)
                    {
                        for (int i = 0; i < block; ++i, ++phase)
                            data[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * phase / fs));
                        meter.process (data.data(), block);
                    }

                    expect (std::abs ((double) meter.getMomentaryLufs() - (-23.0)) <= 0.3,
                            juce::String ("momentary @") + juce::String (fs) + "/" + juce::String (block)
                                + ": " + juce::String (meter.getMomentaryLufs(), 3));
                    expect (std::abs ((double) meter.getIntegratedLufs() - (-23.0)) <= 0.3,
                            juce::String ("integrated @") + juce::String (fs) + "/" + juce::String (block)
                                + ": " + juce::String (meter.getIntegratedLufs(), 3));
                }
        }
    }
};

static LufsKWeightingRateTests lufsKWeightingRateTests;

static LufsMeterCoreTests lufsMeterCoreTests;
