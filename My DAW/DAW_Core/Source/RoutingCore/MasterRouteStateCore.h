#pragma once
#include <JuceHeader.h>
#include "RoutingGraph.h"
#include <functional>

namespace DAW {

// =====================================================================
// MasterRouteStateCore
//
// Single authority for per-track Master route state.
// Drives RoutingConnection::active on the real RoutingGraph so the
// audio engine always reads the correct state without any secondary
// in-memory map.
//
// The Master route is NEVER re-enabled automatically when a send is
// muted, toggled, or deleted.  It changes ONLY through explicit calls
// to this core.
//
// Rules:
//   • registerTrack()       → creates Direct→master if absent, active=true
//   • toggleMasterRoute()   → flips active on the real connection (dot click)
//   • setMasterRouteState() → used by "route only" / "sidechain only"
//   • deleteMasterRoute()   → active=false, edge kept (PDC-safe)
//   • unregisterTrack()     → removes the connection entirely
// =====================================================================

using TrackId = juce::String;

enum class RouteState : uint8_t
{
    DoesNotExist   = 0,  // connection absent or hard-removed
    ExistsInactive = 1,  // connection exists, active == false
    ExistsActive   = 2   // connection exists, active == true
};

class MasterRouteStateCore
{
public:
    explicit MasterRouteStateCore(RoutingGraph& graph) : graph_(graph) {}

    // ── Lifecycle ────────────────────────────────────────────────────

    /** Ensures a Direct→master connection exists and is active. */
    void registerTrack(const TrackId& trackId)
    {
        auto* conn = findDirectToMaster(trackId);
        FORENSIC_LOG("[MASTER ROUTE] registerTrack track=" << trackId
            << " existing=" << (conn != nullptr ? 1 : 0));
        if (!conn)
        {
            if (auto* srcNode = graph_.getNodeByTrackId(trackId))
            {
                conn = graph_.connect(srcNode->id, "master", ConnectionType::Direct);
                FORENSIC_LOG("[MASTER ROUTE] registerTrack created id=" << (conn ? conn->id : juce::String("null")));
            }
        }
        if (conn)
        {
            const bool wasActive = conn->active.load(std::memory_order_relaxed);
            conn->active.store(true, std::memory_order_relaxed);
            graph_.publishSnapshotOnly();
            FORENSIC_LOG("[MASTER ROUTE] registerTrack active " << (int)wasActive << " -> 1 id=" << conn->id);
        }
    }

    /** Removes the direct-to-master connection entirely. */
    void unregisterTrack(const TrackId& trackId)
    {
        if (auto* srcNode = graph_.getNodeByTrackId(trackId))
            graph_.disconnect(srcNode->id, "master", ConnectionType::Direct);
    }

    // ── Query ────────────────────────────────────────────────────────

    RouteState getMasterRouteState(const TrackId& trackId) const
    {
        auto* conn = findDirectToMaster(trackId);
        if (!conn)        return RouteState::DoesNotExist;
        if (conn->active.load(std::memory_order_relaxed)) return RouteState::ExistsActive;
        return RouteState::ExistsInactive;
    }

    bool isMasterRouteActive(const TrackId& trackId) const
    {
        auto* conn = findDirectToMaster(trackId);
        return conn != nullptr && conn->active.load(std::memory_order_relaxed);
    }

    float getMasterRouteLevel(const TrackId& trackId) const
    {
        auto* conn = findDirectToMaster(trackId);
        return conn ? conn->gain.load(std::memory_order_relaxed) : 0.0f;
    }

    // ── Mutation ─────────────────────────────────────────────────────

    /** Dot-click toggle: ExistsActive ↔ ExistsInactive.
     *  Never re-creates a removed connection. Never removes an existing one. */
    void toggleMasterRoute(const TrackId& trackId)
    {
        if (auto* conn = findDirectToMaster(trackId))
        {
            const bool before = conn->active.load(std::memory_order_relaxed);
            conn->active.store(!before, std::memory_order_relaxed);
            graph_.publishSnapshotOnly();
            FORENSIC_LOG("[MASTER ROUTE] toggle track=" << trackId << " active " << (int)before << " -> " << (int)!before);
        }
    }

    /** Explicit set — used by "route only" and "sidechain only".
     *  DoesNotExist silences without removing (topology-safe). */
    void setMasterRouteState(const TrackId& trackId, RouteState state)
    {
        if (auto* conn = findDirectToMaster(trackId))
        {
            conn->active.store(state == RouteState::ExistsActive, std::memory_order_relaxed);
            graph_.publishSnapshotOnly();
        }
    }

    void setMasterRouteLevel(const TrackId& trackId, float level)
    {
        if (auto* conn = findDirectToMaster(trackId))
        {
            conn->gain.store(juce::jlimit(0.0f, 2.0f, level), std::memory_order_relaxed);
            graph_.publishSnapshotOnly();
        }
    }

    void adjustMasterRouteLevel(const TrackId& trackId, float delta)
    {
        if (auto* conn = findDirectToMaster(trackId))
        {
            conn->gain.store(juce::jlimit(0.0f, 2.0f, conn->gain.load(std::memory_order_relaxed) + delta), std::memory_order_relaxed);
            graph_.publishSnapshotOnly();
        }
    }

    /** Fully remove the direct-to-master connection (symmetric with
     *  BubblegumSendStateCore::deleteSend for non-master targets).
     *  Delete ≠ deactivate — use setMasterRouteState(ExistsInactive) for the
     *  "turn off but preserve topology/PDC" case (dot toggle). */
    void deleteMasterRoute(const TrackId& trackId)
    {
        if (auto* srcNode = graph_.getNodeByTrackId(trackId))
            graph_.disconnect(srcNode->id, "master", ConnectionType::Direct);
    }

    // ── Bulk iteration ───────────────────────────────────────────────

    void forEachMasterRoute(std::function<void(const TrackId&, bool active)> fn) const
    {
        for (auto* conn : graph_.getAllConnections())
        {
            if (conn->type != ConnectionType::Direct) continue;
            if (conn->destNodeId != "master")         continue;
            if (auto* srcNode = graph_.getNode(conn->sourceNodeId))
                fn(srcNode->trackId, conn->active.load(std::memory_order_relaxed));
        }
    }

private:
    RoutingGraph& graph_;

    RoutingConnection* findDirectToMaster(const TrackId& trackId) const
    {
        auto* srcNode = graph_.getNodeByTrackId(trackId);
        if (!srcNode) return nullptr;
        for (auto* conn : graph_.getAllConnections())
        {
            if (conn->sourceNodeId == srcNode->id &&
                conn->destNodeId   == "master"    &&
                conn->type         == ConnectionType::Direct)
                return conn;
        }
        return nullptr;
    }
};

} // namespace DAW
