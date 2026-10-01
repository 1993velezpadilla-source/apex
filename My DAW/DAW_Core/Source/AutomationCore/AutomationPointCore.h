#pragma once
#include <JuceHeader.h>
#include "AutomationCurveEvalCore.h"
#include <cstdint>

namespace DAW {

struct AutomationPoint
{
    int64_t timeSamples = 0;
    float value = 1.0f;
    AutomationCurveType curveToNext = AutomationCurveType::Linear;
    float tensionToNext = 0.0f;
};

/**
 * AutomationPointCore — a single automation data point.
 *
 * Represents one value at a specific timeline position.
 * Points are the building blocks of automation lanes.
 */
struct AutomationPointCore
{
    juce::int64 position = 0;   // timeline position in samples
    float value = 0.0f;         // normalized 0..1 or raw parameter value
    bool selected = false;      // UI selection state

    bool operator<(const AutomationPointCore& other) const noexcept
    {
        return position < other.position;
    }
};

} // namespace DAW
