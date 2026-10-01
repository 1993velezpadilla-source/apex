#pragma once

#include "ParametricEQResponseCore.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

namespace APEX::ParametricEQ
{

// Genuine linear-phase processing for the APEX Parametric EQ.
//
// The total canonical EQ target response (the composed 2x2 stereo transfer
// from ResponseCore, including placement semantics) is sampled on a uniform
// frequency grid, given conjugate symmetry, transformed to a REAL impulse,
// centered, windowed (Hann) and published as fixed FIR kernels. The audio
// path performs direct time-domain convolution; kernel design never runs in
// the audio callback.
//
// Kernel geometry (documented tradeoff):
//   FFT size       4096
//   kernel length  4095 (odd: exact integer latency and exact symmetry)
//   latency        2047 samples (~46.4 ms @ 44.1 kHz, ~42.6 ms @ 48 kHz)
//   resolution     ~fs / 4096 (e.g., ~10.8 Hz @ 44.1 kHz)
//   window         Hann (reduces interpolation ripple; slightly smooths
//                   the target magnitude between frequency samples)
// Extreme-Q (> ~20) narrow peaks are approximated within the documented
// resolution; see PARAMETRIC_EQ_PHASE6_EVIDENCE.md for measured tolerances.
constexpr int kLinearPhaseFftSize = 4096;
constexpr int kLinearPhaseKernelLength = kLinearPhaseFftSize - 1;
constexpr int kLinearPhaseLatencySamples = kLinearPhaseKernelLength / 2;

// Fixed-size complex FFT (iterative radix-2). Design/worker threads only.
class LinearPhaseFft final
{
public:
    LinearPhaseFft() noexcept { buildTwiddles(); }

    void forward (std::complex<double>* data) const noexcept
    {
        transform (data, false);
    }

    // Inverse DFT: y[n] = (1/N) * sum_k X[k] * exp(+j 2 pi k n / N).
    void inverse (std::complex<double>* data) const noexcept
    {
        transform (data, true);
        const double scale = 1.0 / kLinearPhaseFftSize;
        for (int index = 0; index < kLinearPhaseFftSize; ++index)
            data[index] *= scale;
    }

private:
    void transform (std::complex<double>* data, bool inverseTransform) const noexcept
    {
        for (int index = 1, reversed = 0; index < kLinearPhaseFftSize; ++index)
        {
            int bit = kLinearPhaseFftSize >> 1;
            for (; reversed & bit; bit >>= 1)
                reversed ^= bit;
            reversed ^= bit;
            if (index < reversed)
                std::swap (data[index], data[reversed]);
        }

        for (int length = 2; length <= kLinearPhaseFftSize; length <<= 1)
        {
            const int half = length >> 1;
            const int step = kLinearPhaseFftSize / length;
            for (int base = 0; base < kLinearPhaseFftSize; base += length)
            {
                for (int index = 0; index < half; ++index)
                {
                    const int twiddle = index * step;
                    std::complex<double> w = twiddles_[twiddle];
                    if (inverseTransform)
                        w = std::conj (w);
                    const auto a = data[base + index];
                    const auto b = data[base + index + half] * w;
                    data[base + index] = a + b;
                    data[base + index + half] = a - b;
                }
            }
        }
    }

    void buildTwiddles() noexcept
    {
        for (int index = 0; index < kLinearPhaseFftSize / 2; ++index)
        {
            const auto angle = -2.0 * kPi * index / kLinearPhaseFftSize;
            twiddles_[index] = std::polar (1.0, angle);
        }
    }

    std::array<std::complex<double>, kLinearPhaseFftSize / 2> twiddles_ {};
};

// Four fixed FIR kernels (LL, LR, RL, RR) describing the total 2x2 transfer.
struct LinearPhaseKernels
{
    std::array<double, kLinearPhaseKernelLength> ll {};
    std::array<double, kLinearPhaseKernelLength> lr {};
    std::array<double, kLinearPhaseKernelLength> rl {};
    std::array<double, kLinearPhaseKernelLength> rr {};

    bool isNeutral() const noexcept
    {
        // The flat/neutral target is represented by a pure delay kernel.
        constexpr int centre = kLinearPhaseLatencySamples;
        constexpr double epsilon = 1.0e-9;
        for (int tap = 0; tap < kLinearPhaseKernelLength; ++tap)
        {
            const double expected = tap == centre ? 1.0 : 0.0;
            if (std::abs (ll[static_cast<std::size_t> (tap)] - expected) > epsilon
                || std::abs (lr[static_cast<std::size_t> (tap)]) > epsilon
                || std::abs (rl[static_cast<std::size_t> (tap)]) > epsilon
                || std::abs (rr[static_cast<std::size_t> (tap)] - expected) > epsilon)
                return false;
        }
        return true;
    }
};

// Designs the linear-phase kernels from the total canonical response.
// Runs on the kernel worker / control thread; never in processBlock.
class LinearPhaseKernelBuilder final
{
public:
    void build (const std::array<BandSettings, kMaxBands>& settings,
                DesignMode mode, double sampleRate,
                LinearPhaseKernels& destination) const noexcept
    {
        const auto rate = std::isfinite (sampleRate) && sampleRate > 1.0
                        ? sampleRate : 44100.0;
        const int half = kLinearPhaseFftSize / 2;

        std::array<std::array<std::complex<double>, kLinearPhaseFftSize>, 4>
            spectra {};

        for (int bin = 0; bin <= half; ++bin)
        {
            const auto frequency = rate * bin / kLinearPhaseFftSize;
            const auto matrix = ResponseCore::wetTransfer (settings, mode, rate,
                                                           frequency);
            // The linear-phase design spectrum is the ZERO-PHASE magnitude of
            // each 2x2 entry: real, non-negative, and conjugate-symmetric by
            // construction. The linear phase is supplied by the symmetric
            // tap arrangement, never by embedding the minimum-phase
            // response's phase into the sampled spectrum.
            const double magnitudeLl = std::abs (matrix.ll);
            const double magnitudeLr = std::abs (matrix.lr);
            const double magnitudeRl = std::abs (matrix.rl);
            const double magnitudeRr = std::abs (matrix.rr);
            // Time-domain centering by a frequency-domain phase rotation:
            // the impulse lands exactly at the odd-kernel centre
            // (kLinearPhaseLatencySamples) with the magnitude unchanged.
            const auto rotation = std::polar (1.0,
                -2.0 * kPi * bin * kLinearPhaseLatencySamples
                    / kLinearPhaseFftSize);
            const auto rotatedLl = magnitudeLl * rotation;
            const auto rotatedLr = magnitudeLr * rotation;
            const auto rotatedRl = magnitudeRl * rotation;
            const auto rotatedRr = magnitudeRr * rotation;
            spectra[0][static_cast<std::size_t> (bin)] = rotatedLl;
            spectra[1][static_cast<std::size_t> (bin)] = rotatedLr;
            spectra[2][static_cast<std::size_t> (bin)] = rotatedRl;
            spectra[3][static_cast<std::size_t> (bin)] = rotatedRr;
            if (bin > 0 && bin < half)
            {
                const auto conjugateBin = kLinearPhaseFftSize - bin;
                spectra[0][static_cast<std::size_t> (conjugateBin)]
                    = std::conj (rotatedLl);
                spectra[1][static_cast<std::size_t> (conjugateBin)]
                    = std::conj (rotatedLr);
                spectra[2][static_cast<std::size_t> (conjugateBin)]
                    = std::conj (rotatedRl);
                spectra[3][static_cast<std::size_t> (conjugateBin)]
                    = std::conj (rotatedRr);
            }
        }

        LinearPhaseFft fft;
        std::array<std::array<double, kLinearPhaseFftSize>, 4> impulses {};
        for (int path = 0; path < 4; ++path)
        {
            auto spectrum = spectra[static_cast<std::size_t> (path)];
            fft.inverse (spectrum.data());
            auto& impulse = impulses[static_cast<std::size_t> (path)];
            for (int sample = 0; sample < kLinearPhaseKernelLength; ++sample)
                impulse[static_cast<std::size_t> (sample)]
                    = spectrum[static_cast<std::size_t> (sample)].real();
        }

        // Hann window (reduces interpolation ripple; documented smoothing).
        std::array<double, kLinearPhaseKernelLength> window {};
        for (int tap = 0; tap < kLinearPhaseKernelLength; ++tap)
            window[static_cast<std::size_t> (tap)] = 0.5 - 0.5 * std::cos (
                2.0 * kPi * tap / (kLinearPhaseKernelLength - 1));

        for (int path = 0; path < 4; ++path)
        {
            const auto& impulse = impulses[static_cast<std::size_t> (path)];
            double* destinationTaps = path == 0 ? destination.ll.data()
                                    : path == 1 ? destination.lr.data()
                                    : path == 2 ? destination.rl.data()
                                                : destination.rr.data();
            for (int tap = 0; tap < kLinearPhaseKernelLength; ++tap)
                destinationTaps[tap] = impulse[static_cast<std::size_t> (tap)]
                                     * window[static_cast<std::size_t> (tap)];
        }
    }
};

// One channel of direct time-domain convolution with the published kernels.
// Fixed storage only: no allocation, locks, or blocking in the audio path.
class LinearPhaseConvolverChannel final
{
public:
    void reset() noexcept
    {
        delay_.fill (0.0);
        position_ = 0;
    }

    void processBlock (float* data, int numberOfSamples,
                       const double* kernel) noexcept
    {
        if (data == nullptr || kernel == nullptr)
            return;
        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            delay_[position_] = static_cast<double> (
                std::isfinite (data[sample]) ? data[sample] : 0.0f);
            double accumulated = 0.0;
            int read = position_;
            for (int tap = 0; tap < kLinearPhaseKernelLength; ++tap)
            {
                accumulated += kernel[tap] * delay_[static_cast<std::size_t> (read)];
                --read;
                if (read < 0)
                    read = kLinearPhaseKernelLength - 1;
            }
            data[sample] = std::isfinite (accumulated)
                         ? static_cast<float> (accumulated) : 0.0f;
            ++position_;
            if (position_ >= kLinearPhaseKernelLength)
                position_ = 0;
        }
    }

private:
    std::array<double, kLinearPhaseKernelLength> delay_ {};
    int position_ = 0;
};

} // namespace APEX::ParametricEQ
