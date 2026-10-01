#pragma once
#include <JuceHeader.h>
#include "C4Types.h"
#include "C4TuningProfile.h"
#include "C4FilterCore.h"
#include "C4BandCore.h"
#include "C4HaloShelfMapper.h"
#include "C4ResidualBankCore.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4EngineCore — realtime-safe DSP kernel for APEX C4 (BLOOM CORE, linear
// foundation; the Bloom Residual Bank and coupling join in later phases).
//
// Topology (all minimum-phase IIR, zero latency):
//
//   input trim
//     -> HPF (3rd-order Butterworth, 18 dB/oct, OFF by crossfade)
//     -> LPF (2nd-order Butterworth, 12 dB/oct, OFF by crossfade)
//     -> WEIGHT, SCULPT, BITE, OPEN in PARALLEL summation:
//          y = x0 + SUM_b (A_b - 1) * shape_b
//        where x0 is the SAME pre-band signal for every band (a parallel
//        console contour, not a serial chain) and shape_b is the SVF bell or
//        the SVF low/high shelf. Bell<->shelf morph is a continuous
//        per-sample blend of the SAME SVF states — no topology switch.
//     -> output trim (+ Auto Gain compensation when enabled)
//
// Neutral contract (spec §6): with every band at 0 dB, trims at unity, HPF
// and LPF OFF, BLOOM 0 and Auto Gain OFF, isSettledNeutral() is true and the
// PROCESSOR takes the bit-identical fast path (engine not invoked: no
// filters run, no state advances). Within the engine every (A - 1) is
// exactly 0.0f at 0 dB and every crossfade is exactly dry at rest, so the
// settled processing path is ALSO bit-identical to a straight wire.
//
// Realtime contract:
//   - zero heap allocation (fixed member arrays only)
//   - zero locks, zero I/O, zero GUI access, zero string construction
//   - bounded per-sample work, shared control state across channels
//     (deterministic mono == stereo-left; output independent of block size)
//   - denormal flush on every filter state
//
// Thread ownership:
//   prepare()/reset()/setProfile() -> message thread (before audio starts)
//   processBlock()                 -> audio thread only
//   set*Target()                   -> any thread (plain float stores)
//   adoptTargets()                 -> audio thread, once per block
// ============================================================================

class C4EngineCore
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels,
                  const C4TuningProfile& profile) noexcept
    {
        jassert (sampleRate > 0.0);
        jassert (numChannels >= 1 && numChannels <= kMaxChannels);
        juce::ignoreUnused (maxBlockSize); // API parity with the processor;
                                           // C4 needs no per-block allocation

        rate_ = (float) sampleRate;
        profile_ = profile;

        for (int b = 0; b < kNumBands; ++b)
        {
            shared_[b].prepare (sampleRate, profile, (C4BandId) b);
            for (int ch = 0; ch < kMaxChannels; ++ch)
                channels_[b][ch].reset();
        }

        inputMs_.prepare (sampleRate, profile.smoothing.trimMs);
        outputMs_.prepare (sampleRate, profile.smoothing.trimMs);
        bloomMs_.prepare (sampleRate, profile.smoothing.bloomMs);
        hpfFreqMs_.prepare (sampleRate, profile.smoothing.freqMs);
        lpfFreqMs_.prepare (sampleRate, profile.smoothing.freqMs);
        hpfMixMs_.prepare (sampleRate, profile.smoothing.modeMs);
        lpfMixMs_.prepare (sampleRate, profile.smoothing.modeMs);

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hpf_[ch].reset();
            lpf_[ch].reset();
        }

        // BLOOM Coupling (Phase 3): per-adjacent-pair contour state.
        coupling_ = profile.coupling;
        for (int p = 0; p < kNumBands - 1; ++p)
        {
            contourGainMs_[p].prepare (sampleRate, profile.smoothing.gainMs);
            contourFreqMs_[p].prepare (sampleRate, profile.smoothing.freqMs);
            for (int ch = 0; ch < kMaxChannels; ++ch)
                contourSvf_[p][ch].reset();
        }

        // BLOOM Residual Bank (Phase 4): nonlinear color layer.
        for (int b = 0; b < kNumBands; ++b)
            color_[b] = profile.color[b];
        bloomLawExponent_ = profile.bloomLawExponent;
        residualBank_.prepare (sampleRate, profile);

        resetTargets();
        snapToTargets();
    }

    void reset() noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            shared_[b].reset();
            for (int ch = 0; ch < kMaxChannels; ++ch)
                channels_[b][ch].reset();
        }
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hpf_[ch].reset();
            lpf_[ch].reset();
        }
        for (int p = 0; p < kNumBands - 1; ++p)
        {
            contourGainMs_[p].reset (0.0f);
            contourFreqMs_[p].reset (0.0f);
            for (int ch = 0; ch < kMaxChannels; ++ch)
                contourSvf_[p][ch].reset();
        }
        residualBank_.reset();
        resetTargets();
        snapToTargets();
    }

    void setProfile (const C4TuningProfile& profile, double sampleRate) noexcept
    {
        profile_ = profile;
        for (int b = 0; b < kNumBands; ++b)
            shared_[b].prepare (sampleRate, profile, (C4BandId) b);
    }

    // ---- Control targets (plain float stores; any thread) -----------------

    void setBandFreqTargetHz (int band, float hz) noexcept
    {
        if (band >= 0 && band < kNumBands)
            shared_[band].setFreqTargetHz (hz);
    }

    void setBandGainTargetDb (int band, float db) noexcept
    {
        if (band >= 0 && band < kNumBands)
            shared_[band].setGainTargetDb (db);
    }

    void setBandQTarget (int band, float q) noexcept
    {
        if (band >= 0 && band < kNumBands)
            shared_[band].setQTarget (q);
    }

    void setBandModeTarget (int band, bool shelf) noexcept
    {
        if (band >= 0 && band < kNumBands)
            shared_[band].setModeTarget (shelf);
    }

    /** TEST ONLY (morph audit): hold a band's bell<->shelf blend at an
        arbitrary continuous value. Production always uses 0/1 targets. */
    void setBandModeBlendForTest (int band, float blend) noexcept
    {
        if (band >= 0 && band < kNumBands)
            shared_[band].setModeBlendTargetForTest (blend);
    }

    void setHpfTarget (float hz, bool enabled) noexcept
    {
        hpfHzTarget_ = std::log2 (juce::jmax (1.0f, hz));
        hpfEnabledTarget_ = enabled;
    }

    void setLpfTarget (float hz, bool enabled) noexcept
    {
        lpfHzTarget_ = std::log2 (juce::jmax (1.0f, hz));
        lpfEnabledTarget_ = enabled;
    }

    void setInputTargetDb (float db) noexcept { inputTargetDb_ = juce::jlimit (kTrimMinDb, kTrimMaxDb, db); }
    void setOutputTargetDb (float db) noexcept { outputTargetDb_ = juce::jlimit (kTrimMinDb, kTrimMaxDb, db); }
    void setBloomTarget01 (float norm) noexcept { bloomTarget_ = juce::jlimit (0.0f, 1.0f, norm); }
    void setAutoGainTarget (bool on) noexcept { autoGainTarget_ = on; }

    /** Audio thread, once per block: adopt the stored targets into the
        smoothers. autoGainCompDb is the Auto Gain compensation (computed by
        the processor from the band gains; 0 when Auto Gain is OFF). */
    void adoptTargets (float autoGainCompDb) noexcept
    {
        inputMs_.setTarget (inputTargetDb_);
        outputMs_.setTarget (outputTargetDb_ + autoGainCompDb);
        bloomMs_.setTarget (bloomTarget_);
        hpfFreqMs_.setTarget (hpfHzTarget_);
        lpfFreqMs_.setTarget (lpfHzTarget_);
        hpfMixMs_.setTarget (hpfEnabledTarget_ ? 1.0f : 0.0f);
        lpfMixMs_.setTarget (lpfEnabledTarget_ ? 1.0f : 0.0f);
    }

    // ---- Neutral gate (spec §6) -------------------------------------------
    // True only when EVERY target is neutral AND every smoother has converged
    // exactly. The processor then skips the engine entirely (bit-identical
    // wire, no filters running, no allocations, no state advancement).
    //
    // Phase 5 (production BLOOM default > 0): BLOOM is deliberately NOT a
    // neutrality condition. The residual bank can only produce non-zero
    // output when a band gain is non-zero (activation is exactly 0 at 0 dB),
    // and every band is required settled-neutral below. The bloom smoother's
    // own convergence is still required, so a BLOOM move to/from any value
    // keeps the engine running until the smoother settles (click-free resume;
    // no transient color can appear after a skipped period).
    bool isSettledNeutral() const noexcept
    {
        if (inputTargetDb_ != 0.0f || outputTargetDb_ != 0.0f)
            return false;
        if (autoGainTarget_)
            return false;
        if (inputMs_.getValue() != inputMs_.getTarget()
            || outputMs_.getValue() != outputMs_.getTarget()
            || bloomMs_.getValue() != bloomMs_.getTarget())
            return false;

        for (int b = 0; b < kNumBands; ++b)
            if (! shared_[b].isSettledNeutral())
                return false;

        if (hpfEnabledTarget_ || lpfEnabledTarget_)
            return false;
        if (hpfMixMs_.getValue() != 0.0f || lpfMixMs_.getValue() != 0.0f)
            return false;
        if (hpfFreqMs_.getValue() != hpfFreqMs_.getTarget()
            || lpfFreqMs_.getValue() != lpfFreqMs_.getTarget())
            return false;

        return true;
    }

    // ---- Audio ------------------------------------------------------------

    void processBlock (float** chans, int numChannels, int numSamples) noexcept
    {
        jassert (chans != nullptr);
        if (numChannels <= 0 || numSamples <= 0)
            return;

        const int nch = juce::jmin (numChannels, kMaxChannels);

        for (int s = 0; s < numSamples; ++s)
        {
            // ---- Shared per-sample state (computed ONCE per sample) -------
            for (int b = 0; b < kNumBands; ++b)
            {
                shared_[b].advance();
                for (int ch = 0; ch < nch; ++ch)
                    channels_[b][ch].setCoefficients (shared_[b], rate_);
            }

            const float inputGain = dbToGain (inputMs_.advance());
            const float outputGain = dbToGain (outputMs_.advance());
            bloomNorm_ = bloomMs_.advance();
            // BLOOM control law (Phase 4): residual scale = bloomNorm^law.
            // Exactly 0 at BLOOM 0 -> the residual bank is skipped entirely
            // (bit-identical linear output, zero nonlinear work).
            const float bloomDrive = std::pow (bloomNorm_, bloomLawExponent_);

            // HPF / LPF crossfade envelopes (exactly 0 at rest -> dry).
            const float hpfMix = hpfMixMs_.advance();
            const float lpfMix = lpfMixMs_.advance();
            // Cutoff clamp: a control value above ~Nyquist is unrealizable;
            // clamp to a safe in-band ceiling (the 24 kHz LPF control at
            // 44.1 kHz therefore behaves as ~21.6 kHz — finite, stable,
            // monotonic; see the sample-rate suite).
            const float nyquistCeiling = 0.49f * rate_;
            const float hpfHz = juce::jmin (std::exp2 (hpfFreqMs_.advance()), nyquistCeiling);
            const float lpfHz = juce::jmin (std::exp2 (lpfFreqMs_.advance()), nyquistCeiling);

            // BLOOM Coupling (Phase 3): shared per-sample contour state —
            // computed ONCE per sample (mono == stereo-left determinism).
            // A contour bell between ADJACENT boosted bands (WEIGHT<->SCULPT,
            // SCULPT<->BITE, BITE<->OPEN). Boost+boost only; it NEVER
            // subtracts from the user's requested peaks — it fills the valley
            // between the facing shoulders with a smooth bump at the log
            // midpoint. The contour gain is smoothed per sample (click-free)
            // and is exactly 0 when either band is at/under 0 dB or the
            // overlap is below the threshold, so the uncoupled response is
            // bit-identical whenever coupling is inactive.
            if (coupling_.enabled)
            {
                for (int p = 0; p < kNumBands - 1; ++p)
                {
                    const int b = p;
                    const float gA = shared_[b].getGainDb();
                    const float gB = shared_[b + 1].getGainDb();
                    float targetDb = 0.0f;
                    float freqLog2Target = 0.0f;
                    if (gA > 0.0f && gB > 0.0f)
                    {
                        const float fA = shared_[b].getFreqHz();
                        const float fB = shared_[b + 1].getFreqHz();
                        const float oct = std::abs (std::log2 (juce::jmax (1.0f, fA)
                                                             / juce::jmax (1.0f, fB)));
                        const float overlap = 1.0f - oct / 5.0f; // 5 octaves = none
                        if (overlap > coupling_.overlapThreshold)
                        {
                            const float engage = (overlap - coupling_.overlapThreshold)
                                               / (1.0f - coupling_.overlapThreshold);
                            targetDb = coupling_.maxContourDb * coupling_.strength
                                     * (gA / kGainMaxDb) * (gB / kGainMaxDb) * engage;
                            freqLog2Target = 0.5f * (std::log2 (juce::jmax (1.0f, fA))
                                                   + std::log2 (juce::jmax (1.0f, fB)));
                        }
                    }
                    contourGainMs_[p].setTarget (targetDb);
                    contourFreqMs_[p].setTarget (freqLog2Target);
                    contourADb_[p] = dbToGain (contourGainMs_[p].advance()) - 1.0f;
                    contourFreqHz_[p] = std::exp2 (contourFreqMs_[p].advance());
                }
            }

            for (int ch = 0; ch < nch; ++ch)
            {
                hpf_[ch].setCoefficients (hpfHz, rate_);
                lpf_[ch].setCoefficients (lpfHz, rate_);

                // ---- Per-channel audio ------------------------------------
                float x = chans[ch][s] * inputGain;

                // HPF (crossfade: x at rest OFF, filtered when ON).
                const float hpfOut = hpf_[ch].process (x);
                x = x * (1.0f - hpfMix) + hpfOut * hpfMix;

                // LPF (same crossfade contract).
                const float lpfOut = lpf_[ch].process (x);
                x = x * (1.0f - lpfMix) + lpfOut * lpfMix;

                // Parallel band summation: every band sees the SAME x0 and
                // the contributions ACCUMULATE: y = x0 + SUM_b (A_b-1)*shape.
                const float x0 = x;
                for (int b = 0; b < kNumBands; ++b)
                    x += channels_[b][ch].process (x0, shared_[b]);

                // Per-channel contour SVF (the shared targets were computed
                // above; the SVF states are per-channel like the band SVFs).
                if (coupling_.enabled)
                {
                    for (int p = 0; p < kNumBands - 1; ++p)
                    {
                        const float aMinus1 = contourADb_[p];
                        if (aMinus1 != 0.0f)
                        {
                            float low, band, high;
                            contourSvf_[p][ch].setCoefficients (contourFreqHz_[p],
                                                                coupling_.contourQ, rate_);
                            contourSvf_[p][ch].process (x0, low, band, high);
                            x += aMinus1 * band;
                        }
                    }
                }

                // BLOOM Residual Bank (Phase 4): nonlinear color. Skipped
                // entirely at BLOOM 0 (exact linear baseline). Activation
                // follows the SMOOTHED band gain: positive boosts activate,
                // cuts are suppressed (cutSuppression), 0 dB is exactly zero.
                if (bloomDrive > 0.0f)
                {
                    for (int b = 0; b < kNumBands; ++b)
                    {
                        const float g = shared_[b].getGainDb();
                        float activation = 0.0f;
                        if (g > 0.0f)
                            activation = std::pow (g / kGainMaxDb, color_[b].activationExp);
                        else if (g < 0.0f)
                            activation = std::pow (-g / kGainMaxDb, color_[b].activationExp)
                                       * (1.0f - color_[b].cutSuppression);

                        const float residual = residualBank_.process (
                            b, ch, channels_[b][ch].getLastShape(),
                            activation, bloomDrive, color_[b]);
                        x += residual;
                    }
                }

                x *= outputGain;

                chans[ch][s] = x;
            }
        }
    }

    // ---- Introspection (tests / UI) ----------------------------------------

    const C4TuningProfile& getProfile() const noexcept { return profile_; }

    float getSmoothedBandGainDb (int band) const noexcept
    {
        return band >= 0 && band < kNumBands ? shared_[band].getGainDb() : 0.0f;
    }

    float getSmoothedBandFreqHz (int band) const noexcept
    {
        return band >= 0 && band < kNumBands ? shared_[band].getFreqHz() : 0.0f;
    }

    float getSmoothedBandQ (int band) const noexcept
    {
        return band >= 0 && band < kNumBands ? shared_[band].getEffectiveQ() : 0.0f;
    }

    float getSmoothedBandModeBlend (int band) const noexcept
    {
        return band >= 0 && band < kNumBands ? shared_[band].getModeBlend() : 0.0f;
    }

    float getSmoothedInputDb() const noexcept { return inputMs_.getValue(); }
    float getSmoothedOutputDb() const noexcept { return outputMs_.getValue(); }

    // ---- Analytic response-curve introspection (Phase 6; read-only,
    //      message/GUI thread; never used by the audio path) ----------------

    float getSmoothedHpfHz() const noexcept { return std::exp2 (hpfFreqMs_.getValue()); }
    float getSmoothedHpfMix() const noexcept { return hpfMixMs_.getValue(); }
    float getSmoothedLpfHz() const noexcept { return std::exp2 (lpfFreqMs_.getValue()); }
    float getSmoothedLpfMix() const noexcept { return lpfMixMs_.getValue(); }
    float getContourGainDb (int pair) const noexcept
    {
        return (pair >= 0 && pair < kNumBands - 1)
            ? juce::jmax (0.0f, gainToDb (juce::jmax (0.0f, contourADb_[pair] + 1.0f)))
            : 0.0f;
    }
    float getContourFreqHz (int pair) const noexcept
    {
        return (pair >= 0 && pair < kNumBands - 1) ? contourFreqHz_[pair] : 0.0f;
    }
    float getCouplingQ() const noexcept { return coupling_.contourQ; }

    float getBloomNorm() const noexcept { return bloomNorm_; }

    // ---- Test introspection (permanent: the lifecycle suite's state
    //      monitor reads these; read-only, never used by the audio path) ----
    struct SvfState { float ic1, ic2; };
    SvfState getBandSvfStateForTest (int band, int channel) const noexcept
    {
        SvfState s {};
        if (band >= 0 && band < kNumBands && channel >= 0 && channel < kMaxChannels)
            channels_[band][channel].getSvfStateForTest (s.ic1, s.ic2);
        return s;
    }
    SvfState getHpfSvfStateForTest (int channel) const noexcept
    {
        SvfState s {};
        if (channel >= 0 && channel < kMaxChannels)
            hpf_[channel].getSvfStateForTest (s.ic1, s.ic2);
        return s;
    }
    SvfState getLpfSvfStateForTest (int channel) const noexcept
    {
        SvfState s {};
        if (channel >= 0 && channel < kMaxChannels)
            lpf_[channel].getSvfStateForTest (s.ic1, s.ic2);
        return s;
    }
    float getHpfOnePoleStateForTest (int channel) const noexcept
    {
        return channel >= 0 && channel < kMaxChannels ? hpf_[channel].getOnePoleStateForTest() : 0.0f;
    }

private:
    void resetTargets() noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            shared_[b].setFreqTargetHz (kBandInfos[b].defaultFreqHz);
            shared_[b].setGainTargetDb (0.0f);
            shared_[b].setQTarget (1.0f);
            shared_[b].setModeTarget (false);
        }
        inputTargetDb_ = 0.0f;
        outputTargetDb_ = 0.0f;
        bloomTarget_ = 0.0f;
        hpfHzTarget_ = std::log2 (kHpfMaxHz);
        lpfHzTarget_ = std::log2 (kLpfMinHz);
        hpfEnabledTarget_ = false;
        lpfEnabledTarget_ = false;
        autoGainTarget_ = false;
    }

    void snapToTargets() noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
            shared_[b].snapToTargets();
        inputMs_.snapToTarget();
        outputMs_.snapToTarget();
        bloomMs_.snapToTarget();
        hpfFreqMs_.snapToTarget();
        lpfFreqMs_.snapToTarget();
        hpfMixMs_.snapToTarget();
        lpfMixMs_.snapToTarget();
    }

    float rate_ = 48000.0f;
    C4TuningProfile profile_;

    C4BandShared shared_[kNumBands];
    C4BandCore channels_[kNumBands][kMaxChannels];
    C4ButterworthHpf3 hpf_[kMaxChannels];
    C4ButterworthLpf2 lpf_[kMaxChannels];

    // BLOOM Coupling (Phase 3): per-adjacent-pair contour state.
    C4CouplingTuning coupling_ {};
    C4Smoother contourGainMs_[kNumBands - 1];
    C4Smoother contourFreqMs_[kNumBands - 1];
    C4SvfSection contourSvf_[kNumBands - 1][kMaxChannels];
    float contourADb_[kNumBands - 1] = {};
    float contourFreqHz_[kNumBands - 1] = {};

    // BLOOM Residual Bank (Phase 4): nonlinear color layer.
    C4ResidualBankCore residualBank_;
    C4BloomColorTuning color_[kNumBands] {};
    float bloomLawExponent_ = 1.0f;

    C4Smoother inputMs_, outputMs_, bloomMs_;
    C4Smoother hpfFreqMs_, lpfFreqMs_;
    C4Smoother hpfMixMs_, lpfMixMs_;

    // Control targets (plain floats; adopted per block on the audio thread).
    float inputTargetDb_ = 0.0f;
    float outputTargetDb_ = 0.0f;
    float bloomTarget_ = 0.0f;
    float hpfHzTarget_ = 0.0f, lpfHzTarget_ = 0.0f;
    bool hpfEnabledTarget_ = false;
    bool lpfEnabledTarget_ = false;
    bool autoGainTarget_ = false;

    float bloomNorm_ = 0.0f;
};

} // namespace C4
} // namespace APEX
