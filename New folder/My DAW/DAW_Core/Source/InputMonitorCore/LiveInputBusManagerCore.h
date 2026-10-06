#pragma once
#include <JuceHeader.h>

namespace DAW {

/** Accumulates the stereo live-monitor mix bus. */
class LiveInputBusManagerCore
{
public:
    void prepare(int blockSize) noexcept
    {
        mixBus_.setSize(2, juce::jmax(1, blockSize), false, true, true);
    }

    void clear(int numSamples) noexcept
    {
        ensureCapacity(numSamples);
        mixBus_.clear(0, numSamples);
        contributorCount_ = 0;
    }

    void addTrackContribution(const float* L, const float* R, int numSamples) noexcept
    {
        if (numSamples <= 0) return;
        ensureCapacity(numSamples);
        if (L) juce::FloatVectorOperations::add(mixBus_.getWritePointer(0), L, numSamples);
        if (R) juce::FloatVectorOperations::add(mixBus_.getWritePointer(1), R, numSamples);
        ++contributorCount_;
    }

    void sumIntoOutput(float* outL, float* outR, int numSamples) const noexcept
    {
        if (contributorCount_ == 0 || numSamples <= 0) return;
        if (numSamples > mixBus_.getNumSamples()) return;
        if (outL) juce::FloatVectorOperations::add(outL, mixBus_.getReadPointer(0), numSamples);
        if (outR) juce::FloatVectorOperations::add(outR, mixBus_.getReadPointer(1), numSamples);
    }

    int getContributorCount() const noexcept { return contributorCount_; }

private:
    void ensureCapacity(int numSamples) noexcept
    {
        if (numSamples > mixBus_.getNumSamples())
            mixBus_.setSize(2, numSamples, false, false, true);
    }

    juce::AudioBuffer<float> mixBus_;
    int contributorCount_ { 0 };
};

} // namespace DAW
