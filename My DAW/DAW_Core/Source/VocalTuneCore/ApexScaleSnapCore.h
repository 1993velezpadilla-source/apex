// =============================================================================
//  ApexScaleSnapCore.h
//  Nearest-note quantization to chromatic or diatonic scales.
//
//  Drop-in: Source/VocalTuneCore/ApexScaleSnapCore.h
//  Depends on: ApexTuneTypes.h, JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  All snap operations are pure functions of (midiIn, scaleRoot, scaleType).
//  No state, no thread restrictions, but typically called from UI thread
//  in response to user actions (double-click note, change scale).
//
//  Supported scaleType strings (case-insensitive):
//      "Chromatic"
//      "Major"
//      "Minor"           -- natural minor
//      "HarmonicMinor"
//      "MelodicMinor"    -- ascending form
//      "MajorPentatonic"
//      "MinorPentatonic"
//      "Blues"
//      "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian"
//
//  scaleRoot accepts "C", "C#", "Db", "D", ... "B" (sharps or flats).
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"

namespace apex { namespace vocaltune {

class ApexScaleSnapCore
{
public:
    // Snap a (possibly fractional) MIDI value to the nearest in-scale pitch.
    // Returns an integer MIDI note as float (e.g., 60.0f).
    static float snapToScale (float midiIn,
                              const juce::String& scaleRoot,
                              const juce::String& scaleType);

    // Apply scale-snap to one note: sets targetMidi to nearest in-scale,
    // recomputes centsOffset = (targetMidi - detectedMidi) * 100.
    static void  snapNote (ApexTuneNote& note,
                           const juce::String& scaleRoot,
                           const juce::String& scaleType);

    // Bulk-apply to all notes in a clip state.
    static void  snapAll  (ApexTuneClipState& clip);

    // Parse "C", "C#", "Db", ... into 0..11. Returns -1 on failure.
    static int   pitchClassFromRoot (const juce::String& root);

    // Return semitone offsets (0..11) for the named scale.
    // Empty for chromatic (handled specially in snapToScale).
    static std::vector<int> degreesForScale (const juce::String& scaleType);
};

}} // namespace apex::vocaltune
