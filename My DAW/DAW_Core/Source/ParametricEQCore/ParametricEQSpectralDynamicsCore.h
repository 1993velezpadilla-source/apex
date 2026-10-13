#pragma once

#include "ParametricEQTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

namespace APEX::ParametricEQ
{

// Independent APEX spectral dynamics experiment. Only the audio thread owns
// this engine. All FFT, overlap/add, dynamics and history storage are fixed;
// prepare() and reset() are called while audio is quiescent.
//
// Signal law: 1024-sample sqrt-Hann STFT, 75% overlap, self-contained radix-2
// FFT, perfect-reconstruction 1/2 OLA normalization, EXACT 1024-sample latency.
// Per-bin attack/release detects individual frequencies in an EQ band's
// neighborhood rather than applying a single envelope to an entire biquad.
// Not FabFilter's proprietary implementation. Stereo, L/R, M/S supported.
//
// Embedded in Processor behind the prepareToPlay-latched spectral ARM and
// host PDC gate. This core alone does not communicate latency to the host.
class SpectralDynamicsCore final
{
public:
    static constexpr int kFftSize = 1024;
    static constexpr int kHopSize = 256;
    static constexpr int kBins = kFftSize / 2 + 1;
    static constexpr int kChannels = 2;
    static constexpr int kOutputRing = 4096;
    static constexpr int kLatencySamples = kFftSize;

    struct Band
    {
        bool enabled = false;
        FilterShape shape = FilterShape::Bell;
        ChannelPlacement placement = ChannelPlacement::Stereo;
        double frequencyHz = 1000.0;
        double q = 2.0;
        DynamicBandParameters dynamic {};
        double density = 0.5;  // [0,1]; higher means tighter spectral focus
        bool tilt = true;       // +3 dB/oct detector weighting
    };

    SpectralDynamicsCore() noexcept
    {
        prepare (48000.0);
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) && sampleRate > 1.0
                    ? sampleRate : 48000.0;
        constexpr double pi = 3.1415926535897932384626433832795;
        double totalWindow = 0.0;
        for (int index = 0; index < kFftSize; ++index)
        {
            const double hann = 0.5 - 0.5 * std::cos (
                2.0 * pi * index / kFftSize);
            window_[static_cast<std::size_t> (index)]
                = std::sqrt (std::max (0.0, hann));
            totalWindow += window_[static_cast<std::size_t> (index)];
        }
        binAmplitudeScale_ = 2.0 / std::max (1.0, totalWindow);
        for (int index = 0; index < kFftSize / 2; ++index)
        {
            const double radians = -2.0 * pi * index / kFftSize;
            twiddles_[static_cast<std::size_t> (index)]
                = { std::cos (radians), std::sin (radians) };
        }
        for (auto& cached : maskCache_)
            cached.valid = false;
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : inputRing_) channel.fill (0.0);
        for (auto& channel : outputRing_) channel.fill (0.0);
        for (auto& band : envelopeDb_)
            for (auto& channel : band)
                channel.fill (0.0);
        sampleCounter_ = 0;
        engaged_.fill (false);
    }

    double sampleRate() const noexcept { return sampleRate_; }

    // In-place processing: exactly kLatencySamples delay including when every
    // spectral band is disabled. The host must add this delay to its PDC when
    // activating the engine. Input/output pointers may alias; no allocations.
    // The number of bands is fixed at APEX's kMaxBands.
    void process (float* const* samples, int channels, int count,
                  const std::array<Band, kMaxBands>& bands) noexcept
    {
        if (samples == nullptr || count <= 0)
            return;
        const int channelCount = std::clamp (channels, 0, kChannels);
        if (channelCount == 0)
            return;
        // Rebuild expensive logarithmic masks only when relevant per-band
        // controls change, NEVER 24 x 513 exp/log/tanh per FFT hop.
        updateMaskCaches (bands);
        for (int index = 0; index < count; ++index)
        {
            const std::uint32_t inputIndex = static_cast<std::uint32_t> (
                sampleCounter_ & (kFftSize - 1));
            const std::uint32_t outputIndex = static_cast<std::uint32_t> (
                sampleCounter_ & (kOutputRing - 1));
            for (int channel = 0; channel < channelCount; ++channel)
            {
                const double in = samples[channel] != nullptr
                    && std::isfinite (samples[channel][index])
                       ? static_cast<double> (samples[channel][index]) : 0.0;
                inputRing_[static_cast<std::size_t> (channel)][inputIndex] = in;
            }

            if (((sampleCounter_ + 1) & (kHopSize - 1)) == 0)
                processFrame (channelCount, bands);

            for (int channel = 0; channel < channelCount; ++channel)
            {
                auto& pending = outputRing_[
                    static_cast<std::size_t> (channel)][outputIndex];
                const double out = pending;
                pending = 0.0;
                if (samples[channel] != nullptr)
                    samples[channel][index] = std::isfinite (out)
                        ? static_cast<float> (std::clamp (out, -64.0, 64.0))
                        : 0.0f;
            }
            ++sampleCounter_;
        }
    }

private:
    using Complex = std::complex<double>;

    void fft (std::array<Complex, kFftSize>& buffer,
              bool inverse) const noexcept
    {
        for (int index = 1, bitReverse = 0; index < kFftSize; ++index)
        {
            int bit = kFftSize >> 1;
            while ((bitReverse & bit) != 0)
            {
                bitReverse ^= bit;
                bit >>= 1;
            }
            bitReverse ^= bit;
            if (index < bitReverse)
                std::swap (buffer[static_cast<std::size_t> (index)],
                           buffer[static_cast<std::size_t> (bitReverse)]);
        }

        for (int length = 2; length <= kFftSize; length <<= 1)
        {
            const int half = length >> 1;
            const int stride = kFftSize / length;
            for (int base = 0; base < kFftSize; base += length)
            {
                for (int index = 0; index < half; ++index)
                {
                    auto twiddle = twiddles_[
                        static_cast<std::size_t> (index * stride)];
                    if (inverse)
                        twiddle = std::conj (twiddle);
                    const auto even = buffer[
                        static_cast<std::size_t> (base + index)];
                    const auto odd = twiddle * buffer[
                        static_cast<std::size_t> (base + index + half)];
                    buffer[static_cast<std::size_t> (base + index)]
                        = even + odd;
                    buffer[static_cast<std::size_t> (base + index + half)]
                        = even - odd;
                }
            }
        }

        if (inverse)
            for (auto& value : buffer)
                value /= static_cast<double> (kFftSize);
    }

    double weight (const Band& band, double frequencyHz) const noexcept
    {
        if (frequencyHz <= 0.0 || ! std::isfinite (band.frequencyHz)
            || band.frequencyHz <= 0.0 || ! std::isfinite (band.q))
            return 0.0;

        const double octave = std::log2 (frequencyHz / band.frequencyHz);
        const double q = std::clamp (band.q, 0.1, 100.0);
        const double bandwidthOctaves = std::clamp (
            2.0 * std::asinh (1.0 / (2.0 * q)) / std::log (2.0),
            0.10, 6.0);
        const double density = std::clamp (
            std::isfinite (band.density) ? band.density : 0.5,
            0.0, 1.0);
        const double sigma = std::clamp (
            bandwidthOctaves * (1.25 - 0.95 * density), 0.03, 8.0);
        if (band.shape == FilterShape::Bell)
        {
            const double x = octave / sigma;
            return std::abs (x) > 9.0 ? 0.0
                 : std::exp (-0.5 * x * x);
        }
        if (band.shape == FilterShape::LowShelf)
            return 0.5 * (1.0 - std::tanh (octave / sigma));
        if (band.shape == FilterShape::HighShelf)
            return 0.5 * (1.0 + std::tanh (octave / sigma));
        return 0.0;
    }

    void applyComponent (int bandIndex, int bin, int envelopeChannel,
                         Complex& component,
                         const DynamicBandParameters& params,
                         double attackCoefficient, double releaseCoefficient,
                         double weightForBin, double tiltDb) noexcept
    {
        if (weightForBin < 1.0e-5)
            return;
        const double level = 20.0 * std::log10 (std::max (
            1.0e-10, std::abs (component) * binAmplitudeScale_));
        const double levelDb = std::clamp (level + tiltDb, -144.0, 48.0);
        const double target = dynamicGainDbForLevel (
            levelDb, params);

        auto& state = envelopeDb_[static_cast<std::size_t> (bandIndex)]
            [static_cast<std::size_t> (envelopeChannel)]
            [static_cast<std::size_t> (bin)];
        const bool attack = std::abs (target) > std::abs (state);
        const double smoothing = attack ? attackCoefficient
                                         : releaseCoefficient;
        const double next = smoothing * state + (1.0 - smoothing) * target;
        state = std::isfinite (next) ? next : 0.0;
        const double gainDb = std::clamp (state * weightForBin, -24.0, 24.0);
        const double linearGain = std::pow (10.0, gainDb / 20.0);
        component *= linearGain;
    }


    struct MaskCache
    {
        bool valid = false;
        FilterShape shape = FilterShape::Bell;
        double frequencyHz = 0.0;
        double q = 0.0;
        double density = 0.0;
        bool tiltEnabled = false;
        std::array<double, kBins> mask {};
        std::array<double, kBins> tilt {};
    };

    void updateMaskCaches (
        const std::array<Band, kMaxBands>& bands) noexcept
    {
        for (int index = 0; index < kMaxBands; ++index)
        {
            const auto& band = bands[static_cast<std::size_t> (index)];
            if (! band.enabled || ! band.dynamic.enabled)
                continue;
            auto& cache = maskCache_[static_cast<std::size_t> (index)];
            if (cache.valid && cache.shape == band.shape
                && cache.frequencyHz == band.frequencyHz
                && cache.q == band.q && cache.density == band.density
                && cache.tiltEnabled == band.tilt)
                continue;
            cache.valid = true;
            cache.shape = band.shape;
            cache.frequencyHz = band.frequencyHz;
            cache.q = band.q;
            cache.density = band.density;
            cache.tiltEnabled = band.tilt;
            for (int bin = 0; bin < kBins; ++bin)
            {
                const double frequencyHz = bin * sampleRate_ / kFftSize;
                cache.mask[static_cast<std::size_t> (bin)] =
                    weight (band, frequencyHz);
                cache.tilt[static_cast<std::size_t> (bin)] =
                    band.tilt && frequencyHz > 0.0
                    && std::isfinite (band.frequencyHz)
                    && band.frequencyHz > 0.0
                    ? std::clamp (3.0 * std::log2 (frequencyHz /
                        std::max (20.0, band.frequencyHz)), -18.0, 18.0)
                    : 0.0;
            }
        }
    }

    void applySpectralBands (
        int channelCount,
        const std::array<Band, kMaxBands>& bands) noexcept
    {
        for (int bandIndex = 0; bandIndex < kMaxBands; ++bandIndex)
        {
            const auto& band = bands[static_cast<std::size_t> (bandIndex)];
            const auto& cache = maskCache_[static_cast<std::size_t> (bandIndex)];
            const bool supported = band.shape == FilterShape::Bell
                || band.shape == FilterShape::LowShelf
                || band.shape == FilterShape::HighShelf;
            const bool active = band.enabled && band.dynamic.enabled
                && std::isfinite (band.dynamic.rangeDb)
                && std::abs (band.dynamic.rangeDb) > 1.0e-8 && supported;
            const bool placementChanged = engaged_[
                static_cast<std::size_t> (bandIndex)]
                && oldPlacement_[static_cast<std::size_t> (bandIndex)]
                   != band.placement;
            if (! active || ! engaged_[static_cast<std::size_t> (bandIndex)]
                || placementChanged)
            {
                for (auto& envelopeChannel : envelopeDb_[
                    static_cast<std::size_t> (bandIndex)])
                    envelopeChannel.fill (0.0);
            }
            engaged_[static_cast<std::size_t> (bandIndex)] = active;
            oldPlacement_[static_cast<std::size_t> (bandIndex)] =
                band.placement;
            if (! active)
                continue;

            const auto params = DynamicBandParameters::sanitised (
                band.dynamic);
            const double attackCoefficient = std::exp (
                -static_cast<double> (kHopSize)
                    / (sampleRate_ * params.attackSeconds));
            const double releaseCoefficient = std::exp (
                -static_cast<double> (kHopSize)
                    / (sampleRate_ * params.releaseSeconds));

            for (int bin = 1; bin < kBins; ++bin)
            {
                const double spectralWeight = cache.mask[
                    static_cast<std::size_t> (bin)];
                if (spectralWeight <= 1.0e-5)
                    continue;
                const double tiltDb = cache.tilt[
                    static_cast<std::size_t> (bin)];

                auto& left = spectrum_[0][static_cast<std::size_t> (bin)];
                if (channelCount < 2)
                {
                    // JUCE mono supports Stereo, Left and Mid as the sole
                    // signal domain. Right is absent and Side is exactly
                    // zero on a mono bus. Never apply right/side-only
                    // dynamic spectral gain to the real mono channel.
                    if (band.placement != ChannelPlacement::Right
                        && band.placement != ChannelPlacement::Side)
                        applyComponent (bandIndex, bin, 0, left, params,
                                        attackCoefficient, releaseCoefficient,
                                        spectralWeight, tiltDb);
                    continue;
                }
                auto& right = spectrum_[1][static_cast<std::size_t> (bin)];
                switch (band.placement)
                {
                    case ChannelPlacement::Stereo:
                        applyComponent (bandIndex, bin, 0, left, params,
                                        attackCoefficient, releaseCoefficient,
                                        spectralWeight, tiltDb);
                        applyComponent (bandIndex, bin, 1, right, params,
                                        attackCoefficient, releaseCoefficient,
                                        spectralWeight, tiltDb);
                        break;
                    case ChannelPlacement::Left:
                        applyComponent (bandIndex, bin, 0, left, params,
                                        attackCoefficient, releaseCoefficient,
                                        spectralWeight, tiltDb);
                        break;
                    case ChannelPlacement::Right:
                        applyComponent (bandIndex, bin, 1, right, params,
                                        attackCoefficient, releaseCoefficient,
                                        spectralWeight, tiltDb);
                        break;
                    case ChannelPlacement::Mid:
                    case ChannelPlacement::Side:
                    {
                        Complex mid = (left + right) * 0.5;
                        Complex side = (left - right) * 0.5;
                        if (band.placement == ChannelPlacement::Mid)
                            applyComponent (bandIndex, bin, 0, mid, params,
                                            attackCoefficient, releaseCoefficient,
                                            spectralWeight, tiltDb);
                        else
                            applyComponent (bandIndex, bin, 1, side, params,
                                            attackCoefficient, releaseCoefficient,
                                            spectralWeight, tiltDb);
                        left = mid + side;
                        right = mid - side;
                        break;
                    }
                }
            }
        }
        // Explicit Hermitian symmetry keeps the inverse transform real.
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto& spectrum = spectrum_[static_cast<std::size_t> (channel)];
            spectrum[0] = { spectrum[0].real(), 0.0 };
            spectrum[kFftSize / 2] = {
                spectrum[kFftSize / 2].real(), 0.0
            };
            for (int bin = 1; bin < kFftSize / 2; ++bin)
                spectrum[static_cast<std::size_t> (kFftSize - bin)]
                    = std::conj (spectrum[static_cast<std::size_t> (bin)]);
        }
    }

    void processFrame (int channelCount,
                       const std::array<Band, kMaxBands>& bands) noexcept
    {
        const auto start = static_cast<std::uint32_t> (
            (sampleCounter_ + 1) & (kFftSize - 1));
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto& spectrum = spectrum_[static_cast<std::size_t> (channel)];
            const auto& history = inputRing_[
                static_cast<std::size_t> (channel)];
            for (int index = 0; index < kFftSize; ++index)
            {
                const int ring = (start + index) & (kFftSize - 1);
                spectrum[static_cast<std::size_t> (index)] = {
                    history[static_cast<std::size_t> (ring)]
                    * window_[static_cast<std::size_t> (index)], 0.0
                };
            }
            fft (spectrum, false);
        }
        applySpectralBands (channelCount, bands);
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto& spectrum = spectrum_[static_cast<std::size_t> (channel)];
            fft (spectrum, true);
            auto& output = outputRing_[static_cast<std::size_t> (channel)];
            for (int index = 0; index < kFftSize; ++index)
            {
                const auto ring = static_cast<std::size_t> (
                    (sampleCounter_ + 1 + index) & (kOutputRing - 1));
                const double addition = 0.5
                    * spectrum[static_cast<std::size_t> (index)].real()
                    * window_[static_cast<std::size_t> (index)];
                if (std::isfinite (addition))
                    output[ring] += addition;
            }
        }
    }

    double sampleRate_ = 48000.0;
    double binAmplitudeScale_ = 1.0;
    std::uint64_t sampleCounter_ = 0;
    std::array<double, kFftSize> window_ {};
    std::array<Complex, kFftSize / 2> twiddles_ {};
    std::array<std::array<double, kFftSize>, kChannels> inputRing_ {};
    std::array<std::array<double, kOutputRing>, kChannels> outputRing_ {};
    std::array<std::array<Complex, kFftSize>, kChannels> spectrum_ {};
    std::array<std::array<std::array<double, kBins>, kChannels>, kMaxBands>
        envelopeDb_ {};
    std::array<MaskCache, kMaxBands> maskCache_ {};
    std::array<bool, kMaxBands> engaged_ {};
    std::array<ChannelPlacement, kMaxBands> oldPlacement_ {};
};

} // namespace APEX::ParametricEQ
