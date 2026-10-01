#pragma once
#include "MultiSelectionCore.h"
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * SelectionBulkActionCore
 *
 * Stateless helpers that execute bulk operations on a MultiSelectionCore
 * using existing track/clip/plugin APIs. UI-thread only. No audio engine
 * changes, no RoutingGraph changes.
 */
struct SelectionBulkActionCore
{
    // ── Track bulk ────────────────────────────────────────────────────────

    /** Apply colour to all selected tracks. */
    static void applyColorToSelectedTracks(const MultiSelectionCore& sel,
                                           TrackManager& tm,
                                           juce::Colour colour)
    {
        for (const auto& t : sel.getSelected(SelectionKind::Track))
        {
            if (auto* track = tm.getTrack(t.trackId))
            {
                track->setColor(colour);
                // Propagate colour to clips on this track — mirrors
                // single-track behaviour in TrackLane::trackPropertyChanged.
                // If ClipManager access is needed, caller should also call
                // ClipManager colour sync. We only touch Track here.
            }
        }
    }

    /** Apply mute to all selected tracks. */
    static void applyMuteToSelectedTracks(const MultiSelectionCore& sel,
                                          TrackManager& tm, bool muted)
    {
        for (const auto& t : sel.getSelected(SelectionKind::Track))
            if (auto* track = tm.getTrack(t.trackId))
                track->setMuted(muted);
    }

    /** Apply solo to all selected tracks. */
    static void applySoloToSelectedTracks(const MultiSelectionCore& sel,
                                          TrackManager& tm, bool soloed)
    {
        for (const auto& t : sel.getSelected(SelectionKind::Track))
            if (auto* track = tm.getTrack(t.trackId))
                track->setSoloed(soloed);
    }

    /** Apply arm to all selected tracks. */
    static void applyArmToSelectedTracks(const MultiSelectionCore& sel,
                                         TrackManager& tm, bool armed)
    {
        for (const auto& t : sel.getSelected(SelectionKind::Track))
            if (auto* track = tm.getTrack(t.trackId))
                track->setArmed(armed);
    }

    /**
     * Rename all selected tracks using numbered suffix.
     * baseName = "Vocal" → Vocal 1, Vocal 2, Vocal 3 in stable track order.
     */
    static void applyNumberedRenameToSelectedTracks(const MultiSelectionCore& sel,
                                                    TrackManager& tm,
                                                    const juce::String& baseName)
    {
        // Build renaming list in track-manager order for stable numbering
        std::vector<Track*> ordered;
        const int n = tm.getNumTracks();
        for (int i = 0; i < n; ++i)
        {
            auto* tr = tm.getTrack(i);
            if (!tr || tr->isMaster()) continue;
            if (sel.contains(SelectionTarget::track(tr->getID())))
                ordered.push_back(tr);
        }
        int idx = 1;
        for (auto* tr : ordered)
            tr->setName(baseName + " " + juce::String(idx++));
    }

    /** Delete all selected tracks (skips master). Returns list of deleted IDs. */
    static std::vector<juce::String> deleteSelectedTracks(const MultiSelectionCore& sel,
                                                          TrackManager& tm)
    {
        std::vector<juce::String> ids;
        for (const auto& t : sel.getSelected(SelectionKind::Track))
            ids.push_back(t.trackId);
        return tm.deleteTracks(ids);
    }

    // ── Clip bulk ─────────────────────────────────────────────────────────

    /** Delete all selected clips. */
    static void deleteSelectedClips(const MultiSelectionCore& sel, ClipManager& cm)
    {
        std::vector<juce::String> ids;
        for (const auto& t : sel.getSelected(SelectionKind::Clip))
            ids.push_back(t.clipId);
        for (const auto& id : ids)
            cm.deleteClip(id);
    }

    /** Apply mute to all selected clips. */
    static void applyMuteToSelectedClips(const MultiSelectionCore& sel,
                                         ClipManager& cm, bool muted)
    {
        for (const auto& t : sel.getSelected(SelectionKind::Clip))
            if (auto* clip = cm.getClip(t.clipId))
                clip->setMuted(muted);
    }

    /** Apply colour to all selected clips. */
    static void applyColorToSelectedClips(const MultiSelectionCore& sel,
                                          ClipManager& cm, juce::Colour colour)
    {
        for (const auto& t : sel.getSelected(SelectionKind::Clip))
            if (auto* clip = cm.getClip(t.clipId))
                clip->setColor(colour);
    }

    // ── Plugin slot bulk ──────────────────────────────────────────────────

    /** Bypass / un-bypass selected plugin slots on a given chain. */
    static void applyBypassToSelectedPlugins(const MultiSelectionCore& sel,
                                             const juce::String& trackId,
                                             PluginChainCore& chain,
                                             bool bypassed)
    {
        for (const auto& t : sel.getSelected(SelectionKind::PluginSlot))
        {
            if (t.trackId != trackId) continue;
            if (t.pluginSlotIndex >= 0 && t.pluginSlotIndex < chain.getNumSlots())
                chain.setSlotBypassed(t.pluginSlotIndex, bypassed);
        }
    }

    /**
     * Remove selected plugin slots on a given chain.
     * Closes editors before removing. Processes in reverse index order to
     * avoid index shifting.
     */
    static void removeSelectedPlugins(const MultiSelectionCore& sel,
                                      const juce::String& trackId,
                                      PluginChainCore& chain)
    {
        std::vector<int> indices;
        for (const auto& t : sel.getSelected(SelectionKind::PluginSlot))
            if (t.trackId == trackId && t.pluginSlotIndex >= 0)
                indices.push_back(t.pluginSlotIndex);

        // Close editors first
        for (int idx : indices)
            if (auto* slot = chain.getSlot(idx))
                if (slot->isEditorOpen() || slot->isEditorMinimized())
                    slot->closeEditor();

        // Remove in reverse order
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int idx : indices)
            chain.removePlugin(idx);
    }
};

} // namespace DAW
