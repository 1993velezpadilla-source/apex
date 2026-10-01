#pragma once
#include <JuceHeader.h>
#include "RoutingNode.h"
#include "RoutingConnection.h"
#include "RoutingSnapshot.h"
#include "RoutingSnapshotPublisher.h"
#include "../UICore/ForensicAuditWindow.h"
#include <set>
#include <algorithm>
#include <unordered_map>

namespace DAW {

/**
 * RoutingGraph — manages the complete audio routing topology.
 *
 * Nodes = tracks, buses, folder buses, master output.
 * Connections = directed edges (direct, send, pre-send, sidechain, folder sum).
 *
 * Enforces:
 *  - No self-loops
 *  - No feedback cycles (DAG constraint)
 *  - Master node always exists
 *  - Default: every new track → master
 *
 * Provides topological sort for audio engine processing order.
 */
class RoutingGraph
{
public:
    RoutingGraph()
    {
        // The master output node always exists.
        // Construct in-place (RoutingNode is not movable — ::active is std::atomic<bool>).
        auto* master    = new RoutingNode();
        master->id      = "master";
        master->name    = "Master";
        master->type    = RoutingNodeType::Master;
        master->trackId = "master";
        nodes_.add(master);
        rebuildAdjacency();
    }

    ~RoutingGraph() = default;

    // ── Node management ───────────────────────────────────────────────────────

    /** Add a node to the graph. Automatically routes to master for Track/Bus types.
     *  Thread-safe: locks graphLock_ (recursive — safe to call connect() inside). */
    RoutingNode* addNode(const juce::String& name,
                         RoutingNodeType type,
                         const TrackID& trackId = {})
    {
        juce::ScopedLock sl(graphLock_);
        auto* node    = new RoutingNode();
        node->id      = "RN_" + juce::String(++nextNodeId_);
        node->name    = name;
        node->type    = type;
        node->trackId = trackId;
        nodes_.add(node);
        rebuildAdjacency();

        // Default: route new tracks/buses directly to master
        if (type == RoutingNodeType::Track || type == RoutingNodeType::Bus)
            connect(node->id, "master", ConnectionType::Direct);

        listeners_.call([node](Listener& l) { l.nodeAdded(node); });
        return node;
    }

    /** Remove a node and all its connections. Cannot remove master.
     *  Thread-safe: locks graphLock_. */
    bool removeNode(const juce::String& nodeId)
    {
        static thread_local int removeNodeDepth = 0;
        juce::ScopedValueSetter<int> depthScope(removeNodeDepth, removeNodeDepth + 1);
        if (removeNodeDepth > 1)
            FORENSIC_LOG("[ROUTING REENTRY] removeNode() recursive depth=" << removeNodeDepth
                << " node=" << nodeId);
        if (removeNodeDepth > 16)
        {
            FORENSIC_LOG("[ROUTING CASCADE] removeNode() aborting at depth " << removeNodeDepth);
            return false;
        }

        if (nodeId == "master") return false;

        juce::ScopedLock sl(graphLock_);

        // Remove all connections to/from this node
        for (int i = connections_.size() - 1; i >= 0; --i)
        {
            if (connections_[i]->sourceNodeId == nodeId ||
                connections_[i]->destNodeId == nodeId)
            {
                auto id = connections_[i]->id;
                connections_.remove(i);
                rebuildAdjacency();
                listeners_.call([&](Listener& l) { l.connectionRemoved(id); });
            }
        }

        for (int i = 0; i < nodes_.size(); ++i)
        {
            if (nodes_[i]->id == nodeId)
            {
                nodes_.remove(i);
                rebuildAdjacency();
                listeners_.call([&](Listener& l) { l.nodeRemoved(nodeId); });
                return true;
            }
        }
        return false;
    }

    RoutingNode* getNode(const juce::String& id) const
    {
        for (auto* n : nodes_)
            if (n->id == id) return n;
        return nullptr;
    }

    RoutingNode* getNodeByTrackId(const TrackID& trackId) const
    {
        for (auto* n : nodes_)
            if (n->trackId == trackId) return n;
        return nullptr;
    }

    RoutingNode* getNodeByTrackIdFast(const TrackID& trackId) const noexcept
    {
        auto it = adjacency_.nodeByTrackId.find(trackId);
        return it != adjacency_.nodeByTrackId.end() ? it->second : nullptr;
    }

    RoutingNode* getMasterNode() const { return getNode("master"); }

    const juce::OwnedArray<RoutingNode>& getAllNodes() const { return nodes_; }

    int getNodeCount() const { return nodes_.size(); }

    // ── Connection management ─────────────────────────────────────────────────

    /** Create a connection. Returns nullptr if self-loop or would create cycle.
     *  Thread-safe: locks graphLock_ (recursive — safe to call from addNode()). */
    RoutingConnection* connect(const juce::String& sourceId,
                               const juce::String& destId,
                               ConnectionType type,
                               float gain = 1.0f)
    {
        static thread_local int connectDepth = 0;
        juce::ScopedValueSetter<int> depthScope(connectDepth, connectDepth + 1);
        if (connectDepth > 1)
            FORENSIC_LOG("[ROUTING REENTRY] connect() recursive depth=" << connectDepth
                << " src=" << sourceId << " dst=" << destId << " type=" << (int)type);
        if (connectDepth > 16)
        {
            FORENSIC_LOG("[ROUTING CASCADE] connect() aborting at depth " << connectDepth);
            return nullptr;
        }

        FORENSIC_LOG("[ROUTING CONNECT] src=" << sourceId << " dst=" << destId
            << " type=" << (int)type << " depth=" << connectDepth);

        if (sourceId == destId) return nullptr;

        juce::ScopedLock sl(graphLock_);

        // Prevent duplicate connections of the same type
        for (auto* c : connections_)
        {
            if (c->sourceNodeId == sourceId && c->destNodeId == destId && c->type == type)
            {
                FORENSIC_LOG("[ROUTING CONNECT DUPLICATE] returning existing id=" << c->id
                    << " src=" << sourceId << " dst=" << destId << " type=" << (int)type
                    << " active=" << (int)c->active.load(std::memory_order_relaxed));
                return c;
            }
        }

        // Sidechain connections don't create audio-path cycles
        if (type != ConnectionType::Sidechain && wouldCreateCycle(sourceId, destId))
            return nullptr;

        auto* conn         = new RoutingConnection();
        conn->id           = "RC_" + juce::String(++nextConnId_);
        conn->sourceNodeId = sourceId;
        conn->destNodeId   = destId;
        conn->type         = type;
        conn->gain.store(gain, std::memory_order_relaxed);
        connections_.add(conn);
        rebuildAdjacency();

        listeners_.call([conn](Listener& l) { l.connectionAdded(conn); });
        return conn;
    }

    /** Remove a connection by ID. Thread-safe: locks graphLock_. */
    bool disconnect(const RouteID& connectionId)
    {
        static thread_local int disconnectDepth = 0;
        juce::ScopedValueSetter<int> depthScope(disconnectDepth, disconnectDepth + 1);
        if (disconnectDepth > 1)
            FORENSIC_LOG("[ROUTING REENTRY] disconnect(id) recursive depth=" << disconnectDepth
                << " id=" << connectionId);
        if (disconnectDepth > 16)
        {
            FORENSIC_LOG("[ROUTING CASCADE] disconnect(id) aborting at depth " << disconnectDepth);
            return false;
        }

        juce::ScopedLock sl(graphLock_);
        for (int i = 0; i < connections_.size(); ++i)
        {
            if (connections_[i]->id == connectionId)
            {
                connections_.remove(i);
                rebuildAdjacency();
                listeners_.call([&](Listener& l) { l.connectionRemoved(connectionId); });
                return true;
            }
        }
        return false;
    }

    /** Remove all connections of a given type between two nodes.
     *  Thread-safe: locks graphLock_. */
    bool disconnect(const juce::String& sourceId,
                    const juce::String& destId,
                    ConnectionType type)
    {
        static thread_local int disconnectDepth = 0;
        juce::ScopedValueSetter<int> depthScope(disconnectDepth, disconnectDepth + 1);
        if (disconnectDepth > 1)
            FORENSIC_LOG("[ROUTING REENTRY] disconnect(edge) recursive depth=" << disconnectDepth
                << " src=" << sourceId << " dst=" << destId << " type=" << (int)type);
        if (disconnectDepth > 16)
        {
            FORENSIC_LOG("[ROUTING CASCADE] disconnect(edge) aborting at depth " << disconnectDepth);
            return false;
        }

        juce::ScopedLock sl(graphLock_);
        bool removed = false;
        for (int i = connections_.size() - 1; i >= 0; --i)
        {
            auto* c = connections_[i];
            if (c->sourceNodeId == sourceId &&
                c->destNodeId == destId &&
                c->type == type)
            {
                auto id = c->id;
                connections_.remove(i);
                rebuildAdjacency();
                listeners_.call([&](Listener& l) { l.connectionRemoved(id); });
                removed = true;
            }
        }
        return removed;
    }

    const juce::OwnedArray<RoutingConnection>& getAllConnections() const { return connections_; }

    int getConnectionCount() const { return connections_.size(); }

    /** Get all outgoing connections from a node. */
    std::vector<RoutingConnection*> getOutputConnections(const juce::String& nodeId) const
    {
        std::vector<RoutingConnection*> result;
        for (auto* c : connections_)
            if (c->sourceNodeId == nodeId) result.push_back(c);
        return result;
    }

    const std::vector<RoutingConnection*>& getOutputConnectionsRef(const juce::String& nodeId) const noexcept
    {
        auto it = adjacency_.outputs.find(nodeId);
        return it != adjacency_.outputs.end() ? it->second : emptyConnections_;
    }

    /** Get all incoming connections to a node. */
    std::vector<RoutingConnection*> getInputConnections(const juce::String& nodeId) const
    {
        std::vector<RoutingConnection*> result;
        for (auto* c : connections_)
            if (c->destNodeId == nodeId) result.push_back(c);
        return result;
    }

    const std::vector<RoutingConnection*>& getInputConnectionsRef(const juce::String& nodeId) const noexcept
    {
        auto it = adjacency_.inputs.find(nodeId);
        return it != adjacency_.inputs.end() ? it->second : emptyConnections_;
    }

    /** Get sidechain sources feeding into a node. */
    std::vector<RoutingConnection*> getSidechainInputs(const juce::String& nodeId) const
    {
        std::vector<RoutingConnection*> result;
        for (auto* c : connections_)
            if (c->destNodeId == nodeId && c->type == ConnectionType::Sidechain)
                result.push_back(c);
        return result;
    }

    const std::vector<RoutingConnection*>& getSidechainInputsRef(const juce::String& nodeId) const noexcept
    {
        auto it = adjacency_.sidechainInputs.find(nodeId);
        return it != adjacency_.sidechainInputs.end() ? it->second : emptyConnections_;
    }

    /** Create a sidechain connection with full plugin-bus metadata.
     *  Returns nullptr if nodes don't exist or a duplicate already exists. */
    RoutingConnection* connectSidechain(const juce::String& sourceId,
                                        const juce::String& destId,
                                        const juce::String& destPluginId,
                                        int destBusIndex = 1,
                                        TapPoint tap = TapPoint::PreFX)
    {
        auto* conn = connect(sourceId, destId, ConnectionType::Sidechain);
        if (conn)
        {
            conn->destinationPluginId = destPluginId;
            conn->destinationBusIndex = destBusIndex;
            conn->tapPoint            = tap;
            rebuildAdjacency();
        }
        return conn;
    }

    // ── Topology queries ──────────────────────────────────────────────────────

    /** Get topologically sorted processing order (for audio engine).
     *  topoSort() prepends each node after visiting descendants, so the
     *  resulting vector is already producer-first (Track → Bus → Master). */
    std::vector<juce::String> getProcessingOrder() const
    {
        std::vector<juce::String> order;
        std::set<juce::String> visited;
        std::set<juce::String> visiting;

        for (auto* n : nodes_)
            topoSort(n->id, visited, visiting, order);

        return order;
    }

    const std::vector<juce::String>& getProcessingOrderRef() const noexcept
    {
        return adjacency_.processingOrder;
    }

    /** Check if adding source→dest would create a feedback cycle. */
    bool wouldCreateCycle(const juce::String& sourceId,
                          const juce::String& destId) const
    {
        // If dest can already reach source, adding source→dest creates a cycle
        std::set<juce::String> visited;
        return hasPath(destId, sourceId, visited);
    }

    // ── Serialization ─────────────────────────────────────────────────────────

    juce::ValueTree getState() const
    {
        juce::ValueTree state("RoutingGraph");

        juce::ValueTree nodesTree("Nodes");
        for (auto* n : nodes_)
            nodesTree.addChild(n->getState(), -1, nullptr);
        state.addChild(nodesTree, -1, nullptr);

        juce::ValueTree connsTree("Connections");
        for (auto* c : connections_)
            connsTree.addChild(c->getState(), -1, nullptr);
        state.addChild(connsTree, -1, nullptr);

        return state;
    }

    void restoreState(const juce::ValueTree& state)
    {
        juce::ScopedLock sl(graphLock_);
        nodes_.clear();
        connections_.clear();

        auto nodesTree = state.getChildWithName("Nodes");
        for (int i = 0; i < nodesTree.getNumChildren(); ++i)
        {
            auto* n = new RoutingNode();
            n->restoreState(nodesTree.getChild(i));
            nodes_.add(n);
        }

        auto connsTree = state.getChildWithName("Connections");
        for (int i = 0; i < connsTree.getNumChildren(); ++i)
        {
            auto* c = new RoutingConnection();
            c->restoreState(connsTree.getChild(i));
            connections_.add(c);
        }

        // Ensure master node always exists
        if (!getNode("master"))
        {
            auto* master = new RoutingNode();
            master->id   = "master";
            master->name = "Master";
            master->type = RoutingNodeType::Master;
            nodes_.add(master);
        }

        // C6-project-reload: rebase the NODE id allocator above every restored
        // node id. Without this, the first node created after a reload gets
        // "RN_1" again — colliding with a restored Day-1 node. The snapshot's
        // nodeIndexById (first-insertion-wins) then resolved the duplicate to
        // the OLD node, so newly-created tracks had no processed audio node
        // (no monitoring, no playback) and node-id-keyed deletions later
        // removed the old nodes instead (old tracks losing playback).
        int maxRestoredNodeId = 0;
        for (auto* node : nodes_)
        {
            if (node == nullptr || !node->id.startsWith("RN_"))
                continue;
            maxRestoredNodeId = juce::jmax(maxRestoredNodeId, node->id.substring(3).getIntValue());
        }
        if (maxRestoredNodeId >= nextNodeId_)
            nextNodeId_ = maxRestoredNodeId + 1;

        int maxRestoredConnectionId = 0;
        for (auto* conn : connections_)
        {
            if (conn == nullptr || !conn->id.startsWith("RC_"))
                continue;
            maxRestoredConnectionId = juce::jmax(maxRestoredConnectionId, conn->id.substring(3).getIntValue());
        }
        if (maxRestoredConnectionId >= nextConnId_)
            nextConnId_ = maxRestoredConnectionId + 1;

        listeners_.call([](Listener& l) { l.graphChanged(); });
        rebuildAdjacency();
    }

    // ── Listener ──────────────────────────────────────────────────────────────

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void nodeAdded(RoutingNode* node) {}
        virtual void nodeRemoved(const juce::String& nodeId) {}
        virtual void connectionAdded(RoutingConnection* conn) {}
        virtual void connectionRemoved(const RouteID& connId) {}
        virtual void graphChanged() {}
    };

    void addListener(Listener* l)
    {
        listeners_.add(l);
        ++listenerCount_;
        dumpListeners();
    }

    void removeListener(Listener* l)
    {
        listeners_.remove(l);
        listenerCount_ = juce::jmax(0, listenerCount_ - 1);
        dumpListeners();
    }

    void dumpListeners() const
    {
        FORENSIC_LOG("[ROUTING LISTENERS] count=" << listenerCount_);
    }

public:
    /** Returns the lock that guards nodes_ and connections_.
     *  Message thread only: ScopedLock before any mutation.
     *  The audio thread uses the lock-free RoutingSnapshotPublisher instead. */
    juce::CriticalSection& getLock() const { return graphLock_; }
    uint64_t getGraphVersion() const noexcept { return graphVersion_.load(std::memory_order_relaxed); }

    /** Returns the snapshot publisher. Audio thread calls get() on this — zero locks. */
    const RoutingSnapshotPublisher& getSnapshotPublisher() const noexcept { return snapshotPublisher_; }

    /**
     * Lightweight republish for non-topology changes (gain, active, bypassed, tapPoint,
     * sidechain metadata).  Does NOT rebuild adjacency or increment graphVersion.
     * Must be called from the message thread while holding graphLock_.
     */
    void publishSnapshotOnly()
    {
        juce::ScopedLock sl(graphLock_);
        snapshotPublisher_.publish(*this);
    }

    /** Notify UI listeners (cable overlay, mixer feedback) that a non-topology
     *  attribute changed (send gain, active flag, bypass, tap point...).
     *  Does NOT increment graphVersion / rebuild adjacency — the audio snapshot
     *  is republished so the audio thread sees the new value immediately.
     *  Message thread only, safe to call without the lock held. */
    void notifyGraphChanged()
    {
        {
            juce::ScopedLock sl(graphLock_);
            snapshotPublisher_.publish(*this);
        }
        listeners_.call([](Listener& l) { l.graphChanged(); });
    }

private:
    struct StringHash
    {
        size_t operator()(const juce::String& s) const noexcept { return (size_t) s.hashCode64(); }
    };

    struct AdjacencyCache
    {
        std::unordered_map<juce::String, std::vector<RoutingConnection*>, StringHash> outputs;
        std::unordered_map<juce::String, std::vector<RoutingConnection*>, StringHash> inputs;
        std::unordered_map<juce::String, std::vector<RoutingConnection*>, StringHash> sidechainInputs;
        std::unordered_map<TrackID, RoutingNode*, StringHash> nodeByTrackId;
        std::vector<juce::String> processingOrder;
    };

    mutable juce::CriticalSection       graphLock_;
    juce::OwnedArray<RoutingNode>       nodes_;
    juce::OwnedArray<RoutingConnection> connections_;
    AdjacencyCache adjacency_;
    mutable std::vector<RoutingConnection*> emptyConnections_;
    std::atomic<uint64_t> graphVersion_ { 1 };
    int nextNodeId_ = 0;
    int nextConnId_ = 0;
    juce::ListenerList<Listener> listeners_;
    int listenerCount_ = 0;
    RoutingSnapshotPublisher snapshotPublisher_;

    void rebuildAdjacency()
    {
        adjacency_.outputs.clear();
        adjacency_.inputs.clear();
        adjacency_.sidechainInputs.clear();
        adjacency_.nodeByTrackId.clear();

        adjacency_.outputs.reserve((size_t) nodes_.size());
        adjacency_.inputs.reserve((size_t) nodes_.size());
        adjacency_.sidechainInputs.reserve((size_t) nodes_.size());
        adjacency_.nodeByTrackId.reserve((size_t) nodes_.size());

        for (auto* node : nodes_)
            if (node != nullptr && node->trackId.isNotEmpty())
                adjacency_.nodeByTrackId[node->trackId] = node;

        for (auto* conn : connections_)
        {
            if (conn == nullptr) continue;
            adjacency_.outputs[conn->sourceNodeId].push_back(conn);
            adjacency_.inputs[conn->destNodeId].push_back(conn);
            if (conn->type == ConnectionType::Sidechain)
                adjacency_.sidechainInputs[conn->destNodeId].push_back(conn);
        }

        adjacency_.processingOrder.clear();
        adjacency_.processingOrder.reserve((size_t) nodes_.size());
        std::set<juce::String> visited;
        std::set<juce::String> visiting;
        for (auto* n : nodes_)
            if (n != nullptr)
                topoSort(n->id, visited, visiting, adjacency_.processingOrder);

        graphVersion_.fetch_add(1, std::memory_order_relaxed);
        snapshotPublisher_.publish(*this);
    }

    /** DFS: check if a path exists from 'from' to 'to' in the current graph. */
    bool hasPath(const juce::String& from,
                 const juce::String& to,
                 std::set<juce::String>& visited) const
    {
        if (from == to) return true;
        if (visited.count(from)) return false;
        visited.insert(from);

        for (auto* c : connections_)
        {
            if (c->type == ConnectionType::Sidechain) continue; // skip sidechain edges
            if (c->sourceNodeId == from && hasPath(c->destNodeId, to, visited))
                return true;
        }
        return false;
    }

    /** Topological sort (DFS post-order, prepending each visited node).
     *
     *  Sidechain edges ARE followed for ordering — the sidechain source must
     *  be rendered before the destination plugin reads its detector bus.
     *  They are NOT followed by hasPath/wouldCreateCycle so a kick→bass
     *  sidechain edge cannot falsely block the bass→master audio path. */
    void topoSort(const juce::String& nodeId,
                  std::set<juce::String>& visited,
                  std::set<juce::String>& visiting,
                  std::vector<juce::String>& order) const
    {
        if (visited.count(nodeId)) return;
        if (visiting.count(nodeId)) return; // cycle guard
        visiting.insert(nodeId);

        for (auto* c : connections_)
        {
            // Follow both audio-path edges AND sidechain edges for ordering.
            // Sidechain: source must be processed before destination.
            if (c->sourceNodeId == nodeId)
                topoSort(c->destNodeId, visited, visiting, order);
        }

        visiting.erase(nodeId);
        visited.insert(nodeId);
        order.insert(order.begin(), nodeId);
    }
};

// ── RoutingSnapshotPublisher::publish() implementation ────────────────────────
// Must live here (after RoutingGraph definition) to access nodes_ / connections_
// without a circular header dependency.
inline void RoutingSnapshotPublisher::publish(const RoutingGraph& graph)
{
    const auto previous = std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
    auto snap = std::make_shared<RoutingSnapshot>();
    snap->version = nextVersion_++;

    // Copy topo-sorted processing order (already built by rebuildAdjacency)
    snap->processingOrder = graph.getProcessingOrderRef();

    // Copy node metadata (plain data — no RoutingNode* retained)
    for (auto* node : graph.getAllNodes())
    {
        if (node == nullptr) continue;
        RoutingSnapshot::NodeSnapshot n;
        n.id      = node->id;
        n.trackId = node->trackId;
        n.type    = node->type;
        n.active  = node->active.load(std::memory_order_relaxed);
        snap->nodes.push_back(std::move(n));
    }

    snap->nodeIndexById.reserve(snap->nodes.size());
    for (size_t i = 0; i < snap->nodes.size(); ++i)
        snap->nodeIndexById.emplace(snap->nodes[i].id, i);

    // Copy every connection into plain EdgeSnapshot (no pointers, no atomics)
    for (auto* conn : graph.getAllConnections())
    {
        if (conn == nullptr) continue;
        RoutingSnapshot::EdgeSnapshot e;
        e.id                   = conn->id;
        e.sourceNodeId         = conn->sourceNodeId;
        e.destNodeId           = conn->destNodeId;
        e.type                 = conn->type;
        e.tapPoint             = conn->tapPoint;
        e.gain                 = conn->gain.load(std::memory_order_relaxed);
        e.active               = conn->active.load(std::memory_order_relaxed);
        e.bypassed             = conn->bypassed.load(std::memory_order_relaxed);
        e.destinationPluginId  = conn->destinationPluginId;
        e.destinationBusIndex  = conn->destinationBusIndex;
        snap->edges.push_back(std::move(e));
    }

    // A removed Direct→Master route is absent from canonical graph intent and
    // from Bubblegum immediately, but its immutable render generation retains
    // one runtime-only tombstone at gain 0. Existing engine ramp state then
    // de-zippers the previous contribution to exact silence instead of cutting
    // at an arbitrary block sample. Tombstones are not carried into a later
    // publication, so any subsequent graph edit reclaims the completed route.
    if (previous != nullptr)
    {
        std::set<RouteID> currentEdgeIds;
        for (const auto& edge : snap->edges)
            currentEdgeIds.insert(edge.id);

        bool addedMasterRetirement = false;
        for (const auto& oldEdge : previous->edges)
        {
            if (oldEdge.retiring
                || oldEdge.type != ConnectionType::Direct
                || oldEdge.destNodeId != "master"
                || !oldEdge.active || oldEdge.bypassed
                || currentEdgeIds.count(oldEdge.id) != 0)
                continue;

            auto retiringEdge = oldEdge;
            retiringEdge.gain = 0.0f;
            retiringEdge.active = true;
            retiringEdge.bypassed = false;
            retiringEdge.retiring = true;
            snap->edges.push_back(std::move(retiringEdge));
            addedMasterRetirement = true;
        }

        // The deleted edge is no longer in the graph's topological order. Keep
        // Master last for the retirement generation so every fading source/bus
        // contributes before hardware output is copied.
        if (addedMasterRetirement)
        {
            auto masterIt = std::find(snap->processingOrder.begin(), snap->processingOrder.end(), juce::String("master"));
            if (masterIt != snap->processingOrder.end())
                snap->processingOrder.erase(masterIt);
            snap->processingOrder.push_back("master");
        }
    }

    // Publish with an exchange and hold the replaced snapshot in the retired
    // slot (message thread). The retired snapshot is destroyed at the NEXT
    // publish on this thread, so the audio thread is never the last owner of
    // a replaced snapshot in any real cadence (documented residual: two
    // publishes inside a single audio block, which UI action rates and the
    // device-preparation gate make unreachable in practice).
    auto oldSnap = std::atomic_exchange_explicit(&snapshot_, snap, std::memory_order_acq_rel);
    retiredSnapshot_ = std::move(oldSnap);
}

} // namespace DAW
