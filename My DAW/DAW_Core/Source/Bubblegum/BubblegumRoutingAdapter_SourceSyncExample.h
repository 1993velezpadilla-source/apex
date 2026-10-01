#pragma once
#include <vector>
#include "BubblegumCableTypes.h"
#include "BubblegumRoutingAdapter.h"
#include "../RoutingCore/RoutingGraph.h"

namespace bubblegum
{
    class BubblegumRoutingAdapterSourceSyncExample
    {
    public:
        template <typename RoutingGraphT, typename SourceSyncT>
        std::vector<BubblegumSendRecord> getSendRecords(
            const RoutingGraphT& routingGraph,
            const SourceSyncT& sourceSync,
            TrackId selectedTrack) const
        {
            std::vector<BubblegumSendRecord> out;

            TrackId resolvedSource = sourceSync.getSourceTrackId();
            if (resolvedSource.isEmpty())
                resolvedSource = selectedTrack;

            if (resolvedSource.isEmpty())
                return out;

            auto* sourceNode = routingGraph.getNodeByTrackId(resolvedSource);
            if (sourceNode == nullptr)
                return out;

            const auto sends = routingGraph.getOutputConnections(sourceNode->id);
            out.reserve((size_t)sends.size());

            for (const auto& send : sends)
            {
                if (send == nullptr) continue;

                // Include Send connections and Direct→master connections.
                // Direct connections to non-master nodes are internal bus routing
                // and should not appear as user-facing cables.
                const bool isSend   = (send->type == DAW::ConnectionType::Send);
                const bool isDirect = (send->type == DAW::ConnectionType::Direct);
                if (!isSend && !isDirect) continue;

                auto* destinationNode = routingGraph.getNode(send->destNodeId);
                if (destinationNode == nullptr || destinationNode->trackId.isEmpty())
                    continue;

                // Only include Direct connections that go to the master output.
                if (isDirect && destinationNode->type != DAW::RoutingNodeType::Master)
                    continue;

                BubblegumSendRecord r;
                r.sourceTrackId = resolvedSource;
                r.destinationTrackId = destinationNode->trackId;
                r.sendId = makeSendId(send->id);
                // Report the real connection gain for BOTH Send and Direct→master
                // routes. Master-route level edits (Bubblegum panel knob, cable
                // anchor drag) write RoutingConnection::gain, so the on-cable
                // readout must read it back — a hardcoded 1.0f froze the master
                // label at "100%" forever.
                r.sendLevel01 = juce::jlimit(0.0f, 1.0f, send->gain.load(std::memory_order_relaxed) * 0.5f);
                out.push_back(r);
            }

            return out;
        }

    private:
        static SendId makeSendId(const juce::String& routeId) noexcept
        {
            return static_cast<SendId>(routeId.hashCode64());
        }
    };
}
