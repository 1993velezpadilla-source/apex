#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>

namespace DAW {

class RmsMeterCore
{
public:
    void prepare(double sampleRate, double windowSeconds = 0.300)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        windowSize_ = juce::jmax(1, (int)std::round(sampleRate_ * windowSeconds));
        ring_.assign((size_t)windowSize_, 0.0f);
        writePos_ = 0;
        sumSquares_ = 0.0;
        filled_ = 0;
    }

    void process(const float* data, int numSamples) noexcept
    {
        if (data == nullptr || ring_.empty()) return;
        for (int i = 0; i < numSamples; ++i)
        {
            const float sq = data[i] * data[i];
            sumSquares_ -= ring_[(size_t)writePos_];
            ring_[(size_t)writePos_] = sq;
            sumSquares_ += sq;
            writePos_ = (writePos_ + 1) % windowSize_;
            filled_ = juce::jmin(filled_ + 1, windowSize_);
        }
    }

    float getRms() const noexcept
    {
        return filled_ > 0 ? std::sqrt((float)(sumSquares_ / (double)filled_)) : 0.0f;
    }

    float getRmsDb() const noexcept { return juce::Decibels::gainToDecibels(getRms(), -160.0f); }

private:
    double sampleRate_ = 44100.0;
    int windowSize_ = 1;
    int writePos_ = 0;
    int filled_ = 0;
    double sumSquares_ = 0.0;
    std::vector<float> ring_;
};

} // namespace DAW
