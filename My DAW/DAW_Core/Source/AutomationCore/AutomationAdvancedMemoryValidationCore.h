#pragma once
#include "AutomationLaneCore.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace DAW {

/**
 * Pure ValueTree validation for project-owned A/B snapshots.
 *
 * This lives separately from AutomationMultiParameterSnapshotCore because
 * AutomationManagerCore is a dependency of that core. The project loader
 * must not accept malformed A/B data merely because its envelope has the
 * expected names: validate the entire detached payload before storing it.
 *
 * Message/control thread only; no DSP-thread state or allocations.
 */
struct AutomationAdvancedMemoryValidationCore
{
    static constexpr int maxMemories = 2;
    static constexpr int maxTargets = 64;
    static constexpr int maxPointsPerTarget = 2048;

    static bool isSupportedTarget(const juce::String& id)
    {
        if (id == AutomationLaneCore::trackVolumeParameterId
            || id == AutomationLaneCore::trackPanParameterId)
            return true;

        if (!id.startsWith("plugin."))
            return false;
        const auto rest = id.substring(7);
        const auto slot = rest.upToFirstOccurrenceOf(".", false, false);
        if (slot.isEmpty() || !slot.containsOnly("0123456789"))
            return false;
        const auto afterSlot = rest.fromFirstOccurrenceOf(".", false, false);
        return afterSlot.containsChar('.')
            && afterSlot.upToFirstOccurrenceOf(".", false, false).isNotEmpty()
            && afterSlot.fromFirstOccurrenceOf(".", false, false).isNotEmpty();
    }

    static bool validSnapshot(const juce::ValueTree& snapshot)
    {
        if (!snapshot.hasType("AutomationMultiSnapshot")
            || (int)snapshot.getProperty("version", 0) != 1
            || snapshot.getNumChildren() <= 0
            || snapshot.getNumChildren() > maxTargets)
            return false;

        // Bounded N² exact ID comparison: 64 targets max, 2048 knots/target.
        // No unbounded hidden heap work or successful partial deserialization.
        std::vector<std::pair<juce::String, juce::String>> seen;
        seen.reserve((size_t)snapshot.getNumChildren());

        for (int i = 0; i < snapshot.getNumChildren(); ++i)
        {
            const auto target = snapshot.getChild(i);
            const auto track = target.getProperty("trackId", {}).toString();
            const auto id = target.getProperty("parameterId", {}).toString();
            if (!target.hasType("Target")
                || track.isEmpty() || !isSupportedTarget(id)
                || target.getNumChildren() <= 0
                || target.getNumChildren() > maxPointsPerTarget)
                return false;

            for (const auto& existing : seen)
                if (existing.first == track && existing.second == id)
                    return false;
            seen.emplace_back(track, id);

            int64_t previous = -1;
            for (int p = 0; p < target.getNumChildren(); ++p)
            {
                const auto knot = target.getChild(p);
                if (!knot.hasType("Point")
                    || !knot.hasProperty("timeSamples")
                    || !knot.hasProperty("value")
                    || !knot.hasProperty("curveToNextName")
                    || !knot.hasProperty("tensionToNext"))
                    return false;

                const auto sample =
                    (int64_t)(juce::int64)knot.getProperty("timeSamples");
                const auto value = (float)knot.getProperty("value");
                const auto tension = (float)knot.getProperty("tensionToNext");
                const auto curve = knot.getProperty("curveToNextName").toString();
                if (sample < 0 || sample <= previous
                    || !std::isfinite(value) || !std::isfinite(tension)
                    || tension < -1.0f || tension > 1.0f
                    || value != AutomationLaneCore::clampValueForParameter(id, value)
                    || automationCurveTypeToString(
                        automationCurveTypeFromString(curve)) != curve)
                    return false;
                previous = sample;
            }
        }
        return true;
    }

    static bool validMemories(const juce::ValueTree& memories)
    {
        if (!memories.hasType("AutomationAdvancedMemories")
            || memories.getNumChildren() <= 0
            || memories.getNumChildren() > maxMemories)
            return false;

        bool seenA = false, seenB = false;
        for (int i = 0; i < memories.getNumChildren(); ++i)
        {
            const auto child = memories.getChild(i);
            const auto slot = child.getProperty("slot", "").toString();
            if ((slot != "A" && slot != "B")
                || (slot == "A" ? seenA : seenB)
                || !validSnapshot(child))
                return false;
            if (slot == "A") seenA = true; else seenB = true;
        }
        return true;
    }
};

} // namespace DAW
