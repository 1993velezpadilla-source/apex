#pragma once
#include "AutomationSnapshotCore.h"

namespace DAW {

/**
 * Realtime-safe authorization for the dedicated host VST3 PPQ bridge.
 *
 * The bridge mirrors a specific legacy automation lane into the APEX PPQ
 * store. A cached PPQ snapshot MUST NOT resurrect an envelope after its
 * originating lane has been bypassed, emptied, or removed.
 *
 * This is a bounded, allocation-free lookup on the immutable DSP snapshot.
 * It does not suppress the plugin's independent canonical APEX lane.
 */
struct AutomationVst3BridgeAuthorityCore
{
    static bool allowsPlayback(const AutomationSnapshot* snapshot,
                               const TrackID& trackId,
                               const juce::String& coreParameterId) noexcept
    {
        if (snapshot == nullptr)
            return false; // no authoritative core state; fail closed

        const auto* lane = snapshot->findLaneRT(trackId, coreParameterId);
        return lane != nullptr && lane->enabled && !lane->points.empty();
    }
};

} // namespace DAW
