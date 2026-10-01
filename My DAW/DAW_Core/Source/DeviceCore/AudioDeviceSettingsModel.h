#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * AudioDeviceSettingsModel — state model for audio device configuration.
 *
 * Stores the user's selected driver type, device, sample rate, buffer size,
 * and reports latency. Separate from the actual JUCE AudioDeviceManager
 * so the UI can read/display settings without touching the device.
 */
struct AudioDeviceSettingsModel
{
    juce::String driverType     = "Windows Audio";   // ASIO, WASAPI, DirectSound
    juce::String outputDevice   = "";
    juce::String inputDevice    = "";
    double       sampleRate     = 44100.0;
    int          bufferSize     = 512;
    double       inputLatencyMs = 0.0;
    double       outputLatencyMs = 0.0;

    double getRoundTripLatencyMs() const noexcept
    {
        return inputLatencyMs + outputLatencyMs;
    }

    /**
     * Populates this model from a live JUCE AudioDeviceManager.
     */
    void readFromDevice(juce::AudioDeviceManager& deviceManager)
    {
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            driverType     = device->getTypeName();
            outputDevice   = device->getName();
            sampleRate     = device->getCurrentSampleRate();
            bufferSize     = device->getCurrentBufferSizeSamples();
            inputLatencyMs = device->getInputLatencyInSamples() / juce::jmax(1.0, sampleRate) * 1000.0;
            outputLatencyMs = device->getOutputLatencyInSamples() / juce::jmax(1.0, sampleRate) * 1000.0;
        }
    }
};

} // namespace DAW
