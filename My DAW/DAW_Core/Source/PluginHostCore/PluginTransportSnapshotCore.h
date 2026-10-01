#pragma once
#include <JuceHeader.h>
#include <cstdint>

namespace DAW {

struct PluginTransportSnapshot
{
    int64_t timeInSamples = 0;
    double timeInSeconds = 0.0;

    double bpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    double ppqPosition = 0.0;
    double ppqLastBarStart = 0.0;

    bool isPlaying = false;
    bool isRecording = false;
    bool isLooping = false;

    int64_t loopStartSamples = 0;
    int64_t loopEndSamples = 0;

    double sampleRate = 44100.0;
};

} // namespace DAW
