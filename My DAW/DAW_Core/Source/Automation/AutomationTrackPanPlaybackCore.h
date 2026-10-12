#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationLaneStoreCore.h"
#include "AutomationModeStateCore.h"

#include <cmath>

namespace apex::automation
{
    /**
        Native track pan evaluated in the audio callback, not through
        MixerStrip's 60 Hz message-thread listener. The persisted PPQ lane
        takes precedence over the legacy sample-domain pan lane while rolling.

        Only immutable snapshots and atomics are used here: no string key
        construction, map mutex, plugin callback or heap allocation.
    */
    inline float getNativeTrackPanRT (
        ParameterID id,
        float fallbackPan,
        double blockPPQ,
        bool transportRolling,
        bool automationMuted,
        const AutomationParameterRegistry& registry,
        const AutomationLaneStore& lanes,
        const AutomationModeState& modes) noexcept
    {
        if (id == kInvalidParameterID || !transportRolling
            || automationMuted || !std::isfinite(blockPPQ))
            return fallbackPan;

        const auto mode = modes.getModeRT(id);
        if (mode == AutomationMode::Off)
            return fallbackPan;

        // A live gesture takes priority immediately, before the next GUI tick.
        // Latch/Write hold their current value while the transport is rolling.
        const auto active = registry.findRT(id);
        if (active != nullptr
            && (active->isGestureActive()
                || ((mode == AutomationMode::Latch
                     || mode == AutomationMode::Write)
                    && modes.latchHeldRT(id))))
        {
            const float normalized = active->getNormalizedValue();
            return std::isfinite(normalized)
                ? juce::jlimit(-1.0f, 1.0f, normalized * 2.0f - 1.0f)
                : fallbackPan;
        }

        const auto lane = lanes.findLaneRT(id);
        if (lane == nullptr)
            return fallbackPan;
        const auto points = lane->getSnapshot();
        if (points == nullptr || points->empty())
            return fallbackPan;

        const float normalized = AutomationLane::evaluateAt(*points, blockPPQ);
        if (!std::isfinite(normalized))
            return fallbackPan;

        return juce::jlimit(-1.0f, 1.0f, normalized * 2.0f - 1.0f);
    }
}
