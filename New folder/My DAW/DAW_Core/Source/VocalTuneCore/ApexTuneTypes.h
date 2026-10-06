// =============================================================================
//  ApexTuneTypes.h
//  APEX Native Vocal Note Editor -- foundational data structures.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneTypes.h
//  Depends on: JUCE.
//  Touches:    nothing else in the APEX codebase. Pure types + helpers.
// =============================================================================

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <cstdint>

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
// Per-frame F0 analysis result. One ApexTuneFrame per analysis hop.
// -----------------------------------------------------------------------------
struct ApexTuneFrame
{
    float f0Hz       = 0.0f;     // Detected fundamental in Hz; 0 if unvoiced.
    float confidence = 0.0f;     // YIN confidence proxy, 0..1.
    bool  voiced     = false;    // True if frame has reliable pitch.
    float energyDb   = -100.0f;  // Frame RMS in dBFS (for segmentation/sibilants).
};

// -----------------------------------------------------------------------------
// Full analysis result for an entire clip. Stable cache target.
// -----------------------------------------------------------------------------
struct ApexTuneAnalysis
{
    std::vector<ApexTuneFrame> frames;
    int    frameSize       = 2048;
    int    hopSamples      = 256;
    double sampleRate      = 48000.0;
    int    numSourceSamples = 0;
};

// -----------------------------------------------------------------------------
// One detected/edited note. Matches the spec one-for-one.
// -----------------------------------------------------------------------------
struct ApexTuneNote
{
    juce::String noteId;

    int64_t startSample = 0;
    int64_t endSample   = 0;

    float detectedMidi  = 60.0f;
    float targetMidi    = 60.0f;
    float centsOffset   = 0.0f;

    float correctionAmount = 1.0f;   // 0=no correction, 1=full snap to targetMidi
    float driftAmount      = 1.0f;   // preserve slow pitch drift (1=keep all)
    float modulationAmount = 1.0f;   // preserve vibrato (1=keep all)
    float formantShift     = 0.0f;   // semitones; 0 = neutral
    float gainDb           = 0.0f;

    bool voiced            = true;
    bool sibilantProtected = false;
};

// -----------------------------------------------------------------------------
// Per-clip state. This is what gets serialized to the APEX project.
// -----------------------------------------------------------------------------
struct ApexTuneClipState
{
    juce::String clipId;
    juce::File   sourceFile;

    std::vector<ApexTuneNote> notes;

    juce::String scaleRoot = "C";
    juce::String scaleType = "Chromatic";

    bool analyzed         = false;
    int  analysisVersion  = 0;  // bumped only on re-analyze (invalidates analysis cache)
    int  renderVersion    = 0;  // bumped on any note edit (invalidates render cache)
    bool bypassed         = false; // when true, dry source is used for playback/export
};

// -----------------------------------------------------------------------------
// Pitch utility helpers. Constexpr-friendly where possible.
// -----------------------------------------------------------------------------
inline float hzToMidi (float hz) noexcept
{
    return (hz > 0.0f) ? (69.0f + 12.0f * std::log2 (hz / 440.0f)) : 0.0f;
}

inline float midiToHz (float midi) noexcept
{
    return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
}

inline float centsBetween (float hzA, float hzB) noexcept
{
    if (hzA <= 0.0f || hzB <= 0.0f) return 0.0f;
    return 1200.0f * std::log2 (hzA / hzB);
}

}} // namespace apex::vocaltune
