#include <JuceHeader.h>

#include "../../../Source/PluginSandboxCore/PluginSandboxFixedQuantumReblockerCore.h"

#include <array>
#include <cmath>
#include <vector>

namespace
{
class DeterministicExactQuantumBackend final
    : public DAW::PluginSandboxExactQuantumBackendCore
{
public:
    void prepare(int quantum, int channels, int pluginLatency,
                 float gain, std::uint64_t generation, int depth = 1)
    {
        quantum_ = quantum;
        channels_ = channels;
        pluginLatency_ = pluginLatency;
        gain_ = gain;
        generation_ = generation;
        depth_ = depth;
        for (auto& pending : pendingOutputs_)
            pending.setSize(channels, quantum, false, true, false);
        delay_.setSize(channels, juce::jmax(1, pluginLatency), false, true, false);
        missRemote_.fill(false);
        missSubmit_.fill(false);
        reset(generation);
    }

    void reset(std::uint64_t generation) noexcept
    {
        generation_ = generation;
        for (auto& pending : pendingOutputs_)
            pending.clear();
        for (auto& submitted : pendingSubmitted_)
            submitted = false;
        pendingSequence_.fill(0);
        delay_.clear();
        delayPosition_ = 0;
        calls_ = 0;
        exactQuantumOnly_ = true;
    }

    void missRemoteSequence(std::uint64_t sequence) noexcept
    {
        if (sequence < missRemote_.size())
            missRemote_[static_cast<std::size_t>(sequence)] = true;
    }

    void missSubmitSequence(std::uint64_t sequence) noexcept
    {
        if (sequence < missSubmit_.size())
            missSubmit_[static_cast<std::size_t>(sequence)] = true;
    }

    DAW::PluginSandboxAudioTransportCore::ExchangeResult exchangeQuantum(
        const float* const* input,
        std::uint32_t inputChannels,
        float* const* output,
        std::uint32_t outputChannels,
        std::uint32_t quantumSamples) noexcept override
    {
        DAW::PluginSandboxAudioTransportCore::ExchangeResult result;
        const auto sequence = ++calls_;
        result.submittedSequence = sequence;
        // Depth-aware consume mirroring Phase B: exchange k resolves k-depth.
        result.expectedSequence =
            sequence > static_cast<std::uint64_t>(depth_)
                ? sequence - static_cast<std::uint64_t>(depth_) : 0;
        result.outputSamples = quantumSamples;
        result.outputChannels = outputChannels;
        exactQuantumOnly_ = exactQuantumOnly_
            && quantumSamples == static_cast<std::uint32_t>(quantum_)
            && inputChannels == static_cast<std::uint32_t>(channels_)
            && outputChannels == static_cast<std::uint32_t>(channels_);

        const auto slot = static_cast<std::size_t>(sequence % depth_);
        const bool remoteMiss = result.expectedSequence < missRemote_.size()
            && missRemote_[static_cast<std::size_t>(result.expectedSequence)];
        if (result.expectedSequence > 0
            && pendingSubmitted_[slot]
            && pendingSequence_[slot] == result.expectedSequence
            && ! remoteMiss)
        {
            for (int channel = 0; channel < channels_; ++channel)
                std::memcpy(output[channel], pendingOutputs_[slot].getReadPointer(channel),
                            sizeof(float) * static_cast<std::size_t>(quantum_));
            result.remoteOutput = true;
            result.fallback = false;
            result.completedSequence = result.expectedSequence;
            result.completedGeneration = generation_;
        }
        else
        {
            // Match frozen Phase B: a miss returns current N, not missing N-depth.
            for (int channel = 0; channel < channels_; ++channel)
                std::memcpy(output[channel], input[channel],
                            sizeof(float) * static_cast<std::size_t>(quantum_));
            result.fallback = true;
        }

        const bool submitMiss = sequence < missSubmit_.size()
            && missSubmit_[static_cast<std::size_t>(sequence)];
        result.submitted = ! submitMiss;
        pendingSequence_[slot] = sequence;
        pendingSubmitted_[slot] = ! submitMiss;
        if (pendingSubmitted_[slot])
            processCurrent(input, slot);
        return result;
    }

    std::uint64_t calls() const noexcept { return calls_; }
    bool exactQuantumOnly() const noexcept { return exactQuantumOnly_; }

private:
    void processCurrent(const float* const* input, std::size_t slot) noexcept
    {
        auto& pendingOutput = pendingOutputs_[slot];
        for (int sample = 0; sample < quantum_; ++sample)
        {
            for (int channel = 0; channel < channels_; ++channel)
            {
                float delayed = input[channel][sample];
                if (pluginLatency_ > 0)
                {
                    delayed = delay_.getSample(channel, delayPosition_);
                    delay_.setSample(channel, delayPosition_, input[channel][sample]);
                }
                pendingOutput.setSample(channel, sample, delayed * gain_);
            }
            if (pluginLatency_ > 0)
                delayPosition_ = (delayPosition_ + 1) % pluginLatency_;
        }
    }

    static constexpr std::size_t kMaximumTestSequences = 256;
    static constexpr std::size_t kMaximumTestDepth = 4;
    std::array<juce::AudioBuffer<float>, kMaximumTestDepth> pendingOutputs_;
    juce::AudioBuffer<float> delay_;
    std::array<bool, kMaximumTestSequences> missRemote_ {};
    std::array<bool, kMaximumTestSequences> missSubmit_ {};
    std::array<bool, kMaximumTestDepth> pendingSubmitted_ {};
    std::array<std::uint64_t, kMaximumTestDepth> pendingSequence_ {};
    int quantum_ = 0;
    int channels_ = 0;
    int pluginLatency_ = 0;
    int depth_ = 1;
    int delayPosition_ = 0;
    float gain_ = 1.0f;
    std::uint64_t generation_ = 0;
    std::uint64_t calls_ = 0;
    bool exactQuantumOnly_ = true;
};

enum class InputPattern
{
    Continuous,
    QuantumMarkers,
    Impulse,
    Constant
};

struct ReblockHarness
{
    bool prepare(int newQuantum, int maximumBlock, int latency = 0,
                 float newGain = 1.0f, std::uint64_t generation = 1)
    {
        quantum = newQuantum;
        gain = newGain;
        cursor = 0;
        output.clear();
        block.setSize(2, maximumBlock, false, true, false);
        const int depth = (maximumBlock + newQuantum - 1) / newQuantum;
        backend.prepare(newQuantum, 2, latency, newGain, generation, depth);
        DAW::PluginSandboxFixedQuantumReblockerCore::Configuration configuration;
        configuration.quantumSamples = newQuantum;
        configuration.maximumHostBlockSamples = maximumBlock;
        configuration.inputChannels = 2;
        configuration.outputChannels = 2;
        configuration.pluginLatencySamples = latency;
        configuration.transportGeneration = generation;
        return reblocker.prepare(configuration);
    }

    float inputAt(std::uint64_t sample, int channel) const noexcept
    {
        switch (pattern)
        {
            case InputPattern::QuantumMarkers:
                return static_cast<float>(sample / static_cast<std::uint64_t>(quantum) + 1)
                     + static_cast<float>(channel) * 0.125f;
            case InputPattern::Impulse:
                if (sample == 0)
                    return channel == 0 ? 1.0f : -1.0f;
                if (impulseTailValue != 0.0f && sample >= static_cast<std::uint64_t>(quantum))
                    return impulseTailValue + static_cast<float>(channel) * 0.125f;
                return 0.0f;
            case InputPattern::Constant:
                return constantValue + static_cast<float>(channel) * 0.125f;
            case InputPattern::Continuous:
            default:
                return static_cast<float>(sample + 1) * 0.001f
                     + static_cast<float>(channel) * 0.25f;
        }
    }

    DAW::PluginSandboxFixedQuantumReblockerCore::ProcessSummary process(int samples)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < samples; ++sample)
                block.setSample(channel, sample,
                                inputAt(cursor + static_cast<std::uint64_t>(sample), channel));

        const auto result = reblocker.processBlock(block, samples, backend);
        for (int channel = 0; channel < 2; ++channel)
        {
            if (output.size() <= static_cast<std::size_t>(channel))
                output.resize(2);
            auto& destination = output[static_cast<std::size_t>(channel)];
            destination.insert(destination.end(), block.getReadPointer(channel),
                               block.getReadPointer(channel) + samples);
        }
        cursor += static_cast<std::uint64_t>(samples);
        return result;
    }

    void runSchedule(const int* sizes, std::size_t count, std::uint64_t totalSamples)
    {
        std::size_t index = 0;
        while (cursor < totalSamples)
        {
            const auto remaining = totalSamples - cursor;
            const int samples = static_cast<int>(juce::jmin<std::uint64_t>(
                static_cast<std::uint64_t>(sizes[index % count]), remaining));
            process(samples);
            ++index;
        }
    }

    DAW::PluginSandboxFixedQuantumReblockerCore reblocker;
    DeterministicExactQuantumBackend backend;
    juce::AudioBuffer<float> block;
    std::vector<std::vector<float>> output = std::vector<std::vector<float>>(2);
    InputPattern pattern = InputPattern::Continuous;
    std::uint64_t cursor = 0;
    int quantum = 0;
    float gain = 1.0f;
    float constantValue = 0.0f;
    float impulseTailValue = 0.0f;
};

class PluginSandboxReblockTest final : public juce::UnitTest
{
public:
    enum class Scenario
    {
        FixedQuantum,
        PartialOutputSafety,
        StreamContinuity,
        ZeroAllocation,
        Latency,
        FallbackAlignment,
        GenerationReset,
        Reprepare,
        DepthWindow
    };

    PluginSandboxReblockTest(const juce::String& name, Scenario scenario)
        : juce::UnitTest(name, "PluginSandboxReblock"), scenario_(scenario) {}

    void runTest() override
    {
        switch (scenario_)
        {
            case Scenario::FixedQuantum: runFixedQuantum(); break;
            case Scenario::PartialOutputSafety: runPartialOutputSafety(); break;
            case Scenario::StreamContinuity: runStreamContinuity(); break;
            case Scenario::ZeroAllocation: runZeroAllocation(); break;
            case Scenario::Latency: runLatency(); break;
            case Scenario::FallbackAlignment: runFallbackAlignment(); break;
            case Scenario::GenerationReset: runGenerationReset(); break;
            case Scenario::Reprepare: runReprepare(); break;
            case Scenario::DepthWindow: runDepthWindow(); break;
        }
    }

private:
    void expectIdentity(const ReblockHarness& harness, int latency, float gain = 1.0f)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto& output = harness.output[static_cast<std::size_t>(channel)];
            for (std::size_t sample = 0; sample < output.size(); ++sample)
            {
                const float expected = sample < static_cast<std::size_t>(latency)
                    ? 0.0f
                    : harness.inputAt(static_cast<std::uint64_t>(sample - latency), channel)
                        * gain;
                expectWithinAbsoluteError(output[sample], expected, 0.0f,
                                          "stream mismatch at sample "
                                              + juce::String(static_cast<juce::int64>(sample)));
            }
        }
    }

    void runFixedQuantum()
    {
        beginTest("variable host blocks form only exact backend quanta");
        static constexpr int sizes[] { 64, 128, 256, 480, 512 };
        ReblockHarness harness;
        expect(harness.prepare(512, 512));
        harness.runSchedule(sizes, std::size(sizes), 6u * 512u);
        expect(harness.backend.exactQuantumOnly());
        expectEquals(static_cast<juce::int64>(harness.backend.calls()), juce::int64(6));
        expectIdentity(harness, 1024);

        beginTest("1024 and 2048 prepared matrices remain exact quantum");
        static constexpr int sizes1024[] { 256, 480, 512, 1024 };
        expect(harness.prepare(1024, 1024));
        harness.runSchedule(sizes1024, std::size(sizes1024), 4u * 1024u);
        expect(harness.backend.exactQuantumOnly());
        expectEquals(harness.reblocker.transportLatencySamples(), 2048);

        static constexpr int sizes2048[] { 512, 1024, 2048 };
        expect(harness.prepare(2048, 2048));
        harness.runSchedule(sizes2048, std::size(sizes2048), 4u * 2048u);
        expect(harness.backend.exactQuantumOnly());
        expectEquals(harness.reblocker.transportLatencySamples(), 4096);
    }

    void runPartialOutputSafety()
    {
        beginTest("defect partition never mixes remote and current dry");
        static constexpr int sizes[] { 256, 512, 256 };
        ReblockHarness harness;
        expect(harness.prepare(512, 512, 0, 0.5f));
        harness.pattern = InputPattern::QuantumMarkers;
        harness.backend.missRemoteSequence(2);
        harness.runSchedule(sizes, std::size(sizes), 6u * 512u);

        for (int channel = 0; channel < 2; ++channel)
        {
            const float offset = static_cast<float>(channel) * 0.125f;
            for (int sample = 1024; sample < 1536; ++sample)
                expectWithinAbsoluteError(harness.output[channel][sample],
                                          (1.0f + offset) * 0.5f, 0.0f);
            for (int sample = 1536; sample < 2048; ++sample)
                expectWithinAbsoluteError(harness.output[channel][sample],
                                          2.0f + offset, 0.0f,
                                          "missed quantum did not use matching retained dry");
        }
        const auto diagnostics = harness.reblocker.diagnostics();
        expectEquals(static_cast<juce::int64>(diagnostics.remoteQuanta), juce::int64(4));
        expectEquals(static_cast<juce::int64>(diagnostics.fallbackQuanta), juce::int64(1));
    }

    void runStreamContinuity()
    {
        beginTest("continuous stream is partition invariant");
        static constexpr int split256[] { 256, 256 };
        static constexpr int irregular[] { 128, 128, 256, 128, 384 };
        ReblockHarness first;
        ReblockHarness second;
        expect(first.prepare(512, 512));
        expect(second.prepare(512, 512));
        first.runSchedule(split256, std::size(split256), 8u * 512u);
        second.runSchedule(irregular, std::size(irregular), 8u * 512u);
        expect(first.output == second.output,
               "different callback partitions changed the output stream");
        expectIdentity(first, 1024);

        beginTest("bypass tag is immutable for a complete quantum");
        ReblockHarness bypass;
        expect(bypass.prepare(512, 512, 0, 0.5f));
        bypass.pattern = InputPattern::QuantumMarkers;
        bypass.process(256);
        bypass.reblocker.requestBypass(true);
        bypass.process(256); // request arrived after quantum 1 began
        bypass.process(512); // quantum 2 is tagged bypassed
        bypass.process(512);
        bypass.process(512);
        for (int sample = 1024; sample < 1536; ++sample)
            expectWithinAbsoluteError(bypass.output[0][sample], 0.5f, 0.0f);
        for (int sample = 1536; sample < 2048; ++sample)
            expectWithinAbsoluteError(bypass.output[0][sample], 2.0f, 0.0f);
    }

    void runZeroAllocation()
    {
        beginTest("warmed variable process calls allocate zero parent memory");
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        ReblockHarness harness;
        expect(harness.prepare(512, 512));
        harness.process(256);
        harness.process(256);
        static constexpr int sizes[] { 64, 128, 256, 480, 512 };
        for (const int samples : sizes)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < samples; ++sample)
                    harness.block.setSample(channel, sample,
                        harness.inputAt(harness.cursor + static_cast<std::uint64_t>(sample), channel));
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                const auto result = harness.reblocker.processBlock(
                    harness.block, samples, harness.backend);
                juce::ignoreUnused(result);
            }
            harness.cursor += static_cast<std::uint64_t>(samples);
        }
       #else
        expect(false, "JUCE allocation hooks are required for reblock allocation proof");
       #endif
    }

    void runLatency()
    {
        beginTest("transport impulse is exactly 2Q");
        static constexpr int sizes[] { 128, 128, 256, 128, 384 };
        ReblockHarness transport;
        expect(transport.prepare(512, 512));
        transport.pattern = InputPattern::Impulse;
        transport.runSchedule(sizes, std::size(sizes), 4u * 512u);
        expectWithinAbsoluteError(transport.output[0][1023], 0.0f, 0.0f);
        expectWithinAbsoluteError(transport.output[0][1024], 1.0f, 0.0f);
        expectWithinAbsoluteError(transport.output[0][1025], 0.0f, 0.0f);

        beginTest("simulated plugin latency adds to 2Q");
        ReblockHarness pluginLatency;
        expect(pluginLatency.prepare(512, 512, 32));
        pluginLatency.pattern = InputPattern::Impulse;
        pluginLatency.runSchedule(sizes, std::size(sizes), 4u * 512u);
        expectEquals(pluginLatency.reblocker.transportLatencySamples(), 1024);
        expectEquals(pluginLatency.reblocker.effectiveLatencySamples(), 1056);
        expectWithinAbsoluteError(pluginLatency.output[0][1055], 0.0f, 0.0f);
        expectWithinAbsoluteError(pluginLatency.output[0][1056], 1.0f, 0.0f);
        expectWithinAbsoluteError(pluginLatency.output[0][1057], 0.0f, 0.0f);
    }

    void runFallbackAlignment()
    {
        beginTest("deadline and submit misses select matching plugin-delayed dry");
        static constexpr int sizes[] { 256, 512, 256 };
        ReblockHarness harness;
        expect(harness.prepare(512, 512, 32));
        harness.pattern = InputPattern::Impulse;
        harness.impulseTailValue = 7.0f;
        harness.backend.missRemoteSequence(1);
        harness.backend.missSubmitSequence(2);
        harness.runSchedule(sizes, std::size(sizes), 5u * 512u);

        for (int sample = 1024; sample < 1056; ++sample)
            expectWithinAbsoluteError(harness.output[0][sample], 0.0f, 0.0f);
        expectWithinAbsoluteError(harness.output[0][1056], 1.0f, 0.0f,
                                  "fallback impulse was not delayed by P+2Q");
        for (int sample = 1057; sample < 1536; ++sample)
            expectWithinAbsoluteError(harness.output[0][sample], 0.0f, 0.0f,
                                      "Phase B current-quantum fallback leaked into N-1");
        for (int sample = 1536; sample < 2048; ++sample)
        {
            const float expected = sample < 1568 ? 0.0f : 7.0f;
            expectWithinAbsoluteError(harness.output[0][sample], expected, 0.0f);
        }
        expect(harness.reblocker.diagnostics().fallbackQuanta >= 2);
    }

    void runGenerationReset()
    {
        beginTest("generation reset flushes all old input and output history");
        static constexpr int sizes[] { 256, 256 };
        ReblockHarness harness;
        expect(harness.prepare(512, 512));
        harness.pattern = InputPattern::Constant;
        harness.constantValue = 9.0f;
        harness.runSchedule(sizes, std::size(sizes), 3u * 512u);

        harness.reblocker.resetForGeneration(2, false);
        harness.backend.reset(2);
        harness.cursor = 0;
        harness.output.assign(2, {});
        harness.constantValue = 3.0f;
        harness.runSchedule(sizes, std::size(sizes), 3u * 512u);
        for (int sample = 0; sample < 1024; ++sample)
            expectWithinAbsoluteError(harness.output[0][sample], 0.0f, 0.0f);
        for (int sample = 1024; sample < 1536; ++sample)
            expectWithinAbsoluteError(harness.output[0][sample], 3.0f, 0.0f);
        expectEquals(static_cast<juce::int64>(harness.reblocker.diagnostics().generation),
                     juce::int64(2));
    }

    void runReprepare()
    {
        beginTest("512 to 1024 reprepare reallocates only on control plane and reprimes");
        ReblockHarness harness;
        expect(harness.prepare(512, 512));
        harness.pattern = InputPattern::Constant;
        harness.constantValue = 5.0f;
        harness.process(512);

        DAW::PluginSandboxFixedQuantumReblockerCore::Configuration configuration;
        configuration.quantumSamples = 1024;
        configuration.maximumHostBlockSamples = 1024;
        configuration.inputChannels = 2;
        configuration.outputChannels = 2;
        configuration.pluginLatencySamples = 0;
        configuration.transportGeneration = 2;
        expect(harness.reblocker.reprepare(configuration));
        harness.backend.prepare(1024, 2, 0, 1.0f, 2);
        harness.block.setSize(2, 1024, false, true, false);
        harness.quantum = 1024;
        harness.cursor = 0;
        harness.output.assign(2, {});
        harness.constantValue = 4.0f;
        static constexpr int sizes[] { 512, 1024 };
        harness.runSchedule(sizes, std::size(sizes), 3u * 1024u);
        expectEquals(harness.reblocker.transportLatencySamples(), 2048);
        expect(harness.backend.exactQuantumOnly());
        for (int sample = 0; sample < 2048; ++sample)
            expectWithinAbsoluteError(harness.output[0][sample], 0.0f, 0.0f);
        for (int sample = 2048; sample < 3072; ++sample)
            expectWithinAbsoluteError(harness.output[0][sample], 4.0f, 0.0f);
    }

    void runDepthWindow()
    {
        beginTest("Bmax=512 depth 1 keeps the validated 1024/1056 timeline");
        {
            static constexpr int sizes[] { 128, 256, 384, 512 };
            ReblockHarness harness;
            expect(harness.prepare(512, 512, 32));
            harness.pattern = InputPattern::Impulse;
            harness.runSchedule(sizes, std::size(sizes), 4u * 512u);
            expectEquals(harness.reblocker.resolutionDepth(), 1);
            expectEquals(harness.reblocker.transportLatencySamples(), 1024);
            expectEquals(harness.reblocker.effectiveLatencySamples(), 1056);
            expectWithinAbsoluteError(harness.output[0][1055], 0.0f, 0.0f);
            expectWithinAbsoluteError(harness.output[0][1056], 1.0f, 0.0f);
            expectWithinAbsoluteError(harness.output[0][1057], 0.0f, 0.0f);
        }

        beginTest("Bmax=1024 depth 2 proves the 1536/1568 timeline");
        {
            static constexpr int sizes[] { 256, 512, 1024 };
            ReblockHarness harness;
            expect(harness.prepare(512, 1024, 32));
            harness.pattern = InputPattern::Impulse;
            harness.runSchedule(sizes, std::size(sizes), 4u * 1024u);
            expectEquals(harness.reblocker.resolutionDepth(), 2);
            expectEquals(harness.reblocker.transportLatencySamples(), 1536);
            expectEquals(harness.reblocker.effectiveLatencySamples(), 1568);
            expectWithinAbsoluteError(harness.output[0][1567], 0.0f, 0.0f);
            expectWithinAbsoluteError(harness.output[0][1568], 1.0f, 0.0f);
            expectWithinAbsoluteError(harness.output[0][1569], 0.0f, 0.0f);
        }

        beginTest("Bmax=2048 depth 4 proves the 2560/2592 timeline with four back-to-back quanta");
        {
            static constexpr int sizes[] { 2048, 512, 1024, 512 };
            ReblockHarness harness;
            expect(harness.prepare(512, 2048, 32));
            harness.pattern = InputPattern::Impulse;
            harness.runSchedule(sizes, std::size(sizes), 4u * 2048u);
            expectEquals(harness.reblocker.resolutionDepth(), 4);
            expectEquals(harness.reblocker.transportLatencySamples(), 2560);
            expectEquals(harness.reblocker.effectiveLatencySamples(), 2592);
            expect(harness.backend.exactQuantumOnly());
            expectWithinAbsoluteError(harness.output[0][2591], 0.0f, 0.0f);
            expectWithinAbsoluteError(harness.output[0][2592], 1.0f, 0.0f);
            expectWithinAbsoluteError(harness.output[0][2593], 0.0f, 0.0f);

            // 8192 samples = 16 exchanges; committed = exchanges - depth = 12.
            // Every resolved quantum was remote: zero dry substitution.
            const auto diagnostics = harness.reblocker.diagnostics();
            expectEquals(static_cast<juce::int64>(diagnostics.backendCalls), juce::int64(16));
            expectEquals(static_cast<juce::int64>(diagnostics.committedQuanta), juce::int64(12));
            expectEquals(static_cast<juce::int64>(diagnostics.fallbackQuanta), juce::int64(0));
            expectEquals(static_cast<juce::int64>(diagnostics.remoteQuanta), juce::int64(12));
        }

        beginTest("depth-4 fallback alignment selects the matching historical dry quantum");
        {
            static constexpr int sizes[] { 2048, 2048, 2048, 2048 };
            ReblockHarness harness;
            expect(harness.prepare(512, 2048, 32, 0.5f));
            harness.pattern = InputPattern::Impulse;
            harness.impulseTailValue = 7.0f;
            harness.backend.missRemoteSequence(2);
            harness.runSchedule(sizes, std::size(sizes), 4u * 2048u);

            // Quantum 2 = input [512,1024): tail 7.0. It missed, so its output
            // region [ (2-1)*512 + 2560, 2*512 + 2560 ) = [3072, 3584) must be
            // the matching DELAYED DRY (7.0 after the 32-sample plugin delay),
            // NOT the remote 0.5-gain result and NOT current callback audio.
            for (int sample = 3072; sample < 3104; ++sample)
                expectWithinAbsoluteError(harness.output[0][sample], 0.0f, 0.0f,
                                          "depth-4 fallback ignored the plugin delay");
            for (int sample = 3104; sample < 3584; ++sample)
                expectWithinAbsoluteError(harness.output[0][sample], 7.0f, 0.0f,
                                          "depth-4 fallback did not use the matching historical dry quantum");

            // Neighbor quantum 3 = input [1024,1536): remote 0.5 * 7.0 = 3.5.
            for (int sample = 3616; sample < 4096; ++sample)
                expectWithinAbsoluteError(harness.output[0][sample], 3.5f, 0.0f,
                                          "neighboring remote quantum was corrupted by the fallback");
            expect(harness.reblocker.diagnostics().fallbackQuanta >= 1);
        }
    }

    Scenario scenario_;
};

PluginSandboxReblockTest fixedQuantumTest(
    "plugin.sandbox.reblock.fixed-quantum.v1",
    PluginSandboxReblockTest::Scenario::FixedQuantum);
PluginSandboxReblockTest partialOutputSafetyTest(
    "plugin.sandbox.reblock.partial-output-safety.v1",
    PluginSandboxReblockTest::Scenario::PartialOutputSafety);
PluginSandboxReblockTest streamContinuityTest(
    "plugin.sandbox.reblock.stream-continuity.v1",
    PluginSandboxReblockTest::Scenario::StreamContinuity);
PluginSandboxReblockTest zeroAllocationTest(
    "plugin.sandbox.reblock.zero-rt-allocation.v1",
    PluginSandboxReblockTest::Scenario::ZeroAllocation);
PluginSandboxReblockTest latencyTest(
    "plugin.sandbox.reblock.latency.v1",
    PluginSandboxReblockTest::Scenario::Latency);
PluginSandboxReblockTest fallbackAlignmentTest(
    "plugin.sandbox.reblock.fallback-alignment.v1",
    PluginSandboxReblockTest::Scenario::FallbackAlignment);
PluginSandboxReblockTest generationResetTest(
    "plugin.sandbox.reblock.generation-reset.v1",
    PluginSandboxReblockTest::Scenario::GenerationReset);
PluginSandboxReblockTest reprepareTest(
    "plugin.sandbox.reblock.reprepare.v1",
    PluginSandboxReblockTest::Scenario::Reprepare);
PluginSandboxReblockTest depthWindowTest(
    "plugin.sandbox.reblock.depth-window.v1",
    PluginSandboxReblockTest::Scenario::DepthWindow);
} // namespace
