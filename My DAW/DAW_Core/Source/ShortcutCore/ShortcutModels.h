#pragma once
#include <JuceHeader.h>
#include <vector>
#include <set>

namespace DAW {

/**
 * ShortcutEntry — a single command/shortcut pair for display and search.
 * Shortcuts are stored in Windows-style ("Ctrl+Z", "Space", "F12").
 * PlatformStyleManager translates them to Apple style on display.
 */
struct ShortcutEntry
{
    juce::String category;   // "Transport", "Editing", "Views", etc.
    juce::String command;    // "Play / Stop", "Undo", "Duplicate"
    juce::String shortcut;   // Windows-style: "Ctrl+Z", "Space", "F12"
    juce::String context;    // optional: "global", "arrangement", "mixer"
};

/**
 * ShortcutProfile — all entries for one DAW layout.
 * One source of truth: powers both the help window display and the key binding engine.
 */
struct ShortcutProfile
{
    juce::String               profileId;    // "pro_tools", "logic_pro", etc.
    juce::String               displayName;  // "Pro Tools", "Logic Pro", etc.
    std::vector<ShortcutEntry> entries;

    /** Returns all unique category names in insertion order. */
    juce::StringArray getCategories() const
    {
        juce::StringArray cats;
        for (auto& e : entries)
            if (!cats.contains(e.category))
                cats.add(e.category);
        return cats;
    }

    /** Returns entries for the given category only. */
    std::vector<ShortcutEntry> getEntriesForCategory(const juce::String& cat) const
    {
        std::vector<ShortcutEntry> out;
        for (auto& e : entries)
            if (e.category == cat) out.push_back(e);
        return out;
    }

    /** Case-insensitive search across command name, shortcut text, and category. */
    std::vector<ShortcutEntry> search(const juce::String& query) const
    {
        if (query.isEmpty()) return entries;
        auto q = query.toLowerCase();
        std::vector<ShortcutEntry> out;
        for (auto& e : entries)
            if (e.command.toLowerCase().contains(q)  ||
                e.shortcut.toLowerCase().contains(q) ||
                e.category.toLowerCase().contains(q))
                out.push_back(e);
        return out;
    }
};

} // namespace DAW
