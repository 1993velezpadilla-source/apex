#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace DAW {

/**
 * StereoWidthCore
 *
 * Measures perceived stereo width via Mid/Side energy ratio.
 *
 *   Mid  (M) = (L + R) * 0.5    — common content (mono-compatible)
 *   Side (S) = (L - R) * 0.5    — stereo-only content
 *
 * Width metric:
 *   widthRatio = sqrt(sideEnergy / (midEnergy + sideEnergy))
 *
 *   widthRatio = 0.0   -> pure mono (no side content)
 *   widthRatio = 0.707 -> "balanced stereo" (M and S equal energy)
 *   widthRatio = 1.0   -> only side content (very unusual, often a mistake)
 *
 * UI presentation as a percentage:
 *   ratio [0, 0.707]   maps to [0%, 50%]   linearly  ("undersized stereo" range)
 *   ratio [0.707, 1.0] maps to [50%, 100%] linearly  ("oversized stereo" range)
 *
 *   So 50% is the "normal commercial mix" anchor point, 0% is mono, 100% is
 *   pathologically side-heavy.
 *
 * Used by the master strip Phase/Width section to give the user a quick read
 * on stereo health: catches mono-summed busses, over-widened masters, and
 * mixes that need M/S correction.
 *
 * Thread model: same as CorrelationMeterCore. Audio thread accumulates,
 * UI thread reads atomic.
 */
class StereoWidthCore
{
public:
    StereoWidthCore() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        constexpr double tau = 0.200;
        smoothingCoeff_ = (float)(1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * tau)));
        reset();
    }

    void reset() noexcept
    {
        smoothedMidEnergy_  = 0.0f;
        smoothedSideEnergy_ = 0.0f;
        widthPercent_.store(50.0f, std::memory_order_relaxed);
    }

    /** Audio thread. Feed one block of stereo data. */
    void processBlock(const float* L, const float* R, int numSamples) noexcept
    {
        if (L == nullptr || R == nullptr || numSamples <= 0) return;

        double midEnergy  = 0.0;
        double sideEnergy = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double m = (L[i] + R[i]) * 0.5;
            const double s = (L[i] - R[i]) * 0.5;
            midEnergy  += m * m;
            sideEnergy += s * s;
        }

        const float invN = 1.0f / (float) numSamples;
        const float blockMid  = (float) (midEnergy  * invN);
        const float blockSide = (float) (sideEnergy * invN);

        smoothedMidEnergy_  += (blockMid  - smoothedMidEnergy_)  * smoothingCoeff_;
        smoothedSideEnergy_ += (blockSide - smoothedSideEnergy_) * smoothingCoeff_;

        const float total = smoothedMidEnergy_ + smoothedSideEnergy_;
        if (total < 1.0e-12f)
        {
            widthPercent_.store(50.0f, std::memory_order_relaxed);
            return;
        }

        const float ratio = std::sqrt(smoothedSideEnergy_ / total);

        // Two-segment map so 0.707 (balanced M/S) anchors at exactly 50%
        constexpr float kBalancedRatio = 0.7071068f;  // sqrt(0.5)
        float percent;
        if (ratio <= kBalancedRatio)
            percent = juce::jmap(ratio, 0.0f, kBalancedRatio, 0.0f, 50.0f);
        else
            percent = juce::jmap(ratio, kBalancedRatio, 1.0f, 50.0f, 100.0f);

        widthPercent_.store(juce::jlimit(0.0f, 100.0f, percent),
                            std::memory_order_relaxed);
    }

    /** UI thread. Returns 0..100 percentage. */
    float getWidthPercent() const noexcept
    {
        return widthPercent_.load(std::memory_order_relaxed);
    }

    /** Returns true when the mix is essentially mono (no side energy). */
    bool isCollapsedToMono() const noexcept
    {
        return getWidthPercent() < 5.0f;
    }

    /** Returns true when stereo width is excessive (often translates poorly). */
    bool isOverWidened() const noexcept
    {
        return getWidthPercent() > 85.0f;
    }

private:
    double sampleRate_       { 44100.0 };
    float  smoothingCoeff_   { 0.001f };

    float smoothedMidEnergy_  { 0.0f };
    float smoothedSideEnergy_ { 0.0f };

    std::atomic<float> widthPercent_ { 50.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoWidthCore)
};

} // namespace DAW
