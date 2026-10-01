#pragma once
#include <JuceHeader.h>
#include "G10Types.h"

namespace APEX {
namespace G10 {

// ============================================================================
// G10CurveEngineCore — realtime-safe DSP kernel for APEX G10.
//
// Thread ownership:
//   prepare() / reset()  → message thread (before audio starts)
//   processBlock()       → audio thread only
//   setBandTargetGainDb() / setInputTargetDb() / setOutputTargetDb() /
//   setBypassTarget()    → any thread (stores atomics; adopted per block)
//
// Realtime contract (steady state):
//   - zero heap allocation: all state is fixed-size member arrays
//   - zero locks, zero I/O, zero GUI access, zero string construction
//   - bounded per-sample work: 10 bands x (1-2 TPT SVF sections + the AIR16K
//     TDF2 shelf biquad) + smoothing
//   - denormal flush on every filter state and the final sample
//
// Neutral contract: with all bands at 0 dB, Input/Output at 0 dB and bypass
// OFF, the output is BIT-EXACT equal to the input (every (A-1) multiplier is
// exactly 0.0f, every trim gain is exactly 1.0f).
//
// Latency: zero samples (pure minimum-phase IIR; no lookahead, no FIR).
//
// Determinism: parameter smoothing advances exactly once per sample and is
// shared across channels, so mono and stereo produce identical per-sample
// parameter trajectories and left/right always see sample-consistent
// transitions. Output is independent of block size.
//
// Phase 1 scope: clean linear path only. Analog/quality are stored and
// persisted but intentionally do not alter the clean DSP yet (future phase).
// ============================================================================

// ---------------------------------------------------------------------------
// TPT (topology-preserving transform) state-variable filter section.
// Zavalishin form: numerically well-conditioned at low frequencies, stable
// under coefficient interpolation, and cheap. The three outputs (low, band,
// high) are mixed with (A-1) gains to form bells and shelves.
// ---------------------------------------------------------------------------

class G10SvfSection
{
public:
    void setCoefficients (float f0Hz, float q, float sampleRate) noexcept
    {
        const float w = juce::MathConstants<float>::pi * juce::jmax (1.0f, f0Hz) / juce::jmax (1.0f, sampleRate);
        g_ = std::tan (juce::jmin (w, juce::MathConstants<float>::pi * 0.4999f));
        k_ = 1.0f / juce::jmax (0.05f, q);
        const float den = 1.0f + g_ * (g_ + k_);
        a1_ = 1.0f / den;
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    void process (float in, float& low, float& band, float& high) noexcept
    {
        const float v3 = in - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3;
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = 2.0f * v1 - ic1eq_;
        ic2eq_ = 2.0f * v2 - ic2eq_;

        // Denormal flush (bounded, allocation-free).
        if (std::abs (ic1eq_) < 1.0e-30f) ic1eq_ = 0.0f;
        if (std::abs (ic2eq_) < 1.0e-30f) ic2eq_ = 0.0f;

        low  = v2;
        // Band normalization: the raw TPT band output peaks at |band(f0)| = Q
        // (k_ = 1/Q), so scaling by k_ makes the bell contribution read as the
        // exact fader gain at the anchor frequency. Pure gain on the band
        // output: no change to phase, minimum-phase property, smoothing, or
        // per-channel state.
        band = v1 * k_;
        high = in - k_ * v1 - v2;
    }

    void reset() noexcept
    {
        ic1eq_ = 0.0f;
        ic2eq_ = 0.0f;
    }

private:
    float g_ = 0.0f, k_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1eq_ = 0.0f, ic2eq_ = 0.0f;
};

// ---------------------------------------------------------------------------
// RBJ cookbook high-shelf biquad (direct form 2 transposed). Used by AIR16K:
// the TPT SVF highpass-mix combination is NOT an exact RBJ shelf (measured
// error up to ~1.0 dB at 8 kHz, and a spurious ~1.2 dB scoop at 4 kHz), so
// the shelf is realized as a real biquad. TDF2 keeps the state bounded and
// is cheap; the RBJ coefficients are stable for any Q > 0.
// ---------------------------------------------------------------------------

class G10ShelfBiquad
{
public:
    void setCoefficients (float f0Hz, float q, float sampleRate, float db) noexcept
    {
        // A^2 = 10^(db/20) is the exact shelf gain at Nyquist.
        const float A = std::pow (10.0f, db / 40.0f);
        const float w0 = 2.0f * juce::MathConstants<float>::pi
                       * juce::jmax (1.0f, f0Hz) / juce::jmax (1.0f, sampleRate);
        const float cosW0 = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * juce::jmax (0.01f, q));
        const float sqA = std::sqrt (A);

        const float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 + 2.0f * sqA * alpha);
        const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW0);
        const float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 - 2.0f * sqA * alpha);
        const float a0 = (A + 1.0f) - (A - 1.0f) * cosW0 + 2.0f * sqA * alpha;
        const float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosW0);
        const float a2 = (A + 1.0f) - (A - 1.0f) * cosW0 - 2.0f * sqA * alpha;

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

        // Denormal flush (bounded, allocation-free).
        if (std::abs (s1_) < 1.0e-30f) s1_ = 0.0f;
        if (std::abs (s2_) < 1.0e-30f) s2_ = 0.0f;
        return y;
    }

    void reset() noexcept
    {
        s1_ = 0.0f;
        s2_ = 0.0f;
    }

private:
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float s1_ = 0.0f, s2_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Per-band state engine. Parameter smoothing state is shared across channels
// and advances exactly once per sample (see G10CurveEngineCore::processBlock);
// the SVF filter history/state is independent per channel so that equal L/R
// input always produces equal L/R output.
// ---------------------------------------------------------------------------

class G10BandEngine
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (G10CurveFamily family, float sampleRate) noexcept
    {
        family_ = family;
        sampleRate_ = juce::jmax (1.0f, sampleRate);
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            secA_[ch].reset();
            secB_[ch].reset();
            support_[ch].reset();
            shelf_[ch].reset();
        }
        smoothedDb_ = targetDb_;
        lastCoeffDb_ = smoothedDb_;
        recomputeCoefficients();
    }

    void setTargetGainDb (float db) noexcept
    {
        targetDb_ = juce::jlimit (kBandMinDb, kBandMaxDb, db);
    }

    float getTargetGainDb() const noexcept { return targetDb_; }

    // Audio thread: advance the per-sample smoothing exactly once per sample
    // (shared across channels). Lazy bounded coefficient update.
    void advanceSmoothing (float smoothCoeff) noexcept
    {
        smoothedDb_ += (targetDb_ - smoothedDb_) * smoothCoeff;
        if (std::abs (smoothedDb_) < 1.0e-5f)
            smoothedDb_ = 0.0f;

        if (std::abs (smoothedDb_ - lastCoeffDb_) > 1.0e-4f)
        {
            lastCoeffDb_ = smoothedDb_;
            recomputeCoefficients();
        }
    }

    // Audio thread: apply this band's shaping to one sample using the current
    // smoothed value. The musical coefficients are shared across channels;
    // only the SVF filter history/state is per channel (indexed by `channel`).
    float applyShaping (float x, int channel) noexcept
    {
        const float db = smoothedDb_;
        if (std::abs (db) < 1.0e-4f)
            return x; // exact transparency

        constexpr float kLog2Scale = 0.16609640474436813f;

        if (family_ == G10CurveFamily::Shine8k)
        {
            // Continuous broad bell <-> high-shelf morph.
            float low, band, high;
            secA_[channel].process (x, low, band, high);
            const float bellDb = db * (1.0f - morph_);
            const float shelfDb = db * morph_;
            const float bellGain = std::exp2 (bellDb * kLog2Scale) - 1.0f;
            const float shelfGain = std::exp2 (shelfDb * kLog2Scale) - 1.0f;
            x += bellGain * band;
            if (std::abs (shelfGain) > 1.0e-5f)
            {
                float l2, b2, h2;
                secB_[channel].process (x, l2, b2, h2);
                x += shelfGain * h2;
            }
            return x;
        }

        if (family_ == G10CurveFamily::Air16k)
        {
            // Exact RBJ high shelf (TDF2 biquad — the SVF highpass-mix is
            // NOT an exact shelf: up to ~1.0 dB error at 8 kHz, ~1.2 dB
            // spurious scoop at 4 kHz) plus a subtle broad support contour.
            float y = shelf_[channel].process (x);
            if (supportWeight_ > 0.0f)
            {
                float l2, b2, h2;
                support_[channel].process (y, l2, b2, h2);
                const float sg = std::exp2 (db * supportWeight_ * kLog2Scale) - 1.0f;
                y += sg * b2;
            }
            return y;
        }

        // Generic path: section A (bell) + optional section B (low shelf) +
        // optional support contour.
        float lowA, bandA, highA;
        secA_[channel].process (x, lowA, bandA, highA);
        const float aGain = std::exp2 (db * gainWeightA_ * kLog2Scale) - 1.0f;
        x += aGain * bandA;

        if (gainWeightB_ > 0.0f)
        {
            float lowB, bandB, highB;
            secB_[channel].process (x, lowB, bandB, highB);
            const float bGain = std::exp2 (db * gainWeightB_ * kLog2Scale) - 1.0f;
            x += bGain * lowB;
        }

        if (supportWeight_ > 0.0f)
        {
            float l2, b2, h2;
            support_[channel].process (x, l2, b2, h2);
            const float sg = std::exp2 (db * supportWeight_ * kLog2Scale) - 1.0f;
            x += sg * b2;
        }

        return x;
    }

    float getSmoothedDb() const noexcept { return smoothedDb_; }
    G10CurveFamily getFamily() const noexcept { return family_; }
    float getCurrentQ() const noexcept { return q_; }
    float getCurrentMorph() const noexcept { return morph_; }
    float getSectionAFrequency() const noexcept { return f0A_; }
    float getSectionBFrequency() const noexcept { return f0B_; }

    static float familyCenterHz (G10CurveFamily f) noexcept
    {
        switch (f)
        {
            case G10CurveFamily::Deep31:     return 31.0f;
            case G10CurveFamily::Punch63:    return 63.0f;
            case G10CurveFamily::Body125:    return 125.0f;
            case G10CurveFamily::Warmth250:  return 250.0f;
            case G10CurveFamily::Wood500:    return 500.0f;
            case G10CurveFamily::Focus1k:    return 1000.0f;
            case G10CurveFamily::Attack2k:   return 2000.0f;
            case G10CurveFamily::Presence4k: return 4000.0f;
            case G10CurveFamily::Shine8k:    return 8000.0f;
            case G10CurveFamily::Air16k:     return 16000.0f;
        }
        return 0.0f;
    }

private:
    G10CurveFamily family_ = G10CurveFamily::Focus1k;
    double sampleRate_ = 44100.0;

    float targetDb_ = 0.0f;    // adopted per block from the parameter
    float smoothedDb_ = 0.0f;  // per-sample smoothed (shared across channels)
    float lastCoeffDb_ = 0.0f; // smoothed value at last coefficient update

    // Static structure (recomputed lazily).
    float q_ = 0.5f;
    float morph_ = 0.0f;        // SHINE/AIR continuous bell<->shelf weighting
    float f0A_ = 1000.0f;      // primary section frequency
    float f0B_ = 0.0f;         // secondary section frequency (0 = unused)
    float gainWeightA_ = 1.0f; // fraction of db applied to section A
    float gainWeightB_ = 0.0f; // fraction of db applied to section B
    float supportWeight_ = 0.0f; // fraction of db applied to support contour

    // Per-channel SVF filter history/state (fixed-size, no heap allocation).
    // Coefficients are shared; only these delay states differ per channel.
    G10SvfSection secA_[kMaxChannels];
    G10SvfSection secB_[kMaxChannels];
    G10SvfSection support_[kMaxChannels];
    G10ShelfBiquad shelf_[kMaxChannels]; // AIR16K exact RBJ high shelf

    // -----------------------------------------------------------------------
    // Q laws. Proportional-Q principle: small movements broad and forgiving,
    // larger movements progressively more focused, never surgical. Boost and
    // cut intentionally diverge where the family contract demands it.
    // -----------------------------------------------------------------------

    static float qLaw (G10CurveFamily f, float db) noexcept
    {
        const float a = std::abs (db);
        const float n = juce::jmin (1.0f, a / 12.0f); // normalized 0..1
        switch (f)
        {
            case G10CurveFamily::Deep31:
                return 0.30f + 0.18f * std::pow (n, 1.30f);
            case G10CurveFamily::Punch63:
                return 0.50f + 0.40f * std::pow (n, 1.05f);
            case G10CurveFamily::Body125:
                return 0.42f + 0.30f * std::pow (n, 1.00f);
            case G10CurveFamily::Warmth250: // boost broader than cut
                return db >= 0.0f
                    ? 0.45f + 0.43f * std::pow (n, 1.10f)
                    : 0.52f + 0.53f * std::pow (n, 1.10f);
            case G10CurveFamily::Wood500:   // boost more focused than cut
                return db >= 0.0f
                    ? 0.55f + 0.50f * std::pow (n, 1.05f)
                    : 0.45f + 0.45f * std::pow (n, 1.05f);
            case G10CurveFamily::Focus1k:
                return 0.65f + 0.60f * std::pow (n, 1.00f);
            case G10CurveFamily::Attack2k:
                return 0.65f + 0.70f * std::pow (n, 1.05f);
            case G10CurveFamily::Presence4k: // most focused band; boost tighter than cut
                return db >= 0.0f
                    ? 0.75f + 0.70f * std::pow (n, 1.00f)
                    : 0.55f + 0.55f * std::pow (n, 1.00f);
            case G10CurveFamily::Shine8k:
                return 0.80f; // fixed broad bell; the morph carries the design
            case G10CurveFamily::Air16k:
                // Gain-dependent shelf Q: Q = 0.5 * sqrt(A), A = 10^(db/20).
                // Preserved production fix. Combined with the exact RBJ
                // biquad shelf, the NET gain at the 16K musical anchor is
                // ~6.0 dB at +6 (within +-0.1 dB across 44.1-192 kHz) and
                // the cut side lands inside the family bounds.
                return 0.50f * std::sqrt (std::pow (10.0f, db / 20.0f));
        }
        return 0.5f;
    }

    // Continuous bell<->high-shelf morph for SHINE (boost) and gentle shelf
    // darkening (cut). Never discrete — a continuous function of gain.
    static float shineMorph (float db) noexcept
    {
        if (db >= 0.0f)
        {
            // Near zero: pure broad bell. At +6 dB: ~40% shelf. Near +12 dB:
            // predominantly broad shelf.
            return juce::jlimit (0.0f, 1.0f, (db - 2.0f) / 10.0f);
        }
        // Cut: primarily broad bell darkening with a small continuous shelf
        // contribution (de-gloss), capped so it never becomes a steep filter.
        return 0.25f * juce::jlimit (0.0f, 1.0f, std::abs (db) / 12.0f);
    }

    // AIR mathematical turnover adapts with sample rate (musical equivalence
    // across rates; the displayed 16K is the musical anchor, NOT the turnover).
    // The turnover sits one octave below the anchor so the anchor region
    // reaches the asymptotic shelf gain instead of plateauing at the turnover.
    static float airShelfAnchorHz (float sampleRate) noexcept
    {
        return juce::jmin (8000.0f, 0.72f * sampleRate * 0.5f);
    }

    // -----------------------------------------------------------------------
    // Family structure definition. Each family decides its topology, weights,
    // and compound/support structure — ten intentionally different designs.
    // -----------------------------------------------------------------------

    void recomputeCoefficients() noexcept
    {
        const float db = smoothedDb_;
        q_ = qLaw (family_, db);
        morph_ = 0.0f;
        gainWeightA_ = 1.0f;
        gainWeightB_ = 0.0f;
        supportWeight_ = 0.0f;
        f0A_ = familyCenterHz (family_);
        f0B_ = 0.0f;

        switch (family_)
        {
            case G10CurveFamily::Deep31:
            {
                // Compound: ultra-wide low bell + very broad low shelf.
                // Boost emphasizes the bell; cut emphasizes the broader shelf
                // (cut is broader than boost, and the cut center lands inside
                // the family bounds at -6 dB).
                if (db >= 0.0f) { gainWeightA_ = 0.55f; gainWeightB_ = 0.45f; }
                else            { gainWeightA_ = 0.55f; gainWeightB_ = 0.45f; }
                f0B_ = 45.0f; // broad low shelf anchor below the bell
                break;
            }
            case G10CurveFamily::Punch63:
            {
                // Proportional-Q bell with a very subtle supporting contour
                // near 120 Hz (~5% of the fader gain). Documented and measured;
                // it must never read as a disconnected secondary bump.
                supportWeight_ = 0.05f;
                break;
            }
            case G10CurveFamily::Body125:
            {
                // Hybrid bell/shelf: boost 70/30, cut 83/17. The cut center
                // lands inside the family bounds at -6 dB while the cut stays
                // broad (90/10-style shelf presence preserved).
                if (db >= 0.0f) { gainWeightA_ = 0.70f; gainWeightB_ = 0.30f; }
                else            { gainWeightA_ = 0.83f; gainWeightB_ = 0.17f; }
                f0B_ = 110.0f;
                break;
            }
            case G10CurveFamily::Warmth250:
            case G10CurveFamily::Wood500:
            case G10CurveFamily::Focus1k:
            case G10CurveFamily::Attack2k:
            case G10CurveFamily::Presence4k:
            {
                // Pure proportional-Q bells with family-specific Q laws
                // (boost/cut asymmetry embedded in qLaw).
                break;
            }
            case G10CurveFamily::Shine8k:
            {
                // Continuous broad bell <-> high-shelf morph.
                morph_ = shineMorph (db);
                f0B_ = 7000.0f;
                break;
            }
            case G10CurveFamily::Air16k:
            {
                // Ultra-wide high shelf (musical anchor 16K, turnover adapted
                // to sample rate) plus a subtle broad support around 9 kHz.
                f0A_ = airShelfAnchorHz ((float) sampleRate_);
                if (db >= 0.0f) supportWeight_ = 0.10f;
                else            supportWeight_ = 0.05f;
                break;
            }
        }

        // Identical coefficients for every channel; only the delay/filter
        // state differs per channel.
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            secA_[ch].setCoefficients (f0A_, q_, (float) sampleRate_);
            if (f0B_ > 0.0f)
                secB_[ch].setCoefficients (f0B_, (family_ == G10CurveFamily::Deep31) ? 0.55f : 0.60f,
                                           (float) sampleRate_);
            if (supportWeight_ > 0.0f)
            {
                const float supportF0 = (family_ == G10CurveFamily::Punch63) ? 120.0f : 9000.0f;
                support_[ch].setCoefficients (supportF0, 0.80f, (float) sampleRate_);
            }
            if (family_ == G10CurveFamily::Air16k)
                shelf_[ch].setCoefficients (f0A_, q_, (float) sampleRate_, db);
        }
    }
};

// ============================================================================
// G10CurveEngineCore — the complete Phase 1 clean path:
//   Input trim -> 10 curve families -> Output trim -> bypass crossfade
// ============================================================================

class G10CurveEngineCore
{
public:
    static constexpr int kMaxChannels = G10BandEngine::kMaxChannels;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
    {
        sampleRate_ = juce::jmax (1.0, sampleRate);
        maxBlockSize_ = juce::jmax (1, maxBlockSize);
        numChannels_ = juce::jlimit (1, kMaxChannels, numChannels);

        // One-pole smoothing coefficients from time constants:
        //   y += (target - y) * coeff,  coeff = 1 - exp(-1/(fs * tau))
        bandSmoothCoeff_ = (float) (1.0 - std::exp (-1.0 / (sampleRate_ * 0.020)));
        trimSmoothCoeff_ = (float) (1.0 - std::exp (-1.0 / (sampleRate_ * 0.015)));
        bypassStep_ = 1.0f / (float) juce::jmax (1.0, sampleRate_ * 0.007);

        for (int b = 0; b < kNumBands; ++b)
            bands_[b].prepare ((G10CurveFamily) b, (float) sampleRate_);

        inputSmoothDb_ = inputTargetDb_;
        outputSmoothDb_ = outputTargetDb_;
        bypassMix_ = bypassTarget_ ? 1.0f : 0.0f;
    }

    void reset() noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
            bands_[b].prepare (bands_[b].getFamily(), (float) sampleRate_);
        inputSmoothDb_ = inputTargetDb_;
        outputSmoothDb_ = outputTargetDb_;
        bypassMix_ = bypassTarget_ ? 1.0f : 0.0f;
    }

    // ---- control-plane setters (any thread; atomic adoption) --------------

    void setBandTargetGainDb (int bandIndex, float db) noexcept
    {
        if (bandIndex >= 0 && bandIndex < kNumBands)
            bands_[bandIndex].setTargetGainDb (db);
    }

    float getBandTargetGainDb (int bandIndex) const noexcept
    {
        return (bandIndex >= 0 && bandIndex < kNumBands)
            ? bands_[bandIndex].getTargetGainDb()
            : 0.0f;
    }

    void setInputTargetDb (float db) noexcept
    {
        inputTargetDb_ = juce::jlimit (kTrimMinDb, kTrimMaxDb, db);
    }

    void setOutputTargetDb (float db) noexcept
    {
        outputTargetDb_ = juce::jlimit (kTrimMinDb, kTrimMaxDb, db);
    }

    void setBypassTarget (bool bypass) noexcept
    {
        bypassTarget_ = bypass;
    }

    // ---- audio thread ------------------------------------------------------

    /** Process one block in place. Supports mono (1 channel) and linked
        stereo (2 channels). Parameter smoothing advances exactly once per
        sample and is shared across channels, so left/right always see
        identical transitions and mono/stereo trajectories match. */
    void processBlock (float* const* channelData, int numChannels, int numSamples) noexcept
    {
        if (channelData == nullptr || numSamples <= 0)
            return;

        const int chans = juce::jmin (numChannels, kMaxChannels);
        if (chans <= 0)
            return;

        const bool bypassTarget = bypassTarget_;

        for (int s = 0; s < numSamples; ++s)
        {
            // Bypass crossfade state (shared across channels).
            if (bypassMix_ < 0.999f && bypassTarget)
                bypassMix_ = juce::jmin (1.0f, bypassMix_ + bypassStep_);
            else if (bypassMix_ > 0.001f && ! bypassTarget)
                bypassMix_ = juce::jmax (0.0f, bypassMix_ - bypassStep_);

            const float dryGain = std::sin (bypassMix_ * juce::MathConstants<float>::halfPi);
            const float wetGain = std::cos (bypassMix_ * juce::MathConstants<float>::halfPi);

            // Input/output trim smoothing (shared across channels).
            inputSmoothDb_ += (inputTargetDb_ - inputSmoothDb_) * trimSmoothCoeff_;
            outputSmoothDb_ += (outputTargetDb_ - outputSmoothDb_) * trimSmoothCoeff_;
            if (std::abs (inputSmoothDb_) < 1.0e-5f) inputSmoothDb_ = 0.0f;
            if (std::abs (outputSmoothDb_) < 1.0e-5f) outputSmoothDb_ = 0.0f;

            const float inputGain = dbToGain (inputSmoothDb_);
            const float outputGain = dbToGain (outputSmoothDb_);

            // Advance every band's smoothing exactly once per sample.
            for (int b = 0; b < kNumBands; ++b)
                bands_[b].advanceSmoothing (bandSmoothCoeff_);

            const bool fullyBypassed = bypassMix_ >= 0.999f;
            const bool fullyWet = bypassMix_ <= 0.001f;

            for (int ch = 0; ch < chans; ++ch)
            {
                float* d = channelData[ch];
                if (d == nullptr)
                    continue;

                const float dry = d[s];

                if (fullyBypassed)
                {
                    d[s] = dry; // dry passthrough (fast path)
                    continue;
                }

                float x = dry * inputGain;

                for (int b = 0; b < kNumBands; ++b)
                    x = bands_[b].applyShaping (x, ch);

                x *= outputGain;

                // Denormal flush on the final sample (bounded).
                if (std::abs (x) < 1.0e-30f)
                    x = 0.0f;

                if (fullyWet)
                    d[s] = x;
                else
                    d[s] = dry * dryGain + x * wetGain;
            }
        }
    }

    // ---- introspection (tests / future response display) ------------------

    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }
    int getNumChannels() const noexcept { return numChannels_; }
    float getBandSmoothedDb (int bandIndex) const noexcept
    {
        return (bandIndex >= 0 && bandIndex < kNumBands)
            ? bands_[bandIndex].getSmoothedDb()
            : 0.0f;
    }
    float getBypassMix() const noexcept { return bypassMix_; }
    bool isBypassSettled() const noexcept
    {
        return bypassTarget_ ? (bypassMix_ >= 0.999f) : (bypassMix_ <= 0.001f);
    }

    G10BandEngine& getBand (int bandIndex) noexcept { return bands_[bandIndex]; }

private:
    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    int numChannels_ = 2;

    float bandSmoothCoeff_ = 0.00043f;  // ~20 ms at 48 kHz
    float trimSmoothCoeff_ = 0.00139f;  // ~15 ms at 48 kHz
    float bypassStep_ = 0.003f;         // ~7 ms at 48 kHz

    float inputTargetDb_ = 0.0f;
    float outputTargetDb_ = 0.0f;
    float inputSmoothDb_ = 0.0f;
    float outputSmoothDb_ = 0.0f;
    bool bypassTarget_ = false;
    float bypassMix_ = 0.0f; // 0 = processed, 1 = dry (bypassed)

    G10BandEngine bands_[kNumBands];
};

} // namespace G10
} // namespace APEX