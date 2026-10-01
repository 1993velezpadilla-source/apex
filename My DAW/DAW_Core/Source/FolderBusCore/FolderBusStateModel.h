#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * FolderBusSnapshot — pre-baked per-block state published by the message thread.
 *
 * The audio engine reads only this snapshot, never traversing the tree at render time.
 * Rebuilt on the message thread whenever any mute/solo state or topology changes.
 */
struct FolderBusSnapshot
{
    // Effective mute set: a track is in here if it or any ancestor is muted.
    std::unordered_set<TrackID> effectivelyMutedTracks;

    // Effective solo set: empty = no solo active (all tracks pass).
    // Non-empty: only tracks in this set are audible.
    std::unordered_set<TrackID> effectiveSoloSet;
};

/**
 * FolderBusStateModel — thread-safe snapshot holder.
 *
 * Message thread writes via update(). Audio thread reads via get().
 * Uses a spin-free double-buffer approach: publish atomically replaces
 * the shared_ptr so reads and writes never contend.
 */
class FolderBusStateModel
{
public:
    FolderBusStateModel()
        : snapshot_(std::make_shared<FolderBusSnapshot>()) {}

    /** Message thread: publish a new snapshot. The replaced snapshot is held
     *  in the retired slot until the next update() on this thread, so the
     *  audio thread is never the last owner of a retired snapshot. */
    void update(FolderBusSnapshot newSnapshot)
    {
        auto ptr = std::make_shared<FolderBusSnapshot>(std::move(newSnapshot));
        auto oldSnap = std::atomic_exchange_explicit(&snapshot_, ptr, std::memory_order_acq_rel);
        retiredSnapshot_ = std::move(oldSnap);
    }

    /** Audio thread: get the current snapshot. Lock-free. */
    std::shared_ptr<const FolderBusSnapshot> get() const
    {
        return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
    }

private:
    std::shared_ptr<FolderBusSnapshot> snapshot_;
    std::shared_ptr<FolderBusSnapshot> retiredSnapshot_;   // message-thread retire slot
};

} // namespace DAW
