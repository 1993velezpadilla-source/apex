#pragma once
#include <JuceHeader.h>
#include <complex>
#include "G10CurveEngineCore.h" // frozen G10SvfSection (TPT SVF) reused for the clean filters

namespace APEX {
namespace G10 {

// ============================================================================
// G10MiniEqCore — the CLEAN digital correction layer (Mini Clean EQ).
//
// Personality: completely transparent, minimum-phase, mathematically defined.
// No analog emulation, no saturation, no resonance bumps, no musical
// proportional-Q behavior. This is intentionally the OPPOSITE personality of
// the frozen musical G10 core (D1B / I1 / the ten musical bands).
//
// Structure (fixed, hard limits):
//   HPF (2nd-order Butterworth, 12 dB/oct, Q = 0.70710678)
//   Bell 1..3 (clean RBJ-style peaking biquads)
//   LPF (2nd-order Butterworth, 12 dB/oct, Q = 0.70710678)
//
// Signal position (see G10Processor::processChunk): AFTER the complete
// oversampled musical/color path and AFTER downsampling, BEFORE Output Trim
// and the analyzer tap — so the Mini EQ can never change how the signal
// excites D1B/I1, and the analyzer measures the true final output.
//
// OFF semantics (true bypass, never "fake OFF" with an ultra-low cutoff):
//   - HPF  OFF: normalized target == 0.0
//   - LPF  OFF: normalized target == 1.0
//   - Bell OFF: enabled == false  (an enabled Bell at gain == 0.0 dB is ALSO
//     skipped — zero floating-point difference from that Bell)
//   - Bell BYPASS (v3): the Bell REMAINS present; only its DSP contribution
//     crossfades to identity with the same 5 ms ramp. While fully bypassed
//     (or disabled) and the rest of the stage is off, the core takes the
//     settled-skip path: NO per-sample audio work and NO hidden smoothers
//     run (bit-identical pass-through). Freq/Gain/Q edits made while a Bell
//     is inaudible are adopted INSTANTLY and INAUDIBLY at re-activation
//     (snapped to the current targets while the mix is still exactly 0),
//     so un-bypass starts from the newly edited configuration and only the
//     ~5 ms mix fade-in is audible. While the loop runs for another stage,
//     a fully out Bell's smoothed parameters stay FROZEN at the last active
//     tuning (they are snapped on re-activation, never stepped).
//   - Bypass:  the whole Mini EQ stage crossfades out when the processor's
//     bypass engages.
// Every stage uses a realtime-safe 10 ms mix/gain ramp, so entering/leaving
// OFF cannot click, and when everything is settled OFF the stage is skipped
// entirely (block-level, zero cost, bit-identical to the frozen G10).
//
// Control laws v3 (shared with the UI so DSP and display agree). Both ACTIVE
// cutoff filters share the SAME log-frequency law as the Bell nodes and the
// analyzer X axis (p = log(hz/20)/log(1000)), so node X, analyzer X and
// cutoff Hz are mathematically aligned:
//   HPF : p == 0 -> OFF; p in (0,1] -> 20 * 1000^p Hz      (20 Hz..20 kHz)
//   LPF : p == 1 -> OFF; p in [0,1) -> 20 * 1000^p Hz      (20 Hz..20 kHz)
//   Bell freq : 20 * 1000^p Hz                              (20 Hz..20 kHz)
//   Bell gain : -12 + 24p dB                                (-12..+12 dB)
//   Bell Q    : 0.10 * 400^p                                (0.10..40.0)
//
// Legacy v2 laws are preserved EXACTLY as explicit helpers for semantic
// state migration (old normalized -> old Hz/Q -> new normalized):
//   HPF : 20 * 21^p                    (20..420 Hz)
//   LPF : 8500 * (16500/8500)^p        (8.5..16 kHz, law headroom 16500)
//   Bell Q : 0.5 * 20^p                (0.5..10)
//
// Nyquist safety: coefficient generation clamps the effective cutoff to
// 0.45 * sampleRate; the stored/public parameter is NEVER altered by the
// sample rate.
//
// Thread contract: prepare()/reset() on the message thread; processBlock()
// on the audio thread only. No allocation, no locks, no strings, no GUI.
// All state is preallocated member storage.
// ============================================================================

class G10MiniEqCore
{
public:
    static constexpr int kMaxBells = 3;
    static constexpr int kMaxChannels = G10CurveEngineCore::kMaxChannels;

    // Product contract v3 (permanent).
    static constexpr float kHpfOffNorm = 0.0f;
    static constexpr float kHpfMinHz = 20.0f;
    static constexpr float kHpfMaxHz = 20000.0f;
    static constexpr float kLpfOffNorm = 1.0f;
    static constexpr float kLpfMinHz = 20.0f;
    static constexpr float kLpfMaxHz = 20000.0f;
    static constexpr float kBellMinHz = 20.0f;
    static constexpr float kBellMaxHz = 20000.0f;
    static constexpr float kBellMinDb = -12.0f;
    static constexpr float kBellMaxDb = 12.0f;
    static constexpr float kBellMinQ = 0.10f;
    static constexpr float kBellMaxQ = 40.0f;
    static constexpr float kBellDefaultQ = 1.0f;

    // ---- Legacy v2 laws (SEMANTIC migration helpers only; NEVER used for
    // live control. Exact copies of the pre-v3 behavior so an old session
    // restores the identical cutoff/Q the user heard.) ----------------------
    //   HPF : p == 0 -> OFF; p in (0,1] -> 20 * 21^p Hz        (20..420 Hz)
    //   LPF : p == 1 -> OFF; p in [0,1) -> 8500 * (16/8.5)^p Hz (8.5..16 kHz;
    //         the old law extended ABOVE the public 16 kHz contract by 33/32
    //         so 16 kHz was an ACTIVE position at p < 1)
    //   Bell Q : 0.5 * 20^p                                    (0.5..10)
    static constexpr float kLegacyHpfMaxHz = 420.0f;
    static constexpr float kLegacyLpfMinHz = 8500.0f;
    static constexpr float kLegacyLpfMaxHz = 16000.0f;
    static constexpr float kLegacyLpfLawMaxHz = kLegacyLpfMaxHz * 1.03125f; // 16500
    static constexpr float kLegacyBellMinQ = 0.5f;
    static constexpr float kLegacyBellMaxQ = 10.0f;

    // Internal safety limit for coefficient generation (never stored).
    static constexpr float kNyquistSafetyFraction = 0.45f;
    // ~5 ms insertion/removal ramps (target; only raised if an extreme
    // transition measurably cannot stay click-free at 5 ms).
    static constexpr float kSmoothSeconds = 0.005f;

    struct BellTarget
    {
        bool enabled = false;
        bool bypassed = false; // v3: Bell stays present; contribution fades out
        float freqNorm = 0.5f; // 0..1 (log law above)
        float gainDb = 0.0f;   // -12..+12
        float qNorm = 0.5f;    // 0..1 (log law above)
    };

    struct Targets
    {
        float hpfNorm = kHpfOffNorm;
        float lpfNorm = kLpfOffNorm;
        BellTarget bell[kMaxBells];
        bool bypass = false;
    };

    // ---- Control laws v3 (normalized <-> product units) -------------------
    // The ACTIVE HPF and LPF share the SAME log-frequency law as the Bell
    // nodes and the analyzer X axis: p == 0 -> 20 Hz, p == 1 -> 20 kHz.
    // OFF is a true bypass sentinel (HPF p == 0; LPF p == 1).

    static float hpfHzFromNorm (float p) noexcept
    {
        return p <= 0.0f ? 0.0f : kHpfMinHz * std::pow (kHpfMaxHz / kHpfMinHz, p);
    }

    static float lpfHzFromNorm (float p) noexcept
    {
        if (p >= 1.0f) return 0.0f; // true OFF (p == 1 is the OFF endpoint)
        return kLpfMinHz * std::pow (kLpfMaxHz / kLpfMinHz, p);
    }

    static float bellHzFromNorm (float p) noexcept
    {
        return kBellMinHz * std::pow (kBellMaxHz / kBellMinHz, p);
    }

    static float bellGainDbFromNorm (float p) noexcept
    {
        return kBellMinDb + (kBellMaxDb - kBellMinDb) * p;
    }

    static float bellQFromNorm (float p) noexcept
    {
        return kBellMinQ * std::pow (kBellMaxQ / kBellMinQ, p); // 0.10 * 400^p
    }

    // Inverse laws (UI: units -> normalized).
    static float hpfNormFromHz (float hz) noexcept
    {
        if (hz <= 0.0f) return kHpfOffNorm;
        return juce::jlimit (0.0f, 1.0f,
            std::log (hz / kHpfMinHz) / std::log (kHpfMaxHz / kHpfMinHz));
    }

    static float lpfNormFromHz (float hz) noexcept
    {
        if (hz <= 0.0f) return kLpfOffNorm;
        return juce::jlimit (0.0f, 1.0f,
            std::log (hz / kLpfMinHz) / std::log (kLpfMaxHz / kLpfMinHz));
    }

    static float bellNormFromHz (float hz) noexcept
    {
        return juce::jlimit (0.0f, 1.0f,
            std::log (hz / kBellMinHz) / std::log (kBellMaxHz / kBellMinHz));
    }

    static float bellNormFromGainDb (float db) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (db - kBellMinDb) / (kBellMaxDb - kBellMinDb));
    }

    static float bellNormFromQ (float q) noexcept
    {
        return juce::jlimit (0.0f, 1.0f,
            std::log (q / kBellMinQ) / std::log (kBellMaxQ / kBellMinQ));
    }

    // ---- Legacy v2 decode helpers (EXACT pre-v3 laws; migration only) -----

    static float legacyHpfHzFromNorm (float p) noexcept
    {
        return p <= 0.0f ? 0.0f : kHpfMinHz * std::pow (kLegacyHpfMaxHz / kHpfMinHz, p);
    }

    static float legacyLpfHzFromNorm (float p) noexcept
    {
        if (p >= 1.0f) return 0.0f; // old true OFF endpoint
        return kLegacyLpfMinHz * std::pow (kLegacyLpfLawMaxHz / kLegacyLpfMinHz, p);
    }

    static float legacyBellQFromNorm (float p) noexcept
    {
        return kLegacyBellMinQ * std::pow (kLegacyBellMaxQ / kLegacyBellMinQ, p);
    }

    // ---- Lifecycle ---------------------------------------------------------

    void prepare (double sampleRate, int numChannels) noexcept
    {
        sampleRate_ = juce::jmax (1.0, sampleRate);
        numChannels_ = juce::jlimit (1, kMaxChannels, numChannels);
        smoothCoeff_ = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * kSmoothSeconds));
        reset();
    }

    void reset() noexcept
    {
        hpfMix_ = 0.0f;
        lpfMix_ = 0.0f;
        bypassMix_ = 1.0f; // not bypassed by default
        for (auto& b : bellMix_) b = 0.0f;
        for (auto& b : bellSmoothedGainDb_) b = 0.0f;
        for (auto& b : bellSmoothedFreqNorm_) b = 0.5f;
        for (auto& b : bellSmoothedQNorm_) b = 0.5f;
        hpfSmoothedNorm_ = kHpfOffNorm;
        lpfSmoothedNorm_ = kLpfOffNorm;
        for (auto& s : hpfSvf_) s.reset();
        for (auto& s : lpfSvf_) s.reset();
        for (auto& b : bellBiquads_) for (auto& s : b) s.reset();
    }

    /** True when every stage is settled OFF — either the whole Mini EQ is
        bypassed (bypassMix_ == 0) or every filter stage has fully left
        (all mixes exactly 0). The caller skips the entire stage with zero
        cost, leaving the samples bit-identical. */
    bool isSettledOff() const noexcept
    {
        if (bypassMix_ == 0.0f)
            return true; // whole stage out of circuit (settled bypass)
        if (hpfMix_ != 0.0f) return false;
        if (lpfMix_ != 0.0f) return false;
        for (int i = 0; i < kMaxBells; ++i)
            if (bellMix_[i] != 0.0f) return false;
        return true;
    }

    /** Audio thread: run the Mini Clean EQ cascade in place.
        @param data    channel pointers (numChannels_ channels), in place
        @param numSamples block length
        @param targets plain-float reads of the current parameter values
    */
    void processBlock (float* const* data, int numSamples, const Targets& targets) noexcept
    {
        const int channels = numChannels_;
        if (channels <= 0 || numSamples <= 0)
            return;

        // --- Settled-OFF skip (zero cost, bit-identical pass-through) -------
        // Skip ONLY when both the current smoothed state AND the incoming
        // targets agree that every stage is off. If any target wants a stage
        // active, we must process so the ramps can move. This keeps the
        // fresh/legacy all-OFF case instant (no initial transition) while a
        // later parameter change always starts a smooth ramp. A bypassed
        // Bell (enabled but out of circuit) does NOT keep the stage alive:
        // once its mix settles to zero the whole stage can be skipped.
        const bool anyBellWanted = [&]
        {
            for (int i = 0; i < kMaxBells; ++i)
                if (targets.bell[i].enabled && ! targets.bell[i].bypassed)
                    return true;
            return false;
        }();
        const bool targetsAllOff = (targets.hpfNorm <= 0.0f)
                                && (targets.lpfNorm >= 1.0f)
                                && ! anyBellWanted;
        if (targetsAllOff && isSettledOff())
        {
            resetFilterStates(); // clean re-entry for the next activation
            return;
        }

        const float c = smoothCoeff_;
        const float butterQ = 0.70710678f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Per-sample smoothed ramps (the 10 ms time constant is per
            // SAMPLE — advancing per block would stretch it to ~5 s).
            // OFF-transition rule: while a stage is crossfading OUT its
            // parameters stay FROZEN at the last active tuning (only the mix
            // moves); they snap to the OFF endpoint only after the mix has
            // fully left. This keeps the response constant during the fade so
            // the transition is click-free. When a target is ACTIVE the
            // parameters always track it (smooth in-ramp from identity).
            advanceMix (bypassMix_, targets.bypass ? 0.0f : 1.0f, c);
            advanceMix (hpfMix_, targets.hpfNorm > 0.0f ? 1.0f : 0.0f, c);
            advanceMix (lpfMix_, targets.lpfNorm < 1.0f ? 1.0f : 0.0f, c);

            if (targets.hpfNorm > 0.0f || hpfMix_ == 0.0f)
                hpfSmoothedNorm_ = smooth (hpfSmoothedNorm_, targets.hpfNorm, c);
            if (targets.lpfNorm < 1.0f || lpfMix_ == 0.0f)
                lpfSmoothedNorm_ = smooth (lpfSmoothedNorm_, targets.lpfNorm, c);

            for (int i = 0; i < kMaxBells; ++i)
            {
                const bool wantsActive = targets.bell[i].enabled && ! targets.bell[i].bypassed;
                const bool mixIsZero = (bellMix_[i] == 0.0f);

                // Entering from silence — fresh enable OR un-bypass: adopt
                // the user's CURRENT Freq/Gain/Q instantly while the mix is
                // still exactly 0. Inaudible by construction (the Bell is
                // fully out of circuit this sample), and un-bypass therefore
                // starts from the newly edited configuration; only the ~5 ms
                // mix fade-in is audible. This is the ONLY sync path for the
                // settled-skip case: while a Bell is fully bypassed (or
                // disabled) and the rest of the stage is off, the core
                // early-skips above and NO per-sample smoothing runs at all —
                // the smoothed parameters simply stay frozen until this snap.
                if (wantsActive && mixIsZero)
                {
                    bellSmoothedFreqNorm_[i] = targets.bell[i].freqNorm;
                    bellSmoothedGainDb_[i]   = targets.bell[i].gainDb;
                    bellSmoothedQNorm_[i]     = targets.bell[i].qNorm;
                }
                else if (wantsActive)
                {
                    // Active and fading in / settled: per-sample smoothing
                    // toward the targets.
                    bellSmoothedFreqNorm_[i] = smooth (bellSmoothedFreqNorm_[i], targets.bell[i].freqNorm, c);
                    bellSmoothedGainDb_[i]   = smooth (bellSmoothedGainDb_[i], targets.bell[i].gainDb, c);
                    bellSmoothedQNorm_[i]     = smooth (bellSmoothedQNorm_[i], targets.bell[i].qNorm, c);
                }
                // else: fading OUT (wantsActive == false, mix > 0) — the
                // parameters stay FROZEN at the last active tuning while the
                // mix leaves (constant response during the fade = click-free).

                advanceMix (bellMix_[i], wantsActive ? 1.0f : 0.0f, c);
            }

            for (int ch = 0; ch < channels; ++ch)
            {
                float x = data[ch][sample];
                const float dry = x;

                // HPF (2nd-order Butterworth, true OFF via mix ramp).
                if (hpfMix_ != 0.0f)
                {
                    const float fc = hpfHzFromNorm (hpfSmoothedNorm_);
                    auto& svf = hpfSvf_[ch];
                    svf.setCoefficients (fc, butterQ, (float) sampleRate_);
                    float low, band, high;
                    svf.process (x, low, band, high);
                    x = dry * (1.0f - hpfMix_) + high * hpfMix_;
                }

                // Bells 1..3 (clean RBJ peaking; skipped at 0 dB / disabled).
                for (int i = 0; i < kMaxBells; ++i)
                {
                    if (bellMix_[i] == 0.0f)
                        continue;
                    const float gainDb = bellSmoothedGainDb_[i];
                    if (gainDb == 0.0f)
                        continue; // enabled at exactly 0 dB: no sample difference

                    const float fc = bellHzFromNorm (bellSmoothedFreqNorm_[i]);
                    const float q = bellQFromNorm (bellSmoothedQNorm_[i]);
                    auto& bq = bellBiquads_[i][ch];
                    bq.setCoefficients (fc, q, (float) sampleRate_, gainDb);
                    const float dryBell = x;
                    const float wetBell = bq.process (x);
                    x = dryBell * (1.0f - bellMix_[i]) + wetBell * bellMix_[i];
                }

                // LPF (2nd-order Butterworth, true OFF via mix ramp).
                if (lpfMix_ != 0.0f)
                {
                    const float fc = lpfHzFromNorm (lpfSmoothedNorm_);
                    auto& svf = lpfSvf_[ch];
                    svf.setCoefficients (fc, butterQ, (float) sampleRate_);
                    float low, band, high;
                    svf.process (x, low, band, high);
                    x = dry * (1.0f - lpfMix_) + low * lpfMix_;
                }

                // Final bypass crossfade (whole Mini EQ out of circuit).
                data[ch][sample] = dry * (1.0f - bypassMix_) + x * bypassMix_;
            }
        }
    }

    /** Nominal static magnitude (linear gain) of the whole cascade at a
        frequency — used ONLY by the UI response curve; never claimed to be
        the exact nonlinear G10 transfer. Message thread. */
    float getNominalMagnitude (float hz, float sampleRate,
                               const Targets& targets) const noexcept
    {
        const float sr = juce::jmax (1.0f, sampleRate);
        const float butterQ = 0.70710678f;

        float g = 1.0f;

        if (targets.hpfNorm > 0.0f)
            g *= svfMagnitude (hpfHzFromNorm (targets.hpfNorm), butterQ, hz, sr, true);

        for (int i = 0; i < kMaxBells; ++i)
        {
            // A bypassed Bell is out of circuit: no contribution to the
            // nominal curve (the DSP crossfades it to identity).
            if (! targets.bell[i].enabled || targets.bell[i].bypassed
                || targets.bell[i].gainDb == 0.0f)
                continue;
            g *= bellMagnitude (bellHzFromNorm (targets.bell[i].freqNorm),
                                bellQFromNorm (targets.bell[i].qNorm),
                                targets.bell[i].gainDb, hz, sr);
        }

        if (targets.lpfNorm < 1.0f)
            g *= svfMagnitude (lpfHzFromNorm (targets.lpfNorm), butterQ, hz, sr, false);

        return g;
    }

    /** Nominal cascade magnitude in dB (UI helper). */
    float getNominalMagnitudeDb (float hz, float sampleRate, const Targets& targets) const noexcept
    {
        return gainToDb (juce::jmax (1.0e-30f, getNominalMagnitude (hz, sampleRate, targets)));
    }

private:
    // Clean RBJ peaking biquad (direct form 2 transposed), identical family
    // to the frozen G10ShelfBiquad but for peaking. Gain=0 dB reduces to
    // identity coefficients (b == a), so an enabled 0 dB bell is skipped by
    // the caller before processing.
    class CleanBellBiquad
    {
    public:
        void setCoefficients (float f0Hz, float q, float sampleRate, float db) noexcept
        {
            const float A = std::pow (10.0f, db / 40.0f);
            const float w0 = 2.0f * juce::MathConstants<float>::pi
                           * juce::jlimit (1.0f, (float) sampleRate * 0.5f - 1.0f, f0Hz)
                           / juce::jmax (1.0f, sampleRate);
            const float cosW0 = std::cos (w0);
            const float alpha = std::sin (w0) / (2.0f * juce::jmax (0.05f, q));

            const float b0 = 1.0f + alpha * A;
            const float b1 = -2.0f * cosW0;
            const float b2 = 1.0f - alpha * A;
            const float a0 = 1.0f + alpha / A;
            const float a1 = -2.0f * cosW0;
            const float a2 = 1.0f - alpha / A;

            b0_ = b0 / a0;
            b1_ = b1 / a0;
            b2_ = b2 / a0;
            a1_ = a1 / a0;
            a2_ = a2 / a0;
        }

        float process (float x) noexcept
        {
            const float y = b0_ * x + s1_;
            s1_ = b1_ * x - a1_ * y + s2_;
            s2_ = b2_ * x - a2_ * y;

            if (std::abs (s1_) < 1.0e-30f) s1_ = 0.0f;
            if (std::abs (s2_) < 1.0e-30f) s2_ = 0.0f;
            return y;
        }

        void reset() noexcept { s1_ = 0.0f; s2_ = 0.0f; }

    private:
        float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
        float s1_ = 0.0f, s2_ = 0.0f;
    };

    static void advanceMix (float& mix, float target, float c) noexcept
    {
        if (mix == target)
            return;
        mix += (target - mix) * c;
        if (std::abs (mix - target) < 1.0e-6f)
            mix = target; // snap: exact OFF means exact skip
    }

    static float smooth (float current, float target, float c) noexcept
    {
        if (current == target)
            return target;
        const float next = current + (target - current) * c;
        return std::abs (next - target) < 1.0e-7f ? target : next;
    }

    void resetFilterStates() noexcept
    {
        for (auto& s : hpfSvf_) s.reset();
        for (auto& s : lpfSvf_) s.reset();
        for (auto& b : bellBiquads_) for (auto& s : b) s.reset();
    }

    // Analytical magnitude helpers for the nominal curve (UI only). These
    // evaluate the EXACT z-domain transfer of the implemented recurrences
    // (same coefficient clamping as the audio path); a unit test locks them
    // against direct simulation of process().

    static void svfTransfers (float fc, float q, float sr, float w,
                              std::complex<float>& hLow,
                              std::complex<float>& hHigh) noexcept
    {
        const float wc = juce::MathConstants<float>::pi
                       * juce::jmax (1.0f, fc) / juce::jmax (1.0f, sr);
        const float g = std::tan (juce::jmin (wc, juce::MathConstants<float>::pi * 0.4999f));
        const float k = 1.0f / juce::jmax (0.05f, q);
        const float den = 1.0f + g * (g + k);
        const float a1 = 1.0f / den;
        const float a2 = g * a1;
        const float a3 = g * a2;

        const std::complex<float> zInv (std::cos (w), -std::sin (w)); // e^{-jw}
        const std::complex<float> A = 1.0f + zInv - 2.0f * a1;
        const std::complex<float> B = 2.0f * a2;
        const std::complex<float> C = zInv - 1.0f + 2.0f * a3;
        const std::complex<float> D = 2.0f * a2;
        const std::complex<float> E = 2.0f * a3;
        const std::complex<float> denom = C * A + D * B;
        const std::complex<float> z2overX = (D * B + E * A) / denom;
        const std::complex<float> z1overX = B * (C - E) / denom;
        const std::complex<float> v1overX = a1 * z1overX + a2 - a2 * z2overX;
        const std::complex<float> v2overX = a2 * z1overX + (1.0f - a3) * z2overX + a3;

        hLow = v2overX;
        hHigh = std::complex<float> (1.0f, 0.0f) - std::complex<float> (k, 0.0f) * v1overX - v2overX;
    }

    static float svfMagnitude (float fc, float q, float hz, float sr, bool isHigh) noexcept
    {
        const float w = 2.0f * juce::MathConstants<float>::pi * hz / juce::jmax (1.0f, sr);
        std::complex<float> hLow, hHigh;
        svfTransfers (fc, q, sr, w, hLow, hHigh);
        return std::abs (isHigh ? hHigh : hLow);
    }

    static float bellMagnitude (float fc, float q, float db, float hz, float sr) noexcept
    {
        const float A = std::pow (10.0f, db / 40.0f);
        const float w0 = 2.0f * juce::MathConstants<float>::pi
                       * juce::jlimit (1.0f, sr * 0.5f - 1.0f, fc) / juce::jmax (1.0f, sr);
        const float w = 2.0f * juce::MathConstants<float>::pi * hz / juce::jmax (1.0f, sr);
        const float cosW0 = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * juce::jmax (0.05f, q));

        const float b0 = 1.0f + alpha * A;
        const float b1 = -2.0f * cosW0;
        const float b2 = 1.0f - alpha * A;
        const float a0 = 1.0f + alpha / A;
        const float a1 = -2.0f * cosW0;
        const float a2 = 1.0f - alpha / A;

        const std::complex<float> zInv (std::cos (w), -std::sin (w));
        const std::complex<float> num = b0 + b1 * zInv + b2 * zInv * zInv;
        const std::complex<float> den = a0 + a1 * zInv + a2 * zInv * zInv;
        return std::abs (num / den);
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    float smoothCoeff_ = 0.0f;

    // Smoothed targets (per-sample ramps; exact snap when settled).
    float hpfMix_ = 0.0f;
    float lpfMix_ = 0.0f;
    float bypassMix_ = 1.0f;
    float hpfSmoothedNorm_ = kHpfOffNorm;
    float lpfSmoothedNorm_ = kLpfOffNorm;
    float bellMix_[kMaxBells] = {};
    float bellSmoothedFreqNorm_[kMaxBells] = {};
    float bellSmoothedGainDb_[kMaxBells] = {};
    float bellSmoothedQNorm_[kMaxBells] = {};

    // Preallocated per-channel filter state (never allocated on the audio
    // thread). The frozen G10SvfSection is the same TPT primitive the musical
    // engine uses; Q is fixed at Butterworth.
    G10SvfSection hpfSvf_[kMaxChannels];
    G10SvfSection lpfSvf_[kMaxChannels];
    CleanBellBiquad bellBiquads_[kMaxBells][kMaxChannels];
};

} // namespace G10
} // namespace APEX
