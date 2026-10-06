#pragma once
#include "RoutingGraphCore.h"

namespace DAW {

// =====================================================================
// RoutingGraphProcessorCore
//
// Per-track post-FX signal fanout.  Called once per track per audio
// block after the track's own FX chain and fader have been applied.
//
// Signal flow per track:
//   TrackInput
//     → clip/input gain
//     → insert FX chain
//     → fader          ← this core starts here (postFaderBuffer)
//       → Master      (if masterRouteState == ExistsActive)
//       → AudioSend / AudioPlusSidechain targets  (post-fader by default)
//       → SidechainOnly targets                   (post-fader by default)
//
// PreFaderSend is tapped BEFORE the fader by a separate mechanism;
// this processor only handles post-fader fan-out.
//
// Callback types supplied by the audio engine:
//   AddToMasterFn    (const TrackId&, const AudioBlock&) → void
//   AddToTrackFn     (const TrackId& dest, const TrackId& src,
//                     const AudioBlock&, float gain, float pan) → void
//   PublishSidechainFn (const TrackId& dest, const TrackId& src,
//                       const AudioBlock&) → void
//
// AudioBlock is intentionally a template parameter so this header stays
// independent of any particular buffer type; callers bind their own.
// =====================================================================

template<typename AudioBlock>
class RoutingGraphProcessorCore
{
public:
    using AddToMasterFn     = std::function<void(const TrackId&, const AudioBlock&)>;
    using AddToTrackFn      = std::function<void(const TrackId& dest,
                                                 const TrackId& src,
                                                 const AudioBlock&,
                                                 float gain, float pan)>;
    using PublishSidechainFn= std::function<void(const TrackId& dest,
                                                 const TrackId& src,
                                                 const AudioBlock&)>;

    RoutingGraphProcessorCore(const MasterRouteStateCore& masterState,
                              const RoutingGraphCore&     graph)
        : master_(masterState), graph_(graph) {}

    void setCallbacks(AddToMasterFn      onMaster,
                      AddToTrackFn       onSend,
                      PublishSidechainFn onSidechain)
    {
        onMaster_    = std::move(onMaster);
        onSend_      = std::move(onSend);
        onSidechain_ = std::move(onSidechain);
    }

    /**
     * Fan out a track's post-fader buffer to all active destinations.
     * Call this once per track after applying the track fader.
     */
    void processTrackOutput(const TrackId& trackId, const AudioBlock& postFaderBuffer)
    {
        // Master route
        if (master_.isMasterRouteActive(trackId) && onMaster_)
            onMaster_(trackId, postFaderBuffer);

        // Send / sidechain edges
        for (const auto& edgeId : graph_.outgoingEdgesOf(trackId))
        {
            const RouteEdge* edge = graph_.getEdge(edgeId);
            if (!edge || edge->state != RouteState::ExistsActive)
                continue;

            switch (edge->type)
            {
                case RouteType::AudioSend:
                    if (onSend_)
                        onSend_(edge->destTrackId, trackId,
                                postFaderBuffer, edge->gain, edge->pan);
                    break;

                case RouteType::AudioPlusSidechain:
                    if (onSend_)
                        onSend_(edge->destTrackId, trackId,
                                postFaderBuffer, edge->gain, edge->pan);
                    if (onSidechain_)
                        onSidechain_(edge->destTrackId, trackId, postFaderBuffer);
                    break;

                case RouteType::SidechainOnly:
                    if (onSidechain_)
                        onSidechain_(edge->destTrackId, trackId, postFaderBuffer);
                    break;

                case RouteType::PreFaderSend:
                    // Handled upstream at the tap point; skip here.
                    break;
            }
        }
    }

private:
    const MasterRouteStateCore& master_;
    const RoutingGraphCore&     graph_;

    AddToMasterFn      onMaster_;
    AddToTrackFn       onSend_;
    PublishSidechainFn onSidechain_;
};

} // namespace DAW
