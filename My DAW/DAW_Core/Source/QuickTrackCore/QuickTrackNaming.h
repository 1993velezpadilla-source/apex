// ===========================================================================
// QuickTrackNaming.h
// APEX Quick Track Builder — display-name numbering authority.
//
// Rules (UX naming only — NEVER identity):
//   * first track of a role: unsuffixed base  ("Coro / Hook")
//   * subsequent: base + " N"                ("Coro / Hook 2", ...)
//   * the next ordinal is the first unused candidate ≥ 2, derived from the
//     ACTUAL existing track names — never from track counts or counters.
//     This is robust across save/reload and hand-typed unusual names.
//   * "Untitled" uses the canonical generic naming ("Audio N").
//
// These names are display/serialization labels only. TrackID and
// RoutingNodeID are generated separately by canonical APEX allocators.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <vector>

namespace DAW {
namespace QuickTrackNaming {

/** Normalize a role/name key for color-family identity:
 *  lowercase, trim, collapse whitespace, strip a trailing ordinal suffix
 *  ("Coro / Hook 2" → "coro / hook") so numbered copies share one family.
 *  Conservative: does NOT merge unrelated user names. */
inline juce::String normalizeFamilyKey(const juce::String& name)
{
    juce::String s = name.trim();
    s = s.toLowerCase();
    // Collapse runs of whitespace into a single space.
    juce::String collapsed;
    bool lastWasSpace = false;
    for (auto c : s)
    {
        if (juce::CharacterFunctions::isWhitespace(c))
        {
            if (!lastWasSpace && !collapsed.isEmpty())
                collapsed += ' ';
            lastWasSpace = true;
        }
        else
        {
            collapsed += c;
            lastWasSpace = false;
        }
    }
    s = collapsed.trim();

    // Strip a trailing " N" (N >= 2) ordinal: "coro / hook 2" → "coro / hook".
    static const juce::StringRef suffixPattern(" ");
    const int lastSpace = s.lastIndexOf(suffixPattern);
    if (lastSpace > 0)
    {
        const auto tail = s.substring(lastSpace + 1);
        if (tail.isNotEmpty() && tail.containsOnly("0123456789"))
        {
            const int ordinal = tail.getIntValue();
            if (ordinal >= 2)
                return s.substring(0, lastSpace).trimEnd();
        }
    }
    return s;
}

/** Compute the next unused display name for a name base, given the set of
 *  existing track names. Candidates: base, base + " 2", base + " 3", ...
 *  Deterministic and robust: never reuses a visible name. */
inline juce::String nextNameFor(const juce::String& nameBase,
                                const std::vector<juce::String>& existingNames)
{
    const juce::String base = nameBase.trim();
    if (base.isEmpty())
        return {};

    for (int n = 1; n <= 100000; ++n)
    {
        const juce::String candidate = (n == 1) ? base : (base + " " + juce::String(n));
        bool used = false;
        for (const auto& existing : existingNames)
        {
            if (existing == candidate)
            {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
    }
    // Unreachable in practice; defensive fallback keeps the builder usable.
    return base + " " + juce::String(existingNames.size() + 1);
}

/** Canonical generic name for the Untitled role: "Audio 1", "Audio 2", ...
 *  (the same generic language AddTrackCommand uses), skipping used names. */
inline juce::String genericUntitledName(const std::vector<juce::String>& existingNames)
{
    for (int n = 1; n <= 100000; ++n)
    {
        const juce::String candidate = "Audio " + juce::String(n);
        bool used = false;
        for (const auto& existing : existingNames)
            if (existing == candidate)
            {
                used = true;
                break;
            }
        if (!used)
            return candidate;
    }
    return "Audio " + juce::String(existingNames.size() + 1);
}

} // namespace QuickTrackNaming
} // namespace DAW
