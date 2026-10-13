#pragma once
#include "AutomationManagerCore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace DAW {

/**
 * Transaction-ready preparation/apply for a group of EXISTING track automation
 * lanes. Tracks are supplied explicitly by the GUI (no implicit all-tracks
 * changes). The batch carries both BEFORE and AFTER per-target lane states,
 * so its undo/redo never restores unrelated tracks or the whole project.
 *
 * Message thread only. The real audio thread reads the AutomationManagerCore
 * immutable published snapshot as before.
 *
 * V1 deliberately supports native volume and pan only. VST3 plugin IDs encode
 * individual instance identity and require explicit target mapping; never
 * scatter one plug-in parameter ID across unrelated tracks by accident.
 */
struct AutomationGroupedCurveCore
{
    struct LaneState
    {
        TrackID trackId;
        juce::String parameterId;
        juce::ValueTree before;
        juce::ValueTree after;
    };

    struct Batch
    {
        std::vector<LaneState> lanes;

        bool isValid() const noexcept { return !lanes.empty(); }
    };

    static constexpr size_t maxTargets = 128;
    static constexpr size_t maxPoints = 32768;

    static bool isSupportedParameter(const juce::String& parameterId) noexcept
    {
        return parameterId == AutomationLaneCore::trackVolumeParameterId
            || parameterId == AutomationLaneCore::trackPanParameterId;
    }

    // Stage a complete curve replacement, with no mutations or snapshots
    // published until the entire multi-target operation can commit.
    static bool prepare(const AutomationManagerCore& manager,
                        const juce::String& parameterId,
                        const std::vector<TrackID>& selectedTrackIds,
                        const std::vector<AutomationPoint>& source,
                        Batch& result)
    {
        if (!isSupportedParameter(parameterId) || selectedTrackIds.empty()
            || selectedTrackIds.size() > maxTargets || source.empty()
            || source.size() > maxPoints)
            return false;

        std::vector<AutomationPoint> clean;
        clean.reserve(source.size());
        int64_t previous = -1;
        for (auto point : source)
        {
            if (point.timeSamples < 0 || point.timeSamples <= previous
                || !std::isfinite(point.value) || !std::isfinite(point.tensionToNext))
                return false;
            previous = point.timeSamples;
            point.value = AutomationLaneCore::clampValueForParameter(parameterId, point.value);
            point.tensionToNext = juce::jlimit(-1.0f, 1.0f, point.tensionToNext);
            clean.push_back(point);
        }

        Batch staged;
        staged.lanes.reserve(selectedTrackIds.size());
        for (const auto& track : selectedTrackIds)
        {
            if (track.isEmpty())
                return false;
            for (const auto& earlier : staged.lanes)
                if (earlier.trackId == track)
                    return false; // reject repeated targets; never edit twice

            const auto* existing = manager.findLane(track, parameterId);
            if (existing == nullptr || !existing->isEnabled())
                return false; // all-or-nothing and no phantom lane creation

            // AutomationClipRegions own their section of the lane and are
            // overlaid again when the manager publishes a snapshot. Refuse
            // this edit until group+region ownership is handled explicitly:
            // accepting it would display a curve that DSP silently ignores.
            for (const auto& region : manager.getClipRegions())
                if (region.trackId == track && region.parameterId == parameterId)
                    return false;

            AutomationLaneCore replacement = *existing;
            replacement.points = clean;
            replacement.sortAndResolveDuplicates();

            LaneState entry;
            entry.trackId = track;
            entry.parameterId = parameterId;
            entry.before = existing->getState();
            entry.after = replacement.getState();
            staged.lanes.push_back(std::move(entry));
        }

        result = std::move(staged);
        return true;
    }

    // Apply or undo a batch; validates ALL targets before writing the first.
    // One publish, one coherent audio-thread-visible snapshot.
    static bool apply(AutomationManagerCore& manager, const Batch& batch, bool useAfter)
    {
        if (batch.lanes.empty() || batch.lanes.size() > maxTargets)
            return false;

        for (size_t i = 0; i < batch.lanes.size(); ++i)
        {
            const auto& entry = batch.lanes[i];
            if (!isSupportedParameter(entry.parameterId)
                || entry.trackId.isEmpty()
                || !entry.before.isValid() || !entry.after.isValid()
                || !entry.before.hasType("AutomationLane")
                || !entry.after.hasType("AutomationLane")
                || entry.before.getProperty("parameterId").toString() != entry.parameterId
                || entry.after.getProperty("parameterId").toString() != entry.parameterId
                || manager.findLane(entry.trackId, entry.parameterId) == nullptr)
                return false;

            // A clip region added after the batch was created must also block
            // its undo/redo rather than overwrite clip-authored automation.
            for (const auto& region : manager.getClipRegions())
                if (region.trackId == entry.trackId
                    && region.parameterId == entry.parameterId)
                    return false;

            for (size_t previous = 0; previous < i; ++previous)
                if (entry.trackId == batch.lanes[previous].trackId
                    && entry.parameterId == batch.lanes[previous].parameterId)
                    return false;
        }

        // Lookup and stage copies before applying to prevent partial updates
        // even if one malformed ValueTree is somehow passed in.
        std::vector<AutomationLaneCore> replacements;
        replacements.reserve(batch.lanes.size());
        for (const auto& entry : batch.lanes)
        {
            AutomationLaneCore copy = *manager.findLane(entry.trackId, entry.parameterId);
            copy.restoreState(useAfter ? entry.after : entry.before);
            if (copy.getParameterId() != entry.parameterId)
                return false;
            replacements.push_back(std::move(copy));
        }

        for (size_t i = 0; i < batch.lanes.size(); ++i)
        {
            const auto& entry = batch.lanes[i];
            // This never creates a new lane: all were just validated.
            manager.getOrCreateLane(entry.trackId, entry.parameterId)
                = std::move(replacements[i]);
        }

        manager.publishSnapshot();
        return true;
    }
};

} // namespace DAW
