#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include <vector>

namespace DAW {

struct VelocityLayer {
    float  velocityRange[2] = {0.0f, 1.0f};
    juce::String filePath;
    juce::AudioBuffer<float> audioData;
    int sampleRate = 44100;
    int64_t numSamples = 0;
};

struct DrumPadConfig {
    juce::String name = "Pad";
    int      midiNote = 36;
    float    gainDb = 0.0f;
    float    pan = 0.0f;
    float    tune = 0.0f;
    float    attackMs = 0.1f;
    float    decayMs = 100.0f;
    float    sustainLevel = 0.8f;
    float    releaseMs = 50.0f;
    int      chokeGroup = -1;
    bool     reverse = false;
    int64_t  startSample = 0;
    int64_t  endSample = -1;
    int      mixerTrack = -1;
    juce::Colour color = juce::Colour(0xff666666);
    std::vector<VelocityLayer> layers;
};

struct DrumSamplerConfig {
    int  numPads = 16;
    int  maxPolyphony = 16;
    bool roundRobin = false;
};

} // namespace DAW
