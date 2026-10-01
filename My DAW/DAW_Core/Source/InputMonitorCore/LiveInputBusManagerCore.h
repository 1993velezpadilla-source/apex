#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * Accumulates the stereo live-monitor mix bus.
 *
 * Pre-allocated for the worst-case block size so processBlock never
 * heap-allocates. Prevents crashes when buffer size changes (e.g. 2048).
 */
class LiveInputBusManagerCore
{
public:
    static constexpr int maxBlockSize = 8192;

    void prepare(int blockSize) noexcept
    {
        const int allocSize = juce::jmax(blockSize, maxBlockSize);
        mixBus_.setSize(2, allocSize, false, true, true);
    }

    void clear(int numSamples) noexcept
    {
        jassert(numSamples <= mixBus_.getNumSamples());
        mixBus_.clear(0, juce::jmin(numSamples, mixBus_.getNumSamples()));
        contributorCount_ = 0;
    }

    void addTrackContribution(const float* L, const float* R, int numSamples) noexcept
    {
        if (numSamples <= 0) return;
        jassert(numSamples <= mixBus_.getNumSamples());
        const int safe = juce::jmin(numSamples, mixBus_.getNumSamples());
        if (L) juce::FloatVectorOperations::add(mixBus_.getWritePointer(0), L, safe);
        if (R) juce::FloatVectorOperations::add(mixBus_.getWritePointer(1), R, safe);
        ++contributorCount_;
    }

    void sumIntoOutput(float* outL, float* outR, int numSamples) const noexcept
    {
        if (contributorCount_ == 0 || numSamples <= 0) return;
        const int safe = juce::jmin(numSamples, mixBus_.getNumSamples());
        if (safe <= 0) return;
        if (outL) juce::FloatVectorOperations::add(outL, mixBus_.getReadPointer(0), safe);
        if (outR) juce::FloatVectorOperations::add(outR, mixBus_.getReadPointer(1), safe);
    }

    int getContributorCount() const noexcept { return contributorCount_; }

private:
    juce::AudioBuffer<float> mixBus_;
    int contributorCount_ { 0 };
};

} // namespace DAW
