#pragma once

#include "ParametricEQSpectralDynamicsCore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace APEX::ParametricEQ
{

// Spectral mode's internal wet/dry transition. It is NOT a whole-plugin
// bypass or a latency-free mode selector. Once its caller has engaged the
// 1024-sample spectral processor mode and reported that latency to the host,
// BOTH paths here are delayed by 1024 samples.
//
// This isolates and solves a subtle but critical audio bug: mixing a 1024
// sample wet path with undelayed dry audio creates comb filtering and shifts
// recorded transients. The only switching allowed while playing is the
// WITHIN-MODE wet target; the host-facing mode transition needs a separate
// transport/latency handshake before production integration.
class SpectralAlignedBlend final
{
public:
    static constexpr int kLatencySamples =
        SpectralDynamicsCore::kLatencySamples;
    static constexpr int kChannels = SpectralDynamicsCore::kChannels;
    static constexpr int kDelayCapacity = 2048;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) && sampleRate > 1.0
            ? sampleRate : 48000.0;
        wet_.prepare (sampleRate_);
        crossfadeStep_ = 1.0 / (0.010 * sampleRate_);
        reset();
    }

    void reset() noexcept
    {
        wet_.reset();
        for (auto& channel : dryRing_)
            channel.fill (0.0f);
        counter_ = 0;
        mix_ = 0.0;
    }

    static constexpr int latencySamples() noexcept
    {
        return kLatencySamples;
    }

    double wetMixForTesting() const noexcept { return mix_; }

    void process (float* const* samples, int channels, int count,
                  const std::array<SpectralDynamicsCore::Band,
                                   kMaxBands>& bands,
                  bool wetTarget) noexcept
    {
        if (samples == nullptr || count <= 0)
            return;
        const int safeChannels = std::clamp (channels, 0, kChannels);
        if (safeChannels <= 0)
            return;

        // Internally cap each work unit: prevents overwriting unread delayed
        // input when the DAW supplies huge, unexpected offline blocks.
        // STFT and crossfade timelines stay sample-accurate regardless of
        // host block segmentation.
        for (int offset = 0; offset < count; offset += 128)
        {
            const int size = std::min (128, count - offset);
            const auto start = counter_;
            std::array<float*, kChannels> chunk { nullptr, nullptr };
            for (int channel = 0; channel < safeChannels; ++channel)
                chunk[static_cast<std::size_t> (channel)] =
                    samples[channel] != nullptr ? samples[channel] + offset
                                                : nullptr;
            for (int index = 0; index < size; ++index)
            {
                const auto write = static_cast<std::uint32_t> (
                    (start + static_cast<std::uint64_t> (index))
                    & (kDelayCapacity - 1));
                for (int channel = 0; channel < safeChannels; ++channel)
                {
                    const float input = chunk[
                        static_cast<std::size_t> (channel)] != nullptr
                        && std::isfinite (chunk[
                            static_cast<std::size_t> (channel)][index])
                        ? chunk[static_cast<std::size_t> (channel)][index]
                        : 0.0f;
                    dryRing_[static_cast<std::size_t> (channel)][write]
                        = input;
                }
            }
            wet_.process (chunk.data(), safeChannels, size, bands);

            const double target = wetTarget ? 1.0 : 0.0;
            for (int index = 0; index < size; ++index)
            {
                mix_ = target > mix_
                    ? std::min (target, mix_ + crossfadeStep_)
                    : std::max (target, mix_ - crossfadeStep_);
                const auto read = static_cast<std::uint32_t> (
                    (start + static_cast<std::uint64_t> (index)
                     + kDelayCapacity - kLatencySamples)
                    & (kDelayCapacity - 1));
                for (int channel = 0; channel < safeChannels; ++channel)
                {
                    auto* output = chunk[static_cast<std::size_t> (channel)];
                    if (output == nullptr)
                        continue;
                    const float dry = dryRing_[
                        static_cast<std::size_t> (channel)][read];
                    const float wet = output[index];
                    output[index] = static_cast<float> (
                        dry + mix_ * (wet - dry));
                }
            }
            counter_ += static_cast<std::uint64_t> (size);
        }
    }

private:
    double sampleRate_ = 48000.0;
    double mix_ = 0.0;
    double crossfadeStep_ = 1.0 / 480.0;
    std::uint64_t counter_ = 0;
    std::array<std::array<float, kDelayCapacity>, kChannels> dryRing_ {};
    SpectralDynamicsCore wet_;
};

} // namespace APEX::ParametricEQ
