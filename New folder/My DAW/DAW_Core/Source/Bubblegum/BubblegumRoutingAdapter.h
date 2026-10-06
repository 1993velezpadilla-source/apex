#pragma once
#include <vector>
#include "BubblegumCableTypes.h"

namespace bubblegum
{
    struct BubblegumSendRecord
    {
        TrackId sourceTrackId;
        TrackId destinationTrackId;
        SendId sendId = 0;
        float sendLevel01 = 0.5f;
    };

    class BubblegumRoutingAdapter
    {
    public:
        template <typename RoutingGraphT>
        std::vector<BubblegumSendRecord> getSendRecords(const RoutingGraphT& routingGraph) const
        {
            std::vector<BubblegumSendRecord> out;

            const auto sends = routingGraph.getAllSends();
            out.reserve((size_t)sends.size());

            for (const auto& send : sends)
            {
                BubblegumSendRecord r;
                r.sourceTrackId = send.sourceTrackId;
                r.destinationTrackId = send.destinationTrackId;
                r.sendId = send.sendId;
                out.push_back(r);
            }

            return out;
        }

        template <typename SendStateStoreT>
        SendVisualState getVisualState(const SendStateStoreT& sendState, SendId sendId) const
        {
            if (!sendState.exists(sendId))
                return SendVisualState::DoesNotExist;

            if (sendState.isActive(sendId))
                return SendVisualState::ExistsActive;

            return SendVisualState::ExistsInactive;
        }
    };
}
