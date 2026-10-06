#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../RoutingCore/RoutingGraph.h"

namespace DAW {

/**
 * BubblegumSendStateCore — manages send ON/OFF via the real RoutingGraph.
 *
 * Tap toggles a ConnectionType::Send between source and target nodes.
 * No self-send. No drag routing. Uses the routing engine directly.
 */
class BubblegumSendStateCore
{
public:
    bool hasSend(const RoutingGraph& graph,
                 const TrackID& sourceId,
                 const TrackID& targetId) const
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return false;

        auto conns = graph.getOutputConnections(srcNode->id);
        for (auto* c : conns)
            if (c->destNodeId == destNode->id &&
                c->type == ConnectionType::Send)
                return true;
        return false;
    }

    /**
     * Dot-tap action: create send if absent (ACTIVE), or toggle active/inactive if present.
     * NEVER deletes the send. Use deleteSend() for trash-button deletion.
     */
    void toggleSend(RoutingGraph& graph,
                    const TrackID& sourceId,
                    const TrackID& targetId,
                    float defaultLevel = 1.0f)
    {
        if (sourceId == targetId || sourceId.isEmpty() || targetId.isEmpty())
            return;

        if (sendExists(graph, sourceId, targetId))
            toggleSendActive(graph, sourceId, targetId);
        else
            createSend(graph, sourceId, targetId, defaultLevel);
    }

    void enableSend(RoutingGraph& graph,
                    const TrackID& sourceId,
                    const TrackID& targetId,
                    float level = 1.0f)
    {
        if (sourceId == targetId || sourceId.isEmpty() || targetId.isEmpty())
            return;
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return;
        graph.connect(srcNode->id, destNode->id, ConnectionType::Send, level);
    }

    void disableSend(RoutingGraph& graph,
                     const TrackID& sourceId,
                     const TrackID& targetId)
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return;
        graph.disconnect(srcNode->id, destNode->id, ConnectionType::Send);
    }

    /** Fully remove a send connection from the graph (delete, not toggle). */
    void deleteSend(RoutingGraph& graph,
                    const TrackID& sourceId,
                    const TrackID& targetId)
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return;
        DBG("DELETE send: source=" << sourceId << " target=" << targetId);
        graph.disconnect(srcNode->id, destNode->id, ConnectionType::Send);
        // Verify full removal
        bool stillExists = sendExists(graph, sourceId, targetId);
        DBG("POST-DELETE hasSend? source=" << sourceId << " target=" << targetId << " -> " << (int)stillExists);
    }

    /** Toggle active flag only — does NOT create or remove the connection. */
    void toggleSendActive(RoutingGraph& graph,
                          const TrackID& sourceId,
                          const TrackID& targetId)
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return;
        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->destNodeId == destNode->id && c->type == ConnectionType::Send)
            {
                const bool before = c->active.load(std::memory_order_relaxed);
                c->active.store(!before, std::memory_order_relaxed);
                graph.publishSnapshotOnly();
                DBG("TOGGLE send: source=" << sourceId << " target=" << targetId << " active=" << (int)!before);
                return;
            }
        }
    }

    /** Create a send connection if it does not exist. If inactive, reactivate. */
    void createSend(RoutingGraph& graph,
                    const TrackID& sourceId,
                    const TrackID& targetId,
                    float defaultLevel = 1.0f)
    {
        if (sourceId == targetId || sourceId.isEmpty() || targetId.isEmpty())
            return;
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return;

        // Check if it already exists
        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->destNodeId == destNode->id && c->type == ConnectionType::Send)
            {
                // Exists but may be inactive — reactivate
                if (!c->active.load(std::memory_order_relaxed))
                {
                    c->active.store(true, std::memory_order_relaxed);
                    graph.publishSnapshotOnly();
                    DBG("CREATE send (reactivated): source=" << sourceId << " target=" << targetId);
                }
                else
                {
                    DBG("CREATE send (already exists): source=" << sourceId << " target=" << targetId);
                }
                return;
            }
        }
        // Does not exist — create new
        DBG("CREATE send: source=" << sourceId << " target=" << targetId);
        graph.connect(srcNode->id, destNode->id, ConnectionType::Send, defaultLevel);
    }

    /** Check if send connection exists (active or inactive). */
    bool sendExists(const RoutingGraph& graph,
                    const TrackID& sourceId,
                    const TrackID& targetId) const
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return false;
        for (auto* c : graph.getOutputConnections(srcNode->id))
            if (c->destNodeId == destNode->id && c->type == ConnectionType::Send)
                return true;
        return false;
    }

    /** Check if the send connection is active (not just existing). */
    bool isSendActive(const RoutingGraph& graph,
                      const TrackID& sourceId,
                      const TrackID& targetId) const
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return false;
        for (auto* c : graph.getOutputConnections(srcNode->id))
            if (c->destNodeId == destNode->id && c->type == ConnectionType::Send)
                return c->active.load(std::memory_order_relaxed);
        return false;
    }

    /** Toggle the active flag on the send connection (keep connection, just mute/unmute). */
    void setSendActive(RoutingGraph& graph,
                       const TrackID& sourceId,
                       const TrackID& targetId,
                       bool active)
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return;
        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->destNodeId == destNode->id && c->type == ConnectionType::Send)
            {
                c->active.store(active, std::memory_order_relaxed);
                graph.publishSnapshotOnly();
                return;
            }
        }
    }
};

} // namespace DAW
