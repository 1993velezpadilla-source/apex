#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "BubblegumCableTypes.h"
#include "BubblegumCableStyleSettingsCore.h"

namespace bubblegum
{
    class BubblegumCableHangingCore
    {
    public:
        struct HangingResult
        {
            juce::Point<float> controlA;
            juce::Point<float> controlB;
            float sagAmount = 0.0f;
        };

        HangingResult solve(
            const juce::Point<float>& source,
            const juce::Point<float>& destination,
            const BubblegumCableStyleSettingsCore::Style& style) const noexcept;
    };
}
