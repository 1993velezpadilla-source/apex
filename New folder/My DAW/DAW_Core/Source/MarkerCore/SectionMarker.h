#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

using MarkerID = juce::String;

/**
 * SectionMarker — a named musical section on the timeline.
 *
 * Markers can be point markers (length == 0) or section regions (length > 0).
 * Each marker carries a human-readable name, an optional section type for
 * semantic understanding, an ordinal for multi-instance sections (Verse 1,
 * Verse 2), and optional aliases for multilingual / shorthand lookup.
 */
struct SectionMarker
{
    MarkerID          id;
    juce::String      name;          // user-visible label ("Verse", "Drop", etc.)
    juce::String      sectionType;   // canonical type: "verse", "chorus", "bridge", "custom"
    SamplePosition    position = 0;  // start position in samples
    SamplePosition    length   = 0;  // 0 = point marker, >0 = section region
    juce::Colour      color;
    int               ordinal  = 1;  // 1st, 2nd, 3rd occurrence of this type

    // Multilingual aliases for fuzzy/AI lookup
    juce::StringArray aliases;

    // Serialization
    juce::ValueTree getState() const
    {
        juce::ValueTree v("Marker");
        v.setProperty("id",          id,                                nullptr);
        v.setProperty("name",        name,                              nullptr);
        v.setProperty("sectionType", sectionType,                       nullptr);
        v.setProperty("position",    (juce::int64)position,             nullptr);
        v.setProperty("length",      (juce::int64)length,               nullptr);
        v.setProperty("color",       color.toString(),                  nullptr);
        v.setProperty("ordinal",     ordinal,                           nullptr);
        v.setProperty("aliases",     aliases.joinIntoString("|"),       nullptr);
        return v;
    }

    void restoreState(const juce::ValueTree& v)
    {
        id          = v.getProperty("id",          "").toString();
        name        = v.getProperty("name",        "").toString();
        sectionType = v.getProperty("sectionType", "custom").toString();
        position    = (SamplePosition)(juce::int64)v.getProperty("position", 0);
        length      = (SamplePosition)(juce::int64)v.getProperty("length",   0);
        color       = juce::Colour::fromString(v.getProperty("color", "ff7c3aed").toString());
        ordinal     = (int)v.getProperty("ordinal", 1);

        juce::String aliasStr = v.getProperty("aliases", "").toString();
        aliases.clear();
        if (aliasStr.isNotEmpty())
            aliases.addTokens(aliasStr, "|", "");
    }
};

/**
 * Default section type names — used for smart marker creation.
 */
namespace SectionTypes
{
    static const juce::String Intro      = "intro";
    static const juce::String Verse      = "verse";
    static const juce::String PreChorus  = "pre-chorus";
    static const juce::String Chorus     = "chorus";
    static const juce::String Hook       = "hook";
    static const juce::String Bridge     = "bridge";
    static const juce::String Breakdown  = "breakdown";
    static const juce::String Drop       = "drop";
    static const juce::String Solo       = "solo";
    static const juce::String Adlib      = "adlib";
    static const juce::String Outro      = "outro";
    static const juce::String Custom     = "custom";
}

} // namespace DAW
