#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace DAW {

/**
 * PichyInputGuardianCore: observes RAW hardware samples, before APEX Input Trim.
 *
 * Exactly one audio callback owns processBlock(). The UI/Pichy read atomics;
 * no allocations, locks, logging, callbacks or trim writes occur in audio.
 *
 * This is a digital sample-peak warning, NOT a hardware clip detector:
 * analogue/interface distortion can occur well below 0 dBFS. A reading near
 * digital full-scale is suspicious, not conclusive proof of clipping.
 * Mono must be counted once even when R aliases L.
 */
class PichyInputGuardianCore
{
public:
    struct Snapshot
    {
        float rawPeakHold = 0.0f;
        std::uint64_t framesObserved = 0;
        std::uint64_t nearFullScaleSamples = 0;
        std::uint64_t aboveFullScaleSamples = 0;
        std::uint64_t nonFiniteSamples = 0;
    };

    static constexpr float nearFullScaleThreshold = 0.999f;

    void processBlock(const float* left, const float* right,
                      int numSamples, bool mono) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;
        if (!mono && right == nullptr)
            return;

        float blockPeak = 0.0f;
        std::uint64_t nearCount = 0;
        std::uint64_t aboveCount = 0;
        std::uint64_t invalidCount = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            observeSample(l, blockPeak, nearCount, aboveCount, invalidCount);
            if (!mono)
                observeSample(right[i], blockPeak, nearCount, aboveCount, invalidCount);
        }
        publishMax(rawPeakHold_, blockPeak);
        framesObserved_.fetch_add(static_cast<std::uint64_t>(numSamples),
                                  std::memory_order_relaxed);
        nearFullScaleSamples_.fetch_add(nearCount, std::memory_order_relaxed);
        aboveFullScaleSamples_.fetch_add(aboveCount, std::memory_order_relaxed);
        nonFiniteSamples_.fetch_add(invalidCount, std::memory_order_relaxed);
    }

    Snapshot snapshot() const noexcept
    {
        Snapshot s;
        s.rawPeakHold = rawPeakHold_.load(std::memory_order_relaxed);
        s.framesObserved = framesObserved_.load(std::memory_order_relaxed);
        s.nearFullScaleSamples = nearFullScaleSamples_.load(std::memory_order_relaxed);
        s.aboveFullScaleSamples = aboveFullScaleSamples_.load(std::memory_order_relaxed);
        s.nonFiniteSamples = nonFiniteSamples_.load(std::memory_order_relaxed);
        return s;
    }

    /** Non-audio-thread reset of display holds; racing block may add new data. */
    void resetHold() noexcept
    {
        rawPeakHold_.store(0.0f, std::memory_order_relaxed);
        framesObserved_.store(0, std::memory_order_relaxed);
        nearFullScaleSamples_.store(0, std::memory_order_relaxed);
        aboveFullScaleSamples_.store(0, std::memory_order_relaxed);
        nonFiniteSamples_.store(0, std::memory_order_relaxed);
    }

private:
    static void observeSample(float sample, float& peak, std::uint64_t& nearCount,
                              std::uint64_t& aboveCount, std::uint64_t& invalidCount) noexcept
    {
        if (!std::isfinite(sample))
        {
            ++invalidCount;
            return;
        }
        const float magnitude = std::abs(sample);
        if (magnitude > peak) peak = magnitude;
        if (magnitude >= nearFullScaleThreshold) ++nearCount;
        if (magnitude > 1.0f) ++aboveCount;
    }

    static void publishMax(std::atomic<float>& destination, float candidate) noexcept
    {
        auto previous = destination.load(std::memory_order_relaxed);
        while (candidate > previous && !destination.compare_exchange_weak(
                   previous, candidate, std::memory_order_relaxed)) {}
    }

    std::atomic<float> rawPeakHold_ { 0.0f };
    std::atomic<std::uint64_t> framesObserved_ { 0 };
    std::atomic<std::uint64_t> nearFullScaleSamples_ { 0 };
    std::atomic<std::uint64_t> aboveFullScaleSamples_ { 0 };
    std::atomic<std::uint64_t> nonFiniteSamples_ { 0 };
};

} // namespace DAW
