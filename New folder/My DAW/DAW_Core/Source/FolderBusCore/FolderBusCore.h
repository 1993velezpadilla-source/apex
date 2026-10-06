#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include "../UtilityCore/Types.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../RoutingCore/MasterRouteStateCore.h"
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * FolderBusCore — full nested FolderBus topology manager.
 *
 * Manages adoption/release of tracks and nested FolderBus nodes.
 * Enforces edge rules:
 *   Root node  → Direct edge to Master
 *   Child node → FolderSum edge to its parent FolderBus
 *
 * No depth limit. RoutingGraph cycle detection is the safety net.
 * Never call from the audio thread — message thread only.
 */
class FolderBusCore
{
public:
    // ── Data model ───────────────────────────────────────────────────────

    struct FolderBusInfo
    {
        TrackID              folderBusTrackId;
        juce::String         nodeId;
        TrackID              parentFolderBusTrackId; // empty = root
        std::vector<TrackID> childTrackIds;          // direct children only
        juce::String         name;
        int                  depth = 0;              // 0 = root
    };

    // ── Creation ─────────────────────────────────────────────────────────

    /**
     * Create a new FolderBus from a set of existing tracks/FolderBuses.
     * parentFolderBusTrackId empty → root FolderBus (Direct→master).
     * parentFolderBusTrackId set  → nested inside parent.
     * Returns the new FolderBus track ID.
     */
    TrackID createFolderBus(
        const juce::String&         name,
        const std::vector<TrackID>& childTrackIds,
        const TrackID&              parentFolderBusTrackId,
        RoutingGraph&               graph,
        MasterRouteStateCore&       masterRoute,
        TrackManager&               tracks)
    {
        // 1. Create Track (role=FolderBus)
        auto* fbTrack = tracks.createTrack(name);
        if (!fbTrack) return {};
        fbTrack->setRole(TrackRole::FolderBus);
        const TrackID fbId = fbTrack->getID();

        // 2. Reuse the lifecycle-created RoutingNode for this track when possible.
        // Creating a second node for the same TrackID leaves ghost topology behind.
        auto* node = graph.getNodeByTrackId(fbId);
        if (node)
        {
            node->name = name;
            node->type = RoutingNodeType::FolderBus;
        }
        else
        {
            node = graph.addNode(name, RoutingNodeType::FolderBus, fbId);
            if (node && parentFolderBusTrackId.isEmpty())
                masterRoute.registerTrack(fbId);
        }
        if (!node) { tracks.deleteTrack(fbId); return {}; }
        const juce::String nodeId = node->id;

        // 3. Register in maps
        FolderBusInfo info;
        info.folderBusTrackId        = fbId;
        info.nodeId                  = nodeId;
        info.parentFolderBusTrackId  = {};
        info.name                    = name;
        info.depth                   = 0;
        folderBuses_[fbId] = info;

        // 4. If nested: adopt the new FolderBus into its parent
        if (parentFolderBusTrackId.isNotEmpty() && folderBuses_.count(parentFolderBusTrackId))
            adoptNode(fbId, parentFolderBusTrackId, graph, masterRoute);

        // 5. Position FolderBus immediately above its first child
        if (!childTrackIds.empty())
        {
            int targetPosition = tracks.getTrackIndex(childTrackIds[0]);
            if (targetPosition < 0)
                targetPosition = tracks.getNumTracks() - 1;
            tracks.moveTrack(fbId, targetPosition);
        }

        // 6. Adopt children
        for (auto& cid : childTrackIds)
            adoptNode(cid, fbId, graph, masterRoute);

        syncTrackParentLinks(tracks);
        normalizeTrackOrder(tracks);
        computeDepths();
        return fbId;
    }

    // ── Child management ─────────────────────────────────────────────────

    void addChildToFolderBus(
        const TrackID& folderBusTrackId,
        const TrackID& childTrackId,
        RoutingGraph&  graph,
        MasterRouteStateCore& masterRoute,
        TrackManager&  tracks)
    {
        adoptNode(childTrackId, folderBusTrackId, graph, masterRoute);

        // Set parent links BEFORE moveTrack so that the trackOrderChanged
        // rebuild sees the correct parentTrackID and renders the child at
        // the right indent depth — not as a stale root-level "copy outside".
        syncTrackParentLinks(tracks);
        computeDepths();
        normalizeTrackOrder(tracks);
    }

    TrackID convertTrackToFolderBus(
        const TrackID& trackId,
        RoutingGraph&  graph,
        MasterRouteStateCore& masterRoute,
        TrackManager&  tracks)
    {
        auto* track = tracks.getTrack(trackId);
        if (!track) return {};

        if (isFolderBus(trackId))
            return trackId;

        track->setRole(TrackRole::FolderBus);

        auto* node = graph.getNodeByTrackId(trackId);
        if (!node)
        {
            node = graph.addNode(track->getName(), RoutingNodeType::FolderBus, trackId);
            if (node && !isChildOfAnyFolderBus(trackId))
                masterRoute.registerTrack(trackId);
        }
        if (!node) return {};

        node->name = track->getName();
        node->type = RoutingNodeType::FolderBus;

        FolderBusInfo info;
        info.folderBusTrackId       = trackId;
        info.nodeId                 = node->id;
        info.parentFolderBusTrackId = getParentFolderBus(trackId);
        info.name                   = track->getName();
        info.depth                  = 0;
        folderBuses_[trackId] = std::move(info);

        syncTrackParentLinks(tracks);
        normalizeTrackOrder(tracks);
        computeDepths();
        return trackId;
    }

    void removeChildFromFolderBus(
        const TrackID& childTrackId,
        RoutingGraph&  graph,
        MasterRouteStateCore& masterRoute,
        TrackManager* tracks = nullptr)
    {
        releaseNode(childTrackId, graph);
        restoreToRoot(childTrackId, graph, masterRoute);
        if (tracks != nullptr)
        {
            syncTrackParentLinks(*tracks);
            normalizeTrackOrder(*tracks);
        }
        computeDepths();
    }

    void moveNodeToFolderBus(
        const TrackID& nodeTrackId,
        const TrackID& newParentFolderBusTrackId,
        RoutingGraph&  graph,
        MasterRouteStateCore& masterRoute,
        TrackManager* tracks = nullptr)
    {
        releaseNode(nodeTrackId, graph);
        adoptNode(nodeTrackId, newParentFolderBusTrackId, graph, masterRoute);
        if (tracks != nullptr)
        {
            syncTrackParentLinks(*tracks);
            normalizeTrackOrder(*tracks);
        }
        computeDepths();
    }

    // ── Dissolve ─────────────────────────────────────────────────────────

    void dissolveFolderBus(
        const TrackID& folderBusTrackId,
        RoutingGraph&  graph,
        MasterRouteStateCore& masterRoute,
        TrackManager&  tracks)
    {
        auto it = folderBuses_.find(folderBusTrackId);
        if (it == folderBuses_.end()) return;

        const TrackID grandparentId = it->second.parentFolderBusTrackId;
        const juce::String nodeId   = it->second.nodeId;

        // Promote children to grandparent or root
        auto children = it->second.childTrackIds; // copy — map will be mutated
        int busPosition = tracks.getTrackIndex(folderBusTrackId);

        for (auto& cid : children)
        {
            releaseNode(cid, graph);
            if (grandparentId.isNotEmpty() && folderBuses_.count(grandparentId))
                adoptNode(cid, grandparentId, graph, masterRoute);
            else
                restoreToRoot(cid, graph, masterRoute);
        }

        // Re-insert each child at the bus's original position in order
        for (int i = 0; i < (int)children.size(); ++i)
            tracks.moveTrack(children[i], busPosition + i);

        // Remove this FolderBus from its own parent if nested
        if (grandparentId.isNotEmpty())
            releaseNode(folderBusTrackId, graph);
        else
        {
            // Root FolderBus: remove its Direct→master edge manually
            masterRoute.deleteMasterRoute(folderBusTrackId);
        }

        folderBuses_.erase(folderBusTrackId);
        childToParent_.erase(folderBusTrackId);
        syncTrackParentLinks(tracks);
        normalizeTrackOrder(tracks);
        computeDepths();

        graph.removeNode(nodeId);
        tracks.deleteTrack(folderBusTrackId);
    }

    // ── Query ─────────────────────────────────────────────────────────────

    bool isFolderBus(const TrackID& trackId) const
    {
        return folderBuses_.count(trackId) > 0;
    }

    bool isChildOfAnyFolderBus(const TrackID& trackId) const
    {
        return childToParent_.count(trackId) > 0;
    }

    bool isRootFolderBus(const TrackID& trackId) const
    {
        auto it = folderBuses_.find(trackId);
        return it != folderBuses_.end() && it->second.parentFolderBusTrackId.isEmpty();
    }

    const TrackID& getParentFolderBus(const TrackID& childTrackId) const
    {
        static const TrackID empty;
        auto it = childToParent_.find(childTrackId);
        return it != childToParent_.end() ? it->second : empty;
    }

    const std::vector<TrackID>& getDirectChildren(const TrackID& folderBusTrackId) const
    {
        static const std::vector<TrackID> empty;
        auto it = folderBuses_.find(folderBusTrackId);
        return it != folderBuses_.end() ? it->second.childTrackIds : empty;
    }

    std::vector<TrackID> getAllDescendants(const TrackID& folderBusTrackId) const
    {
        std::vector<TrackID> result;
        collectDescendants(folderBusTrackId, result);
        return result;
    }

    /** Returns ancestors from outermost to direct parent: [grandparent, ..., parent]. */
    std::vector<TrackID> getAncestorChain(const TrackID& trackId) const
    {
        std::vector<TrackID> chain;
        TrackID cur = trackId;
        std::unordered_set<TrackID> seen;
        while (true)
        {
            auto it = childToParent_.find(cur);
            if (it == childToParent_.end()) break;
            if (seen.count(it->second)) break; // cycle guard
            seen.insert(it->second);
            chain.push_back(it->second);
            cur = it->second;
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    }

    int getDepth(const TrackID& trackId) const
    {
        auto it = folderBuses_.find(trackId);
        if (it != folderBuses_.end()) return it->second.depth;
        // For non-FolderBus tracks, depth = parent's depth + 1
        auto pit = childToParent_.find(trackId);
        if (pit == childToParent_.end()) return 0;
        return getDepth(pit->second) + 1;
    }

    std::vector<TrackID> getRootFolderBuses() const
    {
        std::vector<TrackID> roots;
        for (auto& [id, info] : folderBuses_)
            if (info.parentFolderBusTrackId.isEmpty())
                roots.push_back(id);
        return roots;
    }

    const FolderBusInfo* getFolderBusInfo(const TrackID& folderBusTrackId) const
    {
        auto it = folderBuses_.find(folderBusTrackId);
        return it != folderBuses_.end() ? &it->second : nullptr;
    }

    // ── Serialization ─────────────────────────────────────────────────────

    juce::ValueTree getState() const
    {
        juce::ValueTree root("FolderBuses");
        for (auto& rootId : getRootFolderBuses())
            root.addChild(saveFolderBusTree(rootId), -1, nullptr);
        return root;
    }

    void restoreState(const juce::ValueTree& state,
                      RoutingGraph&          graph,
                      MasterRouteStateCore&  masterRoute,
                      TrackManager&          tracks)
    {
        folderBuses_.clear();
        childToParent_.clear();
        restoreFolderBusTree(state, {}, graph, masterRoute, tracks);
        syncTrackParentLinks(tracks);
        normalizeTrackOrder(tracks);
        computeDepths();
    }

private:
    std::unordered_map<TrackID, FolderBusInfo> folderBuses_;
    std::unordered_map<TrackID, TrackID>       childToParent_; // all adopted nodes → parent

    // ── Internal helpers ──────────────────────────────────────────────────

    void adoptNode(const TrackID& nodeTrackId,
                   const TrackID& newParentFolderBusTrackId,
                   RoutingGraph&  graph,
                   MasterRouteStateCore& masterRoute)
    {
        if (nodeTrackId.isEmpty() || newParentFolderBusTrackId.isEmpty()) return;
        auto parentIt = folderBuses_.find(newParentFolderBusTrackId);
        if (parentIt == folderBuses_.end()) return;

        // If the node already belongs to another folder, fully detach it first so
        // old parent child lists do not retain stale entries.
        if (childToParent_.count(nodeTrackId))
            releaseNode(nodeTrackId, graph);

        // Remove any remaining direct-to-master edge before adopting.
        removeCurrentOutgoingEdge(nodeTrackId, graph, masterRoute);

        // Wire FolderSum → parent
        auto* srcNode  = graph.getNodeByTrackId(nodeTrackId);
        auto* destNode = graph.getNodeByTrackId(newParentFolderBusTrackId);
        if (srcNode && destNode)
            graph.connect(srcNode->id, destNode->id, ConnectionType::FolderSum);

        // Update maps
        childToParent_[nodeTrackId] = newParentFolderBusTrackId;
        auto& children = parentIt->second.childTrackIds;
        if (std::find(children.begin(), children.end(), nodeTrackId) == children.end())
            children.push_back(nodeTrackId);

        // If the adopted node is itself a FolderBus, update its parent field
        auto childFbIt = folderBuses_.find(nodeTrackId);
        if (childFbIt != folderBuses_.end())
            childFbIt->second.parentFolderBusTrackId = newParentFolderBusTrackId;
    }

    void releaseNode(const TrackID& nodeTrackId, RoutingGraph& graph)
    {
        auto parentIt = childToParent_.find(nodeTrackId);
        if (parentIt == childToParent_.end()) return;

        const TrackID parentId = parentIt->second;

        // Remove FolderSum edge to parent
        auto* srcNode  = graph.getNodeByTrackId(nodeTrackId);
        auto* destNode = graph.getNodeByTrackId(parentId);
        if (srcNode && destNode)
            graph.disconnect(srcNode->id, destNode->id, ConnectionType::FolderSum);

        // Update maps
        childToParent_.erase(nodeTrackId);
        auto parentFbIt = folderBuses_.find(parentId);
        if (parentFbIt != folderBuses_.end())
        {
            auto& ch = parentFbIt->second.childTrackIds;
            ch.erase(std::remove(ch.begin(), ch.end(), nodeTrackId), ch.end());
        }

        // If the released node is itself a FolderBus, clear its parent
        auto childFbIt = folderBuses_.find(nodeTrackId);
        if (childFbIt != folderBuses_.end())
            childFbIt->second.parentFolderBusTrackId = {};
    }

    void restoreToRoot(const TrackID& nodeTrackId,
                        RoutingGraph&  graph,
                        MasterRouteStateCore& masterRoute)
    {
        // Give the node its Direct→master edge back
        auto* srcNode = graph.getNodeByTrackId(nodeTrackId);
        if (srcNode)
            graph.connect(srcNode->id, "master", ConnectionType::Direct);
        // Also register with masterRoute so toggle/delete work correctly
        masterRoute.registerTrack(nodeTrackId);
    }

    void removeCurrentOutgoingEdge(const TrackID& nodeTrackId,
                                    RoutingGraph&  graph,
                                    MasterRouteStateCore& masterRoute)
    {
        auto* srcNode = graph.getNodeByTrackId(nodeTrackId);
        if (!srcNode) return;

        // Remove Direct→master if present
        auto conns = graph.getOutputConnections(srcNode->id);
        for (auto* c : conns)
        {
            if (c->type == ConnectionType::Direct && c->destNodeId == "master")
            {
                masterRoute.deleteMasterRoute(nodeTrackId);
                return;
            }
        }

        // Remove FolderSum→any_parent if present
        auto parentIt = childToParent_.find(nodeTrackId);
        if (parentIt != childToParent_.end())
        {
            auto* destNode = graph.getNodeByTrackId(parentIt->second);
            if (srcNode && destNode)
                graph.disconnect(srcNode->id, destNode->id, ConnectionType::FolderSum);
        }
    }

    void syncTrackParentLinks(TrackManager& tracks)
    {
        for (int i = 0; i < tracks.getNumTracks(); ++i)
        {
            if (auto* t = tracks.getTrack(i))
            {
                auto it = childToParent_.find(t->getID());
                const TrackID desiredParent = (it != childToParent_.end()) ? it->second : TrackID{};
                if (t->getParentTrackID() != desiredParent)
                    t->setParentTrackID(desiredParent);
            }
        }

        if (tracks.hasMasterTrack())
        {
            auto* master = tracks.getMasterTrack();
            if (master != nullptr && master->getParentTrackID().isNotEmpty())
                master->setParentTrackID({});
        }
    }

    void normalizeTrackOrder(TrackManager& tracks)
    {
        if (tracks.getNumTracks() < 2)
            return;

        // Collect root tracks (not a child of any folder) in their current
        // track-list order so user-defined root-level ordering is preserved.
        std::vector<TrackID> orderedRoots;
        std::unordered_set<TrackID> seenRoots;

        for (int i = 0; i < tracks.getNumTracks(); ++i)
        {
            auto* track = tracks.getTrack(i);
            if (track == nullptr)
                continue;

            const TrackID trackId = track->getID();
            if (!childToParent_.count(trackId) && !seenRoots.count(trackId))
            {
                seenRoots.insert(trackId);
                orderedRoots.push_back(trackId);
            }
        }

        // Build desired order by DFS: parent always emitted before its children.
        // Use FolderBusInfo.childTrackIds as the canonical sibling order instead
        // of the current (possibly wrong) track-list order.
        std::vector<TrackID> desiredOrder;
        desiredOrder.reserve((size_t) tracks.getNumTracks());
        std::unordered_set<TrackID> visited;

        std::function<void(const TrackID&)> appendSubtree = [&](const TrackID& id)
        {
            if (id.isEmpty() || visited.count(id))
                return;

            visited.insert(id);
            desiredOrder.push_back(id);

            auto fbIt = folderBuses_.find(id);
            if (fbIt == folderBuses_.end())
                return;

            for (const auto& childId : fbIt->second.childTrackIds)
                appendSubtree(childId);
        };

        for (const auto& rootId : orderedRoots)
            appendSubtree(rootId);

        // Safety: pick up any tracks not yet visited (orphans / stale entries).
        for (int i = 0; i < tracks.getNumTracks(); ++i)
        {
            auto* track = tracks.getTrack(i);
            if (track != nullptr)
                appendSubtree(track->getID());
        }

        // Apply the desired order in a single forward pass.
        bool didMove = false;
        for (int targetIndex = 0; targetIndex < (int) desiredOrder.size(); ++targetIndex)
        {
            auto* current = tracks.getTrack(targetIndex);
            if (current != nullptr && current->getID() == desiredOrder[(size_t) targetIndex])
                continue;

            tracks.moveTrack(desiredOrder[(size_t) targetIndex], targetIndex);
            didMove = true;
        }

        // Fire trackOrderChanged even if no physical move happened, because
        // syncTrackParentLinks may have changed indent depth and the UI must rebuild.
        if (!didMove)
            tracks.notifyOrderChanged();
    }

    void collectDescendants(const TrackID& fbId, std::vector<TrackID>& out) const
    {
        auto it = folderBuses_.find(fbId);
        if (it == folderBuses_.end()) return;
        for (auto& cid : it->second.childTrackIds)
        {
            out.push_back(cid);
            if (folderBuses_.count(cid))
                collectDescendants(cid, out);
        }
    }

    void computeDepths()
    {
        // BFS from root FolderBuses
        std::unordered_map<TrackID, int> depths;
        std::vector<TrackID> queue;
        for (auto& [id, info] : folderBuses_)
            if (info.parentFolderBusTrackId.isEmpty())
            { depths[id] = 0; queue.push_back(id); }

        std::unordered_set<TrackID> visited;
        while (!queue.empty())
        {
            auto cur = queue.front(); queue.erase(queue.begin());
            if (visited.count(cur)) continue;
            visited.insert(cur);
            int d = depths.count(cur) ? depths[cur] : 0;
            auto it = folderBuses_.find(cur);
            if (it == folderBuses_.end()) continue;
            it->second.depth = d;
            for (auto& cid : it->second.childTrackIds)
                if (folderBuses_.count(cid))
                { depths[cid] = d + 1; queue.push_back(cid); }
        }
    }

    juce::ValueTree saveFolderBusTree(const TrackID& fbId) const
    {
        auto it = folderBuses_.find(fbId);
        if (it == folderBuses_.end()) return {};
        juce::ValueTree node("FolderBus");
        node.setProperty("trackId", fbId,             nullptr);
        node.setProperty("name",    it->second.name,  nullptr);
        node.setProperty("depth",   it->second.depth, nullptr);
        for (auto& cid : it->second.childTrackIds)
        {
            if (folderBuses_.count(cid))
                node.addChild(saveFolderBusTree(cid), -1, nullptr);
            else
            {
                juce::ValueTree child("Child");
                child.setProperty("trackId", cid, nullptr);
                node.addChild(child, -1, nullptr);
            }
        }
        return node;
    }

    void restoreFolderBusTree(const juce::ValueTree& parent,
                               const TrackID&         parentFbId,
                               RoutingGraph&          graph,
                               MasterRouteStateCore&  masterRoute,
                               TrackManager&          tracks)
    {
        for (int i = 0; i < parent.getNumChildren(); ++i)
        {
            auto child = parent.getChild(i);
            if (child.hasType("FolderBus"))
            {
                TrackID childFbId = child.getProperty("trackId").toString();
                juce::String fbName = child.getProperty("name").toString();
                // Register without creating new tracks (tracks already restored by ProjectManager)
                auto* track = tracks.getTrack(childFbId);
                auto* node  = track ? graph.getNodeByTrackId(childFbId) : nullptr;
                if (track && node)
                {
                    FolderBusInfo info;
                    info.folderBusTrackId       = childFbId;
                    info.nodeId                 = node->id;
                    info.parentFolderBusTrackId = parentFbId;
                    info.name                   = fbName;
                    info.depth                  = 0;
                    folderBuses_[childFbId] = info;
                    if (parentFbId.isNotEmpty())
                        adoptNode(childFbId, parentFbId, graph, masterRoute);
                }
                restoreFolderBusTree(child, childFbId, graph, masterRoute, tracks);
            }
            else if (child.hasType("Child"))
            {
                TrackID cid = child.getProperty("trackId").toString();
                if (parentFbId.isNotEmpty())
                    adoptNode(cid, parentFbId, graph, masterRoute);
            }
        }
    }
};

} // namespace DAW
