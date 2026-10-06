#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../UtilityCore/Types.h"
#include "FolderBusCore.h"
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * FolderBusMuteCore — computes effective mute state across nested FolderBus trees.
 *
 * A track is effectively muted if it is muted OR any ancestor FolderBus is muted.
 * Parent mute stacks additively — child own-mute state is never modified.
 * All tree traversal happens on the message thread only.
 */
class FolderBusMuteCore
{
public:
    std::unordered_set<TrackID> buildEffectiveMuteSet(
        const TrackManager&  tracks,
        const FolderBusCore& folderBus) const
    {
        std::unordered_set<TrackID> result;

        auto checkNode = [&](const TrackID& trackId)
        {
            auto* t = tracks.getTrack(trackId);
            if (!t) return;
            if (t->isMuted()) { result.insert(trackId); return; }
            for (auto& ancestorId : folderBus.getAncestorChain(trackId))
            {
                auto* a = tracks.getTrack(ancestorId);
                if (!a)
                {
                    // ancestor is a FolderBus track — look it up via master as well
                    if (tracks.hasMasterTrack() && tracks.getMasterTrack()->getID() == ancestorId)
                    { if (tracks.getMasterTrack()->isMuted()) { result.insert(trackId); return; } }
                    continue;
                }
                if (a->isMuted()) { result.insert(trackId); return; }
            }
        };

        for (int i = 0; i < tracks.getNumTracks(); ++i)
            if (auto* t = tracks.getTrack(i))
                checkNode(t->getID());
        if (tracks.hasMasterTrack())
            checkNode(tracks.getMasterTrack()->getID());

        return result;
    }

    bool isEffectivelyMuted(const TrackID& trackId,
                             const std::unordered_set<TrackID>& effectiveMuteSet) const
    {
        return effectiveMuteSet.count(trackId) > 0;
    }
};

} // namespace DAW
