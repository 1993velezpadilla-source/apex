#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * BubblegumSourceSyncCore — auto-syncs bubblegum source to selected track.
 *
 * Rule: Selected Track = Bubblegum Source. No manual override.
 * Updates instantly on selection change.
 */
class BubblegumSourceSyncCore
{
public:
    void sync(const TrackID& selectedTrackId) noexcept
    {
        if (sourceTrackId_ == selectedTrackId) return;
        sourceTrackId_ = selectedTrackId;
        dirty_ = true;
    }

    const TrackID& getSourceTrackId() const noexcept { return sourceTrackId_; }

    bool consumeDirty() noexcept
    {
        if (!dirty_) return false;
        dirty_ = false;
        return true;
    }

    void clear() noexcept
    {
        sourceTrackId_ = {};
        dirty_ = true;
    }

private:
    TrackID sourceTrackId_;
    bool dirty_ = false;
};

} // namespace DAW
