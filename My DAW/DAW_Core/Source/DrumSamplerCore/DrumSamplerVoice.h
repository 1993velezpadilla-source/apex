#pragma once
#include <JuceHeader.h>
#include "DrumSamplerTypes.h"

namespace DAW {

class DrumSamplerVoice {
public:
    DrumSamplerVoice() = default;

    void start(const DrumPadConfig& pad, float velocity, int64_t samplePos);
    void process(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);
    void stop();
    bool isActive() const noexcept { return active_; }

    /** Engine/device sample rate. Defaults to 44100 (legacy behaviour when
        the owner never prepares the sampler). Set from the actual granted
        device rate on every device (re)start (Brain §3). */
    void setSampleRate (double engineRate) noexcept
    {
        engineSampleRate_ = (engineRate > 0.0) ? engineRate : 44100.0;
    }

private:
    bool active_ = false;
    const DrumPadConfig* pad_ = nullptr;
    double engineSampleRate_ = 44100.0;
    double srcPosition_ = 0.0;   // source-domain position (fractional for resampling)
    double rateRatio_ = 1.0;     // layer file rate / engine rate, captured at start()
    float velocity_ = 0.0f;
    float currentGain_ = 0.0f;

    enum class EnvStage { Attack, Decay, Sustain, Release, Idle };
    EnvStage envStage_ = EnvStage::Idle;
    float envValue_ = 0.0f;
    int envSampleCounter_ = 0;

    void advanceEnvelope(int numSamples);
};

} // namespace DAW
