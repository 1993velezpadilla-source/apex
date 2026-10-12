#pragma once

#include "AutomationTypes.h"
#include "AutomationParameterRegistryCore.h"
#include "AutomationLaneStoreCore.h"
#include "AutomationModeStateCore.h"
#include "../MixerScaleCore/FaderRangeCore.h"

#include <cmath>

namespace apex::automation
{
    /**
        Audio-thread authority for the NEW PPQ-native track-volume lane.

        The legacy arrangement's sample-domain volume lane remains supported.
        When an APEX lane exists, it takes precedence over the legacy lane for
        this buffer. This explicitly connects recorded native fader gestures
        to the DSP volume ramp, without waiting for 60Hz mixer repaint/UI
        notifications. No String construction, lock, heap allocation or host
        callback is made from the audio thread.
    */
    inline float getNativeTrackVolumeGainRT (
        ParameterID id,
        float fallbackGain,
        double blockPPQ,
        bool transportRolling,
        const AutomationParameterRegistry& registry,
        const AutomationLaneStore& lanes,
        const AutomationModeState& modes,
        const DAW::FaderRangeCore& faderRange) noexcept
    {
        if (id == kInvalidParameterID || !transportRolling
            || !std::isfinite(blockPPQ))
            return fallbackGain;

        const auto mode = modes.getModeRT(id);
        if (mode == AutomationMode::Off)
            return fallbackGain;

        // A live user drag wins over playback; Latch and Write keep the last
        // touched value until the transport stops. Read the parameter atomic,
        // NOT Track::getVolume(): that UI value may lag by one 60Hz dispatch.
        const auto active = registry.findRT(id);
        if (active != nullptr
            && (active->isGestureActive()
                || ((mode == AutomationMode::Latch
                     || mode == AutomationMode::Write)
                    && modes.latchHeldRT(id))))
            return faderRange.normToGain(active->getNormalizedValue());

        const auto lane = lanes.findLaneRT(id);
        if (lane == nullptr)
            return fallbackGain;
        const auto points = lane->getSnapshot();
        if (points == nullptr || points->empty())
            return fallbackGain;

        const float normalized = AutomationLane::evaluateAt(*points, blockPPQ);
        if (!std::isfinite(normalized))
            return fallbackGain;
        return faderRange.normToGain(juce::jlimit(0.0f, 1.0f, normalized));
    }
}
