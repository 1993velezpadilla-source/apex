#pragma once
#include <JuceHeader.h>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "RoutingNode.h"         // RoutingNodeType
#include "RoutingConnection.h"   // ConnectionType, TapPoint
#include "../UtilityCore/Types.h" // RouteID

namespace DAW {

/**
 * RoutingSnapshot — immutable, plain-data picture of the routing graph.
 *
 * Published atomically by RoutingSnapshotPublisher on the message thread
 * after every graph or connection-state change.
 * The audio thread reads this via RoutingSnapshotPublisher::get() with zero locks.
 *
 * Rules:
 *  - Snapshot-owned immutable data only. No pointers into RoutingGraph.
 *    Lookup indexes are fully prepared before publication.
 *  - No ConnectionGainRampCore inside EdgeSnapshot.
 *  - Fully copyable and moveable.
 */
struct RoutingSnapshot
{
    struct StringHash
    {
        size_t operator()(const juce::String& value) const noexcept
        {
            return (size_t) value.hashCode64();
        }
    };

    // ── Node metadata ─────────────────────────────────────────────────────
    struct NodeSnapshot
    {
        juce::String    id;
        juce::String    trackId;
        RoutingNodeType type   = RoutingNodeType::Track;
        bool            active = true;
    };

    // ── Edge (connection) metadata ─────────────────────────────────────────
    struct EdgeSnapshot
    {
        RouteID        id;
        juce::String   sourceNodeId;
        juce::String   destNodeId;
        ConnectionType type               = ConnectionType::Direct;
        TapPoint       tapPoint           = TapPoint::PreFX;
        float          gain               = 1.0f;   // target gain at publish time — NOT a live ramp
        bool           active             = true;
        bool           bypassed           = false;
        // Runtime-only transition tombstone. The editable graph no longer owns
        // this route; the renderer keeps it just long enough to ramp to silence.
        bool           retiring            = false;
        juce::String   destinationPluginId;
        int            destinationBusIndex = 1;
    };

    std::vector<juce::String>  processingOrder;   // topo-sorted, producer-first
    std::vector<NodeSnapshot>  nodes;
    std::vector<EdgeSnapshot>  edges;
    std::unordered_map<juce::String, size_t, StringHash> nodeIndexById;
    uint64_t                   version = 0;
};

} // namespace DAW
