#pragma once
#include <JuceHeader.h>
#include "AutomationClipClipboardCore.h"
#include <algorithm>

namespace DAW {

struct AutomationArticulatorToolsCore
{
    static void copyState(const AutomationClipRegionCore& region, AutomationClipClipboardCore& clipboard)
    {
        clipboard.copyRegionState(region);
    }

    static bool pasteState(AutomationClipRegionCore& destination, const AutomationClipClipboardCore& clipboard)
    {
        return clipboard.pasteStateToRegion(destination);
    }

    static void flipVertically(AutomationClipRegionCore& region)
    {
        for (auto& point : region.localPoints)
            point.value = juce::jlimit(0.0f, 1.0f, 1.0f - point.value);
    }

    static void scaleLevels(AutomationClipRegionCore& region, float amount, float centre = 0.5f)
    {
        for (auto& point : region.localPoints)
            point.value = juce::jlimit(0.0f, 1.0f, centre + (point.value - centre) * amount);
    }

    static void normalizeLevels(AutomationClipRegionCore& region)
    {
        if (region.localPoints.empty())
            return;

        auto minMax = std::minmax_element(region.localPoints.begin(), region.localPoints.end(),
            [](const AutomationPoint& a, const AutomationPoint& b) { return a.value < b.value; });

        const float minValue = minMax.first->value;
        const float maxValue = minMax.second->value;
        const float range = maxValue - minValue;
        if (range <= 0.000001f)
            return;

        for (auto& point : region.localPoints)
            point.value = juce::jlimit(0.0f, 1.0f, (point.value - minValue) / range);
    }

    static void resetLevels(AutomationClipRegionCore& region, float defaultValue = 0.0f)
    {
        defaultValue = juce::jlimit(0.0f, 1.0f, defaultValue);
        for (auto& point : region.localPoints)
            point.value = defaultValue;
    }
};

} // namespace DAW
