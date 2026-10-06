#pragma once
#include <JuceHeader.h>
#include "AutomationManagerCore.h"
#include "AutomationClipRegionCore.h"
#include <functional>

namespace DAW {

struct AutomationQuickCreateCore
{
    struct ControlTarget
    {
        TrackID trackId;
        juce::String parameterId;
        juce::String displayName;
        juce::String pluginInstanceId;
        juce::String pluginDisplayName;
        int pluginSlotIndex = -1;
        float defaultValue = 0.0f;
        int64_t regionStartSample = 0;
        int64_t regionLengthSamples = 0;
    };

    struct Callbacks
    {
        std::function<void(const ControlTarget&)> createAutomation;
        std::function<void(const ControlTarget&)> showAutomationLane;
        std::function<void(const ControlTarget&)> hideAutomationLane;
        std::function<void(const ControlTarget&)> clearAutomation;
    };

    static void createNativeLane(AutomationManagerCore& manager, const ControlTarget& target)
    {
        auto& lane = manager.getOrCreateLane(target.trackId, target.parameterId);
        lane.setDefaultValue(target.defaultValue);
        lane.setVisible(true);
        if (lane.points.empty())
            lane.addPoint(target.regionStartSample, target.defaultValue);
        manager.publishSnapshot();
    }

    static ControlTarget makeTrackVolumeTarget(const TrackID& trackId, float defaultValue = 1.0f)
    {
        ControlTarget target;
        target.trackId = trackId;
        target.parameterId = AutomationLaneCore::trackVolumeParameterId;
        target.displayName = "Track Volume";
        target.defaultValue = defaultValue;
        return target;
    }

    static ControlTarget makeTrackPanTarget(const TrackID& trackId, float defaultValue = 0.0f)
    {
        ControlTarget target;
        target.trackId = trackId;
        target.parameterId = AutomationLaneCore::trackPanParameterId;
        target.displayName = "Track Pan";
        target.defaultValue = defaultValue;
        return target;
    }

    static ControlTarget makeTrackTapeStopTarget(const TrackID& trackId, float defaultValue = 0.0f)
    {
        ControlTarget target;
        target.trackId = trackId;
        target.parameterId = AutomationLaneCore::trackTapeStopParameterId;
        target.displayName = "Tape Stop";
        target.defaultValue = defaultValue;
        return target;
    }

    static ControlTarget makeClipPitchTarget(const TrackID& trackId, const juce::String& clipId, float defaultValue = 0.0f)
    {
        ControlTarget target;
        target.trackId = trackId;
        target.parameterId = AutomationLaneCore::makeClipPitchParameterId(clipId);
        target.displayName = "Clip Pitch";
        target.defaultValue = defaultValue;
        return target;
    }

    static ControlTarget makePluginWetDryTarget(const TrackID& trackId, int pluginSlotIndex, const juce::String& pluginName, float defaultValue = 1.0f)
    {
        ControlTarget target;
        target.trackId = trackId;
        target.pluginSlotIndex = pluginSlotIndex;
        target.pluginDisplayName = pluginName;
        target.parameterId = AutomationManagerCore::makePluginSlotMixId(pluginSlotIndex);
        target.displayName = pluginName.isNotEmpty() ? pluginName + " / Wet" : "Plugin Wet";
        target.defaultValue = defaultValue;
        return target;
    }

    static ControlTarget makePluginParameterTarget(const AutomationManagerCore::PluginParameterTarget& pluginTarget,
                                                   const juce::String& pluginName,
                                                   const juce::String& parameterName,
                                                   float defaultValue = 0.0f)
    {
        ControlTarget target;
        target.trackId = pluginTarget.trackId;
        target.pluginInstanceId = pluginTarget.pluginInstanceId;
        target.pluginSlotIndex = pluginTarget.pluginSlotIndex;
        target.pluginDisplayName = pluginName;
        target.parameterId = AutomationManagerCore::makePluginParameterId(pluginTarget);
        target.displayName = pluginName.isNotEmpty() ? pluginName + " / " + parameterName : parameterName;
        target.defaultValue = defaultValue;
        return target;
    }

    static void createLaneAndOptionalRegion(AutomationManagerCore& manager, const ControlTarget& target, bool createRegion)
    {
        createNativeLane(manager, target);
        if (createRegion && target.regionLengthSamples > 0)
            manager.addClipRegion(createRegionForTarget(target));
    }

    static AutomationClipRegionCore createRegionForTarget(const ControlTarget& target)
    {
        AutomationClipRegionCore region;
        region.trackId = target.trackId;
        region.parameterId = target.parameterId;
        region.startSample = target.regionStartSample;
        region.lengthSamples = juce::jmax<int64_t>(1, target.regionLengthSamples);
        region.name = target.displayName.isNotEmpty() ? target.displayName : target.parameterId;
        region.localPoints.push_back({ 0, juce::jlimit(0.0f, 1.0f, target.defaultValue), AutomationCurveType::Linear, 0.0f });
        region.localPoints.push_back({ region.lengthSamples, juce::jlimit(0.0f, 1.0f, target.defaultValue), AutomationCurveType::Linear, 0.0f });
        return region;
    }

    static void showControlMenu(juce::Point<int> screenPos, ControlTarget target, Callbacks callbacks)
    {
        juce::PopupMenu menu;
        menu.addItem(kCreateAutomation, "Create Automation", callbacks.createAutomation != nullptr);
        menu.addItem(kShowAutomationLane, "Show Automation Lane", callbacks.showAutomationLane != nullptr);
        menu.addItem(kHideAutomationLane, "Hide Automation Lane", callbacks.hideAutomationLane != nullptr);
        menu.addItem(kClearAutomation, "Clear Automation", callbacks.clearAutomation != nullptr);
        menu.addSeparator();
        menu.addItem(kMidiLearnTodo, "MIDI Learn later TODO", false);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
            [target, callbacks](int result)
            {
                if (result == kCreateAutomation && callbacks.createAutomation) callbacks.createAutomation(target);
                else if (result == kShowAutomationLane && callbacks.showAutomationLane) callbacks.showAutomationLane(target);
                else if (result == kHideAutomationLane && callbacks.hideAutomationLane) callbacks.hideAutomationLane(target);
                else if (result == kClearAutomation && callbacks.clearAutomation) callbacks.clearAutomation(target);
            });
    }

    static juce::PopupMenu buildClipAutomationMenu(int baseId)
    {
        juce::PopupMenu automation;
        automation.addItem(baseId + 0, "Automate Volume");
        automation.addItem(baseId + 1, "Automate Pan");
        automation.addItem(baseId + 2, "Automate Pitch");
        automation.addItem(baseId + 3, "Automate Tape Stop");
        automation.addSeparator();
        automation.addItem(baseId + 20, "Project Tempo Automation - TODO", false);
        return automation;
    }

private:
    enum IDs
    {
        kCreateAutomation = 12000,
        kShowAutomationLane,
        kHideAutomationLane,
        kClearAutomation,
        kMidiLearnTodo
    };
};

} // namespace DAW
