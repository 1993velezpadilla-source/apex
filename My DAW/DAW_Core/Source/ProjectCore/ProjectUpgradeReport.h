#pragma once
#include <JuceHeader.h>
#include <vector>

namespace DAW
{
/**
 * ProjectUpgradeReport — collects everything the load-time repair chain
 * changed in a project, so the "Upgrade Project…" tool can present a report.
 *
 * The ProjectManager resets it on every load; the migration steps record
 * into it.
 *
 * FUTURE UPDATES — the contract: whenever a change ships that affects the
 * project format (a new field, a reworked routing model, a moved setting,
 * anything), do TWO things:
 *   1. Add an idempotent repair step to the load-time chain (safe to run on
 *      every load — it heals projects saved by any build, including
 *      intermediate broken ones).
 *   2. Record it here: report->add("What was repaired", count, details).
 * Old projects then heal automatically on load AND through the explicit
 * Upgrade Project tool, with no per-feature work beyond those two steps.
 */
struct ProjectUpgradeReport
{
    struct Entry
    {
        juce::String name;
        int count = 0;
        juce::String details;
    };

    int sourceVersion = 0;
    int targetVersion = 0;
    std::vector<Entry> entries;
    juce::StringArray warnings;

    void reset(int source, int target)
    {
        sourceVersion = source;
        targetVersion = target;
        entries.clear();
        warnings.clear();
    }

    void add(const juce::String& name, int count, const juce::String& details = {})
    {
        if (count <= 0)
            return;

        entries.push_back({ name, count, details });
    }

    void warn(const juce::String& text) { warnings.addIfNotAlreadyThere(text); }

    bool hasChanges() const noexcept { return ! entries.empty(); }

    int totalChanges() const noexcept
    {
        int total = 0;
        for (const auto& e : entries)
            total += e.count;

        return total;
    }

    juce::String toText() const
    {
        juce::String text;
        text << "Project format version: " << sourceVersion
             << " -> " << targetVersion << "\n";

        if (entries.empty())
        {
            text << "\nNo repairs were needed - this project already uses the "
                    "current format.\n";
        }
        else
        {
            text << "\nRepairs applied:\n";
            for (const auto& e : entries)
                text << "  - " << e.name << ": " << e.count
                     << (e.details.isNotEmpty() ? ("  (" + e.details + ")")
                                                : juce::String())
                     << "\n";
        }

        if (! warnings.isEmpty())
        {
            text << "\nNotes:\n";
            for (const auto& w : warnings)
                text << "  - " << w << "\n";
        }

        return text;
    }
};

} // namespace DAW
