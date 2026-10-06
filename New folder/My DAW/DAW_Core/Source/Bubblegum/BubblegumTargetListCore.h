#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * BubblegumTargetListCore — builds the list of valid send targets.
 *
 * Rules:
 *   - Source track is excluded (no self-send, not even shown).
 *   - Master bus track is always first so it reads as the default output.
 *   - All other tracks follow in their natural order.
 *   - Rebuilds instantly on source change.
 */
class BubblegumTargetListCore
{
public:
    void rebuild(const TrackID& sourceId, const TrackManager& tracks)
    {
        targets_.clear();

        // Master bus always first — it lives outside the numbered tracks_ list
        if (auto* master = tracks.getMasterTrack())
            if (master->getID() != sourceId)
                targets_.push_back(master->getID());

        // All regular (non-master) tracks, excluding the source
        for (int i = 0; i < tracks.getNumTracks(); ++i)
        {
            auto* t = tracks.getTrack(i);
            if (t && t->getID() != sourceId)
                targets_.push_back(t->getID());
        }
    }

    const std::vector<TrackID>& getTargets() const noexcept { return targets_; }
    int getCount() const noexcept { return (int)targets_.size(); }

    bool contains(const TrackID& id) const noexcept
    {
        for (auto& t : targets_)
            if (t == id) return true;
        return false;
    }

    void clear() { targets_.clear(); }

private:
    std::vector<TrackID> targets_;
};

} // namespace DAW
