#pragma once
#include "MasterRouteStateCore.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>

namespace DAW {

// =====================================================================
// RoutingGraphCore
//
// Owns all non-master send edges between tracks.  The Master route is
// a separate concern handled by MasterRouteStateCore.
//
// RouteType semantics (matches FL Studio documented behavior):
//
//   AudioSend            — audible post-fader send, no sidechain path
//   AudioPlusSidechain   — audible send + exposes sidechain input on dest
//                          (FL default when "Route selected to this track")
//   SidechainOnly        — no audible mix contribution; sidechain signal only
//   PreFaderSend         — audio tapped before fader; requires tapFxSlot
//
// Rules:
//   • "Route to this track"       → AudioPlusSidechain, master unchanged
//   • "Route to this track only"  → AudioPlusSidechain, master = Inactive
//   • "Sidechain to this track"   → SidechainOnly, master unchanged
//   • "Sidechain only"            → SidechainOnly, master = Inactive
//   • Self-routes, source==Master, feedback loops → rejected
// =====================================================================

static const TrackId kMasterTrackId = "__MASTER__";

enum class RouteType : uint8_t
{
    AudioSend            = 0,
    AudioPlusSidechain   = 1,
    SidechainOnly        = 2,
    PreFaderSend         = 3
};

struct RouteEdge
{
    juce::String edgeId;
    TrackId      sourceTrackId;
    TrackId      destTrackId;
    RouteType    type      = RouteType::AudioPlusSidechain;
    RouteState   state     = RouteState::ExistsActive;

    float gain       = 1.0f;
    float pan        = 0.0f;
    bool  postFader  = true;
    int   tapFxSlot  = -1;   // PreFaderSend only: -1=before all FX
};

class RoutingGraphCore
{
public:
    explicit RoutingGraphCore(MasterRouteStateCore& masterState)
        : master_(masterState) {}

    // ── Edge creation ────────────────────────────────────────────────

    /**
     * Creates an AudioPlusSidechain send from src → dst.
     * Master route of src is left untouched.
     * Returns the new edgeId, or empty string if rejected.
     */
    juce::String routeToTrack(const TrackId& src, const TrackId& dst)
    {
        if (!canCreateRoute(src, dst, RouteType::AudioPlusSidechain))
            return {};
        return addEdge(src, dst, RouteType::AudioPlusSidechain, RouteState::ExistsActive);
    }

    /**
     * Creates an AudioPlusSidechain send from src → dst AND disables
     * src's master route (FL "Route to this track only" behavior).
     */
    juce::String routeToTrackOnly(const TrackId& src, const TrackId& dst)
    {
        if (!canCreateRoute(src, dst, RouteType::AudioPlusSidechain))
            return {};
        master_.setMasterRouteState(src, RouteState::ExistsInactive);
        return addEdge(src, dst, RouteType::AudioPlusSidechain, RouteState::ExistsActive);
    }

    /**
     * Creates a SidechainOnly link from src → dst.
     * Master route of src is left untouched.
     */
    juce::String sidechainToTrack(const TrackId& src, const TrackId& dst)
    {
        if (!canCreateRoute(src, dst, RouteType::SidechainOnly))
            return {};
        return addEdge(src, dst, RouteType::SidechainOnly, RouteState::ExistsActive);
    }

    /**
     * Creates a SidechainOnly link AND disables src's master route.
     */
    juce::String sidechainToTrackOnly(const TrackId& src, const TrackId& dst)
    {
        if (!canCreateRoute(src, dst, RouteType::SidechainOnly))
            return {};
        master_.setMasterRouteState(src, RouteState::ExistsInactive);
        return addEdge(src, dst, RouteType::SidechainOnly, RouteState::ExistsActive);
    }

    /**
     * Creates a PreFaderSend tapped at tapFxSlot from src → dst.
     */
    juce::String preFaderSendToTrack(const TrackId& src, const TrackId& dst, int tapFxSlot)
    {
        if (!canCreateRoute(src, dst, RouteType::PreFaderSend))
            return {};
        auto id = addEdge(src, dst, RouteType::PreFaderSend, RouteState::ExistsActive);
        if (!id.isEmpty())
            edges_[id].tapFxSlot = tapFxSlot;
        return id;
    }

    // ── Edge mutation ────────────────────────────────────────────────

    /** Toggle ExistsActive ↔ ExistsInactive (dot click). */
    void toggleEdge(const juce::String& edgeId)
    {
        auto it = edges_.find(edgeId);
        if (it == edges_.end()) return;
        it->second.state = (it->second.state == RouteState::ExistsActive)
                           ? RouteState::ExistsInactive
                           : RouteState::ExistsActive;
    }

    /** Remove edge entirely (trash action). */
    void deleteEdge(const juce::String& edgeId)
    {
        auto it = edges_.find(edgeId);
        if (it == edges_.end()) return;

        const TrackId& src = it->second.sourceTrackId;
        auto& outgoing = outgoingIndex_[src];
        outgoing.erase(std::remove(outgoing.begin(), outgoing.end(), edgeId), outgoing.end());

        edges_.erase(it);
    }

    /** Set send gain for a given edge (0.0–1.0). */
    void setSendGain(const juce::String& edgeId, float gain)
    {
        auto it = edges_.find(edgeId);
        if (it != edges_.end())
            it->second.gain = juce::jlimit(0.f, 1.f, gain);
    }

    // ── Query ────────────────────────────────────────────────────────

    const RouteEdge* getEdge(const juce::String& edgeId) const
    {
        auto it = edges_.find(edgeId);
        return (it != edges_.end()) ? &it->second : nullptr;
    }

    /** All outgoing edge ids from a source track (including inactive). */
    const std::vector<juce::String>& outgoingEdgesOf(const TrackId& src) const
    {
        static const std::vector<juce::String> kEmpty;
        auto it = outgoingIndex_.find(src);
        return (it != outgoingIndex_.end()) ? it->second : kEmpty;
    }

    /** All edges (for iteration by cable renderer or audio processor). */
    const std::unordered_map<juce::String, RouteEdge>& allEdges() const noexcept
    {
        return edges_;
    }

    // ── Validation ───────────────────────────────────────────────────

    bool canCreateRoute(const TrackId& src, const TrackId& dst, RouteType type) const
    {
        if (src == dst)                           return false;
        if (src == kMasterTrackId)                return false;
        if (dst == kMasterTrackId)                return false; // use MasterRouteStateCore
        if (type == RouteType::PreFaderSend
            && src == kMasterTrackId)             return false;
        if (wouldCreateCycle(src, dst))           return false;
        return true;
    }

    // ── Cleanup ──────────────────────────────────────────────────────

    void unregisterTrack(const TrackId& trackId)
    {
        // Remove all edges where this track is source
        auto& out = outgoingIndex_[trackId];
        for (const auto& id : out)
            edges_.erase(id);
        outgoingIndex_.erase(trackId);

        // Remove all edges where this track is destination
        std::vector<juce::String> toRemove;
        for (auto& [id, edge] : edges_)
            if (edge.destTrackId == trackId)
                toRemove.push_back(id);
        for (auto& id : toRemove)
            deleteEdge(id);
    }

private:
    MasterRouteStateCore&                                master_;
    std::unordered_map<juce::String, RouteEdge>          edges_;
    std::unordered_map<TrackId, std::vector<juce::String>> outgoingIndex_;

    juce::String addEdge(const TrackId& src, const TrackId& dst,
                         RouteType type, RouteState state)
    {
        RouteEdge edge;
        edge.edgeId        = juce::Uuid().toDashedString();
        edge.sourceTrackId = src;
        edge.destTrackId   = dst;
        edge.type          = type;
        edge.state         = state;

        const juce::String id = edge.edgeId;
        outgoingIndex_[src].push_back(id);
        edges_[id] = std::move(edge);
        return id;
    }

    /** DFS cycle check: would adding src→dst create a cycle? */
    bool wouldCreateCycle(const TrackId& src, const TrackId& dst) const
    {
        // If dst can reach src via existing edges, adding src→dst creates a cycle.
        std::vector<TrackId> stack { dst };
        std::vector<TrackId> visited;

        while (!stack.empty())
        {
            TrackId node = stack.back();
            stack.pop_back();

            if (node == src)
                return true;

            if (std::find(visited.begin(), visited.end(), node) != visited.end())
                continue;
            visited.push_back(node);

            auto it = outgoingIndex_.find(node);
            if (it == outgoingIndex_.end()) continue;
            for (const auto& edgeId : it->second)
            {
                auto eit = edges_.find(edgeId);
                if (eit != edges_.end())
                    stack.push_back(eit->second.destTrackId);
            }
        }
        return false;
    }
};

} // namespace DAW
