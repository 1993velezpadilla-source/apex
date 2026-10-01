#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace DAW {

/**
 * MonoCheckProcessor
 *
 * Audible mono compatibility check. When enabled, sums L+R and writes the
 * sum to BOTH output channels in-place — the user hears their mix in mono.
 *
 * Critical detail (and what most other DAWs get wrong): when the user
 * toggles mono on/off, the transition is crossfaded over ~10 ms to prevent
 * the click that would otherwise occur at the discontinuity. This is the
 * difference between a tool you actually use during a mix and one you only
 * use after pre-rendering.
 *
 * Sum normalisation:
 *   The naive sum  L+R  doubles amplitude when L == R, which would clip
 *   any mix that already runs near 0 dBFS. We use the standard half-power
 *   sum  (L + R) * 0.5  which preserves perceived loudness when content is
 *   correlated. For decorrelated content this loses 3 dB, which is the
 *   expected and correct behavior for a mono check.
 *
 * Position in signal path:
 *   This processor runs in the MONITOR path, after the master fader and
 *   after the render tap. It MUST NOT affect the export. The MasterBusEngine
 *   places it accordingly. (Or alternatively, it lives in ControlRoomEngine
 *   which already runs post-render — that's the cleaner architectural
 *   placement and is what the master strip wires to.)
 *
 * Thread model:
 *   - setEnabled() called from UI thread; just stores the target bool.
 *   - processBlock() called from audio thread; ramps actual gain toward
 *     target across samples to anti-click.
 */
class MonoCheckProcessor
{
public:
    MonoCheckProcessor() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        // Fade time = 10 ms. Compute per-sample increment for the ramp.
        constexpr double fadeSeconds = 0.010;
        rampStep_ = (float)(1.0 / juce::jmax(1.0, sampleRate_ * fadeSeconds));
        currentMix_ = 0.0f;
    }

    /** UI thread. Toggle audible mono on/off. */
    void setEnabled(bool enabled) noexcept
    {
        targetEnabled_.store(enabled, std::memory_order_relaxed);
    }

    bool isEnabled() const noexcept
    {
        return targetEnabled_.load(std::memory_order_relaxed);
    }

    /**
     * Audio thread. Process the monitor stereo buffer in-place.
     *
     * When fully enabled (currentMix_ == 1), L and R both become (L+R)*0.5.
     * When fully disabled (currentMix_ == 0), L and R are unchanged.
     * During the 10ms ramp, output crossfades smoothly between the two.
     */
    void processBlock(float* L, float* R, int numSamples) noexcept
    {
        if (L == nullptr || R == nullptr || numSamples <= 0) return;

        const float target = targetEnabled_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;

        // Fast path: already at target, no ramp needed.
        if (currentMix_ == target)
        {
            if (target < 0.001f) return;  // bypassed, no-op
            // Fully on: replace both channels with the half-power mono sum
            for (int i = 0; i < numSamples; ++i)
            {
                const float mono = (L[i] + R[i]) * 0.5f;
                L[i] = mono;
                R[i] = mono;
            }
            return;
        }

        // Ramp path: crossfade between original stereo and mono sum.
        for (int i = 0; i < numSamples; ++i)
        {
            // Advance the ramp toward target, clamped.
            if (currentMix_ < target)
                currentMix_ = juce::jmin(target, currentMix_ + rampStep_);
            else
                currentMix_ = juce::jmax(target, currentMix_ - rampStep_);

            const float mono = (L[i] + R[i]) * 0.5f;
            const float dryMix = 1.0f - currentMix_;
            L[i] = L[i] * dryMix + mono * currentMix_;
            R[i] = R[i] * dryMix + mono * currentMix_;
        }
    }

    /** True when the processor is fully muted to original (no mono blend). */
    bool isFullyDisabled() const noexcept
    {
        return currentMix_ < 0.001f && ! targetEnabled_.load(std::memory_order_relaxed);
    }

private:
    double sampleRate_   { 44100.0 };
    float  rampStep_     { 0.0023f };  // ~ 1 / (44100 * 0.010)
    float  currentMix_   { 0.0f };     // 0 = original stereo, 1 = mono sum
    std::atomic<bool> targetEnabled_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonoCheckProcessor)
};

} // namespace DAW
