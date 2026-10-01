#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace DAW {

class SamplePeakMeterCore
{
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        displayPeak_ = 0.0f;
        holdPeak_ = 0.0f;
        holdSamplesRemaining_ = 0;
    }

    void setDecayDbPerSecond(float dbPerSecond) noexcept { decayDbPerSecond_ = juce::jmin(0.0f, dbPerSecond); }
    void setHoldSeconds(float seconds) noexcept { holdSeconds_ = seconds; }

    void process(const float* data, int numSamples) noexcept
    {
        if (data == nullptr || numSamples <= 0) return;
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax(peak, std::abs(data[i]));

        const float decayDb = decayDbPerSecond_ * ((float)numSamples / (float)sampleRate_);
        const float decayGain = juce::Decibels::decibelsToGain(decayDb);
        displayPeak_ = juce::jmax(peak, displayPeak_ * decayGain);

        if (peak >= holdPeak_)
        {
            holdPeak_ = peak;
            holdSamplesRemaining_ = holdSeconds_ < 0.0f ? std::numeric_limits<int>::max()
                : (int)std::round(holdSeconds_ * sampleRate_);
        }
        else if (holdSamplesRemaining_ > 0)
        {
            holdSamplesRemaining_ = juce::jmax(0, holdSamplesRemaining_ - numSamples);
        }
        else
        {
            holdPeak_ = displayPeak_;
        }
    }

    float getPeak() const noexcept { return displayPeak_; }
    float getPeakDb() const noexcept { return juce::Decibels::gainToDecibels(displayPeak_, -160.0f); }
    float getHoldPeak() const noexcept { return holdPeak_; }

private:
    double sampleRate_ = 44100.0;
    float decayDbPerSecond_ = -17.0f;
    float holdSeconds_ = 3.0f;
    float displayPeak_ = 0.0f;
    float holdPeak_ = 0.0f;
    int holdSamplesRemaining_ = 0;
};

} // namespace DAW
