#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../RoutingCore/RoutingGraph.h"

namespace DAW {

/**
 * BubblegumSendLevelCore — manages per-target send level via RoutingConnection::gain.
 *
 * Knob drag (vertical: up = increase, down = decrease) adjusts level.
 * Level range: 0.0 .. 2.0 (unity = 1.0).
 */
class BubblegumSendLevelCore
{
public:
    float getLevel(const RoutingGraph& graph,
                   const TrackID& sourceId,
                   const TrackID& targetId) const
    {
        auto* conn = findSendConnection(graph, sourceId, targetId);
        return conn ? conn->gain.load(std::memory_order_relaxed) : 0.0f;
    }

    void setLevel(RoutingGraph& graph,
                  const TrackID& sourceId,
                  const TrackID& targetId,
                  float level)
    {
        auto* conn = findSendConnection(graph, sourceId, targetId);
        if (conn)
        {
            conn->gain.store(juce::jlimit(0.0f, 2.0f, level), std::memory_order_relaxed);
            graph.publishSnapshotOnly();
        }
    }

    void adjustLevel(RoutingGraph& graph,
                     const TrackID& sourceId,
                     const TrackID& targetId,
                     float delta)
    {
        auto* conn = findSendConnection(graph, sourceId, targetId);
        if (conn)
        {
            conn->gain.store(juce::jlimit(0.0f, 2.0f, conn->gain.load(std::memory_order_relaxed) + delta), std::memory_order_relaxed);
            graph.publishSnapshotOnly();
        }
    }

private:
    RoutingConnection* findSendConnection(const RoutingGraph& graph,
                                          const TrackID& sourceId,
                                          const TrackID& targetId) const
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return nullptr;

        auto conns = graph.getOutputConnections(srcNode->id);
        for (auto* c : conns)
            if (c->destNodeId == destNode->id && c->type == ConnectionType::Send)
                return c;
        return nullptr;
    }
};

} // namespace DAW
