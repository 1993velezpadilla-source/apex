// ===========================================================================
// TapeResamplePitchEngineCore.h
// High-quality realtime tape/resample pitch engine with smoothed ratio.
// Used by ClipPitchRenderPathCore for tape blend in pitch processing.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include "UnifiedPitchStateCore.h"
#include "WailModulationCore.h"
#include <JuceHeader.h>
#include <atomic>

namespace ArrangementEditor
{

class TapeResamplePitchEngineCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        maxBlockSize_ = std::max(1, maxBlockSize);
        smootherCoeff_ = PitchScaleMathCore::onePoleCoefficient(sampleRate_, 0.008);
        smoothedRatio_ = targetRatio_.load(std::memory_order_relaxed);
        lowpass_.reset();
        wail_.prepare(sampleRate_);
    }

    void reset()
    {
        smoothedRatio_ = targetRatio_.load(std::memory_order_relaxed);
        lowpass_.reset();
        wail_.reset();
        for (auto& state : channelStates_)
            state = ChannelState();
    }

    void setPitchScale(double ratio) noexcept
    {
        targetRatio_.store(std::max(0.0001, PitchScaleMathCore::sanitizeNaN(ratio, 1.0)), std::memory_order_release);
    }

    void setWailAmount(double amount) noexcept
    {
        wailAmount_.store(std::clamp(PitchScaleMathCore::sanitizeNaN(amount), 0.0, 1.0), std::memory_order_release);
    }

    void renderSegment(const float* src, int srcTotal, int64_t srcStart, int64_t srcEnd,
                       float* dst, int dstNumSamples, int channel)
    {
        juce::ScopedNoDenormals noDenormals;
        if (dst == nullptr || dstNumSamples <= 0)
            return;

        const int ch = channel & 7;
        lowpass_.ensureChannel(ch);
        auto& state = channelStates_[ch];

        if (!state.initialized)
            state.sourcePosition = static_cast<double>(srcStart);

        state.initialized = true;

        for (int i = 0; i < dstNumSamples; ++i)
        {
            smoothedRatio_ = PitchScaleMathCore::onePoleNext(smoothedRatio_, targetRatio_.load(std::memory_order_acquire), smootherCoeff_);
            const double ratio = wail_.processRatio(smoothedRatio_, wailAmount_.load(std::memory_order_acquire));
            float sample = lagrange(src, srcTotal, state.sourcePosition);
            sample = lowpass_.process(ch, sample, ratio, sampleRate_);
            dst[i] = PitchScaleMathCore::flushDenormal(sample);
            state.sourcePosition += ratio;
        }

        juce::ignoreUnused(srcEnd);
    }

    int getCurrentLatencySamples() const noexcept { return 2; }

private:
    static float readSafe(const float* src, int srcTotal, int index) noexcept
    {
        return (src != nullptr && index >= 0 && index < srcTotal) ? src[index] : 0.0f;
    }

    static float lagrange(const float* src, int srcTotal, double pos) noexcept
    {
        const int i1 = static_cast<int>(std::floor(pos));
        const float frac = static_cast<float>(pos - static_cast<double>(i1));
        const float y0 = readSafe(src, srcTotal, i1 - 1);
        const float y1 = readSafe(src, srcTotal, i1);
        const float y2 = readSafe(src, srcTotal, i1 + 1);
        const float y3 = readSafe(src, srcTotal, i1 + 2);
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return PitchScaleMathCore::flushDenormal(((c3 * frac + c2) * frac + c1) * frac + c0);
    }

    struct AntiAliasLowpass
    {
        void reset() noexcept
        {
            for (auto& s : state) s = 0.0f;
            cachedRatio = -1.0;
            cachedCoeff = 0.5f;
        }

        void ensureChannel(int) noexcept {}

        float process(int ch, float x, double ratio, double sampleRate) noexcept
        {
            ch &= 7;
            if (std::abs(ratio - cachedRatio) > 0.005)
            {
                const double cutoff = std::clamp(
                    (ratio >= 1.0 ? 0.45 / ratio : 0.45 * ratio) * sampleRate,
                    40.0, sampleRate * 0.45);
                const double coeff = 1.0 - std::exp(
                    -2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate);
                cachedCoeff = static_cast<float>(std::clamp(coeff, 0.001, 1.0));
                cachedRatio = ratio;
            }
            state[ch] += cachedCoeff * (x - state[ch]);
            state[ch] = PitchScaleMathCore::flushDenormal(state[ch]);
            return state[ch];
        }

        float  state[8]    {};
        double cachedRatio = -1.0;
        float  cachedCoeff = 0.5f;
    } lowpass_;

    struct ChannelState
    {
        bool initialized = false;
        double sourcePosition = 0.0;
    };

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    double smootherCoeff_ = 1.0;
    double smoothedRatio_ = 1.0;
    std::atomic<double> targetRatio_ { 1.0 };
    std::atomic<double> wailAmount_ { 0.0 };
    WailModulationCore wail_;
    ChannelState channelStates_[8];
};

} // namespace ArrangementEditor
