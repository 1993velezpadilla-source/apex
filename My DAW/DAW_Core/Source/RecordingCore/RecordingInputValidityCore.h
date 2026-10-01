#pragma once
#include <JuceHeader.h>

namespace DAW {

struct RecordingInputValidityCore
{
    static int clampCallbackChannels (int callbackChannels,
                                      int preparedChannels) noexcept
    {
        return juce::jlimit (0, juce::jmax (0, preparedChannels),
                            juce::jmax (0, callbackChannels));
    }

    static bool isRouteAvailable (int firstChannel,
                                  bool mono,
                                  int validChannels) noexcept
    {
        if (firstChannel < 0 || validChannels <= 0 || firstChannel >= validChannels)
            return false;
        return mono || (firstChannel + 1 < validChannels);
    }

    static void clearInvalidTailChannels (juce::AudioBuffer<float>& buffer,
                                          int validChannels,
                                          int startSample,
                                          int numSamples) noexcept
    {
        const int firstInvalidChannel = juce::jlimit (
            0, buffer.getNumChannels(), juce::jmax (0, validChannels));
        for (int channel = firstInvalidChannel; channel < buffer.getNumChannels(); ++channel)
            buffer.clear (channel, startSample, numSamples);
    }

    static bool prepareRouteSources (const juce::AudioBuffer<float>& inputBuffer,
                                     int firstChannel,
                                     bool mono,
                                     int validChannels,
                                     juce::AudioBuffer<float>& silenceScratch,
                                     int numSamples,
                                     const float* (&sources)[2]) noexcept
    {
        const int boundedValidChannels = juce::jmin (
            inputBuffer.getNumChannels(), juce::jmax (0, validChannels));
        const bool available = isRouteAvailable (
            firstChannel, mono, boundedValidChannels);

        if (available)
        {
            sources[0] = inputBuffer.getReadPointer (firstChannel);
            sources[1] = mono
                ? sources[0]
                : inputBuffer.getReadPointer (firstChannel + 1);
            return true;
        }

        jassert (silenceScratch.getNumChannels() >= 2);
        jassert (numSamples >= 0 && numSamples <= silenceScratch.getNumSamples());
        silenceScratch.clear (0, 0, numSamples);
        silenceScratch.clear (1, 0, numSamples);
        sources[0] = silenceScratch.getReadPointer (0);
        sources[1] = silenceScratch.getReadPointer (1);
        return false;
    }
};

} // namespace DAW
