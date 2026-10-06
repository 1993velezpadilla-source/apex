#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * AutomationSmoothingCore — prevents zipper noise on automated parameters.
 *
 * Audio-rate parameters (volume, pan, filter cutoff) need smooth
 * transitions between automation values to avoid clicks/zips.
 * This core provides per-sample smoothed output.
 */
class AutomationSmoothingCore
{
public:
    void prepare(double sampleRate) noexcept
    {
        smoothed_.reset(sampleRate, 0.005); // 5 ms ramp
    }

    void setTargetValue(float value) noexcept
    {
        smoothed_.setTargetValue(value);
    }

    void setCurrentAndTarget(float value) noexcept
    {
        smoothed_.setCurrentAndTargetValue(value);
    }

    float getNextValue() noexcept
    {
        return smoothed_.getNextValue();
    }

    float getCurrentValue() const noexcept
    {
        return smoothed_.getCurrentValue();
    }

    bool isSmoothing() const noexcept
    {
        return smoothed_.isSmoothing();
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothed_;
};

} // namespace DAW
