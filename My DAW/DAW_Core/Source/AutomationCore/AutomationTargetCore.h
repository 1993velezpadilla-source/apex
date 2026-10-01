#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * AutomationTargetCore — identifies what parameter an automation lane controls.
 *
 * Every automation lane must have a stable target that survives save/load.
 * Targets are identified by:
 *   - Track/bus/master ID
 *   - Parameter type (volume, pan, mute, plugin param)
 *   - Plugin index + parameter index (for plugin params)
 *   - Stable parameter ID string
 */
struct AutomationTargetCore
{
    juce::String ownerNodeId;       // track/bus/master ID
    juce::String parameterType;     // "volume", "pan", "mute", "plugin"
    int pluginSlotIndex = -1;       // -1 = not a plugin parameter
    int pluginParameterIndex = -1;  // -1 = not a plugin parameter
    juce::String stableParameterId; // for plugin params: stable ID from plugin

    bool isValid() const
    {
        return ownerNodeId.isNotEmpty() && parameterType.isNotEmpty();
    }

    bool isPluginParameter() const
    {
        return pluginSlotIndex >= 0 && pluginParameterIndex >= 0;
    }

    juce::ValueTree getState() const
    {
        juce::ValueTree v("AutomationTarget");
        v.setProperty("ownerNodeId", ownerNodeId, nullptr);
        v.setProperty("parameterType", parameterType, nullptr);
        v.setProperty("pluginSlotIndex", pluginSlotIndex, nullptr);
        v.setProperty("pluginParameterIndex", pluginParameterIndex, nullptr);
        v.setProperty("stableParameterId", stableParameterId, nullptr);
        return v;
    }

    void restoreState(const juce::ValueTree& v)
    {
        ownerNodeId = v.getProperty("ownerNodeId", "").toString();
        parameterType = v.getProperty("parameterType", "").toString();
        pluginSlotIndex = (int)v.getProperty("pluginSlotIndex", -1);
        pluginParameterIndex = (int)v.getProperty("pluginParameterIndex", -1);
        stableParameterId = v.getProperty("stableParameterId", "").toString();
    }
};

} // namespace DAW
