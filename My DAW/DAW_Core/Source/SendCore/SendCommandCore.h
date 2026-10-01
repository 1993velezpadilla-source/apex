#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../RoutingCore/RoutingGraph.h"
#include "SendStateCore.h"

namespace DAW {

/**
 * SendCommandCore -- single entry point for all user-initiated send mutations.
 *
 * All Bubblegum UI actions must go through here, never directly into RoutingGraph.
 * Enforces the 3-state model and logs results for undo/redo integration.
 *
 * Thread: message thread only.
 */
class SendCommandCore
{
public:
    SendCommandCore(RoutingGraph& graph, SendStateCore& state)
        : graph_(graph), state_(state) {}

    // ── Create (first connection from source to dest) ─────────────────────────

    bool createAudibleSend(const TrackID& sourceId,
                            const TrackID& destId,
                            float gainLinear = 1.0f)
    {
        bool ok = state_.createSend(graph_, sourceId, destId,
                                    ConnectionType::Send, gainLinear);
        DBG("[SendCommandCore] createAudibleSend " + sourceId + " -> " + destId
            + (ok ? " OK" : " REJECTED"));
        return ok;
    }

    bool createPreFaderSend(const TrackID& sourceId,
                             const TrackID& destId,
                             float gainLinear = 1.0f)
    {
        bool ok = state_.createSend(graph_, sourceId, destId,
                                    ConnectionType::PreSend, gainLinear);
        DBG("[SendCommandCore] createPreFaderSend " + sourceId + " -> " + destId
            + (ok ? " OK" : " REJECTED"));
        return ok;
    }

    bool createSidechainSend(const TrackID& sourceId,
                              const TrackID& destId,
                              float gainLinear = 1.0f)
    {
        bool ok = state_.createSend(graph_, sourceId, destId,
                                    ConnectionType::Sidechain, gainLinear);
        DBG("[SendCommandCore] createSidechainSend " + sourceId + " -> " + destId
            + (ok ? " OK" : " REJECTED"));
        return ok;
    }

    // ── Toggle active / inactive  (dot tap) ──────────────────────────────────

    /**
     * Toggle EXISTS+ACTIVE <-> EXISTS+INACTIVE.
     * If send does not exist, creates it first (active).
     * This matches FL-style: tapping a destination that has no send creates one.
     */
    void tapDot(const TrackID& sourceId,
                const TrackID& destId,
                ConnectionType type = ConnectionType::Send)
    {
        auto s = state_.getState(graph_, sourceId, destId, type);

        if (s == SendStateCore::State::DoesNotExist)
        {
            state_.createSend(graph_, sourceId, destId, type, 1.0f);
            DBG("[SendCommandCore] tapDot: created new send " + sourceId + " -> " + destId);
        }
        else
        {
            state_.toggleActive(graph_, sourceId, destId, type);
            DBG("[SendCommandCore] tapDot: toggled active "
                + sourceId + " -> " + destId
                + " now=" + juce::String(state_.isActive(graph_, sourceId, destId, type) ? "ACTIVE" : "INACTIVE"));
        }
    }

    // ── Delete  (trash button) ────────────────────────────────────────────────

    bool deleteSend(const TrackID& sourceId,
                    const TrackID& destId,
                    ConnectionType type = ConnectionType::Send)
    {
        bool ok = state_.deleteSend(graph_, sourceId, destId, type);
        DBG("[SendCommandCore] deleteSend " + sourceId + " -> " + destId
            + (ok ? " OK" : " NOT FOUND"));
        return ok;
    }

    // ── Gain ─────────────────────────────────────────────────────────────────

    void setSendGain(const TrackID& sourceId,
                     const TrackID& destId,
                     float           gainLinear,
                     ConnectionType  type = ConnectionType::Send)
    {
        state_.setGain(graph_, sourceId, destId, gainLinear, type);
    }

    float getSendGain(const TrackID& sourceId,
                      const TrackID& destId,
                      ConnectionType type = ConnectionType::Send) const
    {
        return state_.getGain(graph_, sourceId, destId, type);
    }

    // ── Route-only (disable master direct output) ─────────────────────────────

    /**
     * Route source to dest only -- disable source's Direct connection to master.
     * Source is then heard only through dest's downstream path.
     */
    void setRouteOnly(const TrackID& sourceId,
                      const TrackID& destId,
                      bool routeOnly)
    {
        auto* srcNode = graph_.getNodeByTrackId(sourceId);
        if (!srcNode) return;

        // Find the Direct connection from source to master
        for (auto* c : graph_.getOutputConnections(srcNode->id))
        {
            if (c->type == ConnectionType::Direct &&
                c->destNodeId == "master")
            {
                c->active = !routeOnly;  // disable direct-to-master when route-only
                DBG("[SendCommandCore] setRouteOnly " + sourceId
                    + (routeOnly ? " -> ROUTE ONLY" : " -> NORMAL (master restored)"));
                return;
            }
        }
    }

    // ── State queries (for UI) ────────────────────────────────────────────────

    SendStateCore::State getState(const TrackID& sourceId,
                                   const TrackID& destId,
                                   ConnectionType type = ConnectionType::Send) const
    {
        return state_.getState(graph_, sourceId, destId, type);
    }

    bool sendExists(const TrackID& sourceId,
                    const TrackID& destId,
                    ConnectionType type = ConnectionType::Send) const
    {
        return state_.exists(graph_, sourceId, destId, type);
    }

    bool sendIsActive(const TrackID& sourceId,
                      const TrackID& destId,
                      ConnectionType type = ConnectionType::Send) const
    {
        return state_.isActive(graph_, sourceId, destId, type);
    }

private:
    RoutingGraph&  graph_;
    SendStateCore& state_;
};

} // namespace DAW
