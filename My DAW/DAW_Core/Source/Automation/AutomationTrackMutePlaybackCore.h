#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationLaneStoreCore.h"
#include "AutomationModeStateCore.h"

#include <algorithm>
#include <cmath>

namespace apex::automation
{
    /**
        Read a persisted native Mute PPQ lane directly at DSP block boundaries.
        Boolean Mute is ALWAYS stepped/held (regardless of a malformed or
        legacy Linear/Smooth curve marker): never interpolate mute through 0.5.
        No locks, UI dispatch, heap allocation, or key construction.
    */
    inline bool getNativeTrackMuteRT (
        ParameterID id,
        bool fallbackMuted,
        double blockPPQ,
        bool transportRolling,
        bool automationMuted,
        const AutomationParameterRegistry& registry,
        const AutomationLaneStore& lanes,
        const AutomationModeState& modes) noexcept
    {
        if (id == kInvalidParameterID || !transportRolling
            || automationMuted || !std::isfinite(blockPPQ))
            return fallbackMuted;

        const auto mode = modes.getModeRT(id);
        if (mode == AutomationMode::Off)
            return fallbackMuted;

        const auto active = registry.findRT(id);
        if (active != nullptr
            && (active->isGestureActive()
                || ((mode == AutomationMode::Latch || mode == AutomationMode::Write)
                    && modes.latchHeldRT(id))))
        {
            const float v = active->getNormalizedValue();
            return std::isfinite(v) ? (v >= 0.5f) : fallbackMuted;
        }

        const auto lane = lanes.findLaneRT(id);
        if (lane == nullptr)
            return fallbackMuted;
        const auto points = lane->getSnapshot();
        if (points == nullptr || points->empty())
            return fallbackMuted;

        const auto upper = std::upper_bound(
            points->begin(), points->end(), blockPPQ,
            [] (double t, const Breakpoint& bp) { return t < bp.timePPQ; });
        const float value = (upper == points->begin()
                                ? points->front() : *(upper - 1)).normalizedValue;
        return std::isfinite(value) ? (value >= 0.5f) : fallbackMuted;
    }
}
