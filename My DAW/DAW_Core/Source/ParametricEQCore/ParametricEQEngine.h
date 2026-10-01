#pragma once

#include "ParametricEQBiquad.h"

#include <array>
#include <complex>

namespace APEX::ParametricEQ
{

// Static zero-latency engine.  It owns fixed storage only: no vectors, maps,
// locks, allocations, GUI calls, or lifecycle work occur in process().
class Engine
{
public:
    void prepare (double sampleRate, int maximumBlockSize,
                  int numberOfChannels) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) && sampleRate > 1.0
                    ? sampleRate : 44100.0;
        maximumBlockSize_ = std::max (1, maximumBlockSize);
        numberOfChannels_ = std::clamp (numberOfChannels, 1, kMaxChannels);
        for (auto& band : bands_)
        {
            band.settings = {};
            band.lower = {};
            band.upper = {};
            band.morph = 0.0;
            band.reset();
        }
    }

    void reset() noexcept
    {
        for (auto& band : bands_)
            band.reset();
    }

    bool setBand (int index, const BandSettings& rawSettings,
                  DesignMode mode = DesignMode::Realtime,
                  bool resetState = true) noexcept
    {
        if (index < 0 || index >= kMaxBands)
            return false;

        auto& runtime = bands_[static_cast<std::size_t> (index)];
        const auto previousDomain = runtime.stateDomain (numberOfChannels_);
        runtime.settings = sanitise (rawSettings, sampleRate_);
        runtime.mode = mode;
        runtime.lower = {};
        runtime.upper = {};
        runtime.morph = 0.0;

        if (runtime.settings.enabled && ! runtime.settings.bypassed)
        {
            if (isCutShape (runtime.settings.shape))
            {
                const auto exactOrder = runtime.settings.slopeDbPerOctave / 6.0;
                const auto lowerOrder = std::clamp (
                    static_cast<int> (std::floor (exactOrder)), 0, kMaxCutOrder);
                const auto upperOrder = std::min (kMaxCutOrder, lowerOrder + 1);
                runtime.morph = upperOrder == lowerOrder
                              ? 0.0 : std::clamp (exactOrder - lowerOrder, 0.0, 1.0);
                const bool highPass = runtime.settings.shape == FilterShape::LowCut;
                runtime.lower = FilterDesigner::designButterworthCut (
                    highPass, lowerOrder, runtime.settings.frequencyHz,
                    sampleRate_, mode);
                runtime.upper = FilterDesigner::designButterworthCut (
                    highPass, upperOrder, runtime.settings.frequencyHz,
                    sampleRate_, mode);
            }
            else
            {
                runtime.lower = FilterDesigner::designBand (runtime.settings,
                                                            sampleRate_, mode);
            }
        }

        if (resetState || previousDomain != runtime.stateDomain (numberOfChannels_))
            runtime.reset();
        return runtime.lower.isValid() && runtime.upper.isValid();
    }

    const BandSettings& getBand (int index) const noexcept
    {
        static const BandSettings invalid {};
        if (index < 0 || index >= kMaxBands)
            return invalid;
        return bands_[static_cast<std::size_t> (index)].settings;
    }

    void process (float* const* channels, int numberOfChannels,
                  int numberOfSamples) noexcept
    {
        processWithDynamicGains (channels, numberOfChannels, numberOfSamples,
                                 0, nullptr);
    }

    // Dynamic-EQ aware processing. Bands whose bit is set in dynamicMask are
    // modulated by the per-sample linear dynamic gain in
    // dynamicGainChannels[band][sample] using a delta-scaled residual
    // (out = x + g * (filtered - x)), preserving the base filter's shape.
    // Every other band processes exactly as the static path. A zero mask (or
    // null gains) is bit-identical to the static path.
    void processWithDynamicGains (float* const* channels, int numberOfChannels,
                                  int numberOfSamples, std::uint32_t dynamicMask,
                                  float* const* dynamicGainChannels) noexcept
    {
        if (channels == nullptr || numberOfSamples <= 0)
            return;
        const auto channelCount = std::clamp (numberOfChannels, 0,
                                              numberOfChannels_);
        if (channelCount <= 0)
            return;

        const bool anyDynamic = dynamicMask != 0 && dynamicGainChannels != nullptr;

        for (int bandIndex = 0; bandIndex < kMaxBands; ++bandIndex)
        {
            auto& band = bands_[static_cast<std::size_t> (bandIndex)];
            if (! band.isActive())
                continue;
            const std::uint32_t bandBit = 1u << bandIndex;
            if (anyDynamic && (dynamicMask & bandBit) != 0
                && isDynamicCompatibleShape (band.settings.shape))
            {
                const auto* gains = dynamicGainChannels[bandIndex];
                processBandDynamic (band, channels, channelCount,
                                    numberOfSamples, gains);
                continue;
            }

            switch (band.settings.placement)
            {
                case ChannelPlacement::Stereo:
                    for (int channel = 0; channel < channelCount; ++channel)
                        processPhysicalChannel (band, channels[channel], channel,
                                                numberOfSamples);
                    break;
                case ChannelPlacement::Left:
                    processPhysicalChannel (band, channels[0], 0, numberOfSamples);
                    break;
                case ChannelPlacement::Right:
                    if (channelCount >= 2)
                        processPhysicalChannel (band, channels[1], 0,
                                                numberOfSamples);
                    break;
                case ChannelPlacement::Mid:
                    if (channelCount == 1)
                        processPhysicalChannel (band, channels[0], 0,
                                                numberOfSamples);
                    else
                        processMidSide (band, channels[0], channels[1], true,
                                        numberOfSamples);
                    break;
                case ChannelPlacement::Side:
                    if (channelCount >= 2)
                        processMidSide (band, channels[0], channels[1], false,
                                        numberOfSamples);
                    break;
            }
        }
    }

    std::complex<double> getBandResponse (int index,
                                          double frequencyHz) const noexcept
    {
        if (index < 0 || index >= kMaxBands)
            return { 1.0, 0.0 };
        const auto& band = bands_[static_cast<std::size_t> (index)];
        if (! band.settings.enabled || band.settings.bypassed)
            return { 1.0, 0.0 };

        const auto lower = band.lower.response (frequencyHz, sampleRate_);
        if (band.morph <= 0.0)
            return lower;
        const auto upper = band.upper.response (frequencyHz, sampleRate_);
        return lower + band.morph * (upper - lower);
    }

    std::complex<double> getResponse (double frequencyHz) const noexcept
    {
        std::complex<double> response { 1.0, 0.0 };
        for (int index = 0; index < kMaxBands; ++index)
            response *= getBandResponse (index, frequencyHz);
        return response;
    }

    double getMagnitudeDb (double frequencyHz) const noexcept
    {
        return 20.0 * std::log10 (std::max (1.0e-15,
                                           std::abs (getResponse (frequencyHz))));
    }

    double getSampleRate() const noexcept { return sampleRate_; }
    int getPreparedChannelCount() const noexcept { return numberOfChannels_; }
    int getMaximumBlockSizeHint() const noexcept { return maximumBlockSize_; }

private:
    enum class StateDomain : std::uint8_t
    {
        None = 0,
        Mono,
        Stereo,
        Left,
        Right,
        Mid,
        Side
    };

    struct BandRuntime
    {
        BandSettings settings {};
        DesignMode mode = DesignMode::Realtime;
        CascadeCoefficients lower {};
        CascadeCoefficients upper {};
        double morph = 0.0;
        std::array<CascadeState, kMaxChannels> lowerState {};
        std::array<CascadeState, kMaxChannels> upperState {};

        bool isIdentity() const noexcept
        {
            return lower.isIdentity()
                && (morph <= 0.0 || upper.isIdentity());
        }

        bool isActive() const noexcept
        {
            return settings.enabled && ! settings.bypassed && ! isIdentity();
        }

        StateDomain stateDomain (int channels) const noexcept
        {
            if (! isActive())
                return StateDomain::None;
            if (channels <= 1)
            {
                if (settings.placement == ChannelPlacement::Right
                    || settings.placement == ChannelPlacement::Side)
                    return StateDomain::None;
                return StateDomain::Mono;
            }

            switch (settings.placement)
            {
                case ChannelPlacement::Stereo: return StateDomain::Stereo;
                case ChannelPlacement::Left: return StateDomain::Left;
                case ChannelPlacement::Right: return StateDomain::Right;
                case ChannelPlacement::Mid: return StateDomain::Mid;
                case ChannelPlacement::Side: return StateDomain::Side;
            }
            return StateDomain::None;
        }

        void reset() noexcept
        {
            for (auto& state : lowerState) state.reset();
            for (auto& state : upperState) state.reset();
        }
    };

    static float processSample (BandRuntime& band, int stateLane,
                                float input) noexcept
    {
        auto& lowerState = band.lowerState[static_cast<std::size_t> (stateLane)];
        const auto lower = lowerState.process (input, band.lower);
        if (band.morph <= 0.0)
            return lower;

        auto& upperState = band.upperState[static_cast<std::size_t> (stateLane)];
        const auto upper = upperState.process (input, band.upper);
        const auto output = static_cast<double> (lower)
                          + band.morph * (static_cast<double> (upper)
                                        - static_cast<double> (lower));
        return std::isfinite (output)
             ? static_cast<float> (output)
             : (std::isfinite (input) ? input : 0.0f);
    }

    static void processPhysicalChannel (BandRuntime& band, float* data,
                                        int stateLane,
                                        int numberOfSamples) noexcept
    {
        if (data == nullptr)
            return;
        for (int sample = 0; sample < numberOfSamples; ++sample)
            data[sample] = processSample (band, stateLane, data[sample]);
    }

    static void processMidSide (BandRuntime& band, float* left, float* right,
                                bool processMid,
                                int numberOfSamples) noexcept
    {
        if (left == nullptr || right == nullptr)
            return;

        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            const double leftInput = std::isfinite (left[sample])
                                   ? static_cast<double> (left[sample]) : 0.0;
            const double rightInput = std::isfinite (right[sample])
                                    ? static_cast<double> (right[sample]) : 0.0;
            double mid = 0.0;
            double side = 0.0;
            encodeMidSide (leftInput, rightInput, mid, side);
            const double component = processMid ? mid : side;
            const double filtered = processSample (
                band, 0, static_cast<float> (component));
            const double delta = filtered - component;
            const double leftOutput = leftInput + delta;
            const double rightOutput = processMid ? rightInput + delta
                                                  : rightInput - delta;
            left[sample] = std::isfinite (leftOutput)
                         ? static_cast<float> (leftOutput) : 0.0f;
            right[sample] = std::isfinite (rightOutput)
                          ? static_cast<float> (rightOutput) : 0.0f;
        }
    }

    // Delta-scaled residual modulation: the base filter's shape is preserved
    // while a per-sample dynamic gain scales only the band's contribution.
    static void processBandDynamic (BandRuntime& band, float* const* channels,
                                    int channelCount, int numberOfSamples,
                                    const float* gains) noexcept
    {
        switch (band.settings.placement)
        {
            case ChannelPlacement::Stereo:
                for (int channel = 0; channel < channelCount; ++channel)
                {
                    auto* data = channels[channel];
                    if (data == nullptr)
                        continue;
                    for (int sample = 0; sample < numberOfSamples; ++sample)
                    {
                        const auto input = data[sample];
                        const auto filtered = processSample (band, channel, input);
                        const auto g = std::isfinite (gains[sample])
                                     ? static_cast<double> (gains[sample]) : 1.0;
                        const auto output = static_cast<double> (input)
                                          + g * (static_cast<double> (filtered)
                                               - static_cast<double> (input));
                        data[sample] = std::isfinite (output)
                                     ? static_cast<float> (output)
                                     : (std::isfinite (input) ? input : 0.0f);
                    }
                }
                break;
            case ChannelPlacement::Left:
                if (channels[0] != nullptr)
                {
                    for (int sample = 0; sample < numberOfSamples; ++sample)
                    {
                        const auto input = channels[0][sample];
                        const auto filtered = processSample (band, 0, input);
                        const auto g = std::isfinite (gains[sample])
                                     ? static_cast<double> (gains[sample]) : 1.0;
                        const auto output = static_cast<double> (input)
                                          + g * (static_cast<double> (filtered)
                                               - static_cast<double> (input));
                        channels[0][sample] = std::isfinite (output)
                                            ? static_cast<float> (output)
                                            : (std::isfinite (input) ? input : 0.0f);
                    }
                }
                break;
            case ChannelPlacement::Right:
                if (channelCount >= 2 && channels[1] != nullptr)
                {
                    for (int sample = 0; sample < numberOfSamples; ++sample)
                    {
                        const auto input = channels[1][sample];
                        const auto filtered = processSample (band, 0, input);
                        const auto g = std::isfinite (gains[sample])
                                     ? static_cast<double> (gains[sample]) : 1.0;
                        const auto output = static_cast<double> (input)
                                          + g * (static_cast<double> (filtered)
                                               - static_cast<double> (input));
                        channels[1][sample] = std::isfinite (output)
                                            ? static_cast<float> (output)
                                            : (std::isfinite (input) ? input : 0.0f);
                    }
                }
                break;
            case ChannelPlacement::Mid:
            case ChannelPlacement::Side:
            {
                if (channelCount < 2 || channels[0] == nullptr
                    || channels[1] == nullptr)
                {
                    if (channelCount == 1 && channels[0] != nullptr
                        && band.settings.placement == ChannelPlacement::Mid)
                    {
                        for (int sample = 0; sample < numberOfSamples; ++sample)
                        {
                            const auto input = channels[0][sample];
                            const auto filtered = processSample (band, 0, input);
                            const auto g = std::isfinite (gains[sample])
                                         ? static_cast<double> (gains[sample]) : 1.0;
                            const auto output = static_cast<double> (input)
                                              + g * (static_cast<double> (filtered)
                                                   - static_cast<double> (input));
                            channels[0][sample] = std::isfinite (output)
                                                ? static_cast<float> (output)
                                                : (std::isfinite (input) ? input : 0.0f);
                        }
                    }
                    break;
                }

                const bool processMid = band.settings.placement
                                        == ChannelPlacement::Mid;
                for (int sample = 0; sample < numberOfSamples; ++sample)
                {
                    const double leftInput = std::isfinite (channels[0][sample])
                                           ? static_cast<double> (channels[0][sample]) : 0.0;
                    const double rightInput = std::isfinite (channels[1][sample])
                                            ? static_cast<double> (channels[1][sample]) : 0.0;
                    double mid = 0.0;
                    double side = 0.0;
                    encodeMidSide (leftInput, rightInput, mid, side);
                    const double component = processMid ? mid : side;
                    const double filtered = processSample (
                        band, 0, static_cast<float> (component));
                    const auto g = std::isfinite (gains[sample])
                                 ? static_cast<double> (gains[sample]) : 1.0;
                    const double delta = g * (filtered - component);
                    const double leftOutput = leftInput + delta;
                    const double rightOutput = processMid ? rightInput + delta
                                                          : rightInput - delta;
                    channels[0][sample] = std::isfinite (leftOutput)
                                        ? static_cast<float> (leftOutput) : 0.0f;
                    channels[1][sample] = std::isfinite (rightOutput)
                                        ? static_cast<float> (rightOutput) : 0.0f;
                }
                break;
            }
        }
    }

    std::array<BandRuntime, kMaxBands> bands_ {};
    double sampleRate_ = 44100.0;
    int maximumBlockSize_ = 1;
    int numberOfChannels_ = 2;
};

} // namespace APEX::ParametricEQ
