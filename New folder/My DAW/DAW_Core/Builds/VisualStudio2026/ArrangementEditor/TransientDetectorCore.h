#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace ArrangementEditor {

class TransientDetectorCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        maxBlockSize_ = juce::jmax(1, maxBlockSize);
        reset();
    }

    void reset() noexcept
    {
        prevInput_ = 0.0f;
        prevHp_ = 0.0f;
    }

    int findTransientInWindow(const float* src, int length) const noexcept
    {
        if (src == nullptr || length < 8) return -1;
        const float alpha = std::exp(-2.0f * juce::MathConstants<float>::pi * 2000.0f / (float)sampleRate_);
        float prevIn = 0.0f;
        float prevHp = 0.0f;
        float avg = 1.0e-8f;
        int best = -1;
        float bestRatio = 3.0f;
        for (int i = 0; i < length; ++i)
        {
            const float x = src[i];
            const float hp = alpha * (prevHp + x - prevIn);
            prevIn = x;
            prevHp = hp;
            const float e = hp * hp;
            const float ratio = e / juce::jmax(avg, 1.0e-8f);
            if (i > 8 && ratio > bestRatio)
            {
                bestRatio = ratio;
                best = i;
            }
            avg += (e - avg) * 0.002f;
        }
        return best;
    }

private:
    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    mutable float prevInput_ = 0.0f;
    mutable float prevHp_ = 0.0f;
};

} // namespace ArrangementEditor
