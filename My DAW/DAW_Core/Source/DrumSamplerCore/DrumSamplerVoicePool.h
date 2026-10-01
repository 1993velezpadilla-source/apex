#pragma once
#include <JuceHeader.h>
#include "DrumSamplerVoice.h"

namespace DAW {

class DrumSamplerVoicePool {
public:
    DrumSamplerVoicePool(int maxVoices = 64);

    DrumSamplerVoice* allocate();
    void release(DrumSamplerVoice* voice);
    void processAll(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);

    /** Propagates the engine/device rate to every voice (message thread or
        prepare-time only — not per-block). */
    void setSampleRate (double engineRate) noexcept;

private:
    juce::OwnedArray<DrumSamplerVoice> voices_;
    // No lock: voices_ is built at construction (message thread); allocate(),
    // release() and processAll() are audio-thread-only afterwards.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerVoicePool)
};

} // namespace DAW
