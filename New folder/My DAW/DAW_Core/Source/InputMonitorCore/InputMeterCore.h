#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace DAW {

/** Per-track input peak/RMS meter updated from the audio thread. */
class InputMeterCore
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        peakDecayPerSample_ = std::pow(10.0f, -17.0f / 20.0f / (float) sampleRate_);
    }

    void processBlock(const float* L, const float* R, int numSamples) noexcept
    {
        if (numSamples <= 0) return;

        float newPeakL = 0.0f, newPeakR = 0.0f;
        double sumSqL = 0.0, sumSqR = 0.0;

        for (int s = 0; s < numSamples; ++s)
        {
            const float l = L ? std::abs(L[s]) : 0.0f;
            const float r = R ? std::abs(R[s]) : 0.0f;
            if (l > newPeakL) newPeakL = l;
            if (r > newPeakR) newPeakR = r;
            sumSqL += (double) l * l;
            sumSqR += (double) r * r;
        }

        const float decayMult = std::pow(peakDecayPerSample_, (float) numSamples);
        float dL = peakLevelL_.load(std::memory_order_relaxed) * decayMult;
        float dR = peakLevelR_.load(std::memory_order_relaxed) * decayMult;
        if (newPeakL > dL) dL = newPeakL;
        if (newPeakR > dR) dR = newPeakR;
        peakLevelL_.store(dL, std::memory_order_relaxed);
        peakLevelR_.store(dR, std::memory_order_relaxed);

        rmsLevelL_.store((float) std::sqrt(sumSqL / numSamples), std::memory_order_relaxed);
        rmsLevelR_.store((float) std::sqrt(sumSqR / numSamples), std::memory_order_relaxed);
    }

    float getPeakLevelL() const noexcept { return peakLevelL_.load(std::memory_order_relaxed); }
    float getPeakLevelR() const noexcept { return peakLevelR_.load(std::memory_order_relaxed); }
    float getRmsLevelL()  const noexcept { return rmsLevelL_.load(std::memory_order_relaxed); }
    float getRmsLevelR()  const noexcept { return rmsLevelR_.load(std::memory_order_relaxed); }

    void reset() noexcept
    {
        peakLevelL_.store(0.0f, std::memory_order_relaxed);
        peakLevelR_.store(0.0f, std::memory_order_relaxed);
        rmsLevelL_.store(0.0f, std::memory_order_relaxed);
        rmsLevelR_.store(0.0f, std::memory_order_relaxed);
    }

private:
    double sampleRate_          { 44100.0 };
    float  peakDecayPerSample_  { 1.0f };
    std::atomic<float> peakLevelL_ { 0.0f };
    std::atomic<float> peakLevelR_ { 0.0f };
    std::atomic<float> rmsLevelL_  { 0.0f };
    std::atomic<float> rmsLevelR_  { 0.0f };
};

} // namespace DAW
