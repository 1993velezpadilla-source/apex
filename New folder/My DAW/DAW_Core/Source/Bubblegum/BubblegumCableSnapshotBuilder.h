#pragma once
#include <vector>
#include <cmath>
#include <type_traits>
#include <JuceHeader.h>
#include "BubblegumCableTypes.h"
#include "BubblegumRoutingAdapter.h"

namespace bubblegum
{
    class BubblegumCableSnapshotBuilder
    {
    public:
        template <typename SendStateStoreT, typename AnchorResolverT>
        std::vector<CableWorldSnapshot> build(
            const std::vector<BubblegumSendRecord>& sendRecords,
            const SendStateStoreT& sendState,
            const AnchorResolverT& anchorResolver) const
        {
            return build<SendStateStoreT, AnchorResolverT, void>(
                sendRecords,
                sendState,
                anchorResolver,
                nullptr);
        }

        template <typename SendStateStoreT, typename AnchorResolverT, typename MeterBridgeT>
        std::vector<CableWorldSnapshot> build(
            const std::vector<BubblegumSendRecord>& sendRecords,
            const SendStateStoreT& sendState,
            const AnchorResolverT& anchorResolver,
            const MeterBridgeT* meterBridge = nullptr) const
        {
            std::vector<CableWorldSnapshot> out;
            out.reserve(sendRecords.size());

            for (const auto& send : sendRecords)
            {
                CableWorldSnapshot s;
                s.key.sourceTrack = send.sourceTrackId;
                s.key.destinationTrack = send.destinationTrackId;
                s.key.sendId = send.sendId;
                s.sendLevel01 = clamp01(send.sendLevel01);

                if (!sendState.exists(send.sendId))
                    s.state = SendVisualState::DoesNotExist;
                else if (sendState.isActive(send.sendId))
                    s.state = SendVisualState::ExistsActive;
                else
                    s.state = SendVisualState::ExistsInactive;

                const auto src = anchorResolver.getBubblegumSendAnchor(send.sourceTrackId);
                const auto dst = anchorResolver.getBubblegumReceiveAnchor(send.destinationTrackId);

                s.endpoints.source = src;
                s.endpoints.destination = dst;
                s.visible = isValidPoint(src) && isValidPoint(dst);

                if constexpr (!std::is_void_v<MeterBridgeT>)
                {
                    if (meterBridge != nullptr && meterBridge->hasSendMeter(send.sendId))
                        s.audioEnergy01 = clamp01(meterBridge->getSendEnergy01(send.sendId));
                    else if (meterBridge != nullptr)
                        s.audioEnergy01 = clamp01(meterBridge->getTrackEnergy01(send.sourceTrackId));
                    else
                        s.audioEnergy01 = 0.0f;
                }
                else
                {
                    s.audioEnergy01 = 0.0f;
                }

                out.push_back(s);
            }

            return out;
        }

    private:
        static bool isValidPoint(const juce::Point<float>& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && p.x > -9999.0f && p.y > -9999.0f;
        }

        static float clamp01(float v) noexcept
        {
            return juce::jlimit(0.0f, 1.0f, v);
        }
    };
}
