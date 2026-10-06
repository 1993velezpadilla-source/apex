#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace DAW {

/**
 * CorrelationMeterCore
 *
 * Computes the stereo correlation coefficient between L and R channels using
 * a Pearson-style normalised cross-correlation over a sliding window.
 *
 * Output range: [-1.0, +1.0]
 *   +1.0 = perfect mono (L == R)
 *    0.0 = uncorrelated (typical wide stereo)
 *   -1.0 = perfectly out of phase (L == -R)  ← THIS IS A BUG WHEN HEARD IN MONO
 *
 * Used by the master strip's Phase/Width section to warn the user when the
 * mix has phase issues that will collapse on mono playback (smartphone
 * speakers, bluetooth speakers, club PA bridged systems, AM radio).
 *
 * Thread model: audio thread writes via processBlock, UI thread reads via
 * getCorrelation(). The intermediate accumulators are NOT atomic — they
 * live exclusively on the audio thread. Only the final smoothed result is
 * published as an atomic.
 *
 * Algorithm details:
 *   - Smoothing time constant: 200 ms (fast enough to feel responsive,
 *     slow enough that brief uncorrelated transients don't make the meter
 *     swing wildly).
 *   - DC component is NOT removed — assumes input is already AC-coupled,
 *     which is true for any audio signal at this point in the master path.
 *   - Numerator and denominator are smoothed separately, then divided.
 *     This is the standard "lock-free Pearson" form used in pro meters.
 */
class CorrelationMeterCore
{
public:
    CorrelationMeterCore() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        constexpr double tau = 0.200;  // 200 ms smoothing
        smoothingCoeff_ = (float)(1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * tau)));
        reset();
    }

    void reset() noexcept
    {
        smoothedXY_ = 0.0f;
        smoothedXX_ = 0.0f;
        smoothedYY_ = 0.0f;
        correlation_.store(0.0f, std::memory_order_relaxed);
    }

    /** Audio thread. Feed one block of stereo data. */
    void processBlock(const float* L, const float* R, int numSamples) noexcept
    {
        if (L == nullptr || R == nullptr || numSamples <= 0) return;

        // Block-level accumulators (audio-thread-only state)
        double sumXY = 0.0;
        double sumXX = 0.0;
        double sumYY = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double l = (double) L[i];
            const double r = (double) R[i];
            sumXY += l * r;
            sumXX += l * l;
            sumYY += r * r;
        }

        const float invN = 1.0f / (float) numSamples;
        const float blockXY = (float) (sumXY * invN);
        const float blockXX = (float) (sumXX * invN);
        const float blockYY = (float) (sumYY * invN);

        // Per-block exponential smoothing
        smoothedXY_ += (blockXY - smoothedXY_) * smoothingCoeff_;
        smoothedXX_ += (blockXX - smoothedXX_) * smoothingCoeff_;
        smoothedYY_ += (blockYY - smoothedYY_) * smoothingCoeff_;

        // Final Pearson coefficient
        const float denom = std::sqrt(juce::jmax(1.0e-12f, smoothedXX_ * smoothedYY_));
        const float c = juce::jlimit(-1.0f, 1.0f, smoothedXY_ / denom);
        correlation_.store(c, std::memory_order_relaxed);
    }

    /** UI thread. Returns the latest smoothed correlation in [-1.0, +1.0]. */
    float getCorrelation() const noexcept
    {
        return correlation_.load(std::memory_order_relaxed);
    }

    /** True when correlation indicates phase issues (< -0.5). */
    bool isPhaseProblem() const noexcept
    {
        return getCorrelation() < -0.5f;
    }

private:
    double sampleRate_      { 44100.0 };
    float  smoothingCoeff_  { 0.001f };

    // Audio-thread-only smoothing state
    float smoothedXY_       { 0.0f };
    float smoothedXX_       { 0.0f };
    float smoothedYY_       { 0.0f };

    std::atomic<float> correlation_ { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorrelationMeterCore)
};

} // namespace DAW
