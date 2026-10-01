#pragma once
#include <JuceHeader.h>
#include "AutomationClipRegionCore.h"
#include <optional>

namespace DAW {

class AutomationClipClipboardCore
{
public:
    struct ClipState
    {
        juce::String sourceParameterId;
        int64_t sourceLengthSamples = 0;
        std::vector<AutomationPoint> normalisedPoints;
        juce::String name;
        juce::Colour color = juce::Colour(0xfff06bd8);
    };

    void copyRegionState(const AutomationClipRegionCore& region)
    {
        ClipState state;
        state.sourceParameterId = region.parameterId;
        state.sourceLengthSamples = juce::jmax<int64_t>(1, region.lengthSamples);
        state.name = region.name;
        state.color = region.color;
        state.normalisedPoints.reserve(region.localPoints.size());

        for (auto point : region.localPoints)
        {
            const double t = (double)juce::jlimit<int64_t>(0, state.sourceLengthSamples, point.timeSamples)
                           / (double)state.sourceLengthSamples;
            point.timeSamples = (int64_t)std::llround(t * 1000000.0);
            point.value = juce::jlimit(0.0f, 1.0f, point.value);
            state.normalisedPoints.push_back(point);
        }

        clipState_ = std::move(state);
    }

    bool hasClipState() const noexcept { return clipState_.has_value(); }
    void clear() noexcept { clipState_.reset(); }
    const std::optional<ClipState>& getClipState() const noexcept { return clipState_; }

    bool pasteStateToRegion(AutomationClipRegionCore& destination) const
    {
        if (!clipState_ || destination.lengthSamples <= 0)
            return false;

        destination.localPoints.clear();
        destination.localPoints.reserve(clipState_->normalisedPoints.size());
        for (auto point : clipState_->normalisedPoints)
        {
            const double t = (double)juce::jlimit<int64_t>(0, 1000000, point.timeSamples) / 1000000.0;
            point.timeSamples = (int64_t)std::llround(t * (double)destination.lengthSamples);
            destination.localPoints.push_back(point);
        }

        return true;
    }

private:
    std::optional<ClipState> clipState_;
};

} // namespace DAW
