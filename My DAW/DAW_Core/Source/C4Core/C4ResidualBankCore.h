#pragma once
#include <JuceHeader.h>
#include "C4Types.h"
#include "C4TuningProfile.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4ResidualBankCore — Phase 4: BLOOM Nonlinear Color (the residual layer).
//
// Architecture (governing rule: ADDING TONE ADDS COLOR, REMOVING TONE STAYS
// CLEANER; C4 remains an EQ first):
//
//   per band, per channel, per sample:
//     exc        = the band's UNITY shape tap (the signal passing through
//                  that tonal region, WITHOUT the EQ gain — the gain is never
//                  double-applied)
//     u          = exc * 10^(-driveRefDb/20)        (calibrated drive)
//     oddRes     = tanh(u) - u                       (bounded odd shaper minus
//                  its linear component = harmonic residual only)
//     evenRes    = dcBlock( tanh(u)^2 )              (bounded even shaper;
//                  DC deliberately managed by a 15 Hz one-pole HPF on the
//                  even residual ONLY — documented, not a random blocker)
//     residual   = residualLevel * activation * bloomDrive
//                  * ( evenWeight*evenRes + (1-evenWeight)*oddRes )
//     output     = linear output + residual
//
// Properties:
//   - at BLOOM = 0 the whole bank is skipped by the ENGINE (bloomDrive == 0)
//     -> EXACTLY the validated Phase 1-3 linear result, bit-identical, zero
//     oversampling work, zero extra phase/gain/latency, no allocations.
//   - activation comes from the SMOOTHED band gain (positive-intent law:
//     g > 0 -> (g/15)^activationExp; g < 0 -> suppressed by cutSuppression;
//     g == 0 -> exactly 0).
//   - all shapers are bounded (tanh family) — no unbounded polynomial.
//   - oversampling candidates (1x/2x/4x) apply to the NONLINEAR path only:
//     causal backward-linear upsample + averaging decimate, ZERO added
//     latency. The factor is a profile candidate (residualOversample);
//     Phase 4 benchmarks choose the live strategy, Phase 5 freezes it.
//   - the DC blocker's phase/low-frequency consequence: it acts only on the
//     even residual, whose musical content sits far above 15 Hz; measured
//     and documented in the Phase 4 report.
// ============================================================================

class C4ResidualBankCore
{
public:
    // ---- Bounded, realtime-safe shaper candidates (benchmarked) -----------
    // S1 (odd):  tanh — symmetric, H3-leaning, no DC, bounded [-1,1].
    // S2 (even): tanh^2 — H2-leaning, bounded [0,1], contains DC (managed).
    static float shaperOdd (float u) noexcept  { return std::tanh (u); }
    static float shaperEven (float u) noexcept
    {
        const float t = std::tanh (u);
        return t * t;
    }

    void prepare (double sampleRate, const C4TuningProfile& profile) noexcept
    {
        oversample_ = juce::jlimit (1, 4, profile.residualOversample);
        // DC blocker pole: ~15 Hz high-pass, applied to the even residual
        // only. Deliberate design (not a random DC blocker): the even
        // residual's musical content is far above 15 Hz.
        dcPole_ = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 15.0
                                    / juce::jmax (1.0, sampleRate));
    }

    void reset() noexcept
    {
        for (auto& band : state_)
            for (auto& ch : band)
                ch = PerBandState {};
    }

    /** Per-band, per-channel residual sample. `activation` (0..1) carries the
        smoothed-gain intent; `bloomDrive` (0..1) the BLOOM control law. */
    float process (int band, int channel, float exc, float activation,
                   float bloomDrive, const C4BloomColorTuning& tuning) noexcept
    {
        jassert (band >= 0 && band < kNumBands);
        jassert (channel >= 0 && channel < kMaxChannels);

        auto& st = state_[band][channel];
        st.prevExc_ = exc; // keep the interpolation state warm at all times

        if (activation <= 0.0f || bloomDrive <= 0.0f || tuning.residualLevel <= 0.0f)
        {
            st.phase_ = 0.0f;
            return 0.0f;
        }

        const float drive = dbToGain (-tuning.driveRefDb); // 10^(-driveRefDb/20)
        const float u = exc * drive;
        const float evenW = tuning.evenWeight;
        const int n = oversample_;
        float sum = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // Causal backward-linear interpolation for the upsampled phases
            // (zero latency; a slight interpolation tilt on the excitation,
            // measured in the alias benchmark).
            const float t = (float) i / (float) n;
            const float uPhase = (i == 0) ? u : (u * (1.0f - t) + st.prevExc_ * t);

            const float oddRes = shaperOdd (uPhase) - uPhase; // fundamental removed
            const float evenRes = dcBlock (shaperEven (uPhase), st);

            sum += evenW * evenRes + (1.0f - evenW) * oddRes;
        }

        st.phase_ = (float) (((int) st.phase_ + 1) % n);
        const float residual = tuning.residualLevel * activation * bloomDrive
                             * (sum / (float) n);
        return residual;
    }

private:
    struct PerBandState
    {
        float dcX1_ = 0.0f, dcY1_ = 0.0f; // even-residual DC blocker (one-pole HPF)
        float prevExc_ = 0.0f;            // oversample interpolation memory
        float phase_ = 0.0f;              // phase counter (kept aligned)
    };

    float dcBlock (float x, PerBandState& st) noexcept
    {
        // y = x - x1 + p * y1  (first-order high-pass)
        const float y = x - st.dcX1_ + dcPole_ * st.dcY1_;
        st.dcX1_ = x;
        st.dcY1_ = y;
        // Denormal flush (bounded, allocation-free).
        if (std::abs (st.dcX1_) < 1.0e-30f) st.dcX1_ = 0.0f;
        if (std::abs (st.dcY1_) < 1.0e-30f) st.dcY1_ = 0.0f;
        return y;
    }

    int oversample_ = 1;
    float dcPole_ = 0.998f;
    PerBandState state_[kNumBands][kMaxChannels];
};

} // namespace C4
} // namespace APEX
