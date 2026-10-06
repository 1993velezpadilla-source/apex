// ===========================================================================
// PitchScaleMathCore.h
// Canonical pitch math and smoothing helpers.
// ===========================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace ArrangementEditor
{

class PitchScaleMathCore
{
public:
    static constexpr double kUiMinSemitones       = -36.0;
    static constexpr double kUiMaxSemitones       =  36.0;
    static constexpr double kInternalMinSemitones = -48.0;
    static constexpr double kInternalMaxSemitones =  48.0;
    static constexpr double kSmoothingTauSeconds  =  0.060;
    static constexpr double kMaxPitchSlewSemitonesPerSecond = 480.0;

    static inline double semitonesToRatio(double st) noexcept
    {
        return std::pow(2.0, st / 12.0);
    }

    static inline double ratioToSemitones(double r) noexcept
    {
        return r > 0.0 ? 12.0 * std::log2(r) : kInternalMinSemitones;
    }

    static inline double centsToSemitones(double c) noexcept
    {
        return c / 100.0;
    }

    static inline double sanitizeNaN(double v, double fallback = 0.0) noexcept
    {
        return std::isfinite(v) ? v : fallback;
    }

    static inline double clampPitch(double st, bool allowLegacyHeadroom = true) noexcept
    {
        st = sanitizeNaN(st);
        return std::clamp(st,
                          allowLegacyHeadroom ? kInternalMinSemitones : kUiMinSemitones,
                          allowLegacyHeadroom ? kInternalMaxSemitones : kUiMaxSemitones);
    }

    static inline double smoothstep(double edge0, double edge1, double x) noexcept
    {
        if (edge0 == edge1)
            return x >= edge1 ? 1.0 : 0.0;

        const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    static inline double lerp(double a, double b, double t) noexcept
    {
        return a + (b - a) * t;
    }

    static inline double onePoleCoefficient(double sampleRate, double tauSeconds = kSmoothingTauSeconds) noexcept
    {
        sampleRate = std::max(1.0, sanitizeNaN(sampleRate, 44100.0));
        tauSeconds = std::max(0.0001, sanitizeNaN(tauSeconds, kSmoothingTauSeconds));
        return 1.0 - std::exp(-1.0 / (tauSeconds * sampleRate));
    }

    static inline double onePoleCoefficientForSamples(double sampleRate, int numSamples, double tauSeconds = kSmoothingTauSeconds) noexcept
    {
        sampleRate = std::max(1.0, sanitizeNaN(sampleRate, 44100.0));
        tauSeconds = std::max(0.0001, sanitizeNaN(tauSeconds, kSmoothingTauSeconds));
        numSamples = std::max(1, numSamples);
        return 1.0 - std::exp(-(double)numSamples / (tauSeconds * sampleRate));
    }

    static inline double onePoleNext(double current, double target, double coefficient) noexcept
    {
        return current + coefficient * (target - current);
    }

    static inline double limitDelta(double current, double target, double maxDelta) noexcept
    {
        maxDelta = std::max(0.0, sanitizeNaN(maxDelta));
        const double delta = target - current;
        return current + std::clamp(delta, -maxDelta, maxDelta);
    }

    static inline float flushDenormal(float v) noexcept
    {
        return std::abs(v) < 1.0e-20f ? 0.0f : v;
    }

    static inline double flushDenormal(double v) noexcept
    {
        return std::abs(v) < 1.0e-20 ? 0.0 : v;
    }
};

} // namespace ArrangementEditor
