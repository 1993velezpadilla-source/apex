#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include "../UtilityCore/Types.h"

namespace DAW {

class CountInCore
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
    }

    void begin(int bars, double tempoBpm, int beatsPerBar)
    {
        jassert(sampleRate_ > 0.0 && "sampleRate_ read before prepareToPlay");
        if (sampleRate_ <= 0.0)
        {
            active_.store(false, std::memory_order_release);
            return;
        }

        if (bars <= 0)
        {
            active_.store(false, std::memory_order_release);
            return;
        }

        const double samplesPerBeat = (60.0 / juce::jmax(1.0, tempoBpm)) * sampleRate_;
        const int64_t samples = (int64_t) std::llround(samplesPerBeat * juce::jmax(1, beatsPerBar) * bars);
        remainingSamples_.store(samples, std::memory_order_release);
        totalSamples_ = samples;
        active_.store(true, std::memory_order_release);
    }

    void cancel() noexcept
    {
        active_.store(false, std::memory_order_release);
        remainingSamples_.store(0, std::memory_order_release);
    }

    bool processBlock(int numSamples) noexcept
    {
        if (!active_.load(std::memory_order_acquire)) return false;

        int64_t r = remainingSamples_.load(std::memory_order_relaxed);
        r -= numSamples;
        if (r <= 0)
        {
            remainingSamples_.store(0, std::memory_order_release);
            active_.store(false, std::memory_order_release);
            return true;
        }

        remainingSamples_.store(r, std::memory_order_relaxed);
        return false;
    }

    bool isActive() const noexcept { return active_.load(std::memory_order_acquire); }

    float getProgress() const noexcept
    {
        if (!isActive() || totalSamples_ <= 0) return 1.0f;
        const int64_t r = remainingSamples_.load(std::memory_order_relaxed);
        return juce::jlimit(0.0f, 1.0f, 1.0f - (float) ((double) r / (double) totalSamples_));
    }

private:
    double  sampleRate_   { 0.0 };
    int64_t totalSamples_ { 0 };
    std::atomic<int64_t> remainingSamples_ { 0 };
    std::atomic<bool>    active_           { false };
};

} // namespace DAW
