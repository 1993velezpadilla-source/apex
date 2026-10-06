#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * BubblegumSidechainTargetListCore — builds the list of valid sidechain destinations.
 *
 * A valid sidechain destination is any track that has at least one plugin in
 * its chain that declares more than one input bus (i.e. has a sidechain bus
 * at index 1+).
 *
 * In practice, at the Bubblegum routing level we do not load and inspect every
 * plugin's bus layout from the message thread during a UI rebuild.  Instead we
 * maintain a registry of "sidechain-capable" tracks that is updated whenever
 * a plugin is loaded or removed.  The initial fallback is: all non-source
 * tracks are valid destinations (same as BubblegumTargetListCore) — the user
 * picks the target, and when the sidechain connection is created the engine
 * discovers whether the plugin actually consumes it.
 *
 * Call markSidechainCapable() from PluginChainCore::onChainChanged to keep
 * the list accurate without a full scan on every panel open.
 */
class BubblegumSidechainTargetListCore
{
public:
    struct SidechainTarget
    {
        TrackID      trackId;
        juce::String trackName;
        bool         hasSidechainCapablePlugin = false;
    };

    // ── Registry (updated from message thread) ───────────────────────────────

    /** Mark a track as having (or not having) a plugin with a sidechain bus.
     *  Call from PluginChainCore::onChainChanged. */
    void markSidechainCapable(const TrackID& trackId, bool capable)
    {
        if (capable)
            capableTracks_.insert(trackId);
        else
            capableTracks_.erase(trackId);
    }

    bool isSidechainCapable(const TrackID& trackId) const
    {
        return capableTracks_.count(trackId) > 0;
    }

    // ── List build ──────────────────────────────────────────────────────────

    /**
     * Rebuild the target list.
     * All non-source tracks are included — sidechain-capable ones are flagged.
     * This matches Ableton's behaviour: you can create a sidechain routing to
     * any track and the plugin picks it up.
     */
    void rebuild(const TrackID& sourceId, const TrackManager& tracks)
    {
        targets_.clear();

        for (int i = 0; i < tracks.getNumTracks(); ++i)
        {
            auto* t = tracks.getTrack(i);
            if (!t || t->getID() == sourceId) continue;

            SidechainTarget entry;
            entry.trackId   = t->getID();
            entry.trackName = t->getName();
            entry.hasSidechainCapablePlugin = isSidechainCapable(t->getID());
            targets_.push_back(entry);
        }
    }

    const std::vector<SidechainTarget>& getTargets() const noexcept { return targets_; }
    int getCount() const noexcept { return (int)targets_.size(); }

    bool contains(const TrackID& id) const noexcept
    {
        for (auto& t : targets_)
            if (t.trackId == id) return true;
        return false;
    }

    void clear() { targets_.clear(); }

private:
    std::vector<SidechainTarget> targets_;
    std::set<TrackID>            capableTracks_;
};

} // namespace DAW
