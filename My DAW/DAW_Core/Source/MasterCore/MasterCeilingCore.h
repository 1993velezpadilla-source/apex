#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include "MasterOversamplingCore.h"

namespace DAW {

class MasterCeilingCore
{
public:
    enum class Mode { Off = 0, HardClip = 1, SoftClip = 2, LookaheadLimiter = 3 };

    static juce::String modeToString(Mode m)
    {
        switch (m)
        {
            case Mode::Off:             return "Off";
            case Mode::HardClip:        return "HardClip";
            case Mode::SoftClip:        return "SoftClip";
            case Mode::LookaheadLimiter: return "LookaheadLimiter";
            default:                    return "SoftClip";
        }
    }

    static Mode stringToMode(const juce::String& s)
    {
        if (s == "Off")             return Mode::Off;
        if (s == "HardClip")        return Mode::HardClip;
        if (s == "LookaheadLimiter") return Mode::LookaheadLimiter;
        return Mode::SoftClip; // safe default
    }

    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = sampleRate;
        blockSize_ = juce::jmax(1, blockSize);
        oversampling_.prepare(sampleRate, blockSize_, 2);
        lookaheadSamples_ = juce::jmax(1, (int)std::round(sampleRate_ * 0.005));
        delayL_.assign((size_t)(lookaheadSamples_ + blockSize_ + 8), 0.0f);
        delayR_.assign((size_t)(lookaheadSamples_ + blockSize_ + 8), 0.0f);
        writePos_ = 0;
        gain_ = 1.0f;
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
        oversampling_.reset();
    }

    void setMode(Mode mode) noexcept { mode_.store((int)mode, std::memory_order_relaxed); }
    Mode getMode() const noexcept { return (Mode)mode_.load(std::memory_order_relaxed); }

    void setCeilingDb(float db) noexcept { ceilingDb_.store(juce::jlimit(-24.0f, 0.0f, db), std::memory_order_relaxed); }
    float getCeilingDb() const noexcept { return ceilingDb_.load(std::memory_order_relaxed); }
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int numSamples)
    {
        if (L == nullptr || R == nullptr || numSamples <= 0) return;
        const auto mode = getMode();
        if (mode == Mode::Off) return;

        const float ceiling = juce::Decibels::decibelsToGain(getCeilingDb());
        if (mode == Mode::HardClip)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] = juce::jlimit(-ceiling, ceiling, L[i]);
                R[i] = juce::jlimit(-ceiling, ceiling, R[i]);
            }
            gainReductionDb_.store(0.0f, std::memory_order_relaxed);
            return;
        }

        if (mode == Mode::SoftClip)
        {
            oversampling_.upsample(L, R, numSamples);
            auto& os = oversampling_.getBuffer();
            const int n = numSamples * oversampling_.getFactor();
            auto* oL = os.getWritePointer(0);
            auto* oR = os.getWritePointer(1);
            for (int i = 0; i < n; ++i)
            {
                oL[i] = ceiling * std::tanh(oL[i] / juce::jmax(1.0e-6f, ceiling));
                oR[i] = ceiling * std::tanh(oR[i] / juce::jmax(1.0e-6f, ceiling));
            }
            oversampling_.downsample(L, R, numSamples);
            gainReductionDb_.store(0.0f, std::memory_order_relaxed);
            return;
        }

        processLookahead(L, R, numSamples, ceiling);
    }

private:
    void processLookahead(float* L, float* R, int numSamples, float ceiling)
    {
        if (delayL_.empty()) return;
        const int size = (int)delayL_.size();
        float minGain = 1.0f;
        const float attackCoeff = std::exp(-1.0f / (float)juce::jmax(1.0, sampleRate_ * 0.001));
        const float releaseCoeff = std::exp(-1.0f / (float)juce::jmax(1.0, sampleRate_ * 0.050));

        for (int i = 0; i < numSamples; ++i)
        {
            delayL_[(size_t)writePos_] = L[i];
            delayR_[(size_t)writePos_] = R[i];

            float peak = 0.0f;
            for (int k = 0; k < lookaheadSamples_; ++k)
            {
                const int idx = (writePos_ - k + size) % size;
                peak = juce::jmax(peak, std::abs(delayL_[(size_t)idx]), std::abs(delayR_[(size_t)idx]));
            }

            const float targetGain = peak > ceiling ? ceiling / juce::jmax(peak, 1.0e-9f) : 1.0f;
            if (targetGain < gain_)
                gain_ = targetGain + attackCoeff * (gain_ - targetGain);
            else
                gain_ = targetGain + releaseCoeff * (gain_ - targetGain);

            const int readPos = (writePos_ - lookaheadSamples_ + size) % size;
            L[i] = delayL_[(size_t)readPos] * gain_;
            R[i] = delayR_[(size_t)readPos] * gain_;
            minGain = juce::jmin(minGain, gain_);
            writePos_ = (writePos_ + 1) % size;
        }

        gainReductionDb_.store(juce::Decibels::gainToDecibels(juce::jmax(minGain, 1.0e-9f)), std::memory_order_relaxed);
    }

    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    int lookaheadSamples_ = 220;
    int writePos_ = 0;
    float gain_ = 1.0f;
    std::atomic<int> mode_ { (int)Mode::SoftClip };
    std::atomic<float> ceilingDb_ { -0.1f };
    std::atomic<float> gainReductionDb_ { 0.0f };
    MasterOversamplingCore oversampling_;
    std::vector<float> delayL_, delayR_;
};

} // namespace DAW
