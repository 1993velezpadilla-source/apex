#pragma once
#include "../UtilityCore/Types.h"

namespace bubblegum
{
    /**
     * Quick Send mode state core.
     *
     * UI-side mode state only — it never touches the routing graph or the
     * audio engine. All topology mutations are performed by the caller
     * (MainComponent) through BubblegumV2System's undo-wrapped API
     * (handleTargetTapFrom / toggleSendActiveFrom / removeSendFrom).
     *
     * Semantics:
     *   - Double-click a track row body  → enter mode with that track as source.
     *   - Click another track row        → create a send source → target.
     *   - Click the source row again     → exit mode.
     *   - Escape                          → exit mode.
     *   - Delete                          → delete send source → selected track.
     */
    class QuickSendModeCore
    {
    public:
        bool isActive() const noexcept { return active_; }

        DAW::TrackID getSourceTrackId() const noexcept { return sourceTrackId_; }

        bool isSourceTrack(const DAW::TrackID& id) const noexcept
        {
            return active_ && id == sourceTrackId_;
        }

        void enter(const DAW::TrackID& source)
        {
            sourceTrackId_ = source;
            active_        = source.isNotEmpty();
        }

        void exit()
        {
            active_        = false;
            sourceTrackId_.clear();
        }

    private:
        bool          active_ = false;
        DAW::TrackID  sourceTrackId_;
    };
}
