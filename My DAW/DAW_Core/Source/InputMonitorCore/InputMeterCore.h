#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <array>
#include "../AnalogVuMeterCore/ClassicVuDetectorCore.h"
#include "../AnalogVuMeterCore/VuChannelMode.h"

namespace DAW {

/** One audio-thread owner publishes peak, block RMS and audio-clock VU.
    UI readers only consume atomics: no timer-driven detector or destructive reads. */
class InputMeterCore
{
public:
    InputMeterCore() noexcept { reset(); }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        peakDecayPerSample_ = std::pow(10.0f, -17.0f / 20.0f / (float) sampleRate_);
        vuL_.prepare(sampleRate_);
        vuR_.prepare(sampleRate_);
        reset();
    }

    void processBlock(const float* L, const float* R, int numSamples) noexcept
    {
        if (numSamples <= 0) return;
        const juce::ScopedNoDenormals noDenormals;

        float newPeakL = 0.0f, newPeakR = 0.0f;
        double sumSqL = 0.0, sumSqR = 0.0;
        float vuL = 0.0f, vuR = 0.0f;
        std::array<float, 5> vuMax {};

        for (int s = 0; s < numSamples; ++s)
        {
            const float l = L && std::isfinite(L[s]) ? std::abs(L[s]) : 0.0f;
            const float r = R && std::isfinite(R[s]) ? std::abs(R[s]) : 0.0f;
            if (l > newPeakL) newPeakL = l;
            if (r > newPeakR) newPeakR = r;
            sumSqL += (double) l * l;
            sumSqR += (double) r * r;
            vuL = vuL_.process(l);
            vuR = vuR_.process(r);
            // Hold the maximum of each combined reading at the same instant,
            // not an average of L/R maxima which may occur at different times.
            const std::array<float, 5> levels { juce::jmax(vuL, vuR), vuL, vuR,
                                                0.5f * (vuL + vuR), vuL + vuR };
            for (size_t i = 0; i < vuMax.size(); ++i)
                vuMax[i] = juce::jmax(vuMax[i], levels[i]);
        }

        const float decayMult = std::pow(peakDecayPerSample_, (float) numSamples);
        float dL = peakLevelL_.load(std::memory_order_relaxed) * decayMult;
        float dR = peakLevelR_.load(std::memory_order_relaxed) * decayMult;
        if (newPeakL > dL) dL = newPeakL;
        if (newPeakR > dR) dR = newPeakR;

        // Guard against non-finite signal (plugin/clip output, denormal edge
        // cases): a NaN/Inf peak would otherwise be stored and permanently
        // peg the VU needle (jmax/ballistics never recover from inf).
        if (!std::isfinite(dL)) dL = 0.0f;
        if (!std::isfinite(dR)) dR = 0.0f;

        peakLevelL_.store(dL, std::memory_order_relaxed);
        peakLevelR_.store(dR, std::memory_order_relaxed);

        publishPeakMax(peakMaxLevelL_, newPeakL);
        publishPeakMax(peakMaxLevelR_, newPeakR);

        float rmsL = (float) std::sqrt(sumSqL / numSamples);
        float rmsR = (float) std::sqrt(sumSqR / numSamples);
        if (!std::isfinite(rmsL)) rmsL = 0.0f;
        if (!std::isfinite(rmsR)) rmsR = 0.0f;
        rmsLevelL_.store(rmsL, std::memory_order_relaxed);
        rmsLevelR_.store(rmsR, std::memory_order_relaxed);
        vuLevelL_.store(vuL, std::memory_order_relaxed);
        vuLevelR_.store(vuR, std::memory_order_relaxed);
        for (size_t i = 0; i < vuMax.size(); ++i)
            publishPeakMax(vuMaxLevels_[i], vuMax[i]);
    }

    float getPeakLevelL() const noexcept { return peakLevelL_.load(std::memory_order_relaxed); }
    float getPeakLevelR() const noexcept { return peakLevelR_.load(std::memory_order_relaxed); }
    float getPeakMaxLevelL() const noexcept { return peakMaxLevelL_.load(std::memory_order_relaxed); }
    float getPeakMaxLevelR() const noexcept { return peakMaxLevelR_.load(std::memory_order_relaxed); }
    float getRmsLevelL()  const noexcept { return rmsLevelL_.load(std::memory_order_relaxed); }
    float getRmsLevelR()  const noexcept { return rmsLevelR_.load(std::memory_order_relaxed); }

    float getVuLevel(VuChannelMode mode) const noexcept
    {
        return combineChannelPeaks(vuLevelL_.load(std::memory_order_relaxed),
                                   vuLevelR_.load(std::memory_order_relaxed), mode);
    }
    float getVuMaxLevel(VuChannelMode mode) const noexcept
    {
        return vuMaxLevels_[(size_t) juce::jlimit(0, 4, (int) mode)].load(std::memory_order_relaxed);
    }
    static float levelToDb(float gain) noexcept
    {
        return std::isfinite(gain) && gain > 1.0e-6f ? 20.0f * std::log10(gain) : -120.0f;
    }
    float getVuDb() const noexcept
    {
        const float db = levelToDb(getVuLevel(getVuChannelMode()));
        return db <= -120.0f ? -120.0f : db - getVuReferenceDb();
    }
    float getVuMaxDb() const noexcept
    {
        const float db = levelToDb(getVuMaxLevel(getVuChannelMode()));
        return db <= -120.0f ? -120.0f : db - getVuReferenceDb();
    }
    float getSamplePeakMaxDb() const noexcept
    {
        // Stereo peak must expose clipping on either channel, even when the
        // VU needle uses the average of channel levels.
        return levelToDb(juce::jmax(getPeakMaxLevelL(), getPeakMaxLevelR()));
    }
    void setVuReferenceDb(float db) noexcept
    {
        vuReferenceDb_.store(std::isfinite(db) ? juce::jlimit(-30.0f, 0.0f, db) : -18.0f,
                             std::memory_order_relaxed);
    }
    float getVuReferenceDb() const noexcept { return vuReferenceDb_.load(std::memory_order_relaxed); }
    void setVuChannelMode(VuChannelMode mode) noexcept
    {
        const int value = (int) mode;
        vuChannelMode_.store(value >= 0 && value <= 4 ? value : (int) VuChannelMode::Average,
                             std::memory_order_relaxed);
    }
    VuChannelMode getVuChannelMode() const noexcept
    {
        return (VuChannelMode) vuChannelMode_.load(std::memory_order_relaxed);
    }
    void resetVuMax() noexcept
    {
        for (auto& level : vuMaxLevels_) level.store(0.0f, std::memory_order_relaxed);
    }

    void resetPeakHold() noexcept
    {
        peakMaxLevelL_.store(0.0f, std::memory_order_relaxed);
        peakMaxLevelR_.store(0.0f, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        peakLevelL_.store(0.0f, std::memory_order_relaxed);
        peakLevelR_.store(0.0f, std::memory_order_relaxed);
        resetPeakHold();
        rmsLevelL_.store(0.0f, std::memory_order_relaxed);
        rmsLevelR_.store(0.0f, std::memory_order_relaxed);
        vuL_.reset();
        vuR_.reset();
        vuLevelL_.store(0.0f, std::memory_order_relaxed);
        vuLevelR_.store(0.0f, std::memory_order_relaxed);
        resetVuMax();
    }

private:
    static void publishPeakMax(std::atomic<float>& destination, float value) noexcept
    {
        if (!std::isfinite(value) || value <= 0.0f)
            return;
        float previous = destination.load(std::memory_order_relaxed);
        while (value > previous
               && !destination.compare_exchange_weak(previous, value,
                                                     std::memory_order_relaxed)) {}
    }

    double sampleRate_          { 44100.0 };
    float  peakDecayPerSample_  { 1.0f };
    std::atomic<float> peakLevelL_ { 0.0f };
    std::atomic<float> peakLevelR_ { 0.0f };
    std::atomic<float> peakMaxLevelL_ { 0.0f };
    std::atomic<float> peakMaxLevelR_ { 0.0f };
    std::atomic<float> rmsLevelL_  { 0.0f };
    std::atomic<float> rmsLevelR_  { 0.0f };
    ClassicVuDetectorCore vuL_, vuR_;
    std::atomic<float> vuLevelL_ { 0.0f }, vuLevelR_ { 0.0f };
    std::array<std::atomic<float>, 5> vuMaxLevels_ {};
    std::atomic<float> vuReferenceDb_ { -18.0f };
    std::atomic<int> vuChannelMode_ { (int) VuChannelMode::Average };
};

} // namespace DAW
