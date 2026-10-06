// =============================================================================
//  ApexPitchCurveCore.h
//  Generates a per-sample pitch-ratio curve from notes + raw F0 analysis.
//
//  Drop-in: Source/VocalTuneCore/ApexPitchCurveCore.h
//  Depends on: ApexTuneTypes.h, JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Threading: pure offline. Called from the render worker, never the audio
//             thread. Output curve is then consumed sample-by-sample by
//             SignalSmith Stretch in ApexTuneRenderCore (Phase 4).
//
//  Math model -- the key piece. Per voiced sample inside a note:
//
//      detectedF0(t)  = raw YIN F0 in Hz at time t
//      detectedMidi   = note-level median (constant across the note)
//      targetMidi     = note-level target (constant across the note)
//
//      offsetMidi(t)  = detectedF0(t)_midi - detectedMidi
//                     ^ this is the singer's natural variation INSIDE the note:
//                       drift (slow) + modulation (fast vibrato).
//
//      We split offsetMidi(t) into:
//          driftMidi(t)      = low-pass of offsetMidi(t)   (slow envelope)
//          modulationMidi(t) = offsetMidi(t) - driftMidi(t) (fast residue)
//
//      Preserved variation = drift*driftAmount + modulation*modulationAmount.
//
//      Final target MIDI per sample:
//          tgtMidi(t) = lerp(detectedMidi + preservedOffset(t),
//                            targetMidi   + preservedOffset(t),
//                            correctionAmount)
//
//      Pitch ratio (what SignalSmith consumes):
//          ratio(t) = 2 ^ ((tgtMidi(t) - detectedMidi_at_t) / 12)
//                   = midiToHz(tgtMidi(t)) / detectedF0(t)
//
//  Unvoiced / outside-any-note samples get ratio = 1.0 (pass-through dry).
//
//  Output curve is sampled at a "control rate" (e.g., 1 value per N audio
//  samples, default N = hopSamples from analysis). The renderer linearly
//  interpolates between control points. This keeps memory bounded and
//  matches the rate at which YIN provided data anyway.
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"

namespace apex { namespace vocaltune {

class ApexPitchCurveCore
{
public:
    struct Params
    {
        int   controlHopSamples   = 256;     // matches default YIN hop @ 48kHz
        float driftCutoffHz       = 4.0f;    // < this = drift; > this = modulation
        float minDetectedF0Hz     = 30.0f;   // guard against divide-by-zero
        float maxRatio            = 4.0f;    // clamp ratio for safety
        float minRatio            = 0.25f;
        int   transitionSmoothingPts = 2;    // light adjacent-note crossfade/smoothing
    };

    // Per-sample (well, per-controlHop) pitch ratio and target MIDI.
    struct CurvePoint
    {
        float ratio       = 1.0f;  // multiply source F0 by this
        float targetMidi  = 0.0f;  // for diagnostics / UI overlay
        bool  voiced      = false; // false = pass-through region
    };

    // Build the full curve for a clip.
    //   analysis    YIN frames + sample rate + hop.
    //   notes       Edited notes (with targetMidi etc.).
    //   params      Tunables.
    //
    // Returns vector of CurvePoint, one per controlHop. Length covers the
    // full source duration (analysis.numSourceSamples), padded with
    // pass-through points if needed.
    static std::vector<CurvePoint> buildCurve (const ApexTuneAnalysis& analysis,
                                               const std::vector<ApexTuneNote>& notes,
                                               const Params& params = {});

private:
    // Single-pole IIR low-pass for drift extraction.
    // y[n] = a*y[n-1] + (1-a)*x[n]
    static void lowPassInPlace (std::vector<float>& signal, float cutoffHz, float rateHz);
};

}} // namespace apex::vocaltune
