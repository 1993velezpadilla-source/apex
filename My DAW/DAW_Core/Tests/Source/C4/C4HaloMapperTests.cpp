#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4HaloMapperTests — Phase 2: the perceptual OPEN/HALO mapping.
//
// The mapper translates the HALO control frequency (2.5..40 kHz, deliberately
// beyond the audio range) into a REALIZABLE in-band shelf frequency
// (<= maxRealizableRatio * Nyquist = 0.90 * Nyquist). A raw 40 kHz control at
// 44.1 kHz would compute g = tan(pi*40000/44100) < 0 — an unrealizable SVF.
//
// Contract (documented in C4HaloShelfMapper.h):
//   - identity for every control at/below the ceiling (Phase 1 low-control
//     OPEN shelf behavior preserved)
//   - continuous and monotonic
//   - bounded: f_real <= 0.90 * Nyquist at every sample rate
//   - softness 0 = hard clamp at the ceiling; higher softness = progressively
//     gentler response as the control rises
//   - OPEN bell mode is NOT mapped (the bell uses the raw control frequency)
//   - the bell<->HALO morph lerps the mapped frequency (continuous by
//     construction)
// ============================================================================

using APEX::C4::C4HaloShelfMapper;
using APEX::C4::C4HaloTuning;
using APEX::C4::C4EngineCore;
using APEX::C4::C4BandId;
using APEX::C4::kHaloFreqMinHz;
using APEX::C4::kHaloFreqMaxHz;
using APEX::C4::makeProfileVariantB;

class C4HaloMapperTests final : public juce::UnitTest
{
public:
    C4HaloMapperTests() : juce::UnitTest ("C4.HaloMapper", "APEX.C4") {}

    void runTest() override
    {
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

        beginTest ("Identity below the knee at every rate");
        for (double rate : rates)
        {
            C4HaloTuning tuning { 0.5f, 1.0f, 0.90f };
            const float ceiling = C4HaloShelfMapper::ceilingHz ((float) rate, tuning);
            const float knee = ceiling * (1.0f - 0.20f * tuning.mappingSoftness);
            // Probes must lie INSIDE the control range [2.5k, 40k] AND below
            // the knee (the mapper clamps controls to the range).
            const float probes[] =
            {
                2500.0f, 5000.0f, 10000.0f,
                juce::jmin (0.5f * ceiling, kHaloFreqMaxHz),
                juce::jmin (knee * 0.99f, kHaloFreqMaxHz)
            };
            for (float c : probes)
                expect (std::abs (C4HaloShelfMapper::mapControlFreq (c, (float) rate, tuning) - c) < 0.5f,
                        "identity at " + juce::String (c, 0) + " Hz, rate "
                            + juce::String (rate, 0));
        }

        beginTest ("Bounded by 0.90 * Nyquist at every rate and control");
        for (double rate : rates)
        {
            C4HaloTuning tuning { 0.5f, 1.0f, 0.90f };
            const float ceiling = C4HaloShelfMapper::ceilingHz ((float) rate, tuning);
            for (float c = kHaloFreqMinHz; c <= kHaloFreqMaxHz; c += 500.0f)
            {
                const float f = C4HaloShelfMapper::mapControlFreq (c, (float) rate, tuning);
                expect (f <= ceiling + 0.5f && f >= kHaloFreqMinHz,
                        "bounded at control " + juce::String (c, 0) + " Hz, rate "
                            + juce::String (rate, 0) + ": " + juce::String (f, 1));
            }
        }

        beginTest ("Monotonic, continuous, softness law");
        {
            const double rate = 44100.0;
            C4HaloTuning tuning { 0.5f, 1.0f, 0.90f };
            const float ceiling = C4HaloShelfMapper::ceilingHz ((float) rate, tuning);
            const float knee = ceiling * (1.0f - 0.20f * tuning.mappingSoftness);

            // Continuity at the knee.
            expect (std::abs (C4HaloShelfMapper::mapControlFreq (knee, (float) rate, tuning) - knee) < 0.5f,
                    "continuous at the knee");

            // Monotonic non-decreasing over the full control range.
            float prev = 0.0f;
            bool monotonic = true;
            for (float c = kHaloFreqMinHz; c <= kHaloFreqMaxHz; c += 250.0f)
            {
                const float f = C4HaloShelfMapper::mapControlFreq (c, (float) rate, tuning);
                if (f < prev - 0.5f)
                    monotonic = false;
                prev = f;
            }
            expect (monotonic, "mapping must be non-decreasing");

            // Softness law at max control: gentler with higher softness
            // (softness 0 = straight/hard clamp at the ceiling).
            const float f0 = C4HaloShelfMapper::mapControlFreq (kHaloFreqMaxHz, (float) rate, { 0.0f, 1.0f, 0.90f });
            const float f05 = C4HaloShelfMapper::mapControlFreq (kHaloFreqMaxHz, (float) rate, { 0.5f, 1.0f, 0.90f });
            const float f1  = C4HaloShelfMapper::mapControlFreq (kHaloFreqMaxHz, (float) rate, { 1.0f, 1.0f, 0.90f });
            expect (std::abs (f0 - ceiling) < 1.0f,
                    "softness 0 = straight clamp at the ceiling (measured "
                        + juce::String (f0, 1) + ")");
            expect (f1 < f05 && f05 <= f0,
                    "gentler with higher softness: " + juce::String (f1, 1)
                        + " < " + juce::String (f05, 1) + " <= " + juce::String (f0, 1));

            // The 40 kHz control at 44.1 kHz must stay realizable (the raw
            // value would compute a negative tan -> unrealizable SVF).
            const float f40k = C4HaloShelfMapper::mapControlFreq (40000.0f, (float) rate, tuning);
            expect (f40k > 15000.0f && f40k <= ceiling,
                    "40 kHz control realizes in-band (" + juce::String (f40k, 1)
                        + " Hz @ 44.1 kHz)");
        }

        beginTest ("Engine integration: HALO maps, bell does not, morph lerps");
        {
            const double rate = 44100.0;
            const int block = 512;

            // HALO (shelf) mode at 20 kHz control: the smoothed frequency must
            // be the mapped value (~18.73 kHz with softness 0.5), NOT the raw
            // 20 kHz.
            {
                C4EngineCore engine;
                engine.prepare (rate, block, 1, makeProfileVariantB());
                engine.setBandFreqTargetHz ((int) C4BandId::Open, 20000.0f);
                engine.setBandGainTargetDb ((int) C4BandId::Open, 6.0f);
                engine.setBandModeTarget ((int) C4BandId::Open, true);
                runSilence (engine, rate, block, 0.5);
                const float f = engine.getSmoothedBandFreqHz ((int) C4BandId::Open);
                expect (std::abs (f - 18734.0f) < 100.0f,
                        "HALO 20 kHz control maps to ~18.73 kHz @ 44.1 kHz (measured "
                            + juce::String (f, 1) + ")");
            }

            // Bell mode at 20 kHz control: UNMAPPED (raw frequency).
            {
                C4EngineCore engine;
                engine.prepare (rate, block, 1, makeProfileVariantB());
                engine.setBandFreqTargetHz ((int) C4BandId::Open, 20000.0f);
                engine.setBandGainTargetDb ((int) C4BandId::Open, 6.0f);
                runSilence (engine, rate, block, 0.5);
                const float f = engine.getSmoothedBandFreqHz ((int) C4BandId::Open);
                expect (std::abs (f - 20000.0f) < 1.0f,
                        "OPEN bell uses the raw control frequency (measured "
                            + juce::String (f, 1) + ")");
            }

            // Morph blend 0.5 at 20 kHz control: lerped frequency.
            {
                C4EngineCore engine;
                engine.prepare (rate, block, 1, makeProfileVariantB());
                engine.setBandFreqTargetHz ((int) C4BandId::Open, 20000.0f);
                engine.setBandGainTargetDb ((int) C4BandId::Open, 6.0f);
                engine.setBandModeBlendForTest ((int) C4BandId::Open, 0.5f);
                runSilence (engine, rate, block, 0.5);
                const float f = engine.getSmoothedBandFreqHz ((int) C4BandId::Open);
                expect (std::abs (f - 19367.0f) < 80.0f,
                        "morph 0.5 lerps the mapped frequency (measured "
                            + juce::String (f, 1) + ")");
            }

            // HALO response at 20 kHz control @ 44.1 kHz: finite, realizable,
            // not pathological.
            {
                C4EngineCore engine;
                engine.prepare (rate, block, 1, makeProfileVariantB());
                engine.setBandFreqTargetHz ((int) C4BandId::Open, 20000.0f);
                engine.setBandGainTargetDb ((int) C4BandId::Open, 6.0f);
                engine.setBandModeTarget ((int) C4BandId::Open, true);
                const auto ir = captureIr (engine, rate, block, 8192);
                expect (C4Test::allFinite (ir), "HALO IR must be finite");
                const float g20k = C4Test::magnitudeDbAtFrequency (ir, rate, 20000.0, 16384);
                const float g10k = C4Test::magnitudeDbAtFrequency (ir, rate, 10000.0, 16384);
                expect (g20k > -30.0f && g20k < 30.0f && std::isfinite (g20k),
                        "HALO response sane at 20 kHz (measured "
                            + juce::String (g20k, 2) + " dB)");
                expect (g20k > g10k,
                        "HALO shelf rises toward the plateau (g20k "
                            + juce::String (g20k, 2) + " vs g10k " + juce::String (g10k, 2) + ")");
            }
        }

        beginTest ("Softness 0: explicit hard clamp at every rate and control");
        for (double rate : rates)
        {
            C4HaloTuning tuning { 0.0f, 1.0f, 0.90f };
            const float ceiling = C4HaloShelfMapper::ceilingHz ((float) rate, tuning);
            for (float c = kHaloFreqMinHz; c <= kHaloFreqMaxHz; c += 500.0f)
            {
                const float expected = juce::jmin (c, ceiling);
                const float f = C4HaloShelfMapper::mapControlFreq (c, (float) rate, tuning);
                expect (f == expected,
                        "softness 0 hard clamp: control " + juce::String (c, 0)
                            + " @ " + juce::String (rate, 0) + " -> " + juce::String (f, 1)
                            + " (expected " + juce::String (expected, 1) + ")");
            }
        }

        beginTest ("Degenerate tuning inputs stay total (no NaN/Inf/UB)");
        {
            const double rate = 44100.0;
            // Softness out of range clamps: -1 behaves as 0, 2 behaves as 1.
            const float fNeg = C4HaloShelfMapper::mapControlFreq (40000.0f, (float) rate, { -1.0f, 1.0f, 0.90f });
            const float fZero = C4HaloShelfMapper::mapControlFreq (40000.0f, (float) rate, { 0.0f, 1.0f, 0.90f });
            const float fTwo = C4HaloShelfMapper::mapControlFreq (40000.0f, (float) rate, { 2.0f, 1.0f, 0.90f });
            const float fOne = C4HaloShelfMapper::mapControlFreq (40000.0f, (float) rate, { 1.0f, 1.0f, 0.90f });
            expect (fNeg == fZero, "softness -1 clamps to the softness-0 branch");
            expect (fTwo == fOne, "softness 2 clamps to the softness-1 behavior");
            // Degenerate ratio clamps to (0,1]; still finite and bounded.
            const float fRatio0 = C4HaloShelfMapper::mapControlFreq (40000.0f, (float) rate, { 0.5f, 1.0f, 0.0f });
            const float fRatio2 = C4HaloShelfMapper::mapControlFreq (40000.0f, (float) rate, { 0.5f, 1.0f, 2.0f });
            expect (std::isfinite (fRatio0) && fRatio0 > 0.0f, "ratio 0 clamps safely (finite)");
            expect (std::isfinite (fRatio2) && fRatio2 <= (float) rate * 0.5f + 0.5f,
                    "ratio 2 clamps to the 1.0 ceiling");
            // 40 kHz control at every supported rate: always finite, always
            // within the realizable ceiling.
            for (double r : rates)
            {
                C4HaloTuning t { 0.5f, 1.0f, 0.90f };
                const float ceiling = C4HaloShelfMapper::ceilingHz ((float) r, t);
                const float f = C4HaloShelfMapper::mapControlFreq (40000.0f, (float) r, t);
                expect (std::isfinite (f) && f > 0.0f && f <= ceiling + 0.5f,
                        "40 kHz control valid at " + juce::String (r, 0) + " Hz: "
                            + juce::String (f, 1));
            }
        }

        beginTest ("Automation torture: Bell 20 kHz <-> HALO 40 kHz, all six rates");
        for (double rate : rates)
            runAutomationTorture (rate, 400, false);

        beginTest ("Automation torture: continuous frequency sweep while morphing");
        for (double rate : rates)
            runAutomationTorture (rate, 400, true);
    }

private:
    /** Repeated Bell/HALO transitions (or a continuous frequency sweep while
        the morph oscillates) with a 10 kHz sine running. Requires: finite
        mapped frequency (never beyond the realizable ceiling, never
        non-positive), finite output, bounded output, no click (bounded
        sample-to-sample delta). */
    void runAutomationTorture (double rate, int blocks, bool sweepFreq)
    {
        C4EngineCore engine;
        engine.prepare (rate, 512, 1, makeProfileVariantB());
        engine.setBandGainTargetDb ((int) C4BandId::Open, 6.0f);

        juce::AudioBuffer<float> buf (1, 512);
        float* chans[1] = { buf.getWritePointer (0) };
        bool finite = true;
        bool freqOk = true;
        float maxOut = 0.0f;
        float prevOut = 0.0f;
        float maxDelta = 0.0f;

        for (int b = 0; b < blocks; ++b)
        {
            if (sweepFreq)
            {
                // Continuous log sweep of the control while the morph
                // oscillates — every intermediate blend is exercised.
                const float phase = (float) b / (float) juce::jmax (1, blocks);
                const float f = kHaloFreqMinHz * std::pow (kHaloFreqMaxHz / kHaloFreqMinHz, phase);
                engine.setBandFreqTargetHz ((int) C4BandId::Open, f);
                engine.setBandModeBlendForTest ((int) C4BandId::Open,
                                                0.5f + 0.5f * std::sin (phase * 40.0f));
            }
            else
            {
                // Bell 20 kHz <-> HALO 40 kHz alternating every block.
                engine.setBandFreqTargetHz ((int) C4BandId::Open, (b % 2) == 0 ? 20000.0f : 40000.0f);
                engine.setBandModeTarget ((int) C4BandId::Open, (b % 2) == 1);
            }

            for (int i = 0; i < 512; ++i)
                buf.setSample (0, i, (float) (0.25 * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 10000.0 * i / rate)));

            engine.adoptTargets (0.0f);
            engine.processBlock (chans, 1, 512);

            finite = finite && C4Test::allFinite (buf);
            const float f = engine.getSmoothedBandFreqHz ((int) C4BandId::Open);
            // Legal Fc contract: strictly positive and REALIZABLE — strictly
            // below Nyquist (the engine's 0.49*fs band bound keeps every
            // intermediate morph blend realizable; the settled HALO mapping
            // is bounded by the 0.90*Nyquist ceiling, tested separately).
            if (! (std::isfinite (f) && f > 0.0f && f <= 0.49f * (float) rate + 1.0f))
                freqOk = false;

            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (0, i);
                maxOut = juce::jmax (maxOut, std::abs (v));
                maxDelta = juce::jmax (maxDelta, std::abs (v - prevOut));
                prevOut = v;
            }
        }

        expect (finite,
                "torture " + juce::String (sweepFreq ? "sweep" : "toggle")
                    + " @ " + juce::String (rate, 0) + ": output must stay finite");
        expect (freqOk,
                "torture " + juce::String (sweepFreq ? "sweep" : "toggle")
                    + " @ " + juce::String (rate, 0)
                    + ": mapped frequency must stay legal (0 < f <= ceiling)");
        expect (maxOut < 1.0f,
                "torture " + juce::String (sweepFreq ? "sweep" : "toggle")
                    + " @ " + juce::String (rate, 0) + ": bounded output (max "
                    + juce::String (maxOut, 3) + ")");
        // The sine itself has a max delta of ~0.71 at 44.1 kHz (0.25 peak,
        // 10 kHz); a click/state explosion would blow far past 1.5.
        expect (maxDelta < 1.5f,
                "torture " + juce::String (sweepFreq ? "sweep" : "toggle")
                    + " @ " + juce::String (rate, 0) + ": no click (max delta "
                    + juce::String (maxDelta, 3) + ")");
    }
    static void runSilence (C4EngineCore& engine, double rate, int blockSize, double seconds)
    {
        juce::AudioBuffer<float> buf (1, blockSize);
        float* chans[1] = { buf.getWritePointer (0) };
        const int total = (int) (rate * seconds);
        int done = 0;
        while (done < total)
        {
            C4Test::clearBufferExplicit (buf);
            engine.adoptTargets (0.0f);
            engine.processBlock (chans, 1, blockSize);
            done += blockSize;
        }
    }

    static std::vector<float> captureIr (C4EngineCore& engine, double rate,
                                         int blockSize, int irLength)
    {
        juce::AudioBuffer<float> buf (1, blockSize);
        float* chans[1] = { buf.getWritePointer (0) };
        const int settle = (int) (rate * 1.0);
        int done = 0;
        while (done < settle)
        {
            C4Test::clearBufferExplicit (buf);
            engine.adoptTargets (0.0f);
            engine.processBlock (chans, 1, blockSize);
            done += blockSize;
        }
        std::vector<float> ir (irLength, 0.0f);
        bool impulseSent = false;
        int written = 0;
        while (written < irLength)
        {
            C4Test::clearBufferExplicit (buf);
            if (! impulseSent)
            {
                buf.setSample (0, 0, 1.0f);
                impulseSent = true;
            }
            engine.adoptTargets (0.0f);
            engine.processBlock (chans, 1, blockSize);
            const int n = juce::jmin (blockSize, irLength - written);
            for (int i = 0; i < n; ++i)
                ir[(size_t) (written + i)] = buf.getSample (0, i);
            written += n;
        }
        return ir;
    }
};

static C4HaloMapperTests c4HaloMapperTests;
