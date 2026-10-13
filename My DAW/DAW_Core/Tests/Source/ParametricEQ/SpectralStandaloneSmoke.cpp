// JUCE-free C++17 stand-alone numerical probe. Intended to compile on
// Windows MSVC and Linux clang/GCC BEFORE the heavy whole-DAW build.
#include "../../../Source/ParametricEQCore/ParametricEQSpectralDynamicsCore.h"
#include "../../../Source/ParametricEQCore/ParametricEQSpectralBlend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

using namespace APEX::ParametricEQ;

namespace
{
void check (bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit (1);
    }
}

constexpr double pi = 3.14159265358979323846;

std::array<SpectralDynamicsCore::Band, kMaxBands> makeBands()
{
    std::array<SpectralDynamicsCore::Band, kMaxBands> bands {};
    auto& one = bands[0];
    one.enabled = true;
    one.shape = FilterShape::Bell;
    one.frequencyHz = 1000.0;
    one.q = 8.0;
    one.density = 0.8;
    one.tilt = false;
    one.dynamic.enabled = true;
    one.dynamic.thresholdDb = -40.0;
    one.dynamic.rangeDb = 18.0;
    one.dynamic.attackSeconds = 0.002;
    one.dynamic.releaseSeconds = 0.05;
    return bands;
}

double toneMagnitude (const std::vector<float>& data, int offset,
                      int count, double frequency, double sampleRate)
{
    double cosine = 0.0, sine = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto radians = 2.0 * pi * frequency * i / sampleRate;
        cosine += data[static_cast<std::size_t> (i + offset)]
                  * std::cos (radians);
        sine += data[static_cast<std::size_t> (i + offset)]
                * std::sin (radians);
    }
    return 2.0 * std::hypot (cosine, sine) / count;
}

void runIdentity()
{
    constexpr int samples = 16384;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<float> source (samples), result;
        for (int i = 0; i < samples; ++i)
            source[static_cast<std::size_t> (i)] = static_cast<float> (
                0.23 * std::sin (2.0 * pi * 997.0 * i / rate));
        for (const int blockSize : { 64, 128, 256, 512, 4096 })
        {
            result = source;
            const auto engine = std::make_unique<SpectralDynamicsCore>();
            engine->prepare (rate);
            const std::array<SpectralDynamicsCore::Band, kMaxBands> bands {};
            for (int start = 0; start < samples; start += blockSize)
            {
                float* pointer[] { result.data() + start };
                engine->process (pointer, 1,
                                 std::min (blockSize, samples - start), bands);
            }
            double peakError = 0.0;
            for (int i = 0; i < samples; ++i)
            {
                const double reference = i < SpectralDynamicsCore::kLatencySamples
                    ? 0.0 : source[static_cast<std::size_t> (
                          i - SpectralDynamicsCore::kLatencySamples)];
                peakError = std::max (peakError, std::abs (
                    static_cast<double> (result[static_cast<std::size_t> (i)])
                    - reference));
            }
            check (peakError < 0.0001, "STFT neutral reconstruction + latency");
        }
    }
    std::cout << "PASS neutral FFT unity / 1024-sample latency / 44.1-96k\n";
}

void runTonal()
{
    constexpr int count = 32768;
    constexpr double rate = 48000.0;
    std::vector<float> in (count), out (count);
    for (int index = 0; index < count; ++index)
    {
        in[static_cast<std::size_t> (index)] = static_cast<float> (
            0.16 * std::sin (2.0 * pi * 1000.0 * index / rate)
            + 0.16 * std::sin (2.0 * pi * 5000.0 * index / rate));
    }
    auto dsp = std::make_unique<SpectralDynamicsCore>();
    dsp->prepare (rate);
    auto bands = makeBands();
    out = in;
    float* pointer[] { out.data() };
    dsp->process (pointer, 1, count, bands);
    constexpr int scanOffset = 16000;
    constexpr int scanCount = 4800;
    const auto low = toneMagnitude (out, scanOffset
        + SpectralDynamicsCore::kLatencySamples, scanCount, 1000.0, rate)
        / toneMagnitude (in, scanOffset, scanCount, 1000.0, rate);
    const auto high = toneMagnitude (out, scanOffset
        + SpectralDynamicsCore::kLatencySamples, scanCount, 5000.0, rate)
        / toneMagnitude (in, scanOffset, scanCount, 5000.0, rate);
    check (low < 0.72, "Spectral attenuation is frequency selective");
    check (high > 0.94 && high < 1.06,
           "Spectral attenuation preserves unrelated frequencies");
    std::cout << "PASS selective 1k attenuation (" << low
              << "), 5k pass (" << high << ")\n";
}

void runAlignedBlend()
{
    constexpr int count = 12000;
    std::vector<float> in (count, 0.0f), out, enabled;
    for (int i = 0; i < count; ++i)
        in[static_cast<std::size_t> (i)] = static_cast<float> (
            0.22 * std::sin (2.0 * pi * 1000.0 * i / 48000.0));
    auto bands = makeBands();
    auto blend = std::make_unique<SpectralAlignedBlend>();
    blend->prepare (48000.0);
    out = in;
    float* pointer[] { out.data() };
    blend->process (pointer, 1, count, bands, false);
    for (int i = 0; i < count; ++i)
    {
        const float wanted = i < SpectralAlignedBlend::kLatencySamples
            ? 0.0f : in[static_cast<std::size_t> (
                  i - SpectralAlignedBlend::kLatencySamples)];
        check (std::abs (out[static_cast<std::size_t> (i)] - wanted)
               < 1.0e-7f, "Spectral 100% dry is PDC aligned");
    }

    blend->reset();
    enabled = in;
    pointer[0] = enabled.data();
    blend->process (pointer, 1, count, bands, true);
    check (std::abs (toneMagnitude (enabled, 6000, 4000, 1000.0, 48000.0))
           < 0.20, "Spectral wet blend converged to attenuation");
    std::cout << "PASS aligned dry/wet and arbitrary huge buffer\n";
}


void runPlacementRouting()
{
    constexpr int count = 32768;
    constexpr double rate = 48000.0;
    constexpr int scanOffset = 16000;
    constexpr int scanCount = 4800;
    std::vector<float> source (count);
    for (int i = 0; i < count; ++i)
        source[static_cast<std::size_t> (i)] = static_cast<float> (
            0.25 * std::sin (2.0 * pi * 1000.0 * i / rate));
    const auto baseline = toneMagnitude (source, scanOffset, scanCount,
                                        1000.0, rate);

    // A mono bus has one channel, which is simultaneously Left and Mid.
    // Right- and Side-only bands cannot process a non-existent channel.
    for (const auto placement : {
        ChannelPlacement::Stereo, ChannelPlacement::Left,
        ChannelPlacement::Right, ChannelPlacement::Mid, ChannelPlacement::Side
    })
    {
        auto bands = makeBands();
        bands[0].placement = placement;
        auto dsp = std::make_unique<SpectralDynamicsCore>();
        dsp->prepare (rate);
        auto rendered = source;
        float* ptr[] { rendered.data() };
        dsp->process (ptr, 1, count, bands);
        const auto ratio = toneMagnitude (rendered, scanOffset
            + SpectralDynamicsCore::kLatencySamples, scanCount, 1000.0, rate)
            / baseline;
        const bool shouldProcess = placement == ChannelPlacement::Stereo
            || placement == ChannelPlacement::Left
            || placement == ChannelPlacement::Mid;
        if (shouldProcess)
            check (ratio < 0.70, "Mono spectral left/mid band attenuates");
        else
            check (ratio > 0.99 && ratio < 1.01,
                   "Mono spectral right/side band is strict no-op");
    }

    // Stereo Left and Right must not cross-feed. Mid and Side act on their
    // mathematical stereo component without corrupting the other domain.
    for (const auto placement : {
        ChannelPlacement::Left, ChannelPlacement::Right,
        ChannelPlacement::Mid, ChannelPlacement::Side
    })
    {
        for (const bool outOfPhase : { false, true })
        {
            auto bands = makeBands();
            bands[0].placement = placement;
            auto dsp = std::make_unique<SpectralDynamicsCore>();
            dsp->prepare (rate);
            auto left = source, right = source;
            if (outOfPhase)
                for (auto& value : right)
                    value = -value;
            float* ptr[] { left.data(), right.data() };
            dsp->process (ptr, 2, count, bands);

            const auto leftRatio = toneMagnitude (left, scanOffset
                + SpectralDynamicsCore::kLatencySamples, scanCount,
                1000.0, rate) / baseline;
            const auto rightRatio = toneMagnitude (right, scanOffset
                + SpectralDynamicsCore::kLatencySamples, scanCount,
                1000.0, rate) / baseline;
            if (placement == ChannelPlacement::Left)
            {
                check (leftRatio < 0.70, "L-only band attenuates L");
                check (rightRatio > 0.99 && rightRatio < 1.01,
                       "L-only band preserves R");
            }
            else if (placement == ChannelPlacement::Right)
            {
                check (rightRatio < 0.70, "R-only band attenuates R");
                check (leftRatio > 0.99 && leftRatio < 1.01,
                       "R-only band preserves L");
            }
            else if ((placement == ChannelPlacement::Mid && ! outOfPhase)
                     || (placement == ChannelPlacement::Side && outOfPhase))
            {
                check (leftRatio < 0.70 && rightRatio < 0.70,
                       "M/S spectral processing changes selected domain");
            }
            else
            {
                check (leftRatio > 0.99 && leftRatio < 1.01
                       && rightRatio > 0.99 && rightRatio < 1.01,
                       "M/S spectral processing preserves orthogonal domain");
            }
        }
    }
    std::cout << "PASS mono and stereo Left/Right/Mid/Side placement routing\\n";
}

void runFaults()
{
    auto engine = std::make_unique<SpectralDynamicsCore>();
    auto bands = makeBands();
    std::array<float, 1024> a {}, b {};
    a[0] = std::numeric_limits<float>::infinity();
    b[0] = std::numeric_limits<float>::quiet_NaN();
    float* stereo[] { a.data(), b.data() };
    engine->process (stereo, 2, 1024, bands);
    for (int i = 0; i < 1024; ++i)
        check (std::isfinite (a[static_cast<std::size_t> (i)])
            && std::isfinite (b[static_cast<std::size_t> (i)]),
            "spectral output finite after nonfinite input");
    std::cout << "PASS NaN/Inf guard\n";
}
}

int main()
{
    runIdentity();
    runTonal();
    runAlignedBlend();
    runPlacementRouting();
    runFaults();
    std::cout << "ALL APEX spectral stand-alone DSP probes PASS\n";
    return 0;
}
