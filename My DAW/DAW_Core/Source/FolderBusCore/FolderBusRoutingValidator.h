#pragma once
#include <JuceHeader.h>
#include <vector>
#include "../UtilityCore/Types.h"
#include "FolderBusCore.h"

namespace DAW {

class TrackManager;
class RoutingGraph;

/**
 * FolderBusRoutingValidator — validates FolderBus topology changes.
 *
 * Rejects self-nesting, cycles, master-track adoption, and duplicate children.
 * Allows any non-cyclic nesting depth — no hard depth limit.
 * RoutingGraph cycle detection is the ultimate safety net.
 */
class FolderBusRoutingValidator
{
public:
    struct ValidationResult
    {
        bool         valid = true;
        juce::String errorMessage;
    };

    ValidationResult canAddChildToFolderBus(
        const TrackID&       targetFolderBusTrackId,
        const TrackID&       candidateChildId,
        const FolderBusCore& folderBus,
        const TrackManager&  tracks,
        const RoutingGraph&  /*graph*/) const
    {
        constexpr auto newFolderSentinel = "__new__";

        const bool creatingNewFolder = (targetFolderBusTrackId == newFolderSentinel);

        if (!creatingNewFolder && !folderBus.isFolderBus(targetFolderBusTrackId))
            return { false, "Target parent is not a valid FolderBus." };

        const bool isMasterTrack = tracks.hasMasterTrack()
                                && tracks.getMasterTrack() != nullptr
                                && tracks.getMasterTrack()->getID() == candidateChildId;

        if (candidateChildId.isEmpty())
            return { false, "Child track is invalid." };

        if (isMasterTrack)
            return { false, "The Master track cannot be adopted into a FolderBus." };

        if (tracks.getTrack(candidateChildId) == nullptr && !folderBus.isFolderBus(candidateChildId))
            return { false, "Child track does not exist." };

        // Must not be the same node
        if (candidateChildId == targetFolderBusTrackId)
            return { false, "A FolderBus cannot contain itself." };

        // Candidate must not be an ancestor of the target (cycle prevention)
        auto ancestors = folderBus.getAncestorChain(targetFolderBusTrackId);
        for (auto& a : ancestors)
            if (a == candidateChildId)
                return { false, "Adding this child would create a routing cycle." };

        // Candidate must not be a descendant-FolderBus that contains the target
        if (folderBus.isFolderBus(candidateChildId))
        {
            auto descendants = folderBus.getAllDescendants(candidateChildId);
            for (auto& d : descendants)
                if (d == targetFolderBusTrackId)
                    return { false, "Adding this child would create a routing cycle." };
        }

        // Candidate must not already be a direct child of this same parent
        for (auto& cid : folderBus.getDirectChildren(targetFolderBusTrackId))
            if (cid == candidateChildId)
                return { false, "Track is already a direct child of this FolderBus." };

        return { true, {} };
    }

    ValidationResult canCreateFolderBus(
        const std::vector<TrackID>& candidateChildIds,
        const TrackID&              parentFolderBusTrackId,
        const FolderBusCore&        folderBus,
        const TrackManager&         tracks,
        const RoutingGraph&         graph) const
    {
        if (candidateChildIds.empty())
            return { false, "A FolderBus must contain at least one child." };

        if (parentFolderBusTrackId.isNotEmpty() && !folderBus.isFolderBus(parentFolderBusTrackId))
            return { false, "Parent target is not a valid FolderBus." };

        std::unordered_set<TrackID> uniqueChildren;
        for (auto& cid : candidateChildIds)
        {
            if (!uniqueChildren.insert(cid).second)
                return { false, "FolderBus child selection contains duplicates." };

            auto r = canAddChildToFolderBus(
                parentFolderBusTrackId.isEmpty() ? juce::String("__new__") : parentFolderBusTrackId,
                cid, folderBus, tracks, graph);
            if (!r.valid) return r;
        }
        return { true, {} };
    }

    ValidationResult canMoveFolderBus(
        const TrackID&       movingFolderBusTrackId,
        const TrackID&       newParentFolderBusTrackId,
        const FolderBusCore& folderBus) const
    {
        if (!folderBus.isFolderBus(movingFolderBusTrackId))
            return { false, "Moving node is not a valid FolderBus." };

        if (newParentFolderBusTrackId.isNotEmpty() && !folderBus.isFolderBus(newParentFolderBusTrackId))
            return { false, "New parent is not a valid FolderBus." };

        if (movingFolderBusTrackId == newParentFolderBusTrackId)
            return { false, "A FolderBus cannot be moved into itself." };

        // New parent must not be a descendant of the moving bus (cycle)
        auto descendants = folderBus.getAllDescendants(movingFolderBusTrackId);
        for (auto& d : descendants)
            if (d == newParentFolderBusTrackId)
                return { false, "Moving this FolderBus would create a routing cycle." };

        return { true, {} };
    }
};

} // namespace DAW
