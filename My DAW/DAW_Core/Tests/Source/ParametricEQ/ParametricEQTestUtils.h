#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../../Source/ParametricEQCore/ParametricEQEngine.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

namespace ParametricEQTest
{

using APEX::ParametricEQ::BandSettings;
using APEX::ParametricEQ::ChannelPlacement;
using APEX::ParametricEQ::DesignMode;
using APEX::ParametricEQ::Engine;
using APEX::ParametricEQ::FilterDesigner;
using APEX::ParametricEQ::FilterShape;
using APEX::ParametricEQ::kMaxBands;

inline BandSettings band (FilterShape shape, double frequencyHz,
                          double gainDb = 0.0, double q = 1.0,
                          double slopeDbPerOctave = 12.0)
{
    BandSettings result;
    result.enabled = true;
    result.shape = shape;
    result.frequencyHz = frequencyHz;
    result.gainDb = gainDb;
    result.q = q;
    result.slopeDbPerOctave = slopeDbPerOctave;
    return result;
}

inline BandSettings placedBand (FilterShape shape, ChannelPlacement placement,
                                double frequencyHz, double gainDb = 0.0,
                                double q = 1.0,
                                double slopeDbPerOctave = 12.0)
{
    auto result = band (shape, frequencyHz, gainDb, q, slopeDbPerOctave);
    result.placement = placement;
    return result;
}

inline std::vector<float> impulsePath (Engine engine, int length,
                                       int sourceChannel, int destinationChannel,
                                       int blockSize = 257)
{
    engine.reset();
    std::vector<float> result (static_cast<std::size_t> (length), 0.0f);
    int offset = 0;
    while (offset < length)
    {
        const int count = std::min (blockSize, length - offset);
        std::vector<float> left (static_cast<std::size_t> (count), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (count), 0.0f);
        if (offset == 0)
            (sourceChannel == 0 ? left : right)[0] = 1.0f;
        float* channels[] = { left.data(), right.data() };
        engine.process (channels, 2, count);
        const auto& measured = destinationChannel == 0 ? left : right;
        std::copy (measured.begin(), measured.end(), result.begin() + offset);
        offset += count;
    }
    return result;
}

// Four measured 2x2 impulse paths from freshly reset copies of the same
// configured engine: [LL, LR, RL, RR].
inline std::array<std::vector<float>, 4> stereoImpulseResponses (
    Engine engine, int length, int blockSize = 257)
{
    std::array<std::vector<float>, 4> result;
    for (int path = 0; path < 4; ++path)
        result[static_cast<std::size_t> (path)]
            = impulsePath (engine, length, path / 2, path % 2, blockSize);
    return result;
}

// Four paths with the SAME dual-mono impulse (1 on both channels at t=0)
// applied to freshly reset engine copies, reading each destination channel.
inline std::array<std::vector<float>, 4> dualMonoImpulseResponses (
    Engine engine, int length, int blockSize = 257)
{
    std::array<std::vector<float>, 4> result;
    for (int path = 0; path < 4; ++path)
    {
        auto copy = engine;
        copy.reset();
        auto& measured = result[static_cast<std::size_t> (path)];
        measured.assign (static_cast<std::size_t> (length), 0.0f);
        const int destinationChannel = path % 2;
        int offset = 0;
        while (offset < length)
        {
            const int count = std::min (blockSize, length - offset);
            std::vector<float> left (static_cast<std::size_t> (count), 0.0f);
            std::vector<float> right (static_cast<std::size_t> (count), 0.0f);
            if (offset == 0)
            {
                left[0] = 1.0f;
                right[0] = 1.0f;
            }
            float* channels[] = { left.data(), right.data() };
            copy.process (channels, 2, count);
            const auto& lane = destinationChannel == 0 ? left : right;
            std::copy (lane.begin(), lane.end(), measured.begin() + offset);
            offset += count;
        }
    }
    return result;
}

inline Engine preparedEngine (double sampleRate = 48000.0,
                              int blockSize = 512,
                              int channels = 2)
{
    Engine engine;
    engine.prepare (sampleRate, blockSize, channels);
    return engine;
}

inline bool allFinite (const std::vector<float>& values)
{
    for (const auto value : values)
        if (! std::isfinite (value))
            return false;
    return true;
}

inline bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (buffer.getSample (channel, sample)))
                return false;
    return true;
}

inline void fillDeterministic (juce::AudioBuffer<float>& buffer,
                               std::uint32_t seed = 0xA9E12026u)
{
    auto state = seed;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            state = state * 1664525u + 1013904223u;
            const auto value = static_cast<float> (
                static_cast<double> (state) / 4294967296.0 * 2.0 - 1.0);
            buffer.setSample (channel, sample, value);
        }
}

inline void processBuffer (Engine& engine, juce::AudioBuffer<float>& buffer,
                           int startSample, int numberOfSamples)
{
    std::array<float*, APEX::ParametricEQ::kMaxChannels> channels {};
    const auto channelCount = std::min (buffer.getNumChannels(),
                                        APEX::ParametricEQ::kMaxChannels);
    for (int channel = 0; channel < channelCount; ++channel)
        channels[static_cast<std::size_t> (channel)]
            = buffer.getWritePointer (channel, startSample);
    engine.process (channels.data(), channelCount, numberOfSamples);
}

inline std::vector<float> impulseResponse (Engine engine, int length,
                                           int blockSize = 257)
{
    std::vector<float> result (static_cast<std::size_t> (length), 0.0f);
    int offset = 0;
    while (offset < length)
    {
        const int count = std::min (blockSize, length - offset);
        std::vector<float> blockData (static_cast<std::size_t> (count), 0.0f);
        if (offset == 0)
            blockData[0] = 1.0f;
        float* channels[] = { blockData.data() };
        engine.process (channels, 1, count);
        std::copy (blockData.begin(), blockData.end(), result.begin() + offset);
        offset += count;
    }
    return result;
}

inline std::complex<double> responseFromImpulse (const std::vector<float>& impulse,
                                                 double sampleRate,
                                                 double frequencyHz)
{
    const auto omega = -2.0 * APEX::ParametricEQ::kPi
                     * frequencyHz / sampleRate;
    const std::complex<double> step = std::polar (1.0, omega);
    std::complex<double> oscillator { 1.0, 0.0 };
    std::complex<double> response {};
    for (const auto sample : impulse)
    {
        response += static_cast<double> (sample) * oscillator;
        oscillator *= step;
    }
    return response;
}

inline double db (std::complex<double> response)
{
    return 20.0 * std::log10 (std::max (1.0e-15, std::abs (response)));
}

inline double phaseDifference (std::complex<double> lhs,
                               std::complex<double> rhs)
{
    return std::abs (std::arg (lhs * std::conj (rhs)));
}

inline bool bitEqual (const juce::AudioBuffer<float>& lhs,
                      const juce::AudioBuffer<float>& rhs)
{
    if (lhs.getNumChannels() != rhs.getNumChannels()
        || lhs.getNumSamples() != rhs.getNumSamples())
        return false;
    for (int channel = 0; channel < lhs.getNumChannels(); ++channel)
        if (std::memcmp (lhs.getReadPointer (channel), rhs.getReadPointer (channel),
                         static_cast<std::size_t> (lhs.getNumSamples())
                             * sizeof (float)) != 0)
            return false;
    return true;
}

inline double analogPeakingMagnitude (double frequencyHz, double centreHz,
                                      double q, double gainDb)
{
    const auto G = std::pow (10.0, gainDb / 20.0);
    const auto rootG = std::sqrt (G);
    const auto r = frequencyHz / centreHz;
    const auto common = (1.0 - r * r) * (1.0 - r * r);
    const auto numerator = common + std::pow (r * rootG / q, 2.0);
    const auto denominator = common + std::pow (r / (rootG * q), 2.0);
    return std::sqrt (numerator / denominator);
}

// ---------------------------------------------------------------------------
// Phase 6 asynchronous linear-phase kernel readiness (ASYNC-DSP TEST
// READINESS RULE). The linear-phase kernel is designed on a low-priority
// worker thread. Unit-test processBlock loops execute far faster than real
// time, so wall-clock worker scheduling is NOT an implicit oracle: tests
// must synchronize with the explicit published-generation observable before
// starting any measurement interval that assumes a settled kernel. These
// helpers only report/observe existing state; they never make the worker
// synchronous and never add sleeps to production code.
// ---------------------------------------------------------------------------

inline void setPhaseMode (APEX::ParametricEQ::Processor& processor,
                          bool linearPhase)
{
    auto* parameter = processor.getParametricEQParameter (
        APEX::ParametricEQ::kPhaseModeParameter);
    parameter->setValue (parameter->toNormalised (linearPhase ? 1.0f : 0.0f));
}

// Polls the published kernel generation with short sleeps between attempts
// (the low-priority worker needs wall-clock time to run) until it becomes
// nonzero or the bounded timeout expires. Returns false on timeout so the
// caller fails the test explicitly. Not a busy-spin.
inline bool waitForLinearKernelGeneration (
    APEX::ParametricEQ::Processor& processor,
    int timeoutMilliseconds = 10000)
{
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    const auto deadline = juce::Time::getMillisecondCounter()
                        + timeoutMilliseconds;
    while (processor.linearKernelGenerationForTesting() == 0)
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        buffer.clear();
        processor.processBlock (buffer, midi);
        juce::Thread::sleep (1);
    }
    return true;
}

// Requests linear phase, waits for the published kernel generation, and
// feeds enough silence for the 10 ms mode crossfade, the 10 ms old/new
// kernel crossfade, and a safety margin to complete. Returns true only when
// the processor itself reports the settled linear-phase mode.
inline bool requestLinearPhaseAndSettle (
    APEX::ParametricEQ::Processor& processor,
    int timeoutMilliseconds = 10000)
{
    setPhaseMode (processor, true);
    if (! waitForLinearKernelGeneration (processor, timeoutMilliseconds))
        return false;

    const auto settleSamples = static_cast<int> (
        processor.getSampleRate() * 0.05); // 50 ms: both fades + margin
    constexpr int blockSize = 512;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    int remaining = settleSamples;
    while (remaining > 0)
    {
        buffer.clear();
        const int count = std::min (blockSize, remaining);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                       2, 0, count);
        processor.processBlock (view, midi);
        remaining -= count;
    }
    return processor.isLinearPhaseModeSettledForTesting();
}

} // namespace ParametricEQTest
