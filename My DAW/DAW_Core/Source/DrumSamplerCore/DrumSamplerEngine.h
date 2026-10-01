#pragma once
#include <JuceHeader.h>
#include "DrumSamplerTypes.h"
#include "DrumSamplerVoicePool.h"
#include <vector>

namespace DAW {

class DrumSamplerEngine {
public:
    DrumSamplerEngine(int numPads = 16);
    ~DrumSamplerEngine() = default;

    void processBlock(juce::MidiBuffer& midiBuffer, juce::AudioBuffer<float>& audioBuffer,
                      int startSample, int numSamples);

    /** Prepares the sampler for the actual granted device rate (Brain §3:
        prepare from the result of negotiation). Call on every device
        (re)start so pad resampling and envelopes track the device. */
    void prepare (double sampleRate) noexcept { voicePool_.setSampleRate (sampleRate); }

    DrumPadConfig& getPad(int index);
    const DrumPadConfig& getPad(int index) const;
    int getNumPads() const noexcept { return (int)pads_.size(); }

    void loadSample(int padIndex, const juce::String& filePath);
    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

private:
    std::vector<DrumPadConfig> pads_;
    DrumSamplerVoicePool voicePool_;
    juce::AudioFormatManager formatManager_;

    int findPadByMidiNote(int note) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerEngine)
};

} // namespace DAW
