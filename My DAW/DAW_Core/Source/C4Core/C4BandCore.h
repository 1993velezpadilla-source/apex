#pragma once
#include <JuceHeader.h>
#include "C4Types.h"
#include "C4TuningProfile.h"
#include "C4FilterCore.h"
#include "C4HaloShelfMapper.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4BandShared — one band's control-plane state, computed ONCE per sample and
// shared by every channel (mono/stereo determinism, G10 pattern).
//
// Per sample it derives from the smoothed parameters:
//   freqHz      — smoothed frequency (log domain)
//   qEff        — effective Q (proportional-Q law from the tuning profile)
//   gain        — A = 10^(gainDb/20), exact 1.0f at 0 dB
//   modeBlend   — 0 = bell .. 1 = shelf (smoothed ~8 ms; same SVF states feed
//                 both shapes, so the morph is continuous by construction)
// ============================================================================

class C4BandShared
{
public:
    void prepare (double sampleRate, const C4TuningProfile& profile, C4BandId band) noexcept
    {
        rate_ = (float) sampleRate;
        geometry_ = profile.geometry[(int) band];
        shelfKind_ = kBandInfos[(int) band].shelfKind;
        halo_ = profile.halo;

        freqMs_.prepare (sampleRate, profile.smoothing.freqMs);
        gainMs_.prepare (sampleRate, profile.smoothing.gainMs);
        qMs_.prepare (sampleRate, profile.smoothing.qMs);
        modeMs_.prepare (sampleRate, profile.smoothing.modeMs);
    }

    void setFreqTargetHz (float hz) noexcept
    {
        freqMs_.setTarget (std::log2 (juce::jmax (1.0f, hz)));
    }

    void setGainTargetDb (float db) noexcept
    {
        gainMs_.setTarget (clampDb (db));
    }

    void setQTarget (float q) noexcept
    {
        qMs_.setTarget (std::log2 (juce::jmax (0.05f, q)));
    }

    void setModeTarget (bool shelf) noexcept
    {
        modeMs_.setTarget (shelf ? 1.0f : 0.0f);
    }

    /** TEST ONLY (morph audit): hold the bell<->shelf blend at an arbitrary
        continuous value so the intermediate response shapes can be measured
        at fixed 0/25/50/75/100% points. Production always uses 0/1 targets;
        the smoothing guarantees every intermediate state is reached anyway. */
    void setModeBlendTargetForTest (float blend) noexcept
    {
        modeMs_.setTarget (juce::jlimit (0.0f, 1.0f, blend));
    }

    /** Advance all smoothers one sample and derive the per-sample band state. */
    void advance() noexcept
    {
        const float freqLog2 = freqMs_.advance();
        freqHz_ = std::exp2 (freqLog2);

        // Engine-level realizable bound for EVERY band: a control at/above
        // Nyquist would compute g = tan(pi*f/fs) <= 0 (negative-g SVF —
        // measured during the HALO automation torture: the blend-weighted
        // HALO morph interpolates between the raw control and the mapped
        // value, so the raw control itself must be realizable). The same
        // 0.49*fs safety bound the HPF/LPF use; a no-op for every in-range
        // control (all band ranges are far below it at every supported rate).
        freqHz_ = juce::jmin (freqHz_, 0.49f * rate_);

        gainDb_ = gainMs_.advance();
        gainA_ = dbToGain (gainDb_);

        modeBlend_ = modeMs_.advance();

        const float qBase = (1.0f - modeBlend_) * geometry_.baseBellQ
                          + modeBlend_ * geometry_.baseShelfQ;
        const float userQ = std::exp2 (qMs_.advance());

        if (geometry_.geometryEnabled && gainDb_ != 0.0f)
        {
            const float drive = gainDb_ > 0.0f ? geometry_.boostQDrive
                                               : geometry_.cutQDrive;
            const float f = std::abs (gainDb_) / kGainMaxDb;
            qEff_ = qBase * (1.0f + drive * std::pow (f, geometry_.qLawExponent));
        }
        else
        {
            qEff_ = qBase;
        }
        qEff_ = juce::jlimit (0.30f, 8.0f, qEff_);

        // The user Q scales the profile base Q for the bands that expose it.
        qEff_ = juce::jlimit (0.30f, 8.0f, qEff_ * userQ);

        // OPEN/HALO (Phase 2): in HALO (high-shelf) mode the control
        // frequency is mapped into the realizable in-band region
        // (<= maxRealizableRatio * Nyquist). The blend-weighted lerp keeps
        // the bell<->HALO morph continuous: at blend 0 the bell uses the
        // unmapped control frequency; at blend 1 the shelf uses the mapped
        // frequency. Both endpoints are realizable (the 0.49*fs clamp above),
        // so every intermediate blend is realizable too.
        if (shelfKind_ == C4ShelfKind::HighShelf && modeBlend_ > 0.0f)
        {
            const float mapped = C4HaloShelfMapper::mapControlFreq (freqHz_, rate_, halo_);
            freqHz_ = freqHz_ * (1.0f - modeBlend_) + mapped * modeBlend_;
        }
    }

    /** Per-sample values (valid after advance()). */
    float getFreqHz() const noexcept { return freqHz_; }
    float getGainDb() const noexcept { return gainDb_; }
    float getGainA() const noexcept { return gainA_; }
    float getModeBlend() const noexcept { return modeBlend_; }
    float getEffectiveQ() const noexcept { return qEff_; }
    C4ShelfKind getShelfKind() const noexcept { return shelfKind_; }

    /** True when the band is exactly neutral (smoothed gain == 0 dB and the
        mode/freq/Q smoothers have converged to their targets). */
    bool isSettledNeutral() const noexcept
    {
        return gainDb_ == 0.0f
            && freqMs_.getValue() == freqMs_.getTarget()
            && gainMs_.getValue() == gainMs_.getTarget()
            && qMs_.getValue() == qMs_.getTarget()
            && modeMs_.getValue() == modeMs_.getTarget();
    }

    void reset() noexcept
    {
        freqMs_.reset (std::log2 (100.0f));
        gainMs_.reset (0.0f);
        qMs_.reset (0.0f);
        modeMs_.reset (0.0f);
        freqHz_ = 100.0f;
        gainDb_ = 0.0f;
        gainA_ = 1.0f;
        modeBlend_ = 0.0f;
        qEff_ = 1.0f;
    }

    void snapToTargets() noexcept
    {
        freqMs_.snapToTarget();
        gainMs_.snapToTarget();
        qMs_.snapToTarget();
        modeMs_.snapToTarget();
        freqHz_ = std::exp2 (freqMs_.getValue());
        gainDb_ = gainMs_.getValue();
        gainA_ = dbToGain (gainDb_);
        modeBlend_ = modeMs_.getValue();
    }

private:
    C4Smoother freqMs_, gainMs_, qMs_, modeMs_;
    C4BandGeometry geometry_ {};
    C4ShelfKind shelfKind_ = C4ShelfKind::LowShelf;
    C4HaloTuning halo_ {};
    float rate_ = 48000.0f;
    float freqHz_ = 100.0f;
    float gainDb_ = 0.0f;
    float gainA_ = 1.0f;
    float modeBlend_ = 0.0f;
    float qEff_ = 1.0f;
};

// ============================================================================
// C4BandCore — one band's per-channel filter state (SVF only).
// ============================================================================

class C4BandCore
{
public:
    void setCoefficients (const C4BandShared& shared, float sampleRate) noexcept
    {
        svf_.setCoefficients (shared.getFreqHz(), shared.getEffectiveQ(), sampleRate);
    }

    /** Process one sample; returns the band's contribution to the output
        (y = x + contribution), zero EXACTLY when the gain is 0 dB. */
    float process (float x, const C4BandShared& shared) noexcept
    {
        float low, band, high;
        svf_.process (x, low, band, high);

        const float m = shared.getModeBlend();
        const float shape = shared.getShelfKind() == C4ShelfKind::LowShelf
            ? (1.0f - m) * band + m * low
            : (1.0f - m) * band + m * high;
        lastShape_ = shape;

        // (A - 1) is exactly 0.0f when the smoothed gain is exactly 0 dB:
        // the contribution is exactly zero (bit-identical neutral behavior).
        return (shared.getGainA() - 1.0f) * shape;
    }

    /** The band's UNITY shape tap from the most recent process() call — the
        band-focused excitation for the BLOOM residual bank (never the gained
        output, so the EQ gain is never double-applied). */
    float getLastShape() const noexcept { return lastShape_; }

    void reset() noexcept
    {
        svf_.reset();
        lastShape_ = 0.0f;
    }

    // Test introspection (permanent; read-only, never used by the audio path).
    void getSvfStateForTest (float& ic1, float& ic2) const noexcept
    {
        svf_.getStateForTest (ic1, ic2);
    }

private:
    C4SvfSection svf_;
    float lastShape_ = 0.0f;
};

} // namespace C4
} // namespace APEX
