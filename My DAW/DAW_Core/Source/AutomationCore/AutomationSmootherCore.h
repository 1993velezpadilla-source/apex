#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace DAW {

/** Block-size-invariant one-pole smoothing advancement.
 *
 *  The plugin/chain automation paths apply ONE smoothed parameter value per
 *  audio block. The smoother must advance coherently across the FULL block
 *  length so the per-block applied value does not depend on how the host
 *  segments audio (256 / 512 / 1024 / 2048 / arbitrary non-power-of-two).
 *
 *  advance() is the closed-form equivalent of iterating the one-pole
 *  recurrence  s += (target - s) * coeff  exactly numSamples times:
 *
 *      s_N = target + (s_0 - target) * (1 - coeff)^N
 *
 *  O(1) regardless of block size and exactly block-size invariant.
 */
class AutomationSmootherCore
{
public:
    /** Snap threshold used by the original per-block iteration code. */
    static constexpr float kSnapTolerance = 0.0001f;

    /** One-pole coefficient for a tau-second smoothing time at sampleRate. */
    static float makeCoeff(double sampleRate, double tauSeconds) noexcept
    {
        return 1.0f - std::exp(-1.0f / (float) juce::jmax(1.0, sampleRate * tauSeconds));
    }

    /** Advance a one-pole smoother across numSamples samples.
     *
     *  Returns the new smoothed value. Snaps to the target when the residual
     *  is below kSnapTolerance (same behavior as the former iteration loop's
     *  completion check). numSamples <= 0 returns the target directly.
     */
    static float advance(float lastValue, float target, float coeff, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return target;

        const float decay = std::pow(1.0f - coeff, (float) numSamples);
        const float smoothed = target + (lastValue - target) * decay;

        if (std::abs(smoothed - target) < kSnapTolerance)
            return target;

        return smoothed;
    }
};

} // namespace DAW
