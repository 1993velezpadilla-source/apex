// =============================================================================
//  ApexTuneRenderCore.h
//  Renders tuned audio from source + analysis + notes via SignalSmith Stretch.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneRenderCore.h
//  Depends on: ApexTuneTypes.h, ApexSibilantDetectorCore.h, JUCE.
//              .cpp also depends on ApexPitchCurveCore, ApexFormantCore,
//              and SignalSmith Stretch.
//  Touches:    nothing else in the APEX codebase.
//
//  Threading: offline only. Call from a background worker. Allocates freely.
//             NEVER call from the audio thread. The returned audio buffer is
//             then either cached and read by the audio thread, or written
//             directly to disk during export.
//
//  Output: mono float buffer, sample-aligned with the input (same length,
//          same sample rate). Voiced regions are SignalSmith-stretched per
//          the pitch curve; sibilant regions are crossfaded to the dry
//          source; per-note gainDb is applied via the gain curve.
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"
#include "ApexSibilantDetectorCore.h"

namespace apex { namespace vocaltune {

class ApexTuneRenderCore
{
public:
    struct Params
    {
        int   processingBlockSize = 512;    // samples per SignalSmith call
        float sibilantFadeMs      = 20.0f;  // raised-cosine ramp at sibilant edges
        bool  preserveFormants    = true;   // drive SignalSmith formant control
    };

    struct Result
    {
        std::vector<float> audio;           // mono, length == numSourceSamples
        double             sampleRate = 0.0;
        bool               success    = false;
        juce::String       errorMessage;
    };

    // Full offline render of a mono clip.
    //   source        Mono input buffer (-1..+1 float).
    //   numSamples    Source length in samples.
    //   sampleRate    Source sample rate.
    //   analysis      YIN analysis (frames + hop + sample rate).
    //   notes         Edited notes (targetMidi, knobs, etc.).
    //   sibilants     Sibilant/breath regions to pass through dry.
    //   params        Tunables.
    //
    // Returns Result with audio buffer + success flag.
    static Result renderClipMono (const float*                            source,
                                  int                                     numSamples,
                                  double                                  sampleRate,
                                  const ApexTuneAnalysis&                 analysis,
                                  const std::vector<ApexTuneNote>&        notes,
                                  const std::vector<ApexSibilantRegion>&  sibilants,
                                  const Params&                           params = {});
};

}} // namespace apex::vocaltune
