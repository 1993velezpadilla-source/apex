#pragma once

// Pure C++17, JUCE-independent schema. Pichy is an OFF-by-default observer;
// audio thread must never read or mutate this registry.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace apex::pichy {

struct PichyParameterCapability
{
    std::string parameterId;    // Plugin-authored stable ID, or display-only param_N
    std::string name;
    std::string label;
    float normalizedValue = 0.0f;
    int numberOfSteps = 0;
    bool automatable = false;
    bool stableId = false;
    bool discrete = false;
    bool booleanValue = false;
};

struct PichyPluginCapability
{
    std::string instanceId;     // Persisted instance UUID; survives insert moves
    std::string displayName;
    std::string formatName;     // APEX Native / VST3 / AU etc, as reported by host
    std::string pluginIdentifier;
    std::size_t slotIndex = 0; // Location is not durable identity
    bool sandboxed = false;
    bool bypassed = false;
    bool processorAccessible = false;
    std::vector<PichyParameterCapability> parameters;
};

struct PichyTrackCapability
{
    std::string trackId;
    std::string displayName;
    bool armed = false;
    bool monitoring = false;
    bool master = false;
    float inputTrimDb = 0.0f;
    std::vector<PichyPluginCapability> plugins;
};

struct PichyAvailablePluginCapability
{
    std::string pluginIdentifier;
    std::string displayName;
    std::string formatName;
    std::string manufacturer;
    std::string version;
    std::int64_t uniqueId = 0;
    bool instrument = false;
};

struct PichyCapabilitySnapshot
{
    std::uint64_t captureId = 0; // Diagnostic capture sequence, NOT a change version
    std::vector<PichyTrackCapability> tracks;
    // Discoverable but not necessarily instantiated plugins. Parameters are
    // intentionally NOT fabricated until the host actually loads a processor.
    std::vector<PichyAvailablePluginCapability> availablePlugins;
};

enum class PichySnapshotStatus : std::uint8_t
{
    Ok,
    InvalidIdentity,
    DuplicateIdentity,
    InvalidParameter,
    LimitsExceeded,
    NotReady
};

/**
 * Read-only capability inventory that does not assume a given plugin exists.
 *
 * Keys: track ID + persistent plugin instance UUID + plugin-authored param ID.
 * Slot index and display label are NEVER authoritative identifiers.
 *
 * A legacy index-only parameter remains visible, but stableId=false means it
 * is NOT eligible for future automatic writes without separate host proof.
 */
class PichyCapabilityRegistryCore final
{
public:
    static constexpr std::size_t kMaxTracks = 512;
    static constexpr std::size_t kMaxAvailablePlugins = 8192;
    static constexpr std::size_t kMaxSlotsPerTrack = 256;
    static constexpr std::size_t kMaxParamsPerPlugin = 4096;
    static constexpr std::size_t kMaxParametersPerSnapshot = 262144;
    static constexpr std::size_t kMaxIdentifierLength = 512;

    static bool validIdentity(const std::string& identity) noexcept
    {
        if (identity.empty() || identity.size() > kMaxIdentifierLength)
            return false;
        for (unsigned char c : identity)
        {
            if (c < 0x20 || c == 0x7f)
                return false;
        }
        return true;
    }

    static PichySnapshotStatus validate(const PichyCapabilitySnapshot& snapshot)
    {
        if (snapshot.tracks.size() > kMaxTracks ||
            snapshot.availablePlugins.size() > kMaxAvailablePlugins)
            return PichySnapshotStatus::LimitsExceeded;

        // Installed inventory: names are presentation only. The complete
        // format+fileOrIdentifier+UID tuple is the lookup key.
        std::unordered_set<std::string> seenAvailable;
        for (const auto& available : snapshot.availablePlugins)
        {
            if (!validIdentity(available.pluginIdentifier) ||
                !validIdentity(available.formatName))
                return PichySnapshotStatus::InvalidIdentity;
            const std::string identity = available.formatName + "\x1f"
                + available.pluginIdentifier + "\x1f"
                + std::to_string(available.uniqueId);
            if (!seenAvailable.insert(identity).second)
                return PichySnapshotStatus::DuplicateIdentity;
        }

        std::unordered_set<std::string> seenTracks;
        std::unordered_set<std::string> seenInstances;
        std::size_t totalParameters = 0;

        for (const auto& track : snapshot.tracks)
        {
            if (!validIdentity(track.trackId))
                return PichySnapshotStatus::InvalidIdentity;
            if (!seenTracks.insert(track.trackId).second)
                return PichySnapshotStatus::DuplicateIdentity;
            if (!std::isfinite(track.inputTrimDb) ||
                track.inputTrimDb < -120.0f || track.inputTrimDb > 24.0f)
                return PichySnapshotStatus::InvalidParameter;
            if (track.plugins.size() > kMaxSlotsPerTrack)
                return PichySnapshotStatus::LimitsExceeded;

            std::unordered_set<std::size_t> seenSlots;
            for (const auto& plugin : track.plugins)
            {
                if (!validIdentity(plugin.instanceId) ||
                    !validIdentity(plugin.pluginIdentifier))
                    return PichySnapshotStatus::InvalidIdentity;
                if (!seenInstances.insert(plugin.instanceId).second ||
                    !seenSlots.insert(plugin.slotIndex).second)
                    return PichySnapshotStatus::DuplicateIdentity;
                if (plugin.slotIndex >= kMaxSlotsPerTrack)
                    return PichySnapshotStatus::LimitsExceeded;
                if (plugin.parameters.size() > kMaxParamsPerPlugin)
                    return PichySnapshotStatus::LimitsExceeded;
                if (plugin.parameters.size() >
                    kMaxParametersPerSnapshot - totalParameters)
                    return PichySnapshotStatus::LimitsExceeded;
                totalParameters += plugin.parameters.size();

                // When sandbox IPC does not expose the processor, report the
                // slot but do not pretend its parameters have been enumerated.
                if (!plugin.processorAccessible && !plugin.parameters.empty())
                    return PichySnapshotStatus::InvalidParameter;

                std::unordered_set<std::string> seenParameterIds;
                for (const auto& param : plugin.parameters)
                {
                    if (!validIdentity(param.parameterId))
                        return PichySnapshotStatus::InvalidIdentity;
                    if (!seenParameterIds.insert(param.parameterId).second)
                        return PichySnapshotStatus::DuplicateIdentity;
                    if (!std::isfinite(param.normalizedValue) ||
                        param.normalizedValue < 0.0f ||
                        param.normalizedValue > 1.0f ||
                        param.numberOfSteps < 0)
                        return PichySnapshotStatus::InvalidParameter;
                }
            }
        }
        return PichySnapshotStatus::Ok;
    }

    static const PichyPluginCapability* findPlugin(
        const PichyCapabilitySnapshot& snapshot,
        const std::string& trackId, const std::string& instanceId) noexcept
    {
        for (const auto& track : snapshot.tracks)
            if (track.trackId == trackId)
                for (const auto& plugin : track.plugins)
                    if (plugin.instanceId == instanceId)
                        return &plugin;
        return nullptr;
    }

    static const PichyParameterCapability* findParameter(
        const PichyCapabilitySnapshot& snapshot,
        const std::string& trackId, const std::string& instanceId,
        const std::string& parameterId) noexcept
    {
        if (const auto* plugin = findPlugin(snapshot, trackId, instanceId))
            for (const auto& param : plugin->parameters)
                if (param.parameterId == parameterId)
                    return &param;
        return nullptr;
    }

    static bool wouldPermitFutureWrite(const PichyParameterCapability& param) noexcept
    {
        // Eligibility ONLY, not authorization. Real writes additionally need
        // an explicitly granted action, current host binding and Undo guard.
        return param.automatable && param.stableId &&
               std::isfinite(param.normalizedValue) &&
               param.normalizedValue >= 0.0f && param.normalizedValue <= 1.0f;
    }
};

} // namespace apex::pichy
