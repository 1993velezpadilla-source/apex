#pragma once
#include <JuceHeader.h>
#include "AutomationClipClipboardCore.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace DAW {

struct AutomationArticulatorToolsCore
{
    static bool copyState(const AutomationClipRegionCore& region, AutomationClipClipboardCore& clipboard)
    {
        return clipboard.copyRegionState(region);
    }

    static bool pasteState(AutomationClipRegionCore& destination, const AutomationClipClipboardCore& clipboard)
    {
        return clipboard.pasteStateToRegion(destination);
    }

    // Continuous ranges must match AutomationLaneCore::clampValueForParameter.
    // Existing normalized plugin params retain [0,1], but native pan, volume,
    // clip gain/pitch/stretch must not be silently reduced to [0,1].
    static std::pair<float, float> valueRange(const juce::String& parameter)
    {
        if (parameter == AutomationLaneCore::trackPanParameterId
            || (parameter.startsWith("clip.") && parameter.endsWith(AutomationLaneCore::clipPanSuffix)))
            return {-1.0f, 1.0f};

        if (parameter == AutomationLaneCore::trackVolumeParameterId)
            return {0.0f, 2.0f};

        if (parameter.startsWith("clip.") && parameter.endsWith(AutomationLaneCore::clipGainSuffix))
            return {0.0f, 4.0f};

        if (parameter.startsWith("clip.") && parameter.endsWith(AutomationLaneCore::clipPitchSuffix))
            return {-36.0f, 36.0f};

        if (parameter.startsWith("clip.") && parameter.endsWith(AutomationLaneCore::clipStretchSuffix))
            return {0.1f, 4.0f};

        // Normalized third-party plugin parameters are [0,1].
        if (parameter.startsWith("plugin.") || parameter.startsWith("instrument.")
            || parameter == AutomationLaneCore::trackTapeStopParameterId
            || (parameter.startsWith("clip.")
                && parameter.endsWith(AutomationLaneCore::clipTapeStopSuffix)))
            return {0.0f, 1.0f};

        // Match AutomationLaneCore's generic host range [0,2]. Otherwise,
        // transformations can silently squash sends and other native lanes.
        return {0.0f, 2.0f};
    }

    static void flipVertically(AutomationClipRegionCore& region)
    {
        const auto [lo, hi] = valueRange(region.parameterId);
        for (auto& point : region.localPoints)
            if (std::isfinite(point.value))
                point.value = AutomationLaneCore::clampValueForParameter(
                    region.parameterId, lo + hi - point.value);
    }

    static void scaleLevels(AutomationClipRegionCore& region, float amount,
                            float centre = std::numeric_limits<float>::quiet_NaN())
    {
        if (!std::isfinite(amount))
            return;

        const auto [lo, hi] = valueRange(region.parameterId);
        if (!std::isfinite(centre))
            centre = (lo + hi) * 0.5f;

        for (auto& point : region.localPoints)
            if (std::isfinite(point.value))
                point.value = AutomationLaneCore::clampValueForParameter(
                    region.parameterId, centre + (point.value - centre) * amount);
    }

    static void normalizeLevels(AutomationClipRegionCore& region)
    {
        if (region.localPoints.empty())
            return;

        for (const auto& point : region.localPoints)
            if (!std::isfinite(point.value))
                return; // no partial edits on bad project data

        auto minMax = std::minmax_element(region.localPoints.begin(), region.localPoints.end(),
            [](const AutomationPoint& a, const AutomationPoint& b) { return a.value < b.value; });

        const float minValue = minMax.first->value;
        const float maxValue = minMax.second->value;
        const float range = maxValue - minValue;
        if (range <= 0.000001f)
            return;

        const auto [lo, hi] = valueRange(region.parameterId);
        for (auto& point : region.localPoints)
            point.value = AutomationLaneCore::clampValueForParameter(
                region.parameterId, lo + (point.value - minValue) * (hi - lo) / range);
    }

    static void resetLevels(AutomationClipRegionCore& region, float defaultValue = 0.0f)
    {
        if (!std::isfinite(defaultValue))
            return;

        defaultValue = AutomationLaneCore::clampValueForParameter(region.parameterId, defaultValue);
        for (auto& point : region.localPoints)
            point.value = defaultValue;
    }
};

} // namespace DAW
