#pragma once
#include <JuceHeader.h>
#include "C4Types.h"
#include "C4TuningProfile.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4HaloShelfMapper — Phase 2: the intentional perceptual OPEN/HALO mapping.
//
// Replaces the Phase 1 TEMPORARY near-Nyquist clamp behavior (0.49 * fs) for
// the OPEN band's HALO (high-shelf) mode. The HALO control range is
// 2.5..40 kHz (kHaloFreqMinHz..kHaloFreqMaxHz) — deliberately beyond the
// audio range — while the DIGITAL filter can only realize cutoffs strictly
// below Nyquist. A raw 40 kHz control at 44.1 kHz would compute
// g = tan(pi*40000/44100) < 0 — an unrealizable (negative-g) SVF. The mapper
// translates every control frequency into a REALIZABLE in-band shelf
// frequency:
//
//     ceiling = maxRealizableRatio * 0.5 * fs          (0.90 * Nyquist)
//     knee    = ceiling * (1 - 0.20 * softness)        (softness 0 -> ceiling)
//
//     c <= knee:            f_real = c                  (straight, 1:1)
//     c >  knee:            f_real = ceiling - (ceiling - knee) * decay
//         t         = (c - knee) / (kHaloFreqMaxHz - knee)               (0,1]
//         k         = 2 + 8 * (1 - softness)
//         decay     = exp(-k * t)                                         (0,1)
//
//   softness 0 -> the identity holds all the way to the ceiling and the
//                 mapping is the straight/hard-clamp contract ("straight
//                 shelf up to the Nyquist limit")
//   softness 1 -> the identity ends at 0.8 * ceiling and the realized shelf
//                 gives up progressively as the control rises ("progressively
//                 gentler as control rises")
//
// Properties (all tested):
//   - identity for every control at/below the knee (Phase 1 low-control OPEN
//     shelf behavior is preserved)
//   - continuous and MONOTONIC NON-DECREASING in the control frequency
//   - bounded: f_real <= maxRealizableRatio * Nyquist at EVERY sample rate
//   - sample-rate consistent (the ceiling scales with Nyquist)
//   - allocation-free, realtime-safe (pure float math)
//
// `amount` (HALO character gain) is a tuning scalar reserved for Phase 5
// perceptual calibration; Phase 2 maps the frequency only.
// ============================================================================

class C4HaloShelfMapper
{
public:
    /** Map a HALO control frequency (Hz) into a realizable in-band shelf
        frequency (Hz) for the given sample rate and HALO tuning.
        Defensive hardening: the tuning inputs are clamped (softness to
        [0,1], ratio to (0,1]) and every division is guarded, so the function
        is total — it cannot produce NaN, Inf or undefined behavior for any
        inputs (sampleRate must be > 0; callers own that contract). */
    static float mapControlFreq (float controlHz, float sampleRate,
                                 const C4HaloTuning& tuning) noexcept
    {
        jassert (sampleRate > 0.0f);
        const float s = juce::jlimit (0.0f, 1.0f, tuning.mappingSoftness);
        const float ratio = juce::jlimit (0.01f, 1.0f, tuning.maxRealizableRatio);
        const float nyquist = 0.5f * sampleRate;
        const float ceiling = ratio * nyquist;
        const float c = juce::jlimit (kHaloFreqMinHz, kHaloFreqMaxHz, controlHz);

        // Explicit softness-0 branch: the mathematically defined straight /
        // hard-clamp contract — min(c, ceiling) — with NO dependence on the
        // bend formula and NO division.
        if (s <= 0.0f)
            return juce::jmin (c, ceiling);

        const float knee = ceiling * (1.0f - 0.20f * s);

        if (c <= knee)
            return c;

        const float denom = juce::jmax (1.0f, kHaloFreqMaxHz - knee);
        const float t = (c - knee) / denom;
        const float k = 2.0f + 8.0f * (1.0f - s);
        const float decay = std::exp (-k * t);
        return ceiling - (ceiling - knee) * decay;
    }

    /** The realizable ceiling for a sample rate (Hz). */
    static float ceilingHz (float sampleRate, const C4HaloTuning& tuning) noexcept
    {
        return tuning.maxRealizableRatio * 0.5f * sampleRate;
    }
};

} // namespace C4
} // namespace APEX
