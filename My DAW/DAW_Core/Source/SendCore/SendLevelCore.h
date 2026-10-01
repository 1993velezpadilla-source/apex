#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * SendLevelCore — manages send level with smoothing for click-free changes.
 *
 * Used by the audio engine to apply send gain without zipper noise.
 */
class SendLevelCore
{
public:
    void prepare(double sampleRate) noexcept
    {
        smoothedLevel_.reset(sampleRate, 0.010); // 10 ms ramp
        smoothedLevel_.setCurrentAndTargetValue(targetLevel_);
    }

    void setLevel(float level) noexcept
    {
        targetLevel_ = juce::jlimit(0.0f, 2.0f, level);
        smoothedLevel_.setTargetValue(targetLevel_);
    }

    float getLevel() const noexcept { return targetLevel_; }

    float getNextSmoothedValue() noexcept
    {
        return smoothedLevel_.getNextValue();
    }

    bool isSmoothing() const noexcept
    {
        return smoothedLevel_.isSmoothing();
    }

private:
    float targetLevel_ = 1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLevel_;
};

} // namespace DAW
