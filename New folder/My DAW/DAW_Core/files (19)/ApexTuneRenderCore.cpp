// =============================================================================
//  ApexTuneRenderCore.cpp
//  See header. Drives SignalSmith Stretch with per-block pitch + formant,
//  then crossfades against dry source in sibilant regions.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneRenderCore.cpp
// =============================================================================

#include "ApexTuneRenderCore.h"
#include "ApexPitchCurveCore.h"
#include "ApexFormantCore.h"

// -----------------------------------------------------------------------------
//  SignalSmith Stretch integration.
//  If your include path differs, adjust this single line. The rest of the
//  SignalSmith API surface is isolated below in renderThroughStretcher().
// -----------------------------------------------------------------------------
#include "signalsmith-stretch.h"

#include <algorithm>
#include <cmath>

namespace apex { namespace vocaltune {

namespace
{
    using SmithStretch = signalsmith::stretch::SignalsmithStretch<float>;

    // -------------------------------------------------------------------------
    // Linear interpolation of a per-control-hop float curve at a sample pos.
    // -------------------------------------------------------------------------
    float sampleControlFloat (const std::vector<float>& curve,
                              int controlHopSamples,
                              int64_t samplePos)
    {
        if (curve.empty() || controlHopSamples <= 0) return 0.0f;

        const float posF = (float) samplePos / (float) controlHopSamples;
        const int   i0   = juce::jlimit (0, (int) curve.size() - 1, (int) std::floor (posF));
        const int   i1   = juce::jlimit (0, (int) curve.size() - 1, i0 + 1);
        const float frac = posF - (float) i0;

        return curve[(size_t) i0] * (1.0f - frac) + curve[(size_t) i1] * frac;
    }

    // -------------------------------------------------------------------------
    // Same for the pitch curve (which carries ratio + voiced flag).
    // We only interpolate the ratio -- voiced is a discrete flag and we just
    // take the nearest control point's voicing state.
    // -------------------------------------------------------------------------
    float sampleControlRatio (const std::vector<ApexPitchCurveCore::CurvePoint>& curve,
                              int controlHopSamples,
                              int64_t samplePos)
    {
        if (curve.empty() || controlHopSamples <= 0) return 1.0f;

        const float posF = (float) samplePos / (float) controlHopSamples;
        const int   i0   = juce::jlimit (0, (int) curve.size() - 1, (int) std::floor (posF));
        const int   i1   = juce::jlimit (0, (int) curve.size() - 1, i0 + 1);
        const float frac = posF - (float) i0;

        return curve[(size_t) i0].ratio * (1.0f - frac)
             + curve[(size_t) i1].ratio * frac;
    }

    // -------------------------------------------------------------------------
    // Pass the source through SignalSmith with per-block ratio + formant
    // updates. Returns a sample-aligned output buffer (same length as input).
    //
    // ADJUST HERE if your SignalSmith API differs. Calls used:
    //   presetDefault(channels, sampleRate)
    //   reset()
    //   inputLatency()
    //   setTransposeFactor(float)
    //   setFormantSemitones(float)  -- only if preserveFormants && nonzero
    //   process(inputs, inputSamples, outputs, outputSamples)
    // -------------------------------------------------------------------------
    std::vector<float> renderThroughStretcher
        (const float*                                          source,
         int                                                   numSamples,
         double                                                sampleRate,
         const std::vector<ApexPitchCurveCore::CurvePoint>&    pitchCurve,
         const std::vector<float>&                             formantCurve,
         int                                                   controlHopSamples,
         int                                                   blockSize,
         bool                                                  preserveFormants)
    {
        std::vector<float> wet ((size_t) numSamples, 0.0f);
        if (numSamples <= 0) return wet;

        SmithStretch stretch;
        stretch.presetDefault (1, (float) sampleRate);
        stretch.reset();

        const int latency = stretch.inputLatency();

        // ---- Prime with `latency` zeros so output is sample-aligned ---------
        // We feed `latency` zero-input samples and discard the output. After
        // this, the stretcher's pipeline is full and subsequent process()
        // calls yield latency-compensated audio.
        if (latency > 0)
        {
            std::vector<float> priming ((size_t) latency, 0.0f);
            std::vector<float> trash   ((size_t) latency, 0.0f);

            stretch.setTransposeFactor (1.0f);
            if (preserveFormants) stretch.setFormantSemitones (0.0f);

            const float* primingPtr[1] = { priming.data() };
            float*       trashPtr  [1] = { trash.data()   };
            stretch.process (primingPtr, latency, trashPtr, latency);
        }

        // ---- Main processing loop ------------------------------------------
        int  pos = 0;
        while (pos < numSamples)
        {
            const int thisBlock = juce::jmin (blockSize, numSamples - pos);

            // Use the block's MIDPOINT for ratio / formant evaluation -- gives
            // smoother behavior than the start, and the linear-interp curves
            // average out across the block.
            const int64_t evalPos = (int64_t) pos + thisBlock / 2;

            const float ratio = sampleControlRatio (pitchCurve, controlHopSamples, evalPos);
            stretch.setTransposeFactor (juce::jlimit (0.25f, 4.0f, ratio));

            if (preserveFormants)
            {
                const float formant = sampleControlFloat (formantCurve, controlHopSamples, evalPos);
                stretch.setFormantSemitones (juce::jlimit (-12.0f, 12.0f, formant));
            }

            const float* inPtr [1] = { source + pos };
            float*       outPtr[1] = { wet.data() + pos };
            stretch.process (inPtr, thisBlock, outPtr, thisBlock);

            pos += thisBlock;
        }

        return wet;
    }

    // -------------------------------------------------------------------------
    // Raised-cosine ramp from `from` to `to` over `len` samples, inclusive
    // of `to` at index len-1.
    // -------------------------------------------------------------------------
    void writeRaisedCosineRamp (std::vector<float>& buf,
                                int                 startIdx,
                                int                 len,
                                float               fromVal,
                                float               toVal)
    {
        if (len <= 0) return;
        const int N = (int) buf.size();

        for (int i = 0; i < len; ++i)
        {
            const int idx = startIdx + i;
            if (idx < 0 || idx >= N) continue;

            const float t = (float) i / (float) juce::jmax (1, len - 1);
            // raised cosine: 0.5 * (1 - cos(pi * t)) -> 0..1 smoothly
            const float w = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * t));
            buf[(size_t) idx] = fromVal + (toVal - fromVal) * w;
        }
    }

    // -------------------------------------------------------------------------
    // Sample-rate dry/wet mix curve. 1.0 = wet, 0.0 = dry. Sibilant regions
    // are pushed to 0.0 with raised-cosine ramps on each edge.
    // -------------------------------------------------------------------------
    std::vector<float> buildMixCurve (int                                     numSamples,
                                      const std::vector<ApexSibilantRegion>&  sibilants,
                                      double                                  sampleRate,
                                      float                                   fadeMs)
    {
    std::vector<float> mix ((size_t) numSamples, 1.0f);    // default: wet
    if (numSamples <= 0 || sampleRate <= 0.0) return mix;

    const int fadeSamples = juce::jmax (1, (int) std::round (fadeMs * 0.001f * (float) sampleRate));

    for (const auto& r : sibilants)
    {
        const int s = juce::jlimit (0, numSamples - 1, (int) r.startSample);
        const int e = juce::jlimit (0, numSamples - 1, (int) (r.endSample - 1));
        if (e < s) continue;

        // Inside region: dry (0.0).
        for (int i = s; i <= e; ++i) mix[(size_t) i] = 0.0f;

        // Ramp in (1 -> 0) before s.
        writeRaisedCosineRamp (mix, s - fadeSamples, fadeSamples, 1.0f, 0.0f);
        // Ramp out (0 -> 1) after e.
        writeRaisedCosineRamp (mix, e + 1,           fadeSamples, 0.0f, 1.0f);
    }

    return mix;
    }
} // anonymous namespace

// -----------------------------------------------------------------------------
ApexTuneRenderCore::Result
ApexTuneRenderCore::renderClipMono (const float*                            source,
                                    int                                     numSamples,
                                    double                                  sampleRate,
                                    const ApexTuneAnalysis&                 analysis,
                                    const std::vector<ApexTuneNote>&        notes,
                                    const std::vector<ApexSibilantRegion>&  sibilants,
                                    const Params&                           params)
{
    Result result;
    result.sampleRate = sampleRate;

    if (source == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    {
        result.errorMessage = "Invalid source buffer.";
        return result;
    }
    if (analysis.hopSamples <= 0 || analysis.frames.empty())
    {
        // Nothing to render -- return dry source unchanged.
        result.audio.assign (source, source + numSamples);
        result.success = true;
        return result;
    }

    // -------------------------------------------------------------------------
    // 1. Build the three control curves.
    // -------------------------------------------------------------------------
    ApexPitchCurveCore::Params pitchP;
    pitchP.controlHopSamples = analysis.hopSamples;
    const auto pitchCurve   = ApexPitchCurveCore::buildCurve (analysis, notes, pitchP);

    ApexFormantCore::Params formantP;
    formantP.controlHopSamples = analysis.hopSamples;
    const auto formantCurve = ApexFormantCore::buildFormantCurve (analysis, notes, formantP);
    const auto gainCurve    = ApexFormantCore::buildGainCurve    (analysis, notes, formantP);

    // -------------------------------------------------------------------------
    // 2. Run source through SignalSmith with per-block ratio + formant.
    // -------------------------------------------------------------------------
    const auto wet = renderThroughStretcher (source, numSamples, sampleRate,
                                             pitchCurve, formantCurve,
                                             analysis.hopSamples,
                                             juce::jmax (32, params.processingBlockSize),
                                             params.preserveFormants);

    if ((int) wet.size() != numSamples)
    {
        result.errorMessage = "Stretcher output length mismatch.";
        return result;
    }

    // -------------------------------------------------------------------------
    // 3. Build sample-rate dry/wet mix curve.
    // -------------------------------------------------------------------------
    const auto mixCurve = buildMixCurve (numSamples, sibilants,
                                         sampleRate, params.sibilantFadeMs);

    // -------------------------------------------------------------------------
    // 4. Final mix + per-note gain.
    // -------------------------------------------------------------------------
    result.audio.assign ((size_t) numSamples, 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const float m       = mixCurve[(size_t) i];
        const float mixed   = m * wet[(size_t) i] + (1.0f - m) * source[i];
        const float gain    = sampleControlFloat (gainCurve, analysis.hopSamples, (int64_t) i);
        result.audio[(size_t) i] = mixed * gain;
    }

    result.success = true;
    return result;
}

}} // namespace apex::vocaltune
