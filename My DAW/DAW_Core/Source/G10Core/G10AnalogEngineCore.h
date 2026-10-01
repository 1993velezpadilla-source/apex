#pragma once
#include <JuceHeader.h>
#include "G10Types.h"
#include "G10CurveEngineCore.h"

namespace APEX {
namespace G10 {

// ============================================================================
// G10AnalogEngineCore — Phase 2 analog engine for APEX G10.
//
// Signal architecture (ANALOG ON):
//   Input Trim -> Oversampling (2x NORMAL / 4x HQ) -> Discrete Input Stage
//   -> SAME G10 Clean Curve Engine (at the oversampled rate)
//   -> Iron Output Stage -> Downsample -> Output Trim -> bypass crossfade
//
// ANALOG OFF is NOT routed through this core: G10Processor keeps the frozen
// Phase 1 clean path bit-exact. This core is only entered when the analog
// crossfade is active.
//
// Thread ownership:
//   prepare() / reset()  -> message thread (before audio starts)
//   processBlock()       -> audio thread only
//   set*Target()         -> any thread (stores atomics; adopted per block)
//
// Realtime contract (steady state):
//   - zero heap allocation: all buffers are fixed-size members, preallocated
//     in prepare() for the maximum oversampling factor (4x)
//   - zero locks, zero I/O, zero GUI access, zero string construction
//   - denormal flush on every filter state and the final sample
//
// Latency: zero samples. The anti-alias filters are minimum-phase IIR
// (8th-order Butterworth), so no FIR delay is introduced in any mode.
//
// Determinism: parameter smoothing advances exactly once per base-rate
// sample and is shared across channels; the oversampled curve engine
// advances its own smoothing once per oversampled sample. Mono and stereo
// produce identical per-sample trajectories and left/right always see
// sample-consistent transitions. Output is independent of block size.
//
// Internal nominal level: 0 dBFS == 1.0 (the curve engine's unity). The
// saturation stages are designed around this: at 0 dBFS the discrete stage
// contributes ~-1.1 dB and the iron stage ~-1.1 dB at low frequencies
// (the documented "saturation-loss level tie"); at -18 dBFS the stages are
// effectively transparent. The saturation is real (measurable THD), not a
// loudness trick.
// ============================================================================

// ---------------------------------------------------------------------------
// Butterworth lowpass (cascaded 2nd-order TDF2 sections), used as the
// anti-alias filter for both the upsampler and the downsampler.
//
// Design policy (Phase 2 AA infrastructure, measured):
//   - NORMAL (2x): 8th order, cutoff at 1.1x the original Nyquist
//     (26.4 kHz at 48 kHz base). Frozen: bit-identical to the original
//     Phase 2 design.
//   - HQ (4x): 10th order, cutoff at 1.0208333x the original Nyquist
//     (24.5 kHz at 48 kHz base). The bilinear (tan) warp stretches the 4x
//     stopband over 24-96 kHz, so the same absolute cutoff as 2x gives
//     ~6.5 dB WEAKER rejection at 30 kHz (the 10 kHz H3 fold) and ~2 dB
//     weaker at 28 kHz (the 20 kHz up-AA image). The 10th-order/24.5 kHz
//     design restores the product-level ordering: 4x beats 2x at the
//     10 kHz stress case (-59.5 vs -57.5 dB) and at 20 kHz (-45.0 vs
//     -40.8 dB) while keeping the 20 kHz passband within -0.05 dB.
// ---------------------------------------------------------------------------

class G10ButterworthAA
{
public:
    static constexpr int kMaxSections = 5; // 10th-order max (HQ AA)

    void setCoefficients (float fcHz, double sampleRate, int order = 8) noexcept
    {
        const int sections = juce::jlimit (1, kMaxSections, order / 2);
        const float w0 = (float) (2.0 * juce::MathConstants<double>::pi
                                  * juce::jmax (1.0, (double) fcHz)
                                  / juce::jmax (1.0, sampleRate));
        const float cosW0 = std::cos (w0);
        const float sinW0 = std::sin (w0);

        for (int i = 0; i < sections; ++i)
        {
            // Butterworth pole-pair Q for order n, section i. For n == 8 this
            // reproduces the original hardcoded Qs {0.5098, 0.6011, 0.8999,
            // 2.5626} in the same application order (bit-identical 2x path).
            float q;
            if (order == 8)
            {
                static constexpr float kQs8[4] = { 0.5098f, 0.6011f, 0.8999f, 2.5626f };
                q = kQs8[i];
            }
            else
            {
                const double theta = juce::MathConstants<double>::pi
                                     * (2.0 * i + order + 1.0) / (2.0 * order);
                q = (float) (1.0 / (2.0 * std::abs (std::cos (theta))));
            }

            const float alpha = sinW0 / (2.0f * q);
            const float b0 = (1.0f - cosW0) * 0.5f;
            const float b1 = 1.0f - cosW0;
            const float b2 = b0;
            const float a0 = 1.0f + alpha;
            const float a1 = -2.0f * cosW0;
            const float a2 = 1.0f - alpha;
            sections_[i].setCoefficients (b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
        }
        numSections_ = sections;
    }

    float process (float x) noexcept
    {
        for (int i = 0; i < numSections_; ++i)
            x = sections_[i].process (x);
        return x;
    }

    void reset() noexcept
    {
        for (int i = 0; i < numSections_; ++i)
            sections_[i].reset();
    }

    // Diagnostic accessor used by the permanent AA-coefficient contract test
    // (G10.Analog.OsDiag / AA coefficients).
    struct AaCoeffs { float b0, b1, b2, a1, a2; };
    AaCoeffs getSectionCoeffs (int section) const noexcept
    {
        return { sections_[section].b0_, sections_[section].b1_,
                 sections_[section].b2_, sections_[section].a1_,
                 sections_[section].a2_ };
    }

private:
    struct Section
    {
        float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
        float s1_ = 0.0f, s2_ = 0.0f;

        void setCoefficients (float b0, float b1, float b2, float a1, float a2) noexcept
        {
            b0_ = b0; b1_ = b1; b2_ = b2; a1_ = a1; a2_ = a2;
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
    };

    Section sections_[kMaxSections];
    int numSections_ = 4;
};

// ---------------------------------------------------------------------------
// G10OversamplerCore — fixed-factor (2x or 4x) up/down sampler.
//
// Upsampler: zero-stuff + Butterworth AA + interpolation gain normalization
// (xL). The xL restores the reconstructed oversampled baseband to the
// ORIGINAL input amplitude (zero insertion alone scales by 1/L), so the
// nonlinear stages downstream see the same level regardless of factor.
// Downsampler: Butterworth AA + decimate (no gain change).
// All buffers are preallocated in prepare() for the maximum factor; the
// factor is fixed per instance (NORMAL and HQ are separate chain instances).
//
// AA design is factor-specific (measured Phase 2 policy):
//   NORMAL (2x): 8th order, fc = 1.1x base Nyquist (bit-identical to the
//   original design; frozen).
//   HQ (4x): 10th order, fc = 1.0208333x base Nyquist (24.5 kHz at 48 kHz
//   base). The bilinear tan-warp stretches the 4x stopband over 24-96 kHz,
//   making the SAME absolute cutoff ~6.5 dB weaker at 30 kHz than at 2x; the
//   10th-order/lower-cutoff HQ design restores 4x >= 2x alias rejection at
//   the 10 kHz and 20 kHz stress cases (see the alias contract in the tests).
// ---------------------------------------------------------------------------

class G10OversamplerCore
{
public:
    static constexpr int kMaxChannels = G10CurveEngineCore::kMaxChannels;

    // Factor-specific AA design policy (Phase 2, measured).
    static constexpr float kAaCutoffRatio2x = 1.1f;        // fc = 1.1 x base Nyquist
    static constexpr float kAaCutoffRatio4x = 1.0208333f;  // fc = 24.5 kHz at 48 kHz base
    static constexpr int kAaOrder2x = 8;
    static constexpr int kAaOrder4x = 10;

    void prepare (double baseRate, int maxBlockSize, int numChannels, int factor) noexcept
    {
        baseRate_ = juce::jmax (1.0, baseRate);
        maxBlockSize_ = juce::jmax (1, maxBlockSize);
        numChannels_ = juce::jlimit (1, kMaxChannels, numChannels);
        factor_ = (factor == 4) ? 4 : 2;

        const float fc = (float) (baseRate_ * 0.5)
                       * (factor_ == 4 ? kAaCutoffRatio4x : kAaCutoffRatio2x);
        const int order = (factor_ == 4) ? kAaOrder4x : kAaOrder2x;
        const double osRate = baseRate_ * factor_;
        for (int ch = 0; ch < kMaxChannels; ++ch)
            aa_[ch].setCoefficients (fc, osRate, order);
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
            aa_[ch].reset();
    }

    int getFactor() const noexcept { return factor_; }

    /** Zero-stuff + AA + xL interpolation normalization. Writes
        numSamples * factor_ samples per channel into `out` (which must be
        preallocated to at least that size). The output baseband amplitude
        equals the input amplitude (the xL compensates the 1/L zero-stuff
        scaling). */
    void processUp (const juce::AudioBuffer<float>& in, int numChannels, int numSamples,
                    juce::AudioBuffer<float>& out) noexcept
    {
        const int chans = juce::jmin (numChannels, numChannels_);
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* src = in.getReadPointer (ch);
            float* dst = out.getWritePointer (ch);
            for (int s = 0; s < numSamples; ++s)
            {
                dst[s * factor_] = src[s];
                for (int k = 1; k < factor_; ++k)
                    dst[s * factor_ + k] = 0.0f;
            }
            const int total = numSamples * factor_;
            for (int i = 0; i < total; ++i)
                dst[i] = aa_[ch].process (dst[i]) * (float) factor_;
        }
    }

    /** AA + decimate. Reads numSamples * factor_ samples from `in` and writes
        numSamples samples to `out`. */
    void processDown (const juce::AudioBuffer<float>& in, int numChannels, int numSamples,
                      juce::AudioBuffer<float>& out) noexcept
    {
        const int chans = juce::jmin (numChannels, numChannels_);
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* src = in.getReadPointer (ch);
            float* dst = out.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                float y = 0.0f;
                for (int k = 0; k < factor_; ++k)
                    y = aa_[ch].process (src[i * factor_ + k]);
                dst[i] = y;
            }
        }
    }

private:
    double baseRate_ = 48000.0;
    int maxBlockSize_ = 512;
    int numChannels_ = 2;
    int factor_ = 2;

    G10ButterworthAA aa_[kMaxChannels];
};

// ---------------------------------------------------------------------------
// G10DiscreteInputStageCore — the "discrete" (transistor-style) input
// saturation. Phase 2B final topology (accepted D1B):
//
//   residual = (tanh(x) - x) * kSaturation + kAsymmetry * x * tanh(x)
//   dcState += (residual - dcState) * dcCoeff      (one-pole LP, 2 Hz)
//   y = x + residual - dcState
//
// The odd core (tanh(x) - x) is the original saturation-excess form; the
// even residual x*tanh(x) adds the controlled musical asymmetry (H2). The
// sub-audio DC blocker operates on the NONLINEAR RESIDUAL ONLY: the linear
// path is exactly x, so there is no low-cut on the audio signal (fundamental
// gain 0.000 dB at every frequency) while the DC created by the even
// residual is removed (>= 26 dB attenuation at 40 Hz, >= 60 dB at 1 kHz).
// dcCoeff is sample-rate derived (cutoff stays 2 Hz at any rate).
//
// Stateful (bounded sub-audio state, tau ~ 79.6 ms at 2 Hz): block-partition
// invariant, deterministic reset, bounded silence recovery, no runaway tail.
// Monotonic, bounded, continuous, zero allocations, denormal-safe.
// ---------------------------------------------------------------------------

class G10DiscreteInputStageCore
{
public:
    static constexpr int kMaxChannels = G10CurveEngineCore::kMaxChannels;
    static constexpr float kSaturation = 0.5f;  // odd core, documented Phase 2 value
    static constexpr float kAsymmetry = 0.05f;  // controlled even residual (Phase 2B)
    static constexpr float kDcBlockerHz = 2.0f; // sub-audio DC blocker (Phase 2B)

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax (1.0, sampleRate);
        dcCoeff_ = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                            * kDcBlockerHz / sampleRate_));
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
            dcState_[ch] = 0.0f;
    }

    void process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        const int chans = juce::jmin (numChannels, kMaxChannels);
        for (int ch = 0; ch < chans; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            float dc = dcState_[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = d[i];
                const float t = std::tanh (x);
                const float residual = (t - x) * kSaturation + kAsymmetry * x * t;
                dc += (residual - dc) * dcCoeff_;
                if (std::abs (dc) < 1.0e-30f) dc = 0.0f;
                float y = x + residual - dc;
                if (std::abs (y) < 1.0e-30f) y = 0.0f;
                d[i] = y;
            }
            dcState_[ch] = dc;
        }
    }

    // Diagnostic accessor: current DC estimate (per channel).
    float getDcState (int ch) const noexcept { return dcState_[ch]; }

private:
    double sampleRate_ = 48000.0;
    float dcCoeff_ = 0.0f;
    float dcState_[kMaxChannels] = { 0.0f, 0.0f };
};

// ---------------------------------------------------------------------------
// G10IronOutputStageCore — output transformer ("iron") saturation. Phase 2B
// final topology (accepted I1): bounded flux-inspired leaky integrator.
//
//   fluxRaw += (x - fluxRaw) * fluxCoeff      (one-pole LP, kFluxHz = 60 Hz)
//   flux = tanh(fluxRaw)                       (bounded, |flux| <= 1)
//   strength = kSaturation * (1 + kFluxDepth * |flux|)
//   y = x + (tanh(x) - x) * strength
//
// At equal amplitude, lower frequency -> larger flux excursion (the leaky
// integrator accumulates more per cycle), which is the required iron
// behavior: the saturation is strongest at low frequencies. kFluxDepth =
// 0.9 caps strength at 0.95, keeping the static transfer monotonic for
// every input (no foldback even at +18 dB trim). The flux modulates the
// nonlinear STRENGTH only — the linear term is x, so no bass is added and
// the low-level response stays flat. No DC from the state (LP of a
// zero-mean signal is zero-mean). Per-channel state, resettable,
// deterministic, zero allocations, denormal-safe.
// ---------------------------------------------------------------------------

class G10IronOutputStageCore
{
public:
    static constexpr int kMaxChannels = G10CurveEngineCore::kMaxChannels;
    static constexpr float kSaturation = 0.5f; // base odd saturation (Phase 2B)
    static constexpr float kFluxHz = 60.0f;    // flux leak cutoff (sub-bass)
    static constexpr float kFluxDepth = 0.9f;  // flux modulation depth (max strength 0.95 -> monotonic)

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = juce::jmax (1.0, sampleRate);
        fluxCoeff_ = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                              * kFluxHz / sampleRate_));
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
            fluxRaw_[ch] = 0.0f;
    }

    void process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        const int chans = juce::jmin (numChannels, kMaxChannels);
        for (int ch = 0; ch < chans; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            float raw = fluxRaw_[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = d[i];
                raw += (x - raw) * fluxCoeff_;
                if (std::abs (raw) < 1.0e-30f) raw = 0.0f;

                const float flux = std::tanh (raw);
                const float strength = kSaturation * (1.0f + kFluxDepth * std::abs (flux));
                float y = x + (std::tanh (x) - x) * strength;
                if (std::abs (y) < 1.0e-30f) y = 0.0f;
                d[i] = y;
            }
            fluxRaw_[ch] = raw;
        }
    }

    // Diagnostic accessor: current bounded flux proxy (per channel).
    float getFlux (int ch) const noexcept { return std::tanh (fluxRaw_[ch]); }

private:
    double sampleRate_ = 48000.0;
    float fluxCoeff_ = 0.0f;
    float fluxRaw_[kMaxChannels] = { 0.0f, 0.0f };
};

// ---------------------------------------------------------------------------
// G10AnalogChainCore — the complete ANALOG ON chain for one quality factor.
//
//   input trim (base rate) -> upsample -> discrete -> curve engine (unity
//   trims, bypass off, at the oversampled rate) -> iron -> downsample ->
//   output trim (base rate) -> bypass crossfade (base rate)
//
// The chain owns its trim smoothing and bypass crossfade at the base rate;
// the internal curve engine is a separate instance from the frozen clean
// engine so the clean path is never perturbed.
// ---------------------------------------------------------------------------

class G10AnalogChainCore
{
public:
    static constexpr int kMaxChannels = G10CurveEngineCore::kMaxChannels;

    void prepare (double baseRate, int maxBlockSize, int numChannels, int factor) noexcept
    {
        baseRate_ = juce::jmax (1.0, baseRate);
        maxBlockSize_ = juce::jmax (1, maxBlockSize);
        numChannels_ = juce::jlimit (1, kMaxChannels, numChannels);
        factor_ = (factor == 4) ? 4 : 2;

        const double osRate = baseRate_ * factor_;
        const int osMax = maxBlockSize_ * factor_;

        up_.prepare (baseRate_, maxBlockSize_, numChannels_, factor_);
        down_.prepare (baseRate_, maxBlockSize_, numChannels_, factor_);
        discrete_.prepare (osRate);
        iron_.prepare (osRate);
        engine_.prepare (osRate, osMax, numChannels_);

        // The chain's engine has unity trims and no bypass: the chain
        // applies trims and the bypass crossfade at the base rate.
        engine_.setInputTargetDb (0.0f);
        engine_.setOutputTargetDb (0.0f);
        engine_.setBypassTarget (false);

        // Base-rate smoothing (same time constants as the frozen engine).
        trimSmoothCoeff_ = (float) (1.0 - std::exp (-1.0 / (baseRate_ * 0.015)));
        bypassStep_ = 1.0f / (float) juce::jmax (1.0, baseRate_ * 0.007);

        dryScratch_.setSize (numChannels_, maxBlockSize_, false, false, true);
        osBuffer_.setSize (numChannels_, osMax, false, false, true);

        inputSmoothDb_ = inputTargetDb_;
        outputSmoothDb_ = outputTargetDb_;
        bypassMix_ = bypassTarget_ ? 1.0f : 0.0f;
    }

    void reset() noexcept
    {
        up_.reset();
        down_.reset();
        discrete_.reset();
        iron_.reset();
        engine_.reset();
        inputSmoothDb_ = inputTargetDb_;
        outputSmoothDb_ = outputTargetDb_;
        bypassMix_ = bypassTarget_ ? 1.0f : 0.0f;
    }

    // ---- control-plane setters (any thread; atomic adoption) --------------

    void setBandTargetGainDb (int bandIndex, float db) noexcept
    {
        engine_.setBandTargetGainDb (bandIndex, db);
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

    // ---- introspection ----------------------------------------------------

    int getLatencySamples() const noexcept { return 0; } // IIR AA: zero latency
    int getFactor() const noexcept { return factor_; }
    G10CurveEngineCore& getEngine() noexcept { return engine_; }
    const G10CurveEngineCore& getEngine() const noexcept { return engine_; }

    // Diagnostic: the bypass crossfade has reached its endpoint (fully dry
    // when bypassed, fully processed when not). The chain owns the bypass
    // crossfade at the base rate; the processor's dormant clean engine is
    // not in the canonical audio path and must not be inspected for it.
    bool isBypassSettled() const noexcept
    {
        return bypassMix_ <= 0.001f || bypassMix_ >= 0.999f;
    }

    // ---- audio thread -----------------------------------------------------

    void processBlock (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        const int chans = juce::jmin (numChannels, kMaxChannels);
        if (chans <= 0 || numSamples <= 0)
            return;

        // Oversized-block defense: the processor chunks oversized host
        // buffers at its ownership boundary, so every invocation here is
        // within the prepared capacity. Debug-only contract check; the
        // release path never truncates or resizes.
        jassert (numSamples <= maxBlockSize_);

        // Fully bypassed fast path: dry passthrough (no trim, no processing).
        if (bypassMix_ >= 0.999f && bypassTarget_)
            return;

        // 1. Save the dry input for the bypass crossfade.
        for (int ch = 0; ch < chans; ++ch)
            dryScratch_.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        // 2. Input trim (base rate, smoothed once per sample, shared).
        for (int s = 0; s < numSamples; ++s)
        {
            inputSmoothDb_ += (inputTargetDb_ - inputSmoothDb_) * trimSmoothCoeff_;
            if (std::abs (inputSmoothDb_) < 1.0e-5f) inputSmoothDb_ = 0.0f;
            const float g = dbToGain (inputSmoothDb_);
            for (int ch = 0; ch < chans; ++ch)
                buffer.getWritePointer (ch)[s] *= g;
        }

        // 3. Upsample.
        up_.processUp (buffer, chans, numSamples, osBuffer_);

        // 4. Discrete input stage (oversampled).
        discrete_.process (osBuffer_, chans, numSamples * factor_);

        // 5. The SAME clean curve engine, at the oversampled rate.
        float* osData[kMaxChannels] = { nullptr, nullptr };
        for (int ch = 0; ch < chans; ++ch)
            osData[ch] = osBuffer_.getWritePointer (ch);
        engine_.processBlock (osData, chans, numSamples * factor_);

        // 6. Iron output stage (oversampled rate).
        iron_.process (osBuffer_, chans, numSamples * factor_);

        // 7. Downsample back to the base rate.
        down_.processDown (osBuffer_, chans, numSamples, buffer);

        // 8. Output trim + bypass crossfade (per-sample, shared).
        for (int s = 0; s < numSamples; ++s)
        {
            if (bypassMix_ < 0.999f && bypassTarget_)
                bypassMix_ = juce::jmin (1.0f, bypassMix_ + bypassStep_);
            else if (bypassMix_ > 0.001f && ! bypassTarget_)
                bypassMix_ = juce::jmax (0.0f, bypassMix_ - bypassStep_);

            const float dryGain = std::sin (bypassMix_ * juce::MathConstants<float>::halfPi);
            const float wetGain = std::cos (bypassMix_ * juce::MathConstants<float>::halfPi);

            outputSmoothDb_ += (outputTargetDb_ - outputSmoothDb_) * trimSmoothCoeff_;
            if (std::abs (outputSmoothDb_) < 1.0e-5f) outputSmoothDb_ = 0.0f;
            const float outGain = dbToGain (outputSmoothDb_);

            for (int ch = 0; ch < chans; ++ch)
            {
                float* d = buffer.getWritePointer (ch);
                const float dry = dryScratch_.getSample (ch, s);
                const float wet = d[s] * outGain;
                d[s] = dry * dryGain + wet * wetGain;
            }
        }
    }

private:
    double baseRate_ = 48000.0;
    int maxBlockSize_ = 512;
    int numChannels_ = 2;
    int factor_ = 2;

    G10OversamplerCore up_;
    G10OversamplerCore down_;
    G10DiscreteInputStageCore discrete_;
    G10IronOutputStageCore iron_;
    G10CurveEngineCore engine_;

    juce::AudioBuffer<float> osBuffer_;
    juce::AudioBuffer<float> dryScratch_;

    float trimSmoothCoeff_ = 0.00139f; // ~15 ms at 48 kHz
    float bypassStep_ = 0.003f;        // ~7 ms at 48 kHz

    float inputTargetDb_ = 0.0f;
    float outputTargetDb_ = 0.0f;
    float inputSmoothDb_ = 0.0f;
    float outputSmoothDb_ = 0.0f;
    bool bypassTarget_ = false;
    float bypassMix_ = 0.0f; // 0 = processed, 1 = dry (bypassed)
};

} // namespace G10
} // namespace APEX