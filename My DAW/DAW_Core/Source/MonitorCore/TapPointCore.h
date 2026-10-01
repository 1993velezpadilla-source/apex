#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * TapPointCore — audio signal tap for monitoring/metering.
 *
 * A tap point captures a copy of the audio signal at a specific
 * point in the signal chain without modifying the original signal.
 *
 * Used for AFL/PFL, metering, spectrum analysis, etc.
 */
class TapPointCore
{
public:
    void prepare(int numChannels, int blockSize)
    {
        tapBuffer_.setSize(numChannels, blockSize);
        tapBuffer_.clear();
        prepared_ = true;
    }

    void releaseResources()
    {
        tapBuffer_.setSize(0, 0);
        prepared_ = false;
    }

    /** Capture a copy of the signal at this tap point. */
    void capture(const float* L, const float* R, int numSamples)
    {
        if (!prepared_ || tapBuffer_.getNumSamples() < numSamples) return;
        juce::FloatVectorOperations::copy(tapBuffer_.getWritePointer(0), L, numSamples);
        if (tapBuffer_.getNumChannels() > 1)
            juce::FloatVectorOperations::copy(tapBuffer_.getWritePointer(1), R, numSamples);
        hasCaptured_ = true;
    }

    const juce::AudioBuffer<float>& getBuffer() const noexcept { return tapBuffer_; }
    bool hasCaptured() const noexcept { return hasCaptured_; }
    void clearCapture() { tapBuffer_.clear(); hasCaptured_ = false; }

private:
    juce::AudioBuffer<float> tapBuffer_;
    bool prepared_ = false;
    bool hasCaptured_ = false;
};

} // namespace DAW
