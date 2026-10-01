#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * HardwareOutputCore — the ONLY component that sends audio to hardware.
 *
 * Receives the monitor path output and sends it to the audio device.
 * NO other component is allowed to talk directly to hardware.
 *
 * Architecture:
 *   Master → Monitor → HardwareOutputCore → Audio Device (ASIO/WASAPI/CoreAudio)
 *
 * Version 1: stereo output.
 * Future: multi-output, surround, multiple headphone outputs.
 */
class HardwareOutputCore
{
public:
    void setOutputChannels(int leftCh, int rightCh) noexcept
    {
        leftChannel_ = leftCh;
        rightChannel_ = rightCh;
    }

    int getLeftChannel() const noexcept { return leftChannel_; }
    int getRightChannel() const noexcept { return rightChannel_; }

    /** Route the monitor output to the hardware buffer.
     *  This is the FINAL step in the audio callback. */
    void writeToHardware(const float* monitorL, const float* monitorR,
                         juce::AudioBuffer<float>& hwBuffer,
                         int startSample, int numSamples) noexcept
    {
        if (hwBuffer.getNumChannels() < 1 || numSamples <= 0) return;

        int chL = juce::jlimit(0, hwBuffer.getNumChannels() - 1, leftChannel_);
        int chR = juce::jlimit(0, hwBuffer.getNumChannels() - 1, rightChannel_);

        auto* outL = hwBuffer.getWritePointer(chL, startSample);
        auto* outR = hwBuffer.getWritePointer(chR, startSample);

        juce::FloatVectorOperations::copy(outL, monitorL, numSamples);
        juce::FloatVectorOperations::copy(outR, monitorR, numSamples);
    }

private:
    int leftChannel_ = 0;
    int rightChannel_ = 1;
};

} // namespace DAW
