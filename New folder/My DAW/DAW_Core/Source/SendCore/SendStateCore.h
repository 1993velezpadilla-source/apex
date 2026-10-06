#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../RoutingCore/RoutingGraph.h"

namespace DAW {

/**
 * SendStateCore -- canonical 3-state send record layer.
 *
 * Three states per (source, destination, type) pair:
 *   1. DOES NOT EXIST    -- no RoutingConnection record
 *   2. EXISTS + ACTIVE   -- RoutingConnection exists, active == true  -> contributes signal
 *   3. EXISTS + INACTIVE -- RoutingConnection exists, active == false -> silent, graph edge kept
 *
 * RULES:
 *   - toggleActive()  flips active flag only. Never deletes.
 *   - deleteSend()    fully removes the connection from the graph.
 *   - Inactive send stays in the graph so topology / PDC remain stable.
 *   - AudioEngine checks conn->active before accumulating -- zero cost when inactive.
 */
class SendStateCore
{
public:
    enum class State { DoesNotExist, ExistsActive, ExistsInactive };

    State getState(const RoutingGraph& graph,
                   const TrackID& sourceId,
                   const TrackID& destId,
                   ConnectionType type = ConnectionType::Send) const
    {
        auto* conn = findConnection(graph, sourceId, destId, type);
        if (!conn)        return State::DoesNotExist;
        if (conn->active) return State::ExistsActive;
        return State::ExistsInactive;
    }

    bool exists(const RoutingGraph& graph,
                const TrackID& sourceId,
                const TrackID& destId,
                ConnectionType type = ConnectionType::Send) const
    {
        return findConnection(graph, sourceId, destId, type) != nullptr;
    }

    bool isActive(const RoutingGraph& graph,
                  const TrackID& sourceId,
                  const TrackID& destId,
                  ConnectionType type = ConnectionType::Send) const
    {
        auto* c = findConnection(graph, sourceId, destId, type);
        return c && c->active;
    }

    /** Create a send (ACTIVE by default). Idempotent. Returns false on cycle or self-route. */
    bool createSend(RoutingGraph& graph,
                    const TrackID& sourceId,
                    const TrackID& destId,
                    ConnectionType type       = ConnectionType::Send,
                    float          gainLinear = 1.0f)
    {
        if (sourceId == destId || sourceId.isEmpty() || destId.isEmpty())
            return false;

        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(destId);
        if (!srcNode || !destNode) return false;

        if (auto* existing = findConnection(graph, sourceId, destId, type))
        {
            existing->active = true;
            existing->gain   = juce::jlimit(0.0f, 2.0f, gainLinear);
            return true;
        }

        auto* conn = graph.connect(srcNode->id, destNode->id, type, gainLinear);
        if (!conn) return false;
        conn->active = true;
        return true;
    }

    /**
     * Toggle EXISTS+ACTIVE <-> EXISTS+INACTIVE.
     * NEVER deletes. NEVER rebuilds topology. Safe for rapid UI toggling.
     */
    bool toggleActive(RoutingGraph& graph,
                      const TrackID& sourceId,
                      const TrackID& destId,
                      ConnectionType type = ConnectionType::Send)
    {
        auto* conn = findConnection(graph, sourceId, destId, type);
        if (!conn) return false;
        conn->active = !conn->active;
        return true;
    }

    void setActive(RoutingGraph& graph,
                   const TrackID& sourceId,
                   const TrackID& destId,
                   bool           active,
                   ConnectionType type = ConnectionType::Send)
    {
        if (auto* c = findConnection(graph, sourceId, destId, type))
            c->active = active;
    }

    /** Fully remove the send. This is the ONLY path to DoesNotExist. */
    bool deleteSend(RoutingGraph& graph,
                    const TrackID& sourceId,
                    const TrackID& destId,
                    ConnectionType type = ConnectionType::Send)
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(destId);
        if (!srcNode || !destNode) return false;
        return graph.disconnect(srcNode->id, destNode->id, type);
    }

    float getGain(const RoutingGraph& graph,
                  const TrackID& sourceId,
                  const TrackID& destId,
                  ConnectionType type = ConnectionType::Send) const
    {
        auto* c = findConnection(graph, sourceId, destId, type);
        return c ? c->gain : 0.0f;
    }

    void setGain(RoutingGraph& graph,
                 const TrackID& sourceId,
                 const TrackID& destId,
                 float          gainLinear,
                 ConnectionType type = ConnectionType::Send)
    {
        if (auto* c = findConnection(graph, sourceId, destId, type))
            c->gain = juce::jlimit(0.0f, 2.0f, gainLinear);
    }

    std::vector<RoutingConnection*> getSendsFromSource(const RoutingGraph& graph,
                                                        const TrackID& sourceId) const
    {
        std::vector<RoutingConnection*> result;
        auto* srcNode = graph.getNodeByTrackId(sourceId);
        if (!srcNode) return result;
        for (auto* c : graph.getOutputConnections(srcNode->id))
            if (isSendType(c->type)) result.push_back(c);
        return result;
    }

    std::vector<RoutingConnection*> getSendsToDestination(const RoutingGraph& graph,
                                                           const TrackID& destId) const
    {
        std::vector<RoutingConnection*> result;
        auto* destNode = graph.getNodeByTrackId(destId);
        if (!destNode) return result;
        for (auto* c : graph.getInputConnections(destNode->id))
            if (isSendType(c->type)) result.push_back(c);
        return result;
    }

private:
    static bool isSendType(ConnectionType t)
    {
        return t == ConnectionType::Send    ||
               t == ConnectionType::PreSend ||
               t == ConnectionType::Sidechain;
    }

    RoutingConnection* findConnection(const RoutingGraph& graph,
                                      const TrackID& sourceId,
                                      const TrackID& destId,
                                      ConnectionType type) const
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(destId);
        if (!srcNode || !destNode) return nullptr;
        for (auto* c : graph.getOutputConnections(srcNode->id))
            if (c->destNodeId == destNode->id && c->type == type)
                return c;
        return nullptr;
    }
};

} // namespace DAW
