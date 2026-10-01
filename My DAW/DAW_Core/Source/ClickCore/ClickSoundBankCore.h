#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <vector>

namespace DAW {

class ClickSoundBankCore
{
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        buildClickSample(accentSample_, 1500.0, 25.0);
        buildClickSample(regularSample_, 900.0, 22.0);
    }

    const std::vector<float>& getAccentSample()  const noexcept { return accentSample_; }
    const std::vector<float>& getRegularSample() const noexcept { return regularSample_; }

private:
    void buildClickSample(std::vector<float>& dst, double frequencyHz, double durationMs)
    {
        const int numSamples = (int) (sampleRate_ * (durationMs / 1000.0));
        dst.assign((size_t) numSamples, 0.0f);

        const double phaseStep = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate_;
        const double decayRate = 8.0 / (durationMs / 1000.0);

        for (int s = 0; s < numSamples; ++s)
        {
            const double t = (double) s / sampleRate_;
            const double env = std::exp(-decayRate * t);
            const double sample = std::sin(phaseStep * s) * env;
            dst[(size_t) s] = (float) (sample * 0.85);
        }
    }

    double             sampleRate_ { 44100.0 };
    std::vector<float> accentSample_;
    std::vector<float> regularSample_;
};

} // namespace DAW
