#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../RoutingCore/RoutingGraph.h"

namespace DAW {

/**
 * BubblegumSendFeedbackCore — real-time visual feedback for active sends.
 *
 * When a track is selected, all tracks receiving audio from that source
 * must visually respond (static glow + optional soft pulse).
 *
 * Activation: Bubblegum active AND track selected.
 * Removal on source change: instant, no flicker.
 */
class BubblegumSendFeedbackCore
{
public:
    struct FeedbackTarget
    {
        TrackID trackId;
        float   sendLevel = 0.0f;
    };

    std::vector<FeedbackTarget> getActiveSendTargets(
        const RoutingGraph& graph,
        const TrackID& sourceId) const
    {
        std::vector<FeedbackTarget> result;
        auto* srcNode = graph.getNodeByTrackId(sourceId);
        if (!srcNode) return result;

        auto conns = graph.getOutputConnections(srcNode->id);
        for (auto* c : conns)
        {
            if (c->type != ConnectionType::Send || !c->active) continue;
            auto* destNode = graph.getNode(c->destNodeId);
            if (destNode && destNode->trackId.isNotEmpty())
                result.push_back({ destNode->trackId, c->gain });
        }
        return result;
    }

    bool isReceiving(const RoutingGraph& graph,
                     const TrackID& sourceId,
                     const TrackID& targetId) const
    {
        auto* srcNode  = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return false;

        auto conns = graph.getOutputConnections(srcNode->id);
        for (auto* c : conns)
            if (c->destNodeId == destNode->id &&
                c->type == ConnectionType::Send && c->active)
                return true;
        return false;
    }
};

} // namespace DAW
