#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4MorphAuditTests — BELL/SHELF MORPH AUDIT (audit B).
//
// The WEIGHT band morphs between bell and shelf by blending the outputs of
// the SAME SVF states: shape = (1-m)*band + m*shelf. This suite measures the
// intermediate response at fixed blend points (0/25/50/75/100%) and during
// fast automation:
//
//   - finite response at every blend point
//   - continuous magnitude (monotone progression between bell and shelf)
//   - continuous phase (no phase jump across blend points)
//   - no transient spike / click during fast morph automation
//   - no hidden resonance (magnitude bounded between the two endpoints)
//   - no state discontinuity (settled blend == directly-requested blend)
//
// The blend target is held via the engine test hook; production always uses
// 0/1 targets and reaches every intermediate state through the 8 ms smoother.
// ============================================================================

using APEX::C4::C4EngineCore;
using APEX::C4::C4BandId;
using APEX::C4::C4TuningProfile;
using APEX::C4::makeProfileVariantB;

class C4MorphAuditTests final : public juce::UnitTest
{
public:
    C4MorphAuditTests() : juce::UnitTest ("C4.MorphAudit", "APEX.C4") {}

    void runTest() override
    {
        const double rate = 48000.0;
        const int block = 512;
        const int band = (int) C4BandId::Weight;
        const float blends[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

        beginTest ("Morph blend points 0/25/50/75/100%: finite, continuous magnitude and phase");

        struct Point { float mag25Hz; float mag100Hz; float mag1000Hz; float phase25Hz; float phase100Hz; };
        Point points[5] = {};
        juce::StringArray failures;

        for (int i = 0; i < 5; ++i)
        {
            C4EngineCore engine;
            engine.prepare (rate, block, 1, makeProfileVariantB());
            engine.setBandFreqTargetHz (band, 100.0f);
            engine.setBandGainTargetDb (band, 6.0f);
            engine.setBandModeBlendForTest (band, blends[i]);

            const auto ir = captureEngineIr (engine, rate, block);
            if (! C4Test::allFinite (ir))
                failures.add ("blend " + juce::String (blends[i]) + ": non-finite IR");

            points[i].mag25Hz  = C4Test::magnitudeDbAtFrequency (ir, rate, 25.0, 16384);
            points[i].mag100Hz = C4Test::magnitudeDbAtFrequency (ir, rate, 100.0, 16384);
            points[i].mag1000Hz= C4Test::magnitudeDbAtFrequency (ir, rate, 1000.0, 16384);
            points[i].phase25Hz = C4Test::phaseRadiansAtFrequency (ir, rate, 25.0, 16384);
            points[i].phase100Hz = C4Test::phaseRadiansAtFrequency (ir, rate, 100.0, 16384);
        }

        // Finite + bounded (no hidden resonance: nothing above the shelf
        // plateau + margin, nothing below the bell floor - margin).
        for (int i = 0; i < 5; ++i)
        {
            const float magMax = juce::jmax (points[i].mag25Hz, points[i].mag100Hz, points[i].mag1000Hz);
            const float magMin = juce::jmin (points[i].mag25Hz, points[i].mag100Hz, points[i].mag1000Hz);
            expect (magMax < 9.0f, "blend " + juce::String (blends[i])
                + ": no resonance bump (max " + juce::String (magMax, 2) + " dB)");
            expect (magMin > -3.0f, "blend " + juce::String (blends[i])
                + ": no pathological dip (min " + juce::String (magMin, 2) + " dB)");
        }

        // Continuous magnitude: the 25 Hz plateau gain must progress
        // monotonically from the bell value (~0 dB) to the shelf value
        // (~+6 dB) with no overshoot beyond the endpoints.
        expect (points[0].mag25Hz < points[1].mag25Hz
                    && points[1].mag25Hz < points[2].mag25Hz
                    && points[2].mag25Hz < points[3].mag25Hz
                    && points[3].mag25Hz < points[4].mag25Hz,
                "25 Hz plateau must progress monotonically: "
                    + juce::String (points[0].mag25Hz, 2) + " < "
                    + juce::String (points[1].mag25Hz, 2) + " < "
                    + juce::String (points[2].mag25Hz, 2) + " < "
                    + juce::String (points[3].mag25Hz, 2) + " < "
                    + juce::String (points[4].mag25Hz, 2));
        expect (points[0].mag25Hz < 1.0f && points[4].mag25Hz > 5.0f,
                "endpoints must be the pure bell (~0 dB) and pure shelf (~+6 dB): "
                    + juce::String (points[0].mag25Hz, 2) + " / "
                    + juce::String (points[4].mag25Hz, 2));
        expect (points[4].mag25Hz <= 6.8f,
                "shelf plateau must not overshoot the +6 dB request");

        // Continuous phase: no step larger than 0.25 rad between adjacent
        // blend points at 25 Hz and 100 Hz (SVF states are shared, so the
        // morph is a linear mix of two signals from the SAME filter; the
        // phase-vs-blend curve is smooth but NOT linear — the swing is
        // fastest near the shelf end, measured max 0.196 rad per 25% step).
        // A true discontinuity (state swap) would be a jump of order 1 rad.
        for (int i = 1; i < 5; ++i)
        {
            const float d25 = std::abs (points[i].phase25Hz - points[i - 1].phase25Hz);
            const float d100 = std::abs (points[i].phase100Hz - points[i - 1].phase100Hz);
            expect (d25 < 0.25f, "phase continuity at 25 Hz between blends "
                + juce::String (blends[i - 1]) + "->" + juce::String (blends[i])
                + ": delta " + juce::String (d25, 3) + " rad");
            expect (d100 < 0.25f, "phase continuity at 100 Hz between blends "
                + juce::String (blends[i - 1]) + "->" + juce::String (blends[i])
                + ": delta " + juce::String (d100, 3) + " rad");
        }

        // State continuity: a blend REACHED by fast automation equals the
        // same blend REQUESTED directly (no state discontinuity).
        beginTest ("Morph state continuity: automated blend == directly requested blend");
        {
            C4EngineCore engine;
            engine.prepare (rate, block, 1, makeProfileVariantB());
            engine.setBandFreqTargetHz (band, 100.0f);
            engine.setBandGainTargetDb (band, 6.0f);

            // Fast automation: alternate bell/shelf targets, then hold shelf.
            juce::AudioBuffer<float> buf (1, block);
            float* chans[1] = { buf.getWritePointer (0) };
            for (int b = 0; b < 400; ++b)
            {
                engine.setBandModeTarget (band, (b % 20) < 10);
                C4Test::clearBufferExplicit (buf); // raw-engine contract
                engine.processBlock (chans, 1, block);
            }
            // Hold shelf: the alternating loop above ends on a BELL target
            // (b=399 -> (399%20)=19 >= 10 -> false), so the final state must
            // be requested explicitly.
            engine.setBandModeTarget (band, true);
            const auto autoIr = captureEngineIr (engine, rate, block);
            const float autoMag = C4Test::magnitudeDbAtFrequency (autoIr, rate, 25.0, 16384);

            expect (std::abs (autoMag - points[4].mag25Hz) < 0.05f,
                    "automated settle to shelf must equal direct shelf: "
                        + juce::String (autoMag, 3) + " vs "
                        + juce::String (points[4].mag25Hz, 3));
        }

        // Fast morph automation with audio running: no click, no spike,
        // everything finite and bounded.
        beginTest ("Fast morph automation: bounded, finite, no click");
        {
            C4EngineCore engine;
            engine.prepare (rate, block, 1, makeProfileVariantB());
            engine.setBandFreqTargetHz (band, 100.0f);
            engine.setBandGainTargetDb (band, 6.0f);

            juce::AudioBuffer<float> buf (1, block);
            float* chans[1] = { buf.getWritePointer (0) };
            const double inPeak = 0.5;
            float maxOut = 0.0f;
            bool finite = true;
            for (int b = 0; b < 500; ++b)
            {
                engine.setBandModeTarget (band, (b % 3) == 0);
                for (int i = 0; i < block; ++i)
                    buf.setSample (0, i, (float) (inPeak * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 1000.0 * (b * block + i) / rate)));
                engine.processBlock (chans, 1, block);
                finite = finite && C4Test::allFinite (buf);
                for (int i = 0; i < block; ++i)
                    maxOut = juce::jmax (maxOut, std::abs (buf.getSample (0, i)));
            }
            expect (finite, "fast morph must stay finite");
            // A +6 dB band with a morphing shape cannot exceed roughly
            // 2x the input peak (6 dB = 2.0x) plus smoothing margin; a click
            // spike would blow well past this bound.
            expect (maxOut <= inPeak * 2.5f + 0.01f,
                    "no click/spike during fast morph: max |out| = "
                        + juce::String (maxOut, 4) + " (input peak "
                        + juce::String (inPeak, 2) + ")");
        }
    }

private:
    static std::vector<float> captureEngineIr (C4EngineCore& engine,
                                               double sampleRate, int blockSize)
    {
        juce::AudioBuffer<float> buf (1, blockSize);
        float* chans[1] = { buf.getWritePointer (0) };

        // Settle smoothing (explicit zero-fill: raw engine writes defeat
        // JUCE's flagged clear() — the measured growing-IR root cause).
        const int settle = (int) (sampleRate * 1.0);
        int done = 0;
        while (done < settle)
        {
            C4Test::clearBufferExplicit (buf);
            engine.processBlock (chans, 1, blockSize);
            done += blockSize;
        }

        std::vector<float> ir (8192, 0.0f);
        bool impulseSent = false;
        int written = 0;
        while (written < 8192)
        {
            C4Test::clearBufferExplicit (buf);
            if (! impulseSent)
            {
                buf.setSample (0, 0, 1.0f);
                impulseSent = true;
            }
            engine.processBlock (chans, 1, blockSize);
            const int n = juce::jmin (blockSize, 8192 - written);
            for (int i = 0; i < n; ++i)
                ir[(size_t) (written + i)] = buf.getSample (0, i);
            written += n;
        }
        return ir;
    }
};

static C4MorphAuditTests c4MorphAuditTests;
