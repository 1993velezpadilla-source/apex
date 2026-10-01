#pragma once
#include <JuceHeader.h>

#include <complex>
#include <cmath>

namespace APEX {
namespace C4 {

// ============================================================================
// C4ResponseCurveCore — Phase 6: the ANALYTIC total C4 linear response curve.
//
// The Spectrum Flag's "total response curve" is derived from the engine's
// CURRENT SMOOTHED filter state (band freq/gain/Q/mode, HPF/LPF, trims, and
// the coupling contours) — "what C4 is mathematically applying" — NOT from a
// bin-wise subtraction of measured PRE/POST audio spectra.
//
// Why: a spectrum-transfer measurement based on single FFT bins is source-
// dependent and window-dependent (independently windowed PRE/POST frames
// suffer different Hann scalloping and frame alignment — a measured false
// gain; see the Phase 6 evidence). The analytic curve is exact at every band
// center and needs no audio at all; the PRE/POST FFTs remain the honest
// signal spectra ("what energy exists").
//
// Scope: LINEAR contribution only. BLOOM's nonlinear residual is NOT faked
// as a linear transfer curve (documented; a BLOOM visualization is a future
// decision, not part of this fix).
//
// Thread: pure const math; callable from the GUI timer / tests. No state,
// no allocation beyond the caller's output array.
// ============================================================================

namespace ResponseCurve
{

// ---------------------------------------------------------------------------
// TPT SVF continuous-prototype transfer magnitudes (Zavalishin, bilinear-
// prewarped like C4FilterCore::C4SvfSection; g = tan(pi*f0/fs), k = 1/Q).
//   Low:   1 / D            Band: (j*k*O) / D       High: -O^2 / D
//   D = (1 - O^2) + j*k*O,  O = f / f0
// The engine's band output is the k-normalized band: |band(f0)| == 1.
// ---------------------------------------------------------------------------

inline std::complex<double> svfLow (double o, double k) noexcept
{
    const std::complex<double> d ((1.0 - o * o), k * o);
    return 1.0 / d;
}

inline std::complex<double> svfBand (double o, double k) noexcept
{
    const std::complex<double> d ((1.0 - o * o), k * o);
    return std::complex<double> (0.0, k * o) / d;
}

inline std::complex<double> svfHigh (double o, double k) noexcept
{
    const std::complex<double> d ((1.0 - o * o), k * o);
    return std::complex<double> (-(o * o), 0.0) / d;
}

// One-pole bilinear high-pass with prewarping (C4OnePoleHpf):
//   H(z) = g*(1 - z^-1)/(1 + p*z^-1), g = 1/(1+w'), p = (w'-1)/(w'+1),
//   w' = tan(pi*f0/fs). Complex evaluation on the unit circle.
inline std::complex<double> onePoleHpf (double f, double f0, double fs) noexcept
{
    const double w = std::tan (juce::MathConstants<double>::pi
                               * juce::jlimit (1.0, fs * 0.49, f0) / fs);
    const double g = 1.0 / (1.0 + w);
    const double p = (w - 1.0) / (w + 1.0);
    const double th = 2.0 * juce::MathConstants<double>::pi * f / fs;
    const std::complex<double> num (g * (1.0 - std::cos (th)), -g * std::sin (th));
    const std::complex<double> den (1.0 + p * std::cos (th), p * std::sin (th));
    return num / den;
}

// ---------------------------------------------------------------------------
// Band state snapshot (filled by the caller from C4EngineCore getters).
// ---------------------------------------------------------------------------

struct BandState
{
    float freqHz = 100.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
    float modeBlend = 0.0f;    // 0 = bell, 1 = shelf
    bool highShelf = false;    // OPEN uses the high-shelf (HALO) output
};

struct FilterState
{
    float hpfHz = 0.0f, hpfMix = 0.0f;   // mix 0 = off, 1 = engaged
    float lpfHz = 0.0f, lpfMix = 0.0f;
};

struct ContourState
{
    float gainDb = 0.0f;   // contour (A-1) contribution in dB (0 = inactive)
    float freqHz = 0.0f;
    float q = 1.0f;
};

// ---------------------------------------------------------------------------
// Total linear response in dB at `freq` for the given state.
// ---------------------------------------------------------------------------

inline double magnitudeDbAt (double freq, double sampleRate,
                             double trimInDb, double trimOutDb,
                             const FilterState& filters,
                             const BandState (&bands)[kNumBands],
                             const ContourState (&contours)[kNumBands - 1]) noexcept
{
    std::complex<double> t (1.0, 0.0); // parallel console sum

    // HPF: 3rd-order Butterworth = SVF high (Q=1) -> one-pole bilinear HPF,
    // crossfaded by the engine: H = (1-mix) + mix * H_filt.
    if (filters.hpfMix > 0.0f && filters.hpfHz > 0.0f)
    {
        const double o = freq / filters.hpfHz;
        const double m = filters.hpfMix;
        const std::complex<double> h = svfHigh (o, 1.0) * onePoleHpf (freq, filters.hpfHz, sampleRate);
        t *= (1.0 - m) + m * h;
    }

    // LPF: 2nd-order Butterworth (Q = 1/sqrt2), same crossfade contract.
    if (filters.lpfMix > 0.0f && filters.lpfHz > 0.0f)
    {
        const double o = freq / filters.lpfHz;
        const double m = filters.lpfMix;
        const std::complex<double> l = svfLow (o, juce::MathConstants<double>::sqrt2);
        t *= (1.0 - m) + m * l;
    }

    // Bands: y = x + SUM (A-1)*shape, shape = (1-m)*band + m*(low|high).
    for (const auto& b : bands)
    {
        const double a = dbToGain (b.gainDb);
        if (a == 1.0)
            continue;
        const double o = juce::jmax (1e-9, (double) freq / b.freqHz);
        const double k = 1.0 / juce::jlimit (0.05, 8.0, (double) b.q);
        const double m = b.modeBlend;
        const std::complex<double> shape =
            (1.0 - m) * svfBand (o, k)
            + m * (b.highShelf ? svfHigh (o, k) : svfLow (o, k));
        t += (a - 1.0) * shape;
    }

    // Coupling contours (adjacent-pair bells; inactive = 0 dB).
    for (const auto& c : contours)
    {
        if (c.gainDb <= 0.0f || c.freqHz <= 0.0f)
            continue;
        const double a = dbToGain (c.gainDb);
        const double o = juce::jmax (1e-9, (double) freq / c.freqHz);
        const double k = 1.0 / juce::jlimit (0.05, 8.0, (double) c.q);
        t += (a - 1.0) * svfBand (o, k);
    }

    const double magDb = 20.0 * std::log10 (std::max (1e-12, std::abs (t)));
    return trimInDb + trimOutDb + magDb;
}

/** Fill a GUI-ready magnitude curve (dB) over `n` log-spaced points from
    fMin..fMax (clamped to 0.49*fs). Caller owns the output array. */
inline void fillCurve (float* outDb, int n, double fMin, double fMax,
                       double sampleRate, double trimInDb, double trimOutDb,
                       const FilterState& filters,
                       const BandState (&bands)[kNumBands],
                       const ContourState (&contours)[kNumBands - 1]) noexcept
{
    if (outDb == nullptr || n <= 0)
        return;
    const double lo = juce::jmax (1.0, fMin);
    const double hi = juce::jmin (0.49 * sampleRate, juce::jmax (lo + 1.0, fMax));
    for (int i = 0; i < n; ++i)
    {
        const double t = n > 1 ? (double) i / (double) (n - 1) : 0.5;
        const double f = lo * std::pow (hi / lo, t);
        outDb[i] = (float) magnitudeDbAt (f, sampleRate, trimInDb, trimOutDb,
                                          filters, bands, contours);
    }
}

} // namespace ResponseCurve
} // namespace C4
} // namespace APEX
