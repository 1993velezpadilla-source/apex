#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

namespace DAW {

class MasterDitherCore
{
public:
    enum class Mode { Off = 0, TPDF = 1, NoiseShaped = 2 };

    void prepare(double sampleRate, int, int targetBits)
    {
        sampleRate_ = sampleRate;
        setTargetBits(targetBits);
        shapeErrorL1_ = shapeErrorL2_ = 0.0f;
        shapeErrorR1_ = shapeErrorR2_ = 0.0f;
    }

    void setMode(Mode mode) noexcept { mode_.store((int)mode, std::memory_order_relaxed); }
    Mode getMode() const noexcept { return (Mode)mode_.load(std::memory_order_relaxed); }

    void setTargetBits(int bits) noexcept
    {
        targetBits_.store(juce::jlimit(8, 32, bits), std::memory_order_relaxed);
    }

    int getTargetBits() const noexcept { return targetBits_.load(std::memory_order_relaxed); }

    void process(float* L, float* R, int numSamples) noexcept
    {
        if (L == nullptr || R == nullptr || numSamples <= 0) return;
        const auto mode = getMode();
        const int bits = getTargetBits();

        if (mode != lastMode_ || bits != lastBits_)
        {
            resetShapeState();
            lastMode_ = mode;
            lastBits_ = bits;
        }

        if (mode == Mode::Off || bits >= 32) return;

        const float lsb = 1.0f / (float)(1u << juce::jlimit(1, 30, bits - 1));
        const float maxShapeError = lsb;
        const float maxShapedDither = lsb * 4.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float dL = tpdf() * lsb;
            float dR = tpdf() * lsb;
            if (mode == Mode::NoiseShaped)
            {
                dL = juce::jlimit(-maxShapedDither, maxShapedDither,
                                  dL + 1.5f * shapeErrorL1_ - 0.5f * shapeErrorL2_);
                dR = juce::jlimit(-maxShapedDither, maxShapedDither,
                                  dR + 1.5f * shapeErrorR1_ - 0.5f * shapeErrorR2_);
                const float inL = L[i];
                const float inR = R[i];

                if (! std::isfinite(inL) || ! std::isfinite(inR))
                {
                    resetShapeState();
                    continue;
                }

                L[i] = inL + dL;
                R[i] = inR + dR;
                const float qL = std::round(L[i] / lsb) * lsb;
                const float qR = std::round(R[i] / lsb) * lsb;
                shapeErrorL2_ = shapeErrorL1_;
                shapeErrorR2_ = shapeErrorR1_;
                shapeErrorL1_ = juce::jlimit(-maxShapeError, maxShapeError, L[i] - qL);
                shapeErrorR1_ = juce::jlimit(-maxShapeError, maxShapeError, R[i] - qR);
            }
            else
            {
                L[i] += dL;
                R[i] += dR;
            }
        }
    }

private:
    void resetShapeState() noexcept
    {
        shapeErrorL1_ = shapeErrorL2_ = 0.0f;
        shapeErrorR1_ = shapeErrorR2_ = 0.0f;
    }

    float uniform() noexcept
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return ((rng_ >> 8) * (1.0f / 16777216.0f)) - 0.5f;
    }

    float tpdf() noexcept { return uniform() - uniform(); }

    double sampleRate_ = 44100.0;
    std::atomic<int> mode_ { (int)Mode::Off };
    std::atomic<int> targetBits_ { 24 };
    uint32_t rng_ = 0x12345678u;
    Mode lastMode_ = Mode::Off;
    int lastBits_ = 24;
    float shapeErrorL1_ = 0.0f, shapeErrorL2_ = 0.0f;
    float shapeErrorR1_ = 0.0f, shapeErrorR2_ = 0.0f;
};

} // namespace DAW
