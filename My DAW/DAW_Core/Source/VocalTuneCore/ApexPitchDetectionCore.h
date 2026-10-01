// =============================================================================
//  ApexPitchDetectionCore.h
//  Monophonic F0 detection via YIN (de Cheveigne & Kawahara 2002).
//
//  Drop-in: Source/VocalTuneCore/ApexPitchDetectionCore.h
//  Depends on: ApexTuneTypes.h, JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Threading: analyzeOffline() is intended to be called from a background
//             worker thread. It is reentrant (no static state). NEVER call
//             from the audio thread -- it allocates.
//
//  V1 quality: clean YIN with parabolic interpolation and energy gating.
//              Future upgrade path: pYIN (HMM smoothing) or neural detection
//              (CREPE-style). Detection is intentionally swappable behind
//              the analyzeOffline() entry point.
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"

namespace apex { namespace vocaltune {

class ApexPitchDetectionCore
{
public:
    struct Params
    {
        int   frameSize         = 2048;    // YIN analysis window
        int   hopSamples        = 256;     // 187 frames/sec @ 48kHz
        float minHz             = 65.0f;   // ~C2 (below typical male low)
        float maxHz             = 1100.0f; // ~C6 (above typical soprano high)
        float yinThreshold      = 0.15f;   // YIN absolute threshold
        float voicedEnergyDb    = -50.0f;  // RMS gate below which frame is unvoiced
        float voicedConfidence  = 0.30f;   // YIN confidence gate
    };

    // Offline analysis of a mono buffer.
    // Returns frame-by-frame F0/voicing/energy.
    //
    //   mono         Pointer to numSamples mono float samples (-1..+1).
    //   numSamples   Length of mono buffer.
    //   sampleRate   Source sample rate.
    //   params       Tunables (see above).
    //
    // Returns: an ApexTuneAnalysis with one frame per hop. Empty on bad input.
    static ApexTuneAnalysis analyzeOffline (const float* mono,
                                            int          numSamples,
                                            double       sampleRate,
                                            const Params& params = {});

private:
    // Single-frame YIN core.
    //   frame        Pointer to frameSize samples.
    //   tauMin/Max   Lag bounds derived from min/max Hz.
    //   threshold    YIN absolute threshold.
    //   outConf      Output: confidence proxy 0..1.
    // Returns: F0 in Hz, or 0.0f if no pitch found.
    static float yinFrameHz (const float* frame,
                             int          frameSize,
                             double       sampleRate,
                             int          tauMin,
                             int          tauMax,
                             float        threshold,
                             float&       outConf);

    // Parabolic interpolation around a 3-point minimum.
    // Returns sub-sample offset (-0.5..+0.5).
    static float parabolicInterp (float yLeft, float yCenter, float yRight) noexcept;
};

}} // namespace apex::vocaltune
