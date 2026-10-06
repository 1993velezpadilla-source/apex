// =============================================================================
//  ApexFormantCore.cpp
//  See header. Generates ramped per-note formant + gain control curves.
//
//  Drop-in: Source/VocalTuneCore/ApexFormantCore.cpp
// =============================================================================

#include "ApexFormantCore.h"
#include <algorithm>
#include <cmath>

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
void ApexFormantCore::writeRampedSlice (std::vector<float>& curve,
                                        int firstPt, int lastPt,
                                        float value, float outOfRangeValue,
                                        int rampPts)
{
    const int N = (int) curve.size();
    firstPt = juce::jlimit (0, N - 1, firstPt);
    lastPt  = juce::jlimit (0, N - 1, lastPt);
    if (lastPt < firstPt) return;

    rampPts = juce::jmax (1, rampPts);
    const int sliceLen = lastPt - firstPt + 1;
    const int usableRamp = juce::jmin (rampPts, sliceLen / 2);

    for (int i = 0; i < sliceLen; ++i)
    {
        float v = value;
        if (usableRamp > 0)
        {
            if (i < usableRamp)
            {
                // Fade in from outOfRangeValue -> value.
                const float t = (float) i / (float) usableRamp;
                v = outOfRangeValue + (value - outOfRangeValue) * t;
            }
            else if (i >= sliceLen - usableRamp)
            {
                // Fade out value -> outOfRangeValue.
                const float t = (float) (sliceLen - 1 - i) / (float) usableRamp;
                v = outOfRangeValue + (value - outOfRangeValue) * t;
            }
        }
        curve[(size_t) (firstPt + i)] = v;
    }
}

// -----------------------------------------------------------------------------
static int numControlPointsFor (const ApexTuneAnalysis& a, int controlHopSamples)
{
    if (a.numSourceSamples <= 0 || controlHopSamples <= 0) return 0;
    return (a.numSourceSamples + controlHopSamples - 1) / controlHopSamples;
}

// -----------------------------------------------------------------------------
std::vector<float> ApexFormantCore::buildFormantCurve (const ApexTuneAnalysis& a,
                                                       const std::vector<ApexTuneNote>& notes,
                                                       const Params& p)
{
    const int numPts = numControlPointsFor (a, p.controlHopSamples);
    std::vector<float> curve ((size_t) numPts, 0.0f);

    if (numPts == 0 || notes.empty()) return curve;

    const float ptsPerMs = (float) a.sampleRate / (float) p.controlHopSamples / 1000.0f;
    const int   rampPts  = juce::jmax (1, (int) std::round (p.rampMs * ptsPerMs));

    for (const auto& n : notes)
    {
        if (n.endSample <= n.startSample) continue;

        const float clamped = juce::jlimit (p.minShift, p.maxShift, n.formantShift);
        if (std::abs (clamped) < 1.0e-4f) continue;   // skip neutral notes

        const int firstPt = (int) (n.startSample / (int64_t) p.controlHopSamples);
        const int lastPt  = (int) ((n.endSample - 1) / (int64_t) p.controlHopSamples);

        writeRampedSlice (curve, firstPt, lastPt, clamped, /*outOfRange*/ 0.0f, rampPts);
    }

    return curve;
}

// -----------------------------------------------------------------------------
std::vector<float> ApexFormantCore::buildGainCurve (const ApexTuneAnalysis& a,
                                                    const std::vector<ApexTuneNote>& notes,
                                                    const Params& p)
{
    const int numPts = numControlPointsFor (a, p.controlHopSamples);
    std::vector<float> curve ((size_t) numPts, 1.0f);

    if (numPts == 0 || notes.empty()) return curve;

    const float ptsPerMs = (float) a.sampleRate / (float) p.controlHopSamples / 1000.0f;
    const int   rampPts  = juce::jmax (1, (int) std::round (p.rampMs * ptsPerMs));

    for (const auto& n : notes)
    {
        if (n.endSample <= n.startSample) continue;
        if (std::abs (n.gainDb) < 1.0e-4f) continue;

        const float linGain = juce::Decibels::decibelsToGain (n.gainDb);
        const int firstPt = (int) (n.startSample / (int64_t) p.controlHopSamples);
        const int lastPt  = (int) ((n.endSample - 1) / (int64_t) p.controlHopSamples);

        writeRampedSlice (curve, firstPt, lastPt, linGain, /*outOfRange*/ 1.0f, rampPts);
    }

    return curve;
}

}} // namespace apex::vocaltune
