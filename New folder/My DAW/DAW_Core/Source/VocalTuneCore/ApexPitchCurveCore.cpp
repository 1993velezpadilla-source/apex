// =============================================================================
//  ApexPitchCurveCore.cpp
//  Per-sample pitch ratio curve. See header for math model.
//
//  Drop-in: Source/VocalTuneCore/ApexPitchCurveCore.cpp
// =============================================================================

#include "ApexPitchCurveCore.h"
#include <algorithm>

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
void ApexPitchCurveCore::lowPassInPlace (std::vector<float>& signal,
                                         float cutoffHz, float rateHz)
{
    if (signal.empty() || rateHz <= 0.0f || cutoffHz <= 0.0f) return;

    // 1-pole IIR coefficient.
    //  a = exp(-2*pi*fc/fs)
    const float a = std::exp (-2.0f * juce::MathConstants<float>::pi * cutoffHz / rateHz);
    const float oneMinusA = 1.0f - a;

    float y = signal.front();
    for (auto& x : signal)
    {
        y = a * y + oneMinusA * x;
        x = y;
    }
}

// -----------------------------------------------------------------------------
std::vector<ApexPitchCurveCore::CurvePoint>
ApexPitchCurveCore::buildCurve (const ApexTuneAnalysis& a,
                                const std::vector<ApexTuneNote>& notes,
                                const Params& p)
{
    std::vector<CurvePoint> out;
    if (a.numSourceSamples <= 0 || p.controlHopSamples <= 0)
        return out;

    // -------------------------------------------------------------------------
    // Determine curve length: 1 control point per controlHopSamples,
    // covering the whole source.
    // -------------------------------------------------------------------------
    const int numPoints = (a.numSourceSamples + p.controlHopSamples - 1)
                           / p.controlHopSamples;
    out.assign ((size_t) numPoints, CurvePoint{});

    if (a.frames.empty() || notes.empty())
        return out;   // all pass-through

    // -------------------------------------------------------------------------
    // Helper: convert a source-sample position to a YIN frame index.
    // -------------------------------------------------------------------------
    auto sampleToFrame = [&] (int64_t samplePos) -> int
    {
        if (a.hopSamples <= 0) return 0;
        const int idx = (int) (samplePos / (int64_t) a.hopSamples);
        return juce::jlimit (0, (int) a.frames.size() - 1, idx);
    };

    // -------------------------------------------------------------------------
    // Process each note independently. For each, extract detected F0 in MIDI
    // at every control point inside the note's sample range, low-pass to get
    // drift, subtract for modulation, scale by knobs, and emit ratio.
    // -------------------------------------------------------------------------
    const float controlRateHz = (float) a.sampleRate / (float) p.controlHopSamples;

    for (const auto& note : notes)
    {
        if (! note.voiced || note.sibilantProtected) continue;
        if (note.endSample <= note.startSample)      continue;

        // Map note sample range to control-point range.
        const int firstPt = juce::jlimit (0, numPoints - 1,
                                          (int) (note.startSample / (int64_t) p.controlHopSamples));
        const int lastPt  = juce::jlimit (0, numPoints - 1,
                                          (int) ((note.endSample - 1) / (int64_t) p.controlHopSamples));
        const int len     = lastPt - firstPt + 1;
        if (len <= 0) continue;

        // Gather per-control-point detected F0 (in MIDI) and a "valid" mask.
        std::vector<float> detMidi   ((size_t) len, note.detectedMidi);
        std::vector<bool>  valid     ((size_t) len, false);
        std::vector<float> rawF0Hz   ((size_t) len, 0.0f);

        for (int i = 0; i < len; ++i)
        {
            const int64_t samplePos = (int64_t) (firstPt + i) * (int64_t) p.controlHopSamples;
            const int     frameIdx  = sampleToFrame (samplePos);
            const auto&   f         = a.frames[(size_t) frameIdx];

            if (f.voiced && f.f0Hz >= p.minDetectedF0Hz)
            {
                detMidi[(size_t) i] = hzToMidi (f.f0Hz);
                rawF0Hz[(size_t) i] = f.f0Hz;
                valid  [(size_t) i] = true;
            }
        }

        // Offset(t) = detected(t) - noteMedianDetected.
        std::vector<float> offset ((size_t) len, 0.0f);
        for (int i = 0; i < len; ++i)
            offset[(size_t) i] = detMidi[(size_t) i] - note.detectedMidi;

        // Drift = low-pass(offset); modulation = offset - drift.
        std::vector<float> drift = offset;
        lowPassInPlace (drift, p.driftCutoffHz, controlRateHz);

        // Emit curve points.
        for (int i = 0; i < len; ++i)
        {
            const int outIdx = firstPt + i;
            if (outIdx < 0 || outIdx >= numPoints) continue;

            if (! valid[(size_t) i])
            {
                // Voiced frame missing inside a note -- pass through to avoid artifacts.
                out[(size_t) outIdx].ratio      = 1.0f;
                out[(size_t) outIdx].targetMidi = 0.0f;
                out[(size_t) outIdx].voiced     = false;
                continue;
            }

            const float driftPart      = drift[(size_t) i];
            const float modulationPart = offset[(size_t) i] - driftPart;

            const float preservedOffset = driftPart      * note.driftAmount
                                        + modulationPart * note.modulationAmount;

            // Base target = lerp between "leave alone" and "snap to target".
            // At correctionAmount=0: target = detected (no tuning).
            // At correctionAmount=1: target = note.targetMidi + preserved variation.
            const float fullyCorrected = note.targetMidi + preservedOffset;
            const float natural        = detMidi[(size_t) i];                // pure detected
            const float tgtMidi        = natural
                                       + (fullyCorrected - natural) * note.correctionAmount;

            // Compute ratio. Guard with detected F0 in Hz (not MIDI) for accuracy.
            const float detHz = rawF0Hz[(size_t) i];
            const float tgtHz = midiToHz (tgtMidi);
            float ratio = (detHz > p.minDetectedF0Hz) ? (tgtHz / detHz) : 1.0f;
            ratio = juce::jlimit (p.minRatio, p.maxRatio, ratio);

            out[(size_t) outIdx].ratio      = ratio;
            out[(size_t) outIdx].targetMidi = tgtMidi;
            out[(size_t) outIdx].voiced     = true;
        }
    }

    if (p.transitionSmoothingPts > 0 && out.size() > 2)
    {
        auto smoothed = out;
        const int radius = juce::jlimit (1, 8, p.transitionSmoothingPts);
        for (int i = 0; i < (int) out.size(); ++i)
        {
            if (! out[(size_t) i].voiced) continue;
            float sum = 0.0f;
            int count = 0;
            for (int k = -radius; k <= radius; ++k)
            {
                const int idx = i + k;
                if (idx < 0 || idx >= (int) out.size()) continue;
                if (! out[(size_t) idx].voiced) continue;
                sum += out[(size_t) idx].ratio;
                ++count;
            }
            if (count > 0)
                smoothed[(size_t) i].ratio = juce::jlimit (p.minRatio, p.maxRatio, sum / (float) count);
        }
        out = std::move (smoothed);
    }

    return out;
}

}} // namespace apex::vocaltune
