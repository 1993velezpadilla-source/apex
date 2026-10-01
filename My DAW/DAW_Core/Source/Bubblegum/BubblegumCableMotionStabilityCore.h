#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include <vector>
#include <cmath>
#include "BubblegumCableTypes.h"
#include "BubblegumCableStyleSettingsCore.h"

namespace bubblegum
{
    class BubblegumCableMotionStabilityCore
    {
    public:
        struct StableEndpoints
        {
            juce::Point<float> source;
            juce::Point<float> destination;
            bool initialized  = false;
            int  framesAlive  = 0;   // 0 = just added this frame, skip render; 1+ = render
        };

        void reset();

        std::vector<CableWorldSnapshot> resolveStableSnapshots(
            const std::vector<CableWorldSnapshot>& snapshots,
            float dt,
            const BubblegumCableStyleSettingsCore::Style& style,
            bool snapToTargets = false);

    private:
        std::unordered_map<CableKey, StableEndpoints, CableKeyHash> stableMap;

        juce::Point<float> smoothPoint(
            const juce::Point<float>& current,
            const juce::Point<float>& target,
            float dt,
            float smoothingHz,
            float snapDistance,
            float maxVelocityPxPerSec) const noexcept;
    };
}
