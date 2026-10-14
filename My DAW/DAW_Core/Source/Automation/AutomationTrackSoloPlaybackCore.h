#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationLaneStoreCore.h"
#include "AutomationModeStateCore.h"
#include "../FolderBusCore/FolderBusStateModel.h"
#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstddef>

namespace apex::automation
{
    // No heap storage on audio callback. Beyond 4096 tracks, sources are
    // still evaluated, using a slower exact overflow path.
    inline constexpr std::size_t kNativeSoloFastCapacity = 4096;
    struct NativeSoloBlockState
    {
        std::bitset<kNativeSoloFastCapacity> active;
        bool anyActive = false;
        double ppq = 0.0;
        bool rolling = false;
    };

    // Discrete held Solo values, regardless of imported curve interpolation.
    inline bool getNativeTrackSoloRT(
        ParameterID id, bool manualSolo, double ppq, bool rolling,
        bool automationMuted, const AutomationParameterRegistry& registry,
        const AutomationLaneStore& lanes, const AutomationModeState& modes) noexcept
    {
        if (id == kInvalidParameterID || !rolling || automationMuted
            || !std::isfinite(ppq))
            return manualSolo;
        const auto mode = modes.getModeRT(id);
        if (mode == AutomationMode::Off)
            return manualSolo;
        const auto live = registry.findRT(id);
        if (live != nullptr
            && (live->isGestureActive()
                || ((mode == AutomationMode::Latch || mode == AutomationMode::Write)
                    && modes.latchHeldRT(id))))
        {
            const float value = live->getNormalizedValue();
            return std::isfinite(value) ? value >= 0.5f : manualSolo;
        }
        const auto lane = AutomationLaneStore::findLaneInSnapshotRT(lanes.getEffectiveLaneSnapshotRT(), id);
        if (lane == nullptr)
            return manualSolo;
        const auto points = lane->getSnapshot();
        if (points == nullptr || points->empty())
            return manualSolo;
        const auto upper = std::upper_bound(
            points->begin(), points->end(), ppq,
            [] (double t, const Breakpoint& bp) { return t < bp.timePPQ; });
        const float value = (upper == points->begin()
                               ? points->front() : *(upper - 1)).normalizedValue;
        return std::isfinite(value) ? value >= 0.5f : manualSolo;
    }

    // Flat/legacy routing has no FolderBusSnapshot membership table. Evaluate
    // its native Solo sources directly rather than silently falling back to
    // manual Track::isSoloed(). Caller supplies sources without allocating.
    struct NativeFlatSoloSource
    {
        ParameterID parameterID = kInvalidParameterID;
        bool manualSoloed = false;
        bool automationMuted = false;
    };

    template <typename SourceAtIndex>
    inline bool hasNativeFlatSoloRT(
        int count, SourceAtIndex&& sourceAt, double ppq, bool rolling,
        const AutomationParameterRegistry& registry,
        const AutomationLaneStore& lanes, const AutomationModeState& modes) noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            const auto src = sourceAt(i);
            if (getNativeTrackSoloRT(src.parameterID, src.manualSoloed,
                                     ppq, rolling, src.automationMuted,
                                     registry, lanes, modes))
                return true;
        }
        return false;
    }

    inline bool isNativeFlatSoloAudibleRT(
        const NativeFlatSoloSource& src, bool anySolo,
        double ppq, bool rolling,
        const AutomationParameterRegistry& registry,
        const AutomationLaneStore& lanes, const AutomationModeState& modes) noexcept
    {
        return !anySolo || getNativeTrackSoloRT(
            src.parameterID, src.manualSoloed, ppq, rolling,
            src.automationMuted, registry, lanes, modes);
    }

    inline NativeSoloBlockState evaluateNativeSoloBlockRT(
        const DAW::FolderBusSnapshot* snap, double ppq, bool rolling,
        const AutomationParameterRegistry& registry,
        const AutomationLaneStore& lanes, const AutomationModeState& modes) noexcept
    {
        NativeSoloBlockState result;
        result.ppq = ppq;
        result.rolling = rolling;
        if (snap == nullptr)
            return result;
        const auto& sources = snap->soloSources;
        const auto fast = std::min(sources.size(), kNativeSoloFastCapacity);
        for (std::size_t i = 0; i < fast; ++i)
        {
            const auto& src = sources[i];
            const bool active = getNativeTrackSoloRT(
                src.parameterID, src.manualSoloed, ppq, rolling,
                src.automationMuted, registry, lanes, modes);
            result.active.set(i, active);
            result.anyActive = result.anyActive || active;
        }
        // Overflow sources cannot be ignored when they solo something.
        for (std::size_t i = fast; i < sources.size(); ++i)
        {
            const auto& src = sources[i];
            if (getNativeTrackSoloRT(src.parameterID, src.manualSoloed, ppq,
                                     rolling, src.automationMuted,
                                     registry, lanes, modes))
            {
                result.anyActive = true;
                break;
            }
        }
        return result;
    }

    inline bool isNativeSoloAudibleRT(
        const DAW::TrackID& trackID, const DAW::FolderBusSnapshot* snap,
        const NativeSoloBlockState& block,
        const AutomationParameterRegistry& registry,
        const AutomationLaneStore& lanes, const AutomationModeState& modes) noexcept
    {
        if (snap == nullptr || !block.anyActive)
            return true;
        const auto it = snap->soloAudibleSourceIndices.find(trackID);
        if (it == snap->soloAudibleSourceIndices.end())
            return false;
        for (const auto i : it->second)
        {
            if (i < kNativeSoloFastCapacity)
            {
                if (block.active.test(i))
                    return true;
            }
            else if (i < snap->soloSources.size())
            {
                const auto& src = snap->soloSources[i];
                if (getNativeTrackSoloRT(src.parameterID, src.manualSoloed,
                                         block.ppq, block.rolling,
                                         src.automationMuted,
                                         registry, lanes, modes))
                    return true;
            }
        }
        return false;
    }
}
