#pragma once
#include "AutomationMacroMappingCore.h"
#include "LastTouchedPluginParameterCore.h"
#include <JuceHeader.h>

namespace DAW {

/**
 * Control-thread-only evidence for a VST3 macro destination.
 *
 * The producer must touch a REAL parameter on a REAL hosted processor.
 * The caller checks that the processor is a VST3 AudioPluginInstance
 * and supplies its current exact track, slot and instance UUID.
 * Never infer parameter identity from display names or a saved bool.
 */
struct AutomationMacroVst3TargetCore
{
    static bool verify(const LastTouchedPluginParameter& touched,
                       const TrackID& liveTrack,
                       int liveSlot,
                       const juce::String& liveInstanceId,
                       const juce::Array<juce::AudioProcessorParameter*>& liveParameters,
                       const AutomationManagerCore& manager,
                       AutomationMacroMappingCore::Binding& result)
    {
        result = {};
        if (!touched.isValid() || touched.pluginInstanceId.isEmpty()
            || touched.trackId != liveTrack
            || touched.pluginSlotIndex != liveSlot
            || touched.pluginInstanceId != liveInstanceId
            || liveInstanceId.isEmpty())
            return false;

        const auto coreId = "plugin." + juce::String(liveSlot)
                          + "." + liveInstanceId + "." + touched.parameterId;
        if (!AutomationMultiParameterSnapshotCore::supported(coreId)
            || manager.hasClipRegionsForParameter(liveTrack, coreId))
            return false;

        const auto* lane = manager.findLane(liveTrack, coreId);
        if (lane == nullptr || !lane->isEnabled())
            return false; // no implicit lane creation / no phantom targets

        int matches = 0;
        for (auto* parameter : liveParameters)
        {
            if (parameter == nullptr)
                continue;
            juce::String stableId;
            if (auto* named = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
                stableId = named->paramID;
            if (stableId.isEmpty())
                stableId = "param_" + juce::String(parameter->getParameterIndex());

            if (stableId != touched.parameterId)
                continue;

            ++matches;
            if (matches > 1 || !parameter->isAutomatable()
                || parameter->isDiscrete() || parameter->isBoolean()
                // JUCE can report an explicitly quantised VST3 range even
                // when isDiscrete() is false. Never interpolate such a lane.
                || parameter->getNumSteps()
                       != juce::AudioProcessorParameter::getDefaultNumParameterSteps())
                return false;
        }
        if (matches != 1)
            return false;

        result.target = {liveTrack, coreId};
        result.minimum = 0.0f;
        result.maximum = 1.0f;
        result.inverted = false;
        result.pluginIsVerifiedContinuous = true; // current instance only
        return true;
    }
};

} // namespace DAW
