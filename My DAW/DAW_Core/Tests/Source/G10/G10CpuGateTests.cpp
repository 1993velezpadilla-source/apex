#include <JuceHeader.h>
#include "G10TestUtils.h"

#include <cmath>
#include <vector>

using APEX::G10::G10Processor;
using APEX::G10::G10Parameter;

// APEX_TEST_CONFIGURATION is defined by the project as a bare identifier
// (Debug/Release); stringize it so it can be logged as text.
#define APEX_STRINGIFY_IMPL(x) #x
#define APEX_STRINGIFY(x) APEX_STRINGIFY_IMPL(x)

// ============================================================================
// G10CpuGateTests — measurement-oriented CPU evidence for the APEX.G10 suite.
//
// This gate records real numbers only: CPU model, build configuration,
// sample rate, block size, channel count, instance count, iteration count,
// total samples processed, equivalent audio duration, wall-clock duration,
// realtime ratio, average time per block, and average time per instance.
//
// Every processor is warmed up before timing. The output checksum is written
// to a volatile sink so the compiler cannot eliminate the processing. No
// fabricated pass/fail thresholds are applied — the canonical Brain does not
// define CPU gates, so this test records evidence instead of asserting.
//
// Required matrix:
//   Block sizes       1, 16, 32, 64, 128, 256, 333, 512, 1024
//   Instance counts   1, 10, 32, 64, 100 (where practical)
//   States            Neutral, Typical shaping, Extreme all-bands
//   Channel layouts   Mono, Stereo
//   Sample rates      48000 (mandatory), 96000, 192000 (where practical)
// ============================================================================

class G10CpuGateTests final : public juce::UnitTest
{
public:
    G10CpuGateTests() : juce::UnitTest ("G10.CpuGate", "APEX.G10") {}

    void runTest() override
    {
        beginTest ("CPU measurement evidence");

        logMessage ("CPU model: " + juce::SystemStats::getCpuModel());
        logMessage ("CPU count: " + juce::String (juce::SystemStats::getNumCpus()));
#ifdef APEX_TEST_CONFIGURATION
        logMessage ("Build configuration: " + juce::String (APEX_STRINGIFY (APEX_TEST_CONFIGURATION)));
#else
        logMessage ("Build configuration: unknown");
#endif
        logMessage ("matrix columns: rate block ch instances state quality iters totalSamples "
                    "audioSeconds wallSeconds realtimeRatio usPerBlock usPerInstanceBlock");

        // 48 kHz mandatory matrix: all block sizes x all instance counts,
        // stereo, typical state.
        const int blockSizes[] = { 1, 16, 32, 64, 128, 256, 333, 512, 1024 };
        const int instanceCounts[] = { 1, 10, 32, 64, 100 };
        for (int block : blockSizes)
            for (int n : instanceCounts)
                measure (48000.0, block, 2, n, State::Typical);

        // Mono layout at representative sizes.
        for (int block : { 64, 256, 1024 })
            for (int n : { 1, 32, 100 })
                measure (48000.0, block, 1, n, State::Typical);

        // Neutral and extreme states at representative sizes.
        for (int block : { 64, 1024 })
            for (int n : { 1, 64 })
            {
                measure (48000.0, block, 2, n, State::Neutral);
                measure (48000.0, block, 2, n, State::Extreme);
            }

        // Higher rates where practical (fewer instances to bound runtime).
        for (double rate : { 96000.0, 192000.0 })
            for (int block : { 64, 256, 1024 })
                for (int n : { 1, 32 })
                    measure (rate, block, 2, n, State::Typical);

        // HQ (4x oversampled) quality: 48 kHz stereo, representative blocks,
        // NORMAL vs HQ cost comparison on the final production chain.
        for (int block : { 64, 128, 256, 512 })
            for (int n : { 1, 32 })
                measure (48000.0, block, 2, n, State::Typical, true);
    }

private:
    enum class State { Neutral, Typical, Extreme };

    void setDb (G10Parameter* p, float db) const
    {
        jassert (p != nullptr);
        p->setValue (p->getValueForText (juce::String (db, 1)));
    }

    void applyState (G10Processor& p, State state) const
    {
        switch (state)
        {
            case State::Neutral:
                break;
            case State::Typical:
                setDb (G10Test::findParam (p, "g10.band31"), 6.0f);
                setDb (G10Test::findParam (p, "g10.band1k"), -3.0f);
                setDb (G10Test::findParam (p, "g10.band16k"), 2.0f);
                break;
            case State::Extreme:
                for (int b = 0; b < APEX::G10::kNumBands; ++b)
                    setDb (G10Test::findParam (p, APEX::G10::kBandInfos[b].paramId),
                           (b % 2) ? 12.0f : -12.0f);
                break;
        }
    }

    static const char* stateName (State state)
    {
        switch (state)
        {
            case State::Neutral:  return "Neutral";
            case State::Typical:  return "Typical";
            case State::Extreme:  return "Extreme";
        }
        return "Unknown";
    }

    void measure (double sampleRate, int blockSize, int channels, int instances, State state, bool hq = false)
    {
        // Build and prepare the whole instance pool and the buffers BEFORE
        // timing; nothing inside the timed region allocates.
        std::vector<std::unique_ptr<G10Processor>> pool;
        pool.reserve ((size_t) instances);
        for (int i = 0; i < instances; ++i)
        {
            // Canonical quality: realtime = NORMAL (2x), offline = HQ (4x).
            // The host sets the non-realtime flag BEFORE prepareToPlay, so
            // the measurement represents the real product configuration.
            auto p = G10Test::makePreparedProcessor (sampleRate, blockSize, hq);
            applyState (*p, state);
            pool.push_back (std::move (p));
        }

        juce::AudioBuffer<float> buffer (channels, blockSize);
        juce::MidiBuffer midi;
        G10Test::fillDeterministic (buffer, 0xA9E12026u);

        // Warm up every processor before timing.
        for (auto& p : pool)
            for (int w = 0; w < 128; ++w)
                p->processBlock (buffer, midi);

        // Iteration count keeps the aggregate sample count practical across
        // the whole matrix: small blocks need more calls to amortize startup.
        const int iterations = (blockSize >= 256) ? 512
                             : (blockSize >= 64)  ? 2048
                             : 8192;

        // Checksum accumulator written to a volatile sink after timing so the
        // compiler cannot eliminate the processing.
        volatile float sink = 0.0f;
        float acc = 0.0f;

        const auto start = juce::Time::getHighResolutionTicks();
        for (int it = 0; it < iterations; ++it)
        {
            for (auto& p : pool)
            {
                p->processBlock (buffer, midi);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int s = 0; s < buffer.getNumSamples(); ++s)
                        acc += buffer.getSample (ch, s);
            }
        }
        const auto end = juce::Time::getHighResolutionTicks();
        sink = acc;

        const double wallSeconds = juce::Time::highResolutionTicksToSeconds (end - start);
        const double totalSamples = (double) iterations * (double) blockSize * (double) instances;
        const double audioSeconds = totalSamples / sampleRate;
        const double realtimeRatio = wallSeconds > 0.0 ? audioSeconds / wallSeconds : 0.0;
        const double usPerBlock = wallSeconds * 1.0e6 / (double) iterations;
        const double usPerInstanceBlock = wallSeconds * 1.0e6 / ((double) iterations * (double) instances);

        logMessage ("G10CPU | " + juce::String (sampleRate, 0)
                    + " " + juce::String (blockSize)
                    + " " + juce::String (channels)
                    + " " + juce::String (instances)
                    + " " + juce::String (stateName (state))
                    + " " + juce::String (hq ? "HQ" : "NORMAL")
                    + " " + juce::String (iterations)
                    + " " + juce::String (totalSamples, 0)
                    + " " + juce::String (audioSeconds, 4)
                    + " " + juce::String (wallSeconds, 6)
                    + " " + juce::String (realtimeRatio, 2)
                    + " " + juce::String (usPerBlock, 4)
                    + " " + juce::String (usPerInstanceBlock, 4));

        expect (wallSeconds > 0.0, "CPU gate: timer did not advance");
    }
};

static G10CpuGateTests g10CpuGateTests;
