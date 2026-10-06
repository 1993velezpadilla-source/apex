// =============================================================================
//  ApexScaleSnapCore.cpp
//  Nearest-note quantization. See header for supported scale strings.
//
//  Drop-in: Source/VocalTuneCore/ApexScaleSnapCore.cpp
// =============================================================================

#include "ApexScaleSnapCore.h"
#include <algorithm>

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
int ApexScaleSnapCore::pitchClassFromRoot (const juce::String& root)
{
    if (root.isEmpty()) return -1;

    const juce::String r = root.trim();
    const juce::juce_wchar letter = juce::CharacterFunctions::toUpperCase (r[0]);

    int pc = -1;
    switch (letter)
    {
        case 'C': pc = 0;  break;
        case 'D': pc = 2;  break;
        case 'E': pc = 4;  break;
        case 'F': pc = 5;  break;
        case 'G': pc = 7;  break;
        case 'A': pc = 9;  break;
        case 'B': pc = 11; break;
        default:  return -1;
    }

    if (r.length() >= 2)
    {
        const juce::juce_wchar acc = r[1];
        if      (acc == '#') pc = (pc + 1) % 12;
        else if (acc == 'b') pc = (pc + 11) % 12;
    }
    return pc;
}

// -----------------------------------------------------------------------------
std::vector<int> ApexScaleSnapCore::degreesForScale (const juce::String& scaleType)
{
    const juce::String s = scaleType.toLowerCase();

    if (s == "chromatic")       return {};                                    // special-cased below
    if (s == "major")           return { 0, 2, 4, 5, 7, 9, 11 };
    if (s == "minor")           return { 0, 2, 3, 5, 7, 8, 10 };
    if (s == "harmonicminor")   return { 0, 2, 3, 5, 7, 8, 11 };
    if (s == "melodicminor")    return { 0, 2, 3, 5, 7, 9, 11 };              // ascending
    if (s == "majorpentatonic") return { 0, 2, 4, 7, 9 };
    if (s == "minorpentatonic") return { 0, 3, 5, 7, 10 };
    if (s == "blues")           return { 0, 3, 5, 6, 7, 10 };
    if (s == "dorian")          return { 0, 2, 3, 5, 7, 9, 10 };
    if (s == "phrygian")        return { 0, 1, 3, 5, 7, 8, 10 };
    if (s == "lydian")          return { 0, 2, 4, 6, 7, 9, 11 };
    if (s == "mixolydian")      return { 0, 2, 4, 5, 7, 9, 10 };
    if (s == "locrian")         return { 0, 1, 3, 5, 6, 8, 10 };

    // Unknown scale -> chromatic fallback.
    return {};
}

// -----------------------------------------------------------------------------
float ApexScaleSnapCore::snapToScale (float midiIn,
                                      const juce::String& scaleRoot,
                                      const juce::String& scaleType)
{
    // Chromatic / unknown -> nearest semitone.
    const auto degrees = degreesForScale (scaleType);
    if (degrees.empty()) return (float) std::lround (midiIn);

    const int rootPc = pitchClassFromRoot (scaleRoot);
    if (rootPc < 0)    return (float) std::lround (midiIn);

    // Build candidate MIDI notes across +/- 1 octave around midiIn; pick nearest.
    const int    nearestInt = (int) std::lround (midiIn);
    const int    baseOct    = (nearestInt / 12) - 1;
    float        bestMidi   = (float) nearestInt;
    float        bestDist   = 1.0e9f;

    for (int oct = baseOct; oct <= baseOct + 3; ++oct)
    {
        for (int d : degrees)
        {
            const int candidate = oct * 12 + ((rootPc + d) % 12);
            const float dist    = std::abs ((float) candidate - midiIn);
            if (dist < bestDist) { bestDist = dist; bestMidi = (float) candidate; }
        }
    }
    return bestMidi;
}

// -----------------------------------------------------------------------------
void ApexScaleSnapCore::snapNote (ApexTuneNote& note,
                                  const juce::String& scaleRoot,
                                  const juce::String& scaleType)
{
    note.targetMidi  = snapToScale (note.detectedMidi, scaleRoot, scaleType);
    note.centsOffset = (note.targetMidi - note.detectedMidi) * 100.0f;
}

// -----------------------------------------------------------------------------
void ApexScaleSnapCore::snapAll (ApexTuneClipState& clip)
{
    for (auto& n : clip.notes)
        snapNote (n, clip.scaleRoot, clip.scaleType);
}

}} // namespace apex::vocaltune
