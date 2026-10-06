// =============================================================================
//  ApexFormantCore.h
//  Per-note formant shift -> control curve. Consumed by ApexTuneRenderCore.
//
//  Drop-in: Source/VocalTuneCore/ApexFormantCore.h
//  Depends on: ApexTuneTypes.h, JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Threading: pure offline. Called once per render pass.
//
//  Why this exists as its own core:
//   SignalSmith Stretch accepts a "formant" parameter per-block. To drive it
//   smoothly from per-note formantShift fields without pops at note edges,
//   we generate a control-rate curve here, with short linear ramps at
//   note boundaries (crossfaded against neutral 0.0 outside notes).
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"

namespace apex { namespace vocaltune {

class ApexFormantCore
{
public:
    struct Params
    {
        int   controlHopSamples = 256;
        float rampMs            = 20.0f;   // linear ramp at note edges
        float minShift          = -12.0f;  // semitones
        float maxShift          = +12.0f;
    };

    // Returns one formant value (in semitones) per controlHop covering the
    // full source duration. Unvoiced / outside-note regions = 0.0.
    static std::vector<float> buildFormantCurve (const ApexTuneAnalysis& analysis,
                                                 const std::vector<ApexTuneNote>& notes,
                                                 const Params& params = {});

    // Returns one gain (linear, not dB) per controlHop. Outside notes = 1.0.
    // Lets the renderer apply per-note gainDb cleanly with edge ramps.
    static std::vector<float> buildGainCurve  (const ApexTuneAnalysis& analysis,
                                                const std::vector<ApexTuneNote>& notes,
                                                const Params& params = {});

private:
    // Writes a value into a slice of `curve` with linear ramps at both edges.
    // outOfRangeValue is the value the curve holds outside the slice
    // (so we can ramp from it back to it).
    static void writeRampedSlice (std::vector<float>& curve,
                                  int firstPt, int lastPt,
                                  float value, float outOfRangeValue,
                                  int rampPts);
};

}} // namespace apex::vocaltune
