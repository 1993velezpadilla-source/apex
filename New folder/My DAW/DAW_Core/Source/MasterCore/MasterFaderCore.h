#pragma once
#include <atomic>

namespace DAW {

/**
 * MasterFaderCore — master bus fader gain control.
 *
 * Manages the master fader level with thread-safe atomic access.
 * The master fader is applied AFTER master insert FX, BEFORE
 * the render tap and monitor section.
 */
class MasterFaderCore
{
public:
    void setGainLinear(float gain) noexcept
    {
        gain_.store(juce::jlimit(0.0f, 4.0f, gain), std::memory_order_relaxed);
    }

    float getGainLinear() const noexcept
    {
        return gain_.load(std::memory_order_relaxed);
    }

    void setGainDb(float db) noexcept
    {
        setGainLinear(std::pow(10.0f, db / 20.0f));
    }

    float getGainDb() const noexcept
    {
        float g = gain_.load(std::memory_order_relaxed);
        return g <= 0.000001f ? -120.0f : 20.0f * std::log10(g);
    }

private:
    std::atomic<float> gain_{ 1.0f };
};

} // namespace DAW
