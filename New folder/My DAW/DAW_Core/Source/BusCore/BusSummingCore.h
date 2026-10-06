#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BusSummingCore — audio buffer summing engine for buses.
 *
 * Provides efficient buffer-level summing operations used by
 * the AudioEngine when combining multiple source signals into
 * a bus or master node.
 *
 * Thread safety: all methods are audio-thread-safe (no allocation).
 */
class BusSummingCore
{
public:
    /** Sum source buffer into destination buffer (additive).
     *  Both buffers must have at least numSamples available. */
    static void sumInto(float* destL, float* destR,
                        const float* srcL, const float* srcR,
                        int numSamples, float gain = 1.0f) noexcept
    {
        if (std::abs(gain - 1.0f) < 0.0001f)
        {
            juce::FloatVectorOperations::add(destL, srcL, numSamples);
            juce::FloatVectorOperations::add(destR, srcR, numSamples);
        }
        else
        {
            juce::FloatVectorOperations::addWithMultiply(destL, srcL, gain, numSamples);
            juce::FloatVectorOperations::addWithMultiply(destR, srcR, gain, numSamples);
        }
    }

    /** Sum a JUCE AudioBuffer into dest pointers. */
    static void sumBufferInto(float* destL, float* destR,
                              const juce::AudioBuffer<float>& src,
                              int numSamples, float gain = 1.0f) noexcept
    {
        const float* srcL = src.getReadPointer(0);
        const float* srcR = src.getNumChannels() > 1
                            ? src.getReadPointer(1) : srcL;
        sumInto(destL, destR, srcL, srcR, numSamples, gain);
    }

    /** Apply gain in-place. */
    static void applyGain(float* L, float* R, int numSamples, float gain) noexcept
    {
        if (gain <= 0.0f)
        {
            juce::FloatVectorOperations::clear(L, numSamples);
            juce::FloatVectorOperations::clear(R, numSamples);
        }
        else if (std::abs(gain - 1.0f) > 0.0001f)
        {
            juce::FloatVectorOperations::multiply(L, gain, numSamples);
            juce::FloatVectorOperations::multiply(R, gain, numSamples);
        }
    }
};

} // namespace DAW
