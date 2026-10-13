#pragma once

#include <JuceHeader.h>
#include <functional>
#include <utility>
#include "../TrackCore/Track.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "PichyCapabilityRegistryCore.h"

namespace apex::pichy {

/**
 * Adapts APEX's ACTUAL hosted plugin chains to the JUCE-free capability
 * schema. Works for installed native, VST3 and other formats, using host
 * parameter metadata; no per-plugin allowlist, invented ID or UI scraping.
 *
 * Called explicitly on the JUCE MESSAGE THREAD, never by the audio callback.
 * Snapshot contains COPIES, never pointers to tracks/plugins/parameters.
 * A failed scan clears the result, so a future assistant cannot act on an
 * accidentally stale snapshot. Does not instantiate/open/touch a plugin GUI.
 */
class PichyPluginDiscoveryAdapter final
{
public:
    using ChainResolver = std::function<const DAW::PluginChainCore*(
        const DAW::TrackID&)>;

    static PichySnapshotStatus capture(
        const DAW::TrackManager& tracks,
        const ChainResolver& findExistingChain,
        const juce::KnownPluginList* installedCatalog,
        std::uint64_t sequence, PichyCapabilitySnapshot& output)
    {
        output = {};
        auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        if (messageManager == nullptr || !messageManager->isThisTheMessageThread()
            || !findExistingChain)
            return PichySnapshotStatus::NotReady;

        if (tracks.getNumTracks() < 0 ||
            static_cast<std::size_t>(tracks.getNumTracks()) + 1 >
                PichyCapabilityRegistryCore::kMaxTracks)
            return PichySnapshotStatus::LimitsExceeded;

        PichyCapabilitySnapshot fresh;
        fresh.captureId = sequence;
        fresh.tracks.reserve(static_cast<std::size_t>(
            juce::jmax(0, tracks.getNumTracks())) + 1);

        // The host-scanner catalog tells Pichy what can be inserted, whereas
        // existing track chains tell it which controls are really loaded.
        // Never instantiate plugin processors just to read the catalog.
        if (installedCatalog != nullptr)
        {
            const auto& types = installedCatalog->getTypes();
            if (types.size() < 0 || static_cast<std::size_t>(types.size()) >
                PichyCapabilityRegistryCore::kMaxAvailablePlugins)
                return PichySnapshotStatus::LimitsExceeded;
            fresh.availablePlugins.reserve(static_cast<std::size_t>(types.size()));
            for (const auto* desc : types)
            {
                if (desc == nullptr)
                    continue;
                PichyAvailablePluginCapability available;
                available.pluginIdentifier = desc->fileOrIdentifier.toStdString();
                available.displayName = desc->name.toStdString();
                available.formatName = desc->pluginFormatName.toStdString();
                available.manufacturer = desc->manufacturerName.toStdString();
                available.version = desc->version.toStdString();
                available.uniqueId = desc->uniqueId;
                available.instrument = desc->isInstrument;
                fresh.availablePlugins.push_back(std::move(available));
            }
        }

        for (int i = 0; i < tracks.getNumTracks(); ++i)
        {
            if (const auto* track = tracks.getTrack(i))
            {
                const auto status = scanTrack(*track, findExistingChain, fresh);
                if (status != PichySnapshotStatus::Ok)
                    return status;
            }
        }
        if (const auto* master = tracks.getMasterTrack())
        {
            const auto status = scanTrack(*master, findExistingChain, fresh);
            if (status != PichySnapshotStatus::Ok)
                return status;
        }

        const auto validation = PichyCapabilityRegistryCore::validate(fresh);
        if (validation != PichySnapshotStatus::Ok)
            return validation;
        output = std::move(fresh);
        return PichySnapshotStatus::Ok;
    }

private:
    static PichySnapshotStatus scanTrack(
        const DAW::Track& track, const ChainResolver& findExistingChain,
        PichyCapabilitySnapshot& snapshot)
    {
        PichyTrackCapability t;
        t.trackId = track.getID().toStdString();
        t.displayName = track.getName().toStdString();
        t.armed = track.isArmed();
        t.master = track.isMaster();
        t.monitoring = track.getMonitoringState().getMode() != DAW::InputMonitorMode::Off;
        t.inputTrimDb = track.getInputTrim().getTargetGainDb();

        // Crucial: the resolver only FINDS existing chains; it must NOT call
        // ApplicationCore::getPluginChain(), which creates/mutates the session.
        const auto* chain = findExistingChain(track.getID());
        if (chain != nullptr)
        {
            if (chain->getNumSlots() < 0 ||
                static_cast<std::size_t>(chain->getNumSlots()) >
                    PichyCapabilityRegistryCore::kMaxSlotsPerTrack)
                return PichySnapshotStatus::LimitsExceeded;

            for (int slotIndex = 0; slotIndex < chain->getNumSlots(); ++slotIndex)
            {
                const auto* instance = chain->getSlot(slotIndex);
                if (instance == nullptr)
                    continue;

                PichyPluginCapability plugin;
                plugin.instanceId = instance->getPluginInstanceId().toStdString();
                plugin.slotIndex = static_cast<std::size_t>(slotIndex);
                plugin.displayName = instance->getName().toStdString();
                const auto desc = instance->getDescription();
                plugin.formatName = desc.pluginFormatName.toStdString();
                plugin.pluginIdentifier = desc.fileOrIdentifier.toStdString();
                if (plugin.pluginIdentifier.empty())
                    plugin.pluginIdentifier = "unknown-host-identifier";
                plugin.sandboxed = instance->isSandboxed();
                plugin.bypassed = instance->isBypassed();
                const auto* processor = instance->getProcessor();
                plugin.processorAccessible = processor != nullptr;

                // A sandboxed worker may not expose a parent-side processor.
                // Never fabricate its parameters or claim it is controllable.
                if (processor != nullptr)
                {
                    const auto& params = processor->getParameters();
                    if (params.size() < 0 ||
                        static_cast<std::size_t>(params.size()) >
                            PichyCapabilityRegistryCore::kMaxParamsPerPlugin)
                        return PichySnapshotStatus::LimitsExceeded;

                    plugin.parameters.reserve(static_cast<std::size_t>(params.size()));
                    for (int pi = 0; pi < params.size(); ++pi)
                    {
                        const auto* parameter = params[pi];
                        if (parameter == nullptr)
                            continue;

                        PichyParameterCapability p;
                        if (const auto* withId = dynamic_cast<
                            const juce::AudioProcessorParameterWithID*>(parameter))
                        {
                            p.parameterId = withId->paramID.toStdString();
                            p.stableId = !p.parameterId.empty();
                        }
                        if (p.parameterId.empty())
                            p.parameterId = ("param_" + juce::String(pi)).toStdString();
                        p.name = parameter->getName(128).toStdString();
                        p.label = parameter->getLabel().toStdString();
                        p.normalizedValue = parameter->getValue();
                        p.numberOfSteps = parameter->getNumSteps();
                        p.automatable = parameter->isAutomatable();
                        p.discrete = parameter->isDiscrete();
                        p.booleanValue = parameter->isBoolean();
                        plugin.parameters.push_back(std::move(p));
                    }
                }
                t.plugins.push_back(std::move(plugin));
            }
        }

        snapshot.tracks.push_back(std::move(t));
        return PichySnapshotStatus::Ok;
    }
};

} // namespace apex::pichy
