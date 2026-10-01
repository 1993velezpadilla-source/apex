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
    static bool isSendConnection(ConnectionType type) noexcept
    {
        return type == ConnectionType::Send || type == ConnectionType::PreSend;
    }

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
                isSendConnection(c->type))
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
        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->destNodeId == destNode->id && isSendConnection(c->type))
            {
                c->active.store(true, std::memory_order_relaxed);
                c->gain.store(level, std::memory_order_relaxed);
                graph.publishSnapshotOnly();
                return;
            }
        }
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
        graph.disconnect(srcNode->id, destNode->id, ConnectionType::PreSend);
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
            if (c->destNodeId == destNode->id && isSendConnection(c->type))
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
            if (c->destNodeId == destNode->id && isSendConnection(c->type))
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
            if (c->destNodeId == destNode->id && isSendConnection(c->type))
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
            if (c->destNodeId == destNode->id && isSendConnection(c->type))
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
            if (c->destNodeId == destNode->id && isSendConnection(c->type))
            {
                c->active.store(active, std::memory_order_relaxed);
                graph.publishSnapshotOnly();
                return;
            }
        }
    }

    /**
     * True if the source has at least one Send-family connection (Send or
     * PreSend) and ALL of them are pre-fader.  False when there are no sends
     * or when any send is still post-fader.  The UI button shows "PRE" when
     * this returns true, "POST" otherwise.
     */
    bool areAllSendsPreFader(const RoutingGraph& graph,
                             const TrackID& sourceId) const
    {
        auto* srcNode = graph.getNodeByTrackId(sourceId);
        if (!srcNode) return false;

        bool foundAny = false;
        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->type != ConnectionType::Send && c->type != ConnectionType::PreSend)
                continue;
            foundAny = true;
            if (c->type != ConnectionType::PreSend)
                return false;
        }
        return foundAny;
    }

    /**
     * Switch every Send-family connection from this source between post-fader
     * (ConnectionType::Send) and pre-fader (ConnectionType::PreSend).
     *
     * The type is mutated in place under the graph lock and the snapshot is
     * republished — the RouteID (and therefore the automation parameter
     * bindings keyed on routeID) is preserved.  The audio renderer already
     * mixes PreSend edges before the fader ramp and Send edges after it.
     */
    void setAllSendPreFader(RoutingGraph& graph,
                            const TrackID& sourceId,
                            bool preFader)
    {
        auto* srcNode = graph.getNodeByTrackId(sourceId);
        if (!srcNode) return;

        bool changed = false;
        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->type != ConnectionType::Send && c->type != ConnectionType::PreSend)
                continue;
            const auto desired = preFader ? ConnectionType::PreSend : ConnectionType::Send;
            if (c->type != desired)
            {
                c->type = desired;
                changed = true;
            }
        }
        if (changed)
        {
            graph.publishSnapshotOnly();
            DBG("SET pre-fader sends: source=" << sourceId << " preFader=" << (int)preFader);
        }
    }

    // ── Per-target (source→target) pre/post-fader control ────────────────

    /** True when the single send source→target is pre-fader.
     *  False when the send does not exist or is still post-fader. */
    bool isSendPreFader(const RoutingGraph& graph,
                        const TrackID& sourceId,
                        const TrackID& targetId) const
    {
        auto* srcNode = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return false;
        for (auto* c : graph.getOutputConnections(srcNode->id))
            if (c->destNodeId == destNode->id &&
                (c->type == ConnectionType::Send || c->type == ConnectionType::PreSend))
                return c->type == ConnectionType::PreSend;
        return false;
    }

    /**
     * Switch ONE send (source→target) between post-fader (ConnectionType::Send)
     * and pre-fader (ConnectionType::PreSend).  The type is mutated in place
     * under the graph lock and the snapshot is republished — the RouteID (and
     * therefore automation bindings keyed on routeID) is preserved.  Other
     * sends from the same source are untouched.
     */
    void setSendPreFader(RoutingGraph& graph,
                         const TrackID& sourceId,
                         const TrackID& targetId,
                         bool preFader)
    {
        auto* srcNode = graph.getNodeByTrackId(sourceId);
        auto* destNode = graph.getNodeByTrackId(targetId);
        if (!srcNode || !destNode) return;

        const auto desired = preFader ? ConnectionType::PreSend : ConnectionType::Send;
        bool changed = false;
        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->destNodeId != destNode->id)
                continue;
            if (c->type != ConnectionType::Send && c->type != ConnectionType::PreSend)
                continue;
            if (c->type != desired)
            {
                c->type = desired;
                changed = true;
            }
            break;
        }
        if (changed)
        {
            graph.publishSnapshotOnly();
            DBG("SET send pre-fader: source=" << sourceId << " target=" << targetId
                << " preFader=" << (int)preFader);
        }
    }

    /**
     * Summary of the pre/post-fader state of every send leaving `sourceId`:
     *   0 = no sends, 1 = all post-fader, 2 = all pre-fader, 3 = mixed.
     * Used by the POST/PRE button label ("—" / "POST" / "PRE" / "MIX").
     */
    int getSendPreFaderSummary(const RoutingGraph& graph,
                               const TrackID& sourceId) const
    {
        auto* srcNode = graph.getNodeByTrackId(sourceId);
        if (!srcNode) return 0;

        int sendCount = 0, preCount = 0;
        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->type != ConnectionType::Send && c->type != ConnectionType::PreSend)
                continue;
            ++sendCount;
            if (c->type == ConnectionType::PreSend)
                ++preCount;
        }
        if (sendCount == 0)     return 0;
        if (preCount == sendCount) return 2;
        if (preCount == 0)      return 1;
        return 3;
    }

    /** Every (targetId, isPreFader) pair for the sends leaving `sourceId`.
     *  Used by the per-target toggle menu on the strip/row button. */
    std::vector<std::pair<TrackID, bool>> getSendPreFaderTargets(const RoutingGraph& graph,
                                                                 const TrackID& sourceId) const
    {
        std::vector<std::pair<TrackID, bool>> out;
        auto* srcNode = graph.getNodeByTrackId(sourceId);
        if (!srcNode) return out;

        for (auto* c : graph.getOutputConnections(srcNode->id))
        {
            if (c->type != ConnectionType::Send && c->type != ConnectionType::PreSend)
                continue;
            auto* destNode = graph.getNode(c->destNodeId);
            if (destNode == nullptr)
                continue;
            out.emplace_back(destNode->trackId, c->type == ConnectionType::PreSend);
        }
        return out;
    }
};

} // namespace DAW
