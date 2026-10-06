#pragma once
#include <JuceHeader.h>
#include <unordered_set>
#include <vector>
#include "../UtilityCore/Types.h"
#include "FolderBusCore.h"
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * FolderBusSoloCore — computes effective solo set across nested FolderBus trees.
 *
 * Rules:
 *  - Solo on a leaf → leaf + all ancestors audible (so signal can flow up to Master).
 *  - Solo on a FolderBus → that bus + all its descendants audible.
 *  - Master is always audible.
 *  - Empty effective set means no solo is active (all tracks audible).
 */
class FolderBusSoloCore
{
public:
    std::unordered_set<TrackID> buildEffectiveSoloSet(
        const TrackManager&  tracks,
        const FolderBusCore& folderBus) const
    {
        std::unordered_set<TrackID> explicitlySoloed;
        for (int i = 0; i < tracks.getNumTracks(); ++i)
            if (auto* t = tracks.getTrack(i); t && t->isSoloed())
                explicitlySoloed.insert(t->getID());
        if (tracks.hasMasterTrack() && tracks.getMasterTrack()->isSoloed())
            explicitlySoloed.insert(tracks.getMasterTrack()->getID());

        if (explicitlySoloed.empty()) return {};

        std::unordered_set<TrackID> effective;
        for (auto& soloedId : explicitlySoloed)
        {
            effective.insert(soloedId);
            // All ancestors must pass signal upward
            for (auto& aid : folderBus.getAncestorChain(soloedId))
                effective.insert(aid);
            // If the soloed node is a FolderBus, all its descendants are audible
            if (folderBus.isFolderBus(soloedId))
                for (auto& did : folderBus.getAllDescendants(soloedId))
                    effective.insert(did);
        }
        // Master is always audible
        if (tracks.hasMasterTrack())
            effective.insert(tracks.getMasterTrack()->getID());

        return effective;
    }

    bool isAudible(const TrackID& trackId,
                   const std::unordered_set<TrackID>& effectiveSoloSet) const
    {
        if (effectiveSoloSet.empty()) return true;
        return effectiveSoloSet.count(trackId) > 0;
    }
};

} // namespace DAW
