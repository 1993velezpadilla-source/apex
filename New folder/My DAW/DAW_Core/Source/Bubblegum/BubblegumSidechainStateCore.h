#pragma once
#include <JuceHeader.h>
#include "../RoutingCore/RoutingGraph.h"
#include "../RoutingCore/RoutingConnection.h"
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * BubblegumSidechainStateCore — manages sidechain edges in the RoutingGraph.
 *
 * Mirrors BubblegumSendStateCore but operates on ConnectionType::Sidechain.
 * Each sidechain connection also carries plugin-bus destination metadata
 * (destinationPluginId, destinationBusIndex, tapPoint).
 *
 * State semantics are identical to send state:
 *   - does not exist          → tap creates it (active)
 *   - exists + active         → tap disables it (inactive)
 *   - exists + inactive       → tap re-enables it (active)
 *   - delete (trash button)   → removes the connection entirely
 *
 * No zombie state. No new rules. Same mental model as audio sends.
 */
class BubblegumSidechainStateCore
{
public:
    // ── Queries ──────────────────────────────────────────────────────────────

    bool sidechainExists(const RoutingGraph& graph,
                         const TrackID& sourceId,
                         const TrackID& destId) const
    {
        return findConnection(graph, sourceId, destId) != nullptr;
    }

    bool isSidechainActive(const RoutingGraph& graph,
                            const TrackID& sourceId,
                            const TrackID& destId) const
    {
        auto* c = findConnection(graph, sourceId, destId);
        return c && c->active.load(std::memory_order_relaxed);
    }

    /** Returns nullptr if no sidechain connection exists. */
    RoutingConnection* getConnection(const RoutingGraph& graph,
                                     const TrackID& sourceId,
                                     const TrackID& destId) const
    {
        return findConnection(graph, sourceId, destId);
    }

    // ── Mutations ────────────────────────────────────────────────────────────

    /**
     * Dot-tap action.
     * Creates the sidechain if absent; toggles active/inactive if present.
     * destPluginId / destBusIndex / tapPoint are used only on creation.
     */
    void toggleSidechain(RoutingGraph& graph,
                          const TrackID& sourceId,
                          const TrackID& destId,
                          const juce::String& destPluginId = {},
                          int destBusIndex = 1,
                          TapPoint tap = TapPoint::PreFX)
    {
        if (sourceId == destId || sourceId.isEmpty() || destId.isEmpty())
            return;

        if (sidechainExists(graph, sourceId, destId))
        {
            toggleActive(graph, sourceId, destId);
        }
        else
        {
            createSidechain(graph, sourceId, destId, destPluginId, destBusIndex, tap);
        }
    }

    /**
     * Create a sidechain connection. If it already exists (inactive), reactivates it.
     * Returns the connection, or nullptr if nodes don't exist.
     */
    RoutingConnection* createSidechain(RoutingGraph& graph,
                                        const TrackID& sourceId,
                                        const TrackID& destId,
                                        const juce::String& destPluginId = {},
                                        int destBusIndex = 1,
                                        TapPoint tap = TapPoint::PreFX)
    {
        if (sourceId == destId || sourceId.isEmpty() || destId.isEmpty())
            return nullptr;

        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(destId);
        if (!srcNode || !destNode) return nullptr;

        // Reactivate existing inactive connection
        auto* existing = findConnection(graph, sourceId, destId);
        if (existing)
        {
            existing->active.store(true, std::memory_order_relaxed);
            DBG("[SC] createSidechain reactivated: " << sourceId << " -> " << destId);
            return existing;
        }

        auto* conn = graph.connectSidechain(srcNode->id, destNode->id,
                                            destPluginId, destBusIndex, tap);
        if (conn)
            DBG("[SC] createSidechain: " << sourceId << " -> " << destId
                << " plugin=" << destPluginId << " bus=" << destBusIndex);
        return conn;
    }

    /** Toggle active flag without removing the connection. */
    void toggleActive(RoutingGraph& graph,
                       const TrackID& sourceId,
                       const TrackID& destId)
    {
        auto* c = findConnection(graph, sourceId, destId);
        if (!c) return;
        const bool newActive = !c->active.load(std::memory_order_relaxed);
        c->active.store(newActive, std::memory_order_relaxed);
        DBG("[SC] toggleActive: " << sourceId << " -> " << destId
            << " active=" << (int)newActive);
    }

    void setActive(RoutingGraph& graph,
                    const TrackID& sourceId,
                    const TrackID& destId,
                    bool active)
    {
        auto* c = findConnection(graph, sourceId, destId);
        if (c) c->active.store(active, std::memory_order_relaxed);
    }

    /** Fully remove the sidechain connection (trash button). */
    void deleteSidechain(RoutingGraph& graph,
                          const TrackID& sourceId,
                          const TrackID& destId)
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(destId);
        if (!srcNode || !destNode) return;
        graph.disconnect(srcNode->id, destNode->id, ConnectionType::Sidechain);
        DBG("[SC] deleteSidechain: " << sourceId << " -> " << destId);
    }

    /** Update the tap-point on an existing sidechain connection. */
    void setTapPoint(RoutingGraph& graph,
                      const TrackID& sourceId,
                      const TrackID& destId,
                      TapPoint tap)
    {
        auto* c = findConnection(graph, sourceId, destId);
        if (c) c->tapPoint = tap;
    }

    /** Update destination plugin / bus on an existing sidechain connection. */
    void setDestination(RoutingGraph& graph,
                         const TrackID& sourceId,
                         const TrackID& destId,
                         const juce::String& pluginId,
                         int busIndex)
    {
        auto* c = findConnection(graph, sourceId, destId);
        if (!c) return;
        c->destinationPluginId = pluginId;
        c->destinationBusIndex = busIndex;
    }

private:
    RoutingConnection* findConnection(const RoutingGraph& graph,
                                       const TrackID& sourceId,
                                       const TrackID& destId) const
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(destId);
        if (!srcNode || !destNode) return nullptr;

        for (auto* c : graph.getOutputConnections(srcNode->id))
            if (c->destNodeId == destNode->id && c->type == ConnectionType::Sidechain)
                return c;
        return nullptr;
    }
};

} // namespace DAW
