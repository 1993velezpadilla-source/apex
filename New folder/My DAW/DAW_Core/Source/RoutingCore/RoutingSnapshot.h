#pragma once
#include <JuceHeader.h>
#include <vector>
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
 *  - Plain data only.  No pointers into RoutingGraph.  No RoutingConnection*.
 *  - No ConnectionGainRampCore inside EdgeSnapshot.
 *  - Fully copyable and moveable.
 */
struct RoutingSnapshot
{
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
        juce::String   destinationPluginId;
        int            destinationBusIndex = 1;
    };

    std::vector<juce::String>  processingOrder;   // topo-sorted, producer-first
    std::vector<NodeSnapshot>  nodes;
    std::vector<EdgeSnapshot>  edges;
    uint64_t                   version = 0;
};

} // namespace DAW
