// =============================================================================
//  ApexNoteSegmentationCore.h
//  Groups voiced YIN frames into ApexTuneNote blocks for editing.
//
//  Drop-in: Source/VocalTuneCore/ApexNoteSegmentationCore.h
//  Depends on: ApexTuneTypes.h, JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  Threading: pure offline. Call from the same worker thread that ran
//             ApexPitchDetectionCore::analyzeOffline. No allocations on
//             the audio thread.
//
//  Algorithm (V1):
//   1. Smooth per-frame F0 with a short median filter to kill octave errors.
//   2. Quantize each frame to nearest semitone (just for grouping, not output).
//   3. Walk frames left-to-right with hysteresis:
//        - voiced -> voiced + same semitone   = extend current note
//        - voiced -> voiced + different semi  = close current, open new
//        - voiced -> unvoiced                 = candidate close (with grace frames)
//        - unvoiced -> voiced                 = open new note
//   4. Drop notes shorter than minDurationMs (likely noise/transients).
//   5. For each surviving note compute detectedMidi from a weighted median
//      of frame MIDI values (weighted by YIN confidence).
//   6. Flag sibilantProtected on unvoiced regions with high energy
//      (handled in ApexSibilantDetectorCore later -- here we just leave
//      the flag false; segmenter only emits voiced notes).
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"

namespace apex { namespace vocaltune {

class ApexNoteSegmentationCore
{
public:
    struct Params
    {
        int   medianFilterFrames = 5;     // odd; smooths octave jumps
        int   graceFramesUnvoiced = 3;    // brief unvoiced dips don't close a note
        float minDurationMs      = 60.0f; // notes shorter than this get dropped
        float semitoneTolerance  = 0.6f;  // jump > this (semis) splits the note
    };

    // Segments an analysis into notes.
    //   analysis   YIN output from ApexPitchDetectionCore::analyzeOffline.
    //   params     Tunables (see above).
    //
    // Returns: notes vector populated with detectedMidi/targetMidi/start/end.
    //          targetMidi is initialized = detectedMidi (no correction yet).
    //          noteId is assigned as "n_<index>".
    static std::vector<ApexTuneNote> segment (const ApexTuneAnalysis& analysis,
                                              const Params& params = {});

private:
    // Median of an arbitrary float window (copies + nth_element).
    static float medianOf (std::vector<float>& scratch);

    // Weighted median: sort by value, pick value where cumulative weight crosses 50%.
    static float weightedMedianMidi (const std::vector<float>& midis,
                                     const std::vector<float>& weights);
};

}} // namespace apex::vocaltune
