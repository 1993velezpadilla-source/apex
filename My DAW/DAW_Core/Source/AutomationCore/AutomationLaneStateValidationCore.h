#pragma once
#include "AutomationLaneCore.h"
#include <cmath>
#include <cstdint>

namespace DAW {

/**
 * Constant-extra-memory validation of Undo/Redo ValueTree payloads.
 *
 * Undo history can be replayed after session edits. Validate stored lanes
 * before restoreState() allocates points or silently clamps invalid values.
 * Both grouped edits and 64-target snapshot/macro transactions share this
 * gate, preventing a corrupt lane from partially updating the other targets.
 * Control/message thread only; never use ValueTree on the audio callback.
 */
struct AutomationLaneStateValidationCore
{
    static bool valid(const juce::ValueTree& lane,
                      const juce::String& expectedParameterId,
                      size_t maxPoints)
    {
        if (expectedParameterId.isEmpty()
            || !lane.hasType("AutomationLane")
            || lane.getProperty("parameterId", {}).toString()
                    != expectedParameterId
            || !(bool)lane.getProperty("enabled", false)
            || lane.getNumChildren() < 0
            || (size_t)lane.getNumChildren() > maxPoints)
            return false;

        int64_t lastSample = -1;
        for (int i = 0; i < lane.getNumChildren(); ++i)
        {
            const auto point = lane.getChild(i);
            if (!point.hasType("Point")
                || !point.hasProperty("timeSamples")
                || !point.hasProperty("value")
                || !point.hasProperty("curveToNextName")
                || !point.hasProperty("tensionToNext"))
                return false;

            const int64_t sample =
                (int64_t)(juce::int64)point.getProperty("timeSamples");
            const float value = (float)point.getProperty("value");
            const float tension = (float)point.getProperty("tensionToNext");
            const auto name = point.getProperty("curveToNextName").toString();
            if (sample < 0 || sample <= lastSample
                || !std::isfinite(value) || !std::isfinite(tension)
                || tension < -1.0f || tension > 1.0f
                || value != AutomationLaneCore::clampValueForParameter(
                    expectedParameterId, value))
                return false;
            if (juce::String(automationCurveTypeToString(
                    automationCurveTypeFromString(name))) != name)
                return false;

            lastSample = sample;
        }

        return true;
    }
};

} // namespace DAW
