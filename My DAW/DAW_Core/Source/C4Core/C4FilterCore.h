#pragma once
#include <JuceHeader.h>
#include "C4Types.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4FilterCore — realtime-safe DSP primitives for C4.
//
// All classes are fixed-size, allocation-free, denormal-flushed, and safe to
// run on the audio thread. Coefficients may be updated per sample (the TPT
// SVF is stable under coefficient interpolation, which is exactly what
// parameter smoothing requires).
//
// Latency: zero samples (pure minimum-phase IIR).
// ============================================================================

// ---------------------------------------------------------------------------
// One-pole smoother with exact-convergence snap. Sample-rate consistent:
// the coefficient derives from a time constant in milliseconds.
//
//   snapped == target when |target - value| < snapTolerance * max(1,|target|)
//
// The snap is what makes the neutral-path detection exact: a smoother whose
// target is exactly 0.0f eventually reports exactly 0.0f.
// ---------------------------------------------------------------------------

class C4Smoother
{
public:
    void prepare (double sampleRate, float timeMs) noexcept
    {
        jassert (sampleRate > 0.0);
        coeff_ = 1.0f - std::exp (-1.0f / (float) juce::jmax (1.0, sampleRate * 0.001 * timeMs));
        if (! (coeff_ > 0.0f && coeff_ < 1.0f))
            coeff_ = 1.0f;
    }

    void setTimeMs (double sampleRate, float timeMs) noexcept { prepare (sampleRate, timeMs); }

    void setTarget (float t) noexcept { target_ = t; }
    float getTarget() const noexcept { return target_; }

    void snapToTarget() noexcept { value_ = target_; }

    /** Advance one sample; returns the smoothed value.
        Convergence guarantee: the value ALWAYS reaches the target EXACTLY.
        Two snap paths, both explicit:
          1. tolerance snap — |target - value| within snapTol_ of the target
             magnitude (fast exact convergence near the target scale);
          2. representable no-progress snap — the prospective one-pole update
             `value + coeff*diff` is numerically identical to `value` (the
             per-sample step fell below half an ULP: float stagnation). In
             that state no further iteration can make progress, so we snap
             exactly to the target.
        Without path 2 the smoother can stagnate permanently below its
        target (measured: 5.999828 dB instead of 6.0 dB), which breaks the
        neutral gate's exact-convergence contract. */
    float advance() noexcept
    {
        const float diff = target_ - value_;

        if (std::abs (diff) <= snapTol_ * juce::jmax (1.0f, std::abs (target_)))
        {
            value_ = target_;
            return value_;
        }

        const float next = value_ + coeff_ * diff;

        if (next == value_)   // no representable progress: snap exactly
            value_ = target_;
        else
            value_ = next;

        return value_;
    }

    float getValue() const noexcept { return value_; }

    void reset (float initialValue = 0.0f) noexcept
    {
        value_ = initialValue;
        target_ = initialValue;
    }

private:
    float coeff_ = 1.0f;
    float value_ = 0.0f;
    float target_ = 0.0f;
    static constexpr float snapTol_ = 1.0e-7f;
};

// ---------------------------------------------------------------------------
// TPT (topology-preserving transform) state-variable filter section.
// Zavalishin form: numerically well-conditioned at low frequencies, stable
// under coefficient interpolation, cheap. Provides low / band / high.
//
// The band output is normalized so |band(f0)| == 1 (G10 pattern: scale the
// raw TPT band, which peaks at Q, by k = 1/Q).
// ---------------------------------------------------------------------------

class C4SvfSection
{
public:
    void setCoefficients (float f0Hz, float q, float sampleRate) noexcept
    {
        const float w = juce::MathConstants<float>::pi
                      * juce::jmax (1.0f, f0Hz) / juce::jmax (1.0f, sampleRate);
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
        band = v1 * k_;   // |band(f0)| == 1
        high = in - k_ * v1 - v2;
    }

    void reset() noexcept
    {
        ic1eq_ = 0.0f;
        ic2eq_ = 0.0f;
    }

    // Test introspection (permanent; read-only, never used by the audio path).
    void getStateForTest (float& ic1, float& ic2) const noexcept
    {
        ic1 = ic1eq_;
        ic2 = ic2eq_;
    }

private:
    float g_ = 0.0f, k_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1eq_ = 0.0f, ic2eq_ = 0.0f;
};

// ---------------------------------------------------------------------------
// First-order bilinear high-pass with prewarping (one real pole).
// H(z) = g * (1 - z^-1) / (1 + p * z^-1),  g = 1/(1+w'), p = (w'-1)/(w'+1).
// ---------------------------------------------------------------------------

class C4OnePoleHpf
{
public:
    void setCoefficients (float f0Hz, float sampleRate) noexcept
    {
        const float w = juce::MathConstants<float>::pi
                      * juce::jmax (1.0f, f0Hz) / juce::jmax (1.0f, sampleRate);
        const float ww = std::tan (juce::jmin (w, juce::MathConstants<float>::pi * 0.4999f));
        g_ = 1.0f / (1.0f + ww);
        p_ = (ww - 1.0f) / (ww + 1.0f);
    }

    float process (float x) noexcept
    {
        const float y = g_ * (x - x1_) - p_ * y1_;
        x1_ = x;
        y1_ = y;
        if (std::abs (x1_) < 1.0e-30f) x1_ = 0.0f;
        if (std::abs (y1_) < 1.0e-30f) y1_ = 0.0f;
        return y;
    }

    void reset() noexcept
    {
        x1_ = 0.0f;
        y1_ = 0.0f;
    }

    // Test introspection (permanent; read-only, never used by the audio path).
    float getStateForTest() const noexcept { return y1_; }

private:
    float g_ = 0.0f, p_ = 0.0f;
    float x1_ = 0.0f, y1_ = 0.0f;
};

// ---------------------------------------------------------------------------
// 3rd-order Butterworth high-pass (18 dB/oct), no resonant bump.
// Decomposition: one real pole + one complex pair (Q = 1). The complex pair
// is realized with the SVF high output (Q = 1 -> k = 1).
// ---------------------------------------------------------------------------

class C4ButterworthHpf3
{
public:
    void setCoefficients (float f0Hz, float sampleRate) noexcept
    {
        svf_.setCoefficients (f0Hz, 1.0f, sampleRate);
        pole_.setCoefficients (f0Hz, sampleRate);
    }

    float process (float x) noexcept
    {
        float low, band, high;
        svf_.process (x, low, band, high);
        return pole_.process (high);
    }

    void reset() noexcept
    {
        svf_.reset();
        pole_.reset();
    }

    // Test introspection (permanent; read-only, never used by the audio path).
    void getSvfStateForTest (float& ic1, float& ic2) const noexcept
    {
        svf_.getStateForTest (ic1, ic2);
    }
    float getOnePoleStateForTest() const noexcept
    {
        return pole_.getStateForTest();
    }

private:
    C4SvfSection svf_;
    C4OnePoleHpf pole_;
};

// ---------------------------------------------------------------------------
// 2nd-order Butterworth low-pass (12 dB/oct): SVF low output with Q = 1/sqrt2
// (k = sqrt2). Smooth, no unnecessary resonance.
// ---------------------------------------------------------------------------

class C4ButterworthLpf2
{
public:
    void setCoefficients (float f0Hz, float sampleRate) noexcept
    {
        svf_.setCoefficients (f0Hz, 0.70710678f, sampleRate);
    }

    float process (float x) noexcept
    {
        float low, band, high;
        svf_.process (x, low, band, high);
        return low;
    }

    void reset() noexcept { svf_.reset(); }

    // Test introspection (permanent; read-only, never used by the audio path).
    void getSvfStateForTest (float& ic1, float& ic2) const noexcept
    {
        svf_.getStateForTest (ic1, ic2);
    }

private:
    C4SvfSection svf_;
};

} // namespace C4
} // namespace APEX
