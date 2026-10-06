#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * SoloLogicCore — central solo state resolution.
 *
 * Determines which tracks/buses are audible based on solo state.
 * Master is ALWAYS excluded from solo logic (master has no solo).
 * Solo-safe tracks/buses remain audible even when other tracks are soloed.
 *
 * Solo modes supported:
 *   - SIP (Solo In Place): default, mutes all non-soloed tracks
 *   - AFL (After Fader Listen): future, routes post-fader to monitor
 *   - PFL (Pre Fader Listen): future, routes pre-fader to monitor
 */
class SoloLogicCore
{
public:
    enum class SoloMode { SIP, AFL, PFL };

    void setSoloMode(SoloMode mode) noexcept { mode_ = mode; }
    SoloMode getSoloMode() const noexcept { return mode_; }

    /** Returns true if any regular (non-master) track is soloed. */
    bool isAnySoloed(const TrackManager& tracks) const
    {
        for (int i = 0; i < tracks.getNumTracks(); ++i)
            if (auto* t = tracks.getTrack(i); t && t->isSoloed())
                return true;
        return false;
    }

    /** Returns true if the given track should produce audio given current solo state. */
    bool isTrackAudible(const Track& track, bool anySoloed) const
    {
        // Master is always audible (never affected by solo)
        if (track.isMaster()) return !track.isMuted();

        if (track.isMuted()) return false;
        if (!anySoloed) return true;
        return track.isSoloed();
    }

private:
    SoloMode mode_ = SoloMode::SIP;
};

} // namespace DAW
