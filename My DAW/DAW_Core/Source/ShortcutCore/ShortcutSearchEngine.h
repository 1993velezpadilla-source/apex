#pragma once
#include "ShortcutModels.h"

namespace DAW {

/**
 * ShortcutSearchEngine — real-time search/filter across shortcut entries.
 *
 * Searches command name, shortcut text, and category simultaneously.
 * Supports partial matching (typing "mute" finds Mute tool, Mute track, etc.).
 */
class ShortcutSearchEngine
{
public:
    struct FilteredResult
    {
        juce::String            category;
        std::vector<ShortcutEntry> entries;
    };

    /**
     * Filter entries by query and group by category.
     * Returns results preserving original category order.
     */
    static std::vector<FilteredResult> filter(const ShortcutProfile& profile,
                                              const juce::String& query)
    {
        auto matched = profile.search(query);

        // Group by category, preserving insertion order
        std::vector<FilteredResult> grouped;
        juce::StringArray seenCats;

        for (auto& e : matched)
        {
            int catIdx = seenCats.indexOf(e.category);
            if (catIdx < 0)
            {
                seenCats.add(e.category);
                grouped.push_back({ e.category, { e } });
            }
            else
            {
                grouped[(size_t)catIdx].entries.push_back(e);
            }
        }
        return grouped;
    }

    /** Quick count of matches (for UI feedback). */
    static int countMatches(const ShortcutProfile& profile, const juce::String& query)
    {
        return (int)profile.search(query).size();
    }
};

} // namespace DAW
