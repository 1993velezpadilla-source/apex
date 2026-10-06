#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * OutputRoutingEngine — routes the master bus output to hardware channels.
 *
 * Separates the concept of "where the master bus goes" from
 * "what the engineer hears" (which is the Control Room / Monitor section).
 *
 * Version 1: simple stereo pass-through to channels 0/1.
 * Future: supports multi-output hardware, surround, alternate outputs.
 */
class OutputRoutingEngine
{
public:
    struct OutputAssignment
    {
        juce::String name  = "Main Out";
        int          channelL = 0;
        int          channelR = 1;
    };

    void setMainOutput(int chL, int chR) noexcept
    {
        mainOut_.channelL = chL;
        mainOut_.channelR = chR;
    }

    const OutputAssignment& getMainOutput() const noexcept { return mainOut_; }

    /**
     * Routes the processed master signal to the hardware output buffer.
     * Called after MasterBusEngine and before ControlRoomEngine.
     */
    void routeToHardware(const float* masterL, const float* masterR,
                         juce::AudioBuffer<float>& hwOutput,
                         int startSample, int numSamples) noexcept
    {
        int chL = juce::jlimit(0, hwOutput.getNumChannels() - 1, mainOut_.channelL);
        int chR = juce::jlimit(0, hwOutput.getNumChannels() - 1, mainOut_.channelR);

        auto* outL = hwOutput.getWritePointer(chL, startSample);
        auto* outR = hwOutput.getWritePointer(chR, startSample);

        juce::FloatVectorOperations::copy(outL, masterL, numSamples);
        juce::FloatVectorOperations::copy(outR, masterR, numSamples);
    }

private:
    OutputAssignment mainOut_;
};

} // namespace DAW
