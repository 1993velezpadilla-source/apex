#pragma once

#include "PluginSandboxProcessCore.h"

#include <iostream>

namespace DAW {

class PluginSandboxSelfTestCore
{
public:
    static int run(const juce::StringArray& arguments)
    {
        bool scenarioDuplicate = false;
        auto scenario = PluginSandboxCommandLineCore::getSingleValue(arguments,
                                                                     "--scenario",
                                                                     scenarioDuplicate);
        bool iterationsDuplicate = false;
        const auto iterationsText = PluginSandboxCommandLineCore::getSingleValue(arguments,
                                                                                 "--iterations",
                                                                                 iterationsDuplicate);
        int requestedIterations = iterationsText.isNotEmpty()
                                    ? iterationsText.getIntValue() : 1;
        requestedIterations = juce::jlimit(1, 100, requestedIterations);

        if (isPhaseBScenario(scenario))
            return runPhaseBScenario(scenario, requestedIterations,
                                     scenarioDuplicate || iterationsDuplicate);

        const bool knownScenario = scenario == "single-executable-distribution"
                                || scenario == "worker-handshake"
                                || scenario == "protocol-mismatch"
                                || scenario == "worker-clean-shutdown"
                                || scenario == "worker-forced-termination"
                                || scenario == "no-recursive-worker";

        juce::DynamicObject::Ptr report = new juce::DynamicObject();
        report->setProperty("scenario", scenario);
        report->setProperty("protocol", static_cast<int>(APEX_PLUGIN_SANDBOX_PROTOCOL_V1));
       #if JUCE_WINDOWS
        report->setProperty("parentPid", static_cast<int>(GetCurrentProcessId()));
       #else
        report->setProperty("parentPid", 0);
       #endif

        bool pass = knownScenario && ! scenarioDuplicate && ! iterationsDuplicate;
        bool handshake = false;
        bool workerMode = false;
        bool normalGuiCreated = false;
        bool sameExecutable = true;
        bool mismatchRejected = false;
        bool gracefulShutdown = true;
        bool forcedTermination = false;
        bool reaped = true;
        bool endpointReleased = true;
        bool handlesClosed = true;
        int activeAfter = 0;
        int maxActive = 0;
        int iterationsCompleted = 0;
        int lastWorkerPid = 0;
        int handleCountBefore = -1;
        int handleCountAfter = -1;
        bool handleAuditAvailable = false;
        bool recursionGuardEnabled = true;
        juce::String lastError;

        const int iterations = scenario == "worker-clean-shutdown"
                                 ? requestedIterations : 1;

        // Windows/JUCE may create process-lifetime infrastructure on the first
        // child launch. Run one fully cleaned warm-up of the SAME lifecycle
        // path, then audit all requested iterations against that stable base.
        for (int iteration = -1; iteration < iterations && pass; ++iteration)
        {
            PluginSandboxProcessCore process;
            PluginSandboxProcessCore::Options options;
            options.requestedProtocol = scenario == "protocol-mismatch"
                ? APEX_PLUGIN_SANDBOX_PROTOCOL_V1 + 1
                : APEX_PLUGIN_SANDBOX_PROTOCOL_V1;
            options.refuseShutdownForTest = scenario == "worker-forced-termination";
            options.startupTimeoutMs = 5000;

            const auto startResult = process.start(options);
            auto diagnostics = process.diagnostics();
            lastWorkerPid = static_cast<int>(diagnostics.workerProcessId);
            maxActive = juce::jmax(maxActive, diagnostics.maxJobActiveProcesses);
            recursionGuardEnabled = recursionGuardEnabled
                                  && diagnostics.recursionGuardEnabled;
            sameExecutable = sameExecutable && diagnostics.sameExecutable;
            workerMode = workerMode || diagnostics.workerMode;
            normalGuiCreated = normalGuiCreated || diagnostics.normalGuiCreated;

            if (scenario == "protocol-mismatch")
            {
                mismatchRejected = startResult
                    == PluginSandboxProcessCore::StartResult::ProtocolMismatch
                    && diagnostics.protocolMismatchRejected;
                pass = pass && mismatchRejected;
            }
            else
            {
                const bool started = startResult
                    == PluginSandboxProcessCore::StartResult::Started;
                handshake = handshake || (started && diagnostics.handshake);
                pass = pass && started && diagnostics.handshake
                            && diagnostics.workerMode
                            && ! diagnostics.normalGuiCreated
                            && diagnostics.sameExecutable
                            && diagnostics.maxJobActiveProcesses == 1;
                if (started)
                {
                    const DWORD shutdownTimeout = options.refuseShutdownForTest ? 350 : 2000;
                    pass = process.shutdown(shutdownTimeout) && pass;
                }
            }

            diagnostics = process.diagnostics();
            lastError = diagnostics.error;
            reaped = reaped && diagnostics.reaped;
            endpointReleased = endpointReleased && diagnostics.endpointReleased;
            handlesClosed = handlesClosed && diagnostics.handlesClosed;
            activeAfter = juce::jmax(activeAfter, diagnostics.activeProcessesAfter);
            gracefulShutdown = gracefulShutdown
                && (scenario == "protocol-mismatch" || options.refuseShutdownForTest
                    || diagnostics.gracefulShutdown);
            forcedTermination = forcedTermination || diagnostics.forcedTermination;

            if (options.refuseShutdownForTest)
                pass = pass && diagnostics.forcedTermination && diagnostics.reaped;
            else if (scenario != "protocol-mismatch")
                pass = pass && diagnostics.gracefulShutdown;

            pass = pass && diagnostics.reaped
                        && diagnostics.activeProcessesAfter == 0
                        && diagnostics.endpointReleased
                        && diagnostics.handlesClosed;
            if (pass && iteration >= 0)
                ++iterationsCompleted;

            if (iteration == -1)
            {
               #if JUCE_WINDOWS
                DWORD baselineHandleCount = 0;
                handleAuditAvailable = GetProcessHandleCount(GetCurrentProcess(),
                                                             &baselineHandleCount) != FALSE;
                if (handleAuditAvailable)
                    handleCountBefore = static_cast<int>(baselineHandleCount);
               #endif
            }

        }

        if (scenario == "protocol-mismatch")
            iterationsCompleted = mismatchRejected ? 1 : 0;

       #if JUCE_WINDOWS
        DWORD finalHandleCount = 0;
        handleAuditAvailable = handleAuditAvailable
            && GetProcessHandleCount(GetCurrentProcess(), &finalHandleCount) != FALSE;
        if (handleAuditAvailable)
            handleCountAfter = static_cast<int>(finalHandleCount);
       #endif

        const int osHandleDelta = handleAuditAvailable
                                ? handleCountAfter - handleCountBefore : -1;
        pass = pass && handleAuditAvailable && osHandleDelta == 0;
        if (scenario == "no-recursive-worker")
            pass = pass && recursionGuardEnabled && maxActive == 1;

        report->setProperty("pass", pass);
        report->setProperty("executablePath",
                            PluginSandboxWin32::resolveCurrentExecutablePath());
        report->setProperty("workerPid", lastWorkerPid);
        report->setProperty("sameExecutable", sameExecutable);
        report->setProperty("handshake", handshake);
        report->setProperty("protocolMismatchRejected", mismatchRejected);
        report->setProperty("gracefulShutdown", gracefulShutdown);
        report->setProperty("forcedTermination", forcedTermination);
        report->setProperty("reaped", reaped);
        report->setProperty("activeProcessesAfter", activeAfter);
        report->setProperty("endpointReleased", endpointReleased);
        report->setProperty("handlesClosed", handlesClosed);
        report->setProperty("osHandleDelta", osHandleDelta);
        report->setProperty("handleAuditAvailable", handleAuditAvailable);
        report->setProperty("recursionGuardEnabled", recursionGuardEnabled);
        report->setProperty("workerMode", workerMode);
        report->setProperty("normalGuiCreated", normalGuiCreated);
        report->setProperty("maxJobActiveProcesses", maxActive);
        report->setProperty("iterationsCompleted", iterationsCompleted);
        report->setProperty("error", lastError);

        std::cout << "APEX_PLUGIN_SANDBOX_RESULT "
                  << juce::JSON::toString(juce::var(report.get()), false)
                  << std::endl;
        return pass ? 0 : 1;
    }

private:
    static bool isPhaseBScenario(const juce::String& scenario) noexcept
    {
        return scenario == "audio-shared-memory"
            || scenario == "audio-block-matrix"
            || scenario == "audio-variable-block"
            || scenario == "audio-zero-rt-allocation"
            || scenario == "audio-deadline-fallback"
            || scenario == "audio-worker-loss"
            || scenario == "audio-generation-reset"
            || scenario == "audio-two-worker-isolation"
            || scenario == "audio-channel-integrity"
            || scenario == "audio-block-invariance";
    }

    struct AudioBuffers
    {
        AudioBuffers()
        {
            for (std::size_t channel = 0; channel < input.size(); ++channel)
            {
                inputPointers[channel] = input[channel].data();
                outputPointers[channel] = output[channel].data();
            }
        }

        void fillInput(std::uint32_t channels, std::uint32_t samples,
                       float seed) noexcept
        {
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                for (std::uint32_t sample = 0; sample < samples; ++sample)
                    input[channel][sample] = seed + static_cast<float>(channel) * 0.125f
                                         + static_cast<float>(sample) * 0.00003125f;
        }

        void fillOutput(float value) noexcept
        {
            for (auto& channel : output)
                channel.fill(value);
        }

        bool outputMatches(std::uint32_t channels, std::uint32_t samples,
                           float seed, PluginSandboxAudioTestDspMode mode) const noexcept
        {
            for (std::uint32_t channel = 0; channel < channels; ++channel)
            {
                const auto gain = PluginSandboxAudioShared::gainForMode(mode, channel);
                for (std::uint32_t sample = 0; sample < samples; ++sample)
                {
                    const auto inputValue = seed + static_cast<float>(channel) * 0.125f
                                          + static_cast<float>(sample) * 0.00003125f;
                    if (std::abs(output[channel][sample] - inputValue * gain) > 1.0e-6f)
                        return false;
                }
            }
            return true;
        }

        bool dryMatches(std::uint32_t channels, std::uint32_t samples,
                        float seed) const noexcept
        {
            return outputMatches(channels, samples, seed,
                                 PluginSandboxAudioTestDspMode::Identity);
        }

        bool sentinelIntact(std::uint32_t channels, std::uint32_t firstSample,
                            float sentinel) const noexcept
        {
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                for (std::uint32_t sample = firstSample;
                     sample < PluginSandboxAudioShared::kPhysicalMaxSamples; ++sample)
                    if (output[channel][sample] != sentinel)
                        return false;
            return true;
        }

        bool inactiveChannelsIntact(std::uint32_t firstInactiveChannel,
                                    float sentinel) const noexcept
        {
            for (std::uint32_t channel = firstInactiveChannel;
                 channel < PluginSandboxAudioShared::kPhysicalMaxChannels; ++channel)
                for (const auto sample : output[channel])
                    if (sample != sentinel)
                        return false;
            return true;
        }

        std::array<std::array<float, PluginSandboxAudioShared::kPhysicalMaxSamples>,
                   PluginSandboxAudioShared::kPhysicalMaxChannels> input {};
        std::array<std::array<float, PluginSandboxAudioShared::kPhysicalMaxSamples>,
                   PluginSandboxAudioShared::kPhysicalMaxChannels> output {};
        std::array<const float*, PluginSandboxAudioShared::kPhysicalMaxChannels> inputPointers {};
        std::array<float*, PluginSandboxAudioShared::kPhysicalMaxChannels> outputPointers {};
    };

    struct PhaseBMetrics
    {
        bool pass = true;
        bool sharedMemory = false;
        bool sharedMemoryReleased = false;
        bool deterministicAudio = false;
        bool blockMatrix = false;
        bool variableBlock = false;
        bool deadlineFallback = false;
        bool recoveredAfterDeadline = false;
        bool workerLossFallback = false;
        bool generationReset = false;
        bool twoWorkerIsolation = false;
        bool channelIntegrity = false;
        bool blockInvariance = false;
        bool allocationProofDelegated = false;
        int parentRtAllocations = -1;
        int workerRtAllocations = -1;
        int staleOutputsAccepted = 0;
        int staleOutputsRejected = 0;
        int submitMisses = 0;
        int deadlineMisses = 0;
        int orphanWorkers = 0;
        int iterationsCompleted = 0;
        int mappedBytes = 0;
        int latencyBlocks = 0;
        double maxCallbackMicros = 0.0;
        std::uint64_t firstGeneration = 0;
        std::uint64_t finalGeneration = 0;
        juce::String error;
    };

    static PluginSandboxAudioTransportCore::ExchangeResult exchange(
        PluginSandboxAudioTransportCore& transport,
        AudioBuffers& buffers,
        std::uint32_t channels,
        std::uint32_t samples,
        PhaseBMetrics& metrics) noexcept
    {
        LARGE_INTEGER frequency {};
        LARGE_INTEGER before {};
        LARGE_INTEGER after {};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&before);
        const auto result = transport.exchangeBlock(buffers.inputPointers.data(), channels,
                                                    buffers.outputPointers.data(), channels,
                                                    samples,
                                                    PluginSandboxAudioShared::kPhysicalMaxSamples);
        QueryPerformanceCounter(&after);
        if (frequency.QuadPart > 0)
        {
            const auto micros = static_cast<double>(after.QuadPart - before.QuadPart)
                              * 1000000.0 / static_cast<double>(frequency.QuadPart);
            metrics.maxCallbackMicros = juce::jmax(metrics.maxCallbackMicros, micros);
        }
        return result;
    }

    static bool waitForWorker(PluginSandboxAudioTransportCore& transport,
                              std::uint64_t sequence, DWORD timeoutMs = 5000) noexcept
    {
        const auto deadline = GetTickCount64() + timeoutMs;
        while (transport.workerCompletedSequence() < sequence)
        {
            if (GetTickCount64() >= deadline)
                return false;
            Sleep(1);
        }
        return true;
    }

    static bool exercisePair(PluginSandboxProcessCore& process,
                             AudioBuffers& buffers,
                             std::uint32_t channels,
                             std::uint32_t samples,
                             float seed,
                             PluginSandboxAudioTestDspMode mode,
                             PhaseBMetrics& metrics,
                             bool requireFirstBlockFallback = false)
    {
        constexpr float sentinel = -9876.5f;
        const auto countersBefore = process.audioTransport().counters();
        buffers.fillInput(channels, samples, seed);
        buffers.fillOutput(sentinel);
        const auto first = exchange(process.audioTransport(), buffers,
                                    channels, samples, metrics);
        const bool firstValid = ! requireFirstBlockFallback
            || (first.fallback && ! first.remoteOutput
                && first.expectedSequence == 0
                && buffers.dryMatches(channels, samples, seed)
                && buffers.sentinelIntact(channels, samples, sentinel));
        if (! firstValid || ! first.submitted
            || ! waitForWorker(process.audioTransport(),
                                                   first.submittedSequence))
            return false;

        buffers.fillInput(channels, samples, seed + 10.0f);
        buffers.fillOutput(sentinel);
        const auto second = exchange(process.audioTransport(), buffers,
                                     channels, samples, metrics);
        const bool valid = second.remoteOutput
                        && second.completedSequence == first.submittedSequence
                        && second.completedGeneration
                               == process.audioTransport().generation()
                        && second.outputSamples == samples
                        && buffers.outputMatches(channels, samples, seed, mode)
                        && buffers.sentinelIntact(channels, samples, sentinel)
                        && second.submitted;
        const bool completed = valid
            && waitForWorker(process.audioTransport(), second.submittedSequence);
        const auto countersAfter = process.audioTransport().counters();
        return completed
            && countersAfter.deadlineMisses == countersBefore.deadlineMisses
            && countersAfter.submitMisses == countersBefore.submitMisses;
    }

    static bool exerciseVariableSequence(PluginSandboxProcessCore& process,
                                         AudioBuffers& buffers,
                                         const std::uint32_t* sizes,
                                         std::size_t count,
                                         PhaseBMetrics& metrics)
    {
        constexpr float sentinel = -9876.5f;
        constexpr auto mode = PluginSandboxAudioTestDspMode::GainHalf;
        const auto generation = process.audioTransport().generation();
        std::uint32_t previousSize = 0;
        float previousSeed = 0.0f;
        std::uint64_t previousSequence = 0;
        bool valid = true;

        for (std::size_t index = 0; index < count && valid; ++index)
        {
            const auto size = sizes[index];
            const auto seed = 20.0f + static_cast<float>(index);
            buffers.fillInput(2, size, seed);
            buffers.fillOutput(sentinel);
            const auto result = exchange(process.audioTransport(), buffers, 2, size, metrics);

            if (index == 0)
                valid = result.fallback && ! result.remoteOutput
                     && result.expectedSequence == 0
                     && buffers.dryMatches(2, size, seed)
                     && buffers.sentinelIntact(2, size, sentinel);
            else
                valid = result.remoteOutput
                     && result.completedSequence == previousSequence
                     && result.completedGeneration == generation
                     && result.outputSamples == previousSize
                     && buffers.outputMatches(2, previousSize, previousSeed, mode)
                     && buffers.sentinelIntact(2, previousSize, sentinel);

            valid = valid && result.submitted
                 && waitForWorker(process.audioTransport(), result.submittedSequence);
            previousSize = size;
            previousSeed = seed;
            previousSequence = result.submittedSequence;
        }

        // Flush and verify the final submitted block without accepting the flush input.
        if (valid)
        {
            buffers.fillInput(2, 64, 999.0f);
            buffers.fillOutput(sentinel);
            const auto flushed = exchange(process.audioTransport(), buffers, 2, 64, metrics);
            valid = flushed.remoteOutput
                 && flushed.completedSequence == previousSequence
                 && flushed.completedGeneration == generation
                 && flushed.outputSamples == previousSize
                 && buffers.outputMatches(2, previousSize, previousSeed, mode)
                 && buffers.sentinelIntact(2, previousSize, sentinel)
                 && flushed.submitted;
        }

        const auto counters = process.audioTransport().counters();
        metrics.deadlineMisses = static_cast<int>(counters.deadlineMisses);
        metrics.submitMisses = static_cast<int>(counters.submitMisses);
        return valid && counters.deadlineMisses == 0 && counters.submitMisses == 0;
    }

    static bool startAudioProcess(PluginSandboxProcessCore& process,
                                  PluginSandboxAudioTestDspMode mode,
                                  PhaseBMetrics& metrics,
                                  std::uint64_t delaySequence = 0,
                                  DWORD delayMilliseconds = 0)
    {
        PluginSandboxProcessCore::Options options;
        options.enableAudioTransport = true;
        options.audioConfiguration.maxInputChannels =
            PluginSandboxAudioShared::kPhysicalMaxChannels;
        options.audioConfiguration.maxOutputChannels =
            PluginSandboxAudioShared::kPhysicalMaxChannels;
        options.audioConfiguration.maxSamples =
            PluginSandboxAudioShared::kPhysicalMaxSamples;
        options.audioConfiguration.nominalBlockSamples = 512;
        options.audioConfiguration.sampleRate = 48000.0;
        options.audioDspMode = mode;
        options.audioDelaySequence = delaySequence;
        options.audioDelayMilliseconds = delayMilliseconds;

        const auto started = process.start(options)
            == PluginSandboxProcessCore::StartResult::Started;
        const auto& diagnostics = process.diagnostics();
        metrics.sharedMemory = metrics.sharedMemory
            || (started && diagnostics.handshake
                && diagnostics.audioTransportReady
                && diagnostics.sharedMemoryBytes > 0
                && diagnostics.sharedMemoryName.startsWith(
                    "Local\\APEX.PluginSandbox.Audio."));
        metrics.mappedBytes = juce::jmax(metrics.mappedBytes,
                                         static_cast<int>(diagnostics.sharedMemoryBytes));
        metrics.latencyBlocks = PluginSandboxAudioTransportCore::getTransportLatencyBlocks();
        metrics.firstGeneration = metrics.firstGeneration == 0
            ? diagnostics.audioGeneration : metrics.firstGeneration;
        metrics.finalGeneration = diagnostics.audioGeneration;
        if (! started)
            metrics.error = diagnostics.error;
        return started;
    }

    static bool shutdownAndAudit(PluginSandboxProcessCore& process,
                                 PhaseBMetrics& metrics,
                                 DWORD timeoutMs = 3000)
    {
        const bool shutdown = process.shutdown(timeoutMs);
        const auto& diagnostics = process.diagnostics();
        metrics.sharedMemoryReleased = metrics.sharedMemoryReleased
            || diagnostics.sharedMemoryReleased;
        if (! diagnostics.reaped || diagnostics.activeProcessesAfter != 0)
            ++metrics.orphanWorkers;
        if (metrics.error.isEmpty() && diagnostics.error.isNotEmpty())
            metrics.error = diagnostics.error;
        return shutdown && diagnostics.reaped
            && diagnostics.activeProcessesAfter == 0
            && diagnostics.sharedMemoryReleased
            && diagnostics.endpointReleased
            && diagnostics.handlesClosed;
    }

    static bool runSharedMemoryScenario(PhaseBMetrics& metrics)
    {
        PluginSandboxProcessCore process;
        AudioBuffers buffers;
        if (! startAudioProcess(process, PluginSandboxAudioTestDspMode::GainHalf, metrics))
            return false;

        constexpr float seed = 0.25f;
        constexpr std::uint32_t channels = 2;
        constexpr std::uint32_t samples = 512;
        buffers.fillInput(channels, samples, seed);
        buffers.fillOutput(-1.0f);
        const auto first = exchange(process.audioTransport(), buffers,
                                    channels, samples, metrics);
        bool valid = first.fallback && ! first.remoteOutput && first.submitted
                  && buffers.dryMatches(channels, samples, seed)
                  && waitForWorker(process.audioTransport(), first.submittedSequence);

        buffers.fillInput(channels, samples, seed + 1.0f);
        buffers.fillOutput(-1.0f);
        const auto second = exchange(process.audioTransport(), buffers,
                                     channels, samples, metrics);
        valid = valid && second.remoteOutput
            && second.completedSequence == first.submittedSequence
            && buffers.outputMatches(channels, samples, seed,
                                     PluginSandboxAudioTestDspMode::GainHalf)
            && process.audioTransport().getTransportLatencySamples()
                   == static_cast<int>(samples)
            && PluginSandboxAudioTransportCore::getTransportLatencySamples(samples)
                   == static_cast<int>(samples);
        metrics.deterministicAudio = valid;
        return shutdownAndAudit(process, metrics) && valid;
    }

    static bool runMatrixScenario(PhaseBMetrics& metrics, bool variableOnly)
    {
        static constexpr std::uint32_t sizes[] = {
            64, 128, 256, 480, 512, 1024, 2048
        };
        static constexpr std::uint32_t variableSizes[] = {
            480, 1024, 128, 2048, 256, 512, 64, 2048
        };

        bool valid = true;
        if (variableOnly)
        {
            PluginSandboxProcessCore process;
            AudioBuffers buffers;
            if (! startAudioProcess(process, PluginSandboxAudioTestDspMode::GainHalf,
                                    metrics))
                return false;
            valid = exerciseVariableSequence(process, buffers, variableSizes,
                                             std::size(variableSizes), metrics);
            metrics.variableBlock = valid;
            valid = shutdownAndAudit(process, metrics) && valid;
        }
        else
        {
            for (std::size_t index = 0; index < std::size(sizes) && valid; ++index)
            {
                PluginSandboxProcessCore process;
                AudioBuffers buffers;
                if (! startAudioProcess(process,
                                        PluginSandboxAudioTestDspMode::GainHalf,
                                        metrics))
                    return false;
                const auto channels = static_cast<std::uint32_t>((index % 4) + 1);
                valid = exercisePair(process, buffers, channels, sizes[index],
                                     40.0f + static_cast<float>(index),
                                     PluginSandboxAudioTestDspMode::GainHalf, metrics,
                                     true);
                const auto counters = process.audioTransport().counters();
                valid = valid && counters.deadlineMisses == 0
                              && counters.submitMisses == 0;
                valid = shutdownAndAudit(process, metrics) && valid;
            }
            metrics.blockMatrix = valid;
        }
        return valid;
    }

    static bool runZeroAllocationScenario(PhaseBMetrics& metrics)
    {
        static constexpr std::uint32_t sizes[] = {
            64, 128, 256, 480, 512, 1024, 2048,
            480, 1024, 128, 2048, 256, 512, 64, 2048
        };
        PluginSandboxProcessCore process;
        AudioBuffers buffers;
        if (! startAudioProcess(process, PluginSandboxAudioTestDspMode::Identity, metrics))
            return false;

        bool valid = true;
        for (std::size_t block = 0; block < std::size(sizes) && valid; ++block)
            valid = exercisePair(process, buffers, 2, sizes[block],
                                 60.0f + static_cast<float>(block),
                                 PluginSandboxAudioTestDspMode::Identity, metrics);

        // The sibling APEXTests process owns JUCE_ENABLE_ALLOCATION_HOOKS and
        // wraps the exact parent exchange and worker processing methods. This
        // production self-test intentionally does not fabricate allocation counts.
        metrics.allocationProofDelegated = true;
        return shutdownAndAudit(process, metrics) && valid;
    }

    static bool runDeadlineScenario(PhaseBMetrics& metrics)
    {
        PluginSandboxProcessCore process;
        AudioBuffers buffers;
        if (! startAudioProcess(process,
                               PluginSandboxAudioTestDspMode::DeadlineIdentity,
                               metrics, 2, 120))
            return false;

        buffers.fillInput(2, 256, 80.0f);
        const auto first = exchange(process.audioTransport(), buffers, 2, 256, metrics);
        bool valid = first.submitted
                  && waitForWorker(process.audioTransport(), first.submittedSequence);

        buffers.fillInput(2, 256, 81.0f);
        const auto delayed = exchange(process.audioTransport(), buffers, 2, 256, metrics);
        valid = valid && delayed.remoteOutput && delayed.submitted;

        buffers.fillInput(2, 256, 82.0f);
        buffers.fillOutput(-1.0f);
        const auto miss = exchange(process.audioTransport(), buffers, 2, 256, metrics);
        metrics.deadlineFallback = miss.fallback && ! miss.remoteOutput
                                && buffers.dryMatches(2, 256, 82.0f);
        valid = valid && metrics.deadlineFallback && miss.submitted
            && waitForWorker(process.audioTransport(), miss.submittedSequence);

        buffers.fillInput(2, 256, 83.0f);
        buffers.fillOutput(-1.0f);
        const auto recovered = exchange(process.audioTransport(), buffers, 2, 256, metrics);
        metrics.recoveredAfterDeadline = recovered.remoteOutput
            && recovered.completedSequence == miss.submittedSequence
            && buffers.outputMatches(2, 256, 82.0f,
                                     PluginSandboxAudioTestDspMode::Identity);
        const auto counters = process.audioTransport().counters();
        metrics.deadlineMisses = static_cast<int>(counters.deadlineMisses);
        metrics.staleOutputsRejected = static_cast<int>(counters.staleOutputsRejected);
        metrics.submitMisses = static_cast<int>(counters.submitMisses);
        valid = valid && metrics.recoveredAfterDeadline
            && recovered.completedSequence != delayed.submittedSequence
            && metrics.deadlineMisses > 0
            && metrics.staleOutputsRejected > 0;
        return shutdownAndAudit(process, metrics) && valid;
    }

    static bool runWorkerLossScenario(PhaseBMetrics& metrics)
    {
        PluginSandboxProcessCore process;
        AudioBuffers buffers;
        if (! startAudioProcess(process, PluginSandboxAudioTestDspMode::GainHalf, metrics))
            return false;
        bool valid = exercisePair(process, buffers, 2, 512, 100.0f,
                                  PluginSandboxAudioTestDspMode::GainHalf, metrics);
        valid = process.terminateWorkerAndRetainTransportForTest() && valid;
        const auto retainedDiagnostics = process.diagnostics();
        valid = valid && retainedDiagnostics.reaped
            && retainedDiagnostics.workerUnavailableDetected
            && retainedDiagnostics.sharedMemoryRetainedAfterWorkerExit
            && process.audioTransport().isPrepared()
            && PluginSandboxAudioTransportCore::mappingExists(
                   retainedDiagnostics.sharedMemoryName);

        buffers.fillInput(2, 512, 101.0f);
        buffers.fillOutput(-1.0f);
        const auto fallback = exchange(process.audioTransport(), buffers, 2, 512, metrics);
        metrics.workerLossFallback = fallback.fallback && fallback.workerUnavailable
                                  && ! fallback.remoteOutput
                                  && buffers.dryMatches(2, 512, 101.0f);
        valid = valid && metrics.workerLossFallback
            && process.cleanupTerminatedWorkerForTest();
        const auto& cleanedDiagnostics = process.diagnostics();
        metrics.sharedMemoryReleased = cleanedDiagnostics.sharedMemoryReleased;
        metrics.orphanWorkers = cleanedDiagnostics.reaped
                             && cleanedDiagnostics.activeProcessesAfter == 0 ? 0 : 1;
        return valid && cleanedDiagnostics.sharedMemoryReleased
            && metrics.orphanWorkers == 0;
    }

    static bool runGenerationScenario(PhaseBMetrics& metrics)
    {
        PluginSandboxProcessCore process;
        AudioBuffers buffers;
        if (! startAudioProcess(process, PluginSandboxAudioTestDspMode::GainHalf, metrics))
            return false;
        bool valid = exercisePair(process, buffers, 2, 512, 120.0f,
                                  PluginSandboxAudioTestDspMode::GainHalf, metrics);
        const auto oldGeneration = process.audioTransport().generation();
        PluginSandboxAudioTransportCore::Configuration configuration;
        configuration.maxInputChannels = 8;
        configuration.maxOutputChannels = 8;
        configuration.maxSamples = 2048;
        configuration.nominalBlockSamples = 256;
        configuration.sampleRate = 96000.0;
        valid = process.reprepareAudio(configuration, 5000) && valid;
        const auto newGeneration = process.audioTransport().generation();

        buffers.fillInput(2, 256, 121.0f);
        buffers.fillOutput(-1.0f);
        const auto first = exchange(process.audioTransport(), buffers, 2, 256, metrics);
        valid = valid && first.fallback && ! first.remoteOutput && first.submitted
            && waitForWorker(process.audioTransport(), first.submittedSequence);
        valid = valid && process.audioTransport().injectStaleGenerationOutputForSelfTest(
            oldGeneration, first.submittedSequence + 1000, 2, 256, -4444.0f);
        buffers.fillInput(2, 256, 122.0f);
        buffers.fillOutput(-1.0f);
        const auto second = exchange(process.audioTransport(), buffers, 2, 256, metrics);
        metrics.generationReset = oldGeneration != newGeneration
            && second.remoteOutput
            && second.completedSequence == first.submittedSequence
            && second.completedGeneration == newGeneration
            && buffers.outputMatches(2, 256, 121.0f,
                                     PluginSandboxAudioTestDspMode::GainHalf);
        const auto counters = process.audioTransport().counters();
        metrics.staleOutputsRejected = static_cast<int>(counters.staleOutputsRejected);
        metrics.generationReset = metrics.generationReset
            && metrics.staleOutputsRejected > 0;
        metrics.finalGeneration = newGeneration;
        valid = valid && metrics.generationReset;
        return shutdownAndAudit(process, metrics) && valid;
    }

    static bool runTwoWorkerScenario(PhaseBMetrics& metrics)
    {
        PluginSandboxProcessCore firstProcess;
        PluginSandboxProcessCore secondProcess;
        AudioBuffers firstBuffers;
        AudioBuffers secondBuffers;
        bool valid = startAudioProcess(firstProcess,
                                       PluginSandboxAudioTestDspMode::GainHalf, metrics)
                  && startAudioProcess(secondProcess,
                                       PluginSandboxAudioTestDspMode::GainQuarter, metrics);
        if (valid)
        {
            bool firstValid = false;
            bool secondValid = false;
            PhaseBMetrics firstMetrics;
            PhaseBMetrics secondMetrics;
            std::thread firstThread([&]
            {
                firstValid = exercisePair(firstProcess, firstBuffers, 2, 512, 140.0f,
                                          PluginSandboxAudioTestDspMode::GainHalf,
                                          firstMetrics, true);
            });
            std::thread secondThread([&]
            {
                secondValid = exercisePair(secondProcess, secondBuffers, 2, 512, 140.0f,
                                           PluginSandboxAudioTestDspMode::GainQuarter,
                                           secondMetrics, true);
            });
            firstThread.join();
            secondThread.join();
            metrics.maxCallbackMicros = juce::jmax(metrics.maxCallbackMicros,
                juce::jmax(firstMetrics.maxCallbackMicros, secondMetrics.maxCallbackMicros));

            valid = firstValid && secondValid
                 && firstProcess.diagnostics().workerProcessId
                        != secondProcess.diagnostics().workerProcessId
                 && firstProcess.diagnostics().sharedMemoryName
                        != secondProcess.diagnostics().sharedMemoryName
                 && firstProcess.diagnostics().sessionToken
                        != secondProcess.diagnostics().sessionToken
                 && firstProcess.audioTransport().generation()
                        != secondProcess.audioTransport().generation();

            // Loss of worker A must not interrupt worker B's independent mapping.
            valid = firstProcess.terminateWorkerAndRetainTransportForTest() && valid;
            valid = exercisePair(secondProcess, secondBuffers, 2, 256, 142.0f,
                                 PluginSandboxAudioTestDspMode::GainQuarter, metrics)
                 && valid;
            valid = firstProcess.cleanupTerminatedWorkerForTest() && valid;
        }
        const bool firstShutdown = shutdownAndAudit(firstProcess, metrics);
        const bool secondShutdown = shutdownAndAudit(secondProcess, metrics);
        metrics.twoWorkerIsolation = valid && firstShutdown && secondShutdown;
        metrics.sharedMemoryReleased = firstProcess.diagnostics().sharedMemoryReleased
                                    && secondProcess.diagnostics().sharedMemoryReleased;
        return metrics.twoWorkerIsolation;
    }

    static bool runChannelScenario(PhaseBMetrics& metrics)
    {
        constexpr float sentinel = -9876.5f;
        PluginSandboxProcessCore monoProcess;
        AudioBuffers monoBuffers;
        if (! startAudioProcess(monoProcess,
                               PluginSandboxAudioTestDspMode::ChannelMarker, metrics))
            return false;
        bool valid = exercisePair(monoProcess, monoBuffers, 1, 480, 161.0f,
                                  PluginSandboxAudioTestDspMode::ChannelMarker,
                                  metrics, true)
                  && monoBuffers.inactiveChannelsIntact(1, sentinel);
        valid = shutdownAndAudit(monoProcess, metrics) && valid;

        PluginSandboxProcessCore stereoProcess;
        AudioBuffers stereoBuffers;
        if (! startAudioProcess(stereoProcess,
                               PluginSandboxAudioTestDspMode::ChannelMarker, metrics))
            return false;
        valid = exercisePair(stereoProcess, stereoBuffers, 2, 480, 162.0f,
                             PluginSandboxAudioTestDspMode::ChannelMarker,
                             metrics, true)
             && stereoBuffers.inactiveChannelsIntact(2, sentinel)
             && valid;
        valid = shutdownAndAudit(stereoProcess, metrics) && valid;
        metrics.channelIntegrity = valid;
        return valid;
    }

    static bool renderInvariantPartition(
        std::uint32_t blockSize,
        std::array<std::array<float, 2048>, 2>& rendered,
        PhaseBMetrics& metrics)
    {
        PluginSandboxProcessCore process;
        AudioBuffers buffers;
        if (! startAudioProcess(process,
                               PluginSandboxAudioTestDspMode::GainQuarter, metrics))
            return false;

        constexpr std::uint32_t channels = 2;
        constexpr std::uint32_t totalSamples = 2048;
        constexpr float sentinel = -9876.5f;
        const auto generation = process.audioTransport().generation();
        bool valid = totalSamples % blockSize == 0;
        std::uint32_t offset = 0;
        std::uint64_t previousSequence = 0;

        while (offset < totalSamples && valid)
        {
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                for (std::uint32_t sample = 0; sample < blockSize; ++sample)
                    buffers.input[channel][sample] = 0.25f
                        + static_cast<float>(channel) * 0.5f
                        + static_cast<float>(offset + sample) * 0.00025f;
            buffers.fillOutput(sentinel);
            const auto result = exchange(process.audioTransport(), buffers,
                                         channels, blockSize, metrics);

            if (offset == 0)
                valid = result.fallback && ! result.remoteOutput
                     && result.expectedSequence == 0;
            else
            {
                valid = result.remoteOutput
                     && result.completedSequence == previousSequence
                     && result.completedGeneration == generation
                     && result.outputSamples == blockSize
                     && buffers.sentinelIntact(channels, blockSize, sentinel);
                for (std::uint32_t channel = 0; channel < channels; ++channel)
                    std::memcpy(rendered[channel].data() + offset - blockSize,
                                buffers.output[channel].data(),
                                sizeof(float) * blockSize);
            }

            valid = valid && result.submitted
                 && waitForWorker(process.audioTransport(), result.submittedSequence);
            previousSequence = result.submittedSequence;
            offset += blockSize;
        }

        if (valid)
        {
            buffers.fillInput(channels, blockSize, 999.0f);
            buffers.fillOutput(sentinel);
            const auto flush = exchange(process.audioTransport(), buffers,
                                        channels, blockSize, metrics);
            valid = flush.remoteOutput
                 && flush.completedSequence == previousSequence
                 && flush.completedGeneration == generation
                 && flush.outputSamples == blockSize
                 && buffers.sentinelIntact(channels, blockSize, sentinel);
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                std::memcpy(rendered[channel].data() + totalSamples - blockSize,
                            buffers.output[channel].data(),
                            sizeof(float) * blockSize);
        }

        for (std::uint32_t channel = 0; channel < channels && valid; ++channel)
            for (std::uint32_t sample = 0; sample < totalSamples; ++sample)
            {
                const auto expected = (0.25f + static_cast<float>(channel) * 0.5f
                                     + static_cast<float>(sample) * 0.00025f) * 0.25f;
                if (std::abs(rendered[channel][sample] - expected) > 1.0e-6f)
                {
                    valid = false;
                    break;
                }
            }

        const auto counters = process.audioTransport().counters();
        valid = valid && counters.deadlineMisses == 0 && counters.submitMisses == 0;
        return shutdownAndAudit(process, metrics) && valid;
    }

    static bool runInvarianceScenario(PhaseBMetrics& metrics)
    {
        std::array<std::array<float, 2048>, 2> rendered256 {};
        std::array<std::array<float, 2048>, 2> rendered512 {};
        std::array<std::array<float, 2048>, 2> rendered1024 {};
        std::array<std::array<float, 2048>, 2> rendered2048 {};
        bool valid = renderInvariantPartition(256, rendered256, metrics)
                  && renderInvariantPartition(512, rendered512, metrics)
                  && renderInvariantPartition(1024, rendered1024, metrics)
                  && renderInvariantPartition(2048, rendered2048, metrics);
        for (std::uint32_t channel = 0; channel < 2 && valid; ++channel)
            for (std::uint32_t sample = 0; sample < 2048; ++sample)
            {
                const auto reference = rendered2048[channel][sample];
                if (std::abs(rendered256[channel][sample] - reference) > 1.0e-6f
                    || std::abs(rendered512[channel][sample] - reference) > 1.0e-6f
                    || std::abs(rendered1024[channel][sample] - reference) > 1.0e-6f)
                {
                    valid = false;
                    break;
                }
            }
        metrics.blockInvariance = valid;
        return valid;
    }

    static int runPhaseBScenario(const juce::String& scenario,
                                 int iterations,
                                 bool invalidArguments)
    {
        PhaseBMetrics aggregate;
        aggregate.pass = ! invalidArguments;
        for (int iteration = 0; iteration < iterations && aggregate.pass; ++iteration)
        {
            PhaseBMetrics current;
            bool iterationPass = false;
            if (scenario == "audio-shared-memory")
                iterationPass = runSharedMemoryScenario(current);
            else if (scenario == "audio-block-matrix")
                iterationPass = runMatrixScenario(current, false);
            else if (scenario == "audio-variable-block")
                iterationPass = runMatrixScenario(current, true);
            else if (scenario == "audio-zero-rt-allocation")
                iterationPass = runZeroAllocationScenario(current);
            else if (scenario == "audio-deadline-fallback")
                iterationPass = runDeadlineScenario(current);
            else if (scenario == "audio-worker-loss")
                iterationPass = runWorkerLossScenario(current);
            else if (scenario == "audio-generation-reset")
                iterationPass = runGenerationScenario(current);
            else if (scenario == "audio-two-worker-isolation")
                iterationPass = runTwoWorkerScenario(current);
            else if (scenario == "audio-channel-integrity")
                iterationPass = runChannelScenario(current);
            else if (scenario == "audio-block-invariance")
                iterationPass = runInvarianceScenario(current);

            aggregate.pass = aggregate.pass && iterationPass;
            aggregate.sharedMemory = aggregate.sharedMemory || current.sharedMemory;
            aggregate.sharedMemoryReleased = current.sharedMemoryReleased;
            aggregate.deterministicAudio = aggregate.deterministicAudio
                                        || current.deterministicAudio;
            aggregate.blockMatrix = aggregate.blockMatrix || current.blockMatrix;
            aggregate.variableBlock = aggregate.variableBlock || current.variableBlock;
            aggregate.deadlineFallback = aggregate.deadlineFallback
                                      || current.deadlineFallback;
            aggregate.recoveredAfterDeadline = aggregate.recoveredAfterDeadline
                                            || current.recoveredAfterDeadline;
            aggregate.workerLossFallback = aggregate.workerLossFallback
                                        || current.workerLossFallback;
            aggregate.generationReset = aggregate.generationReset || current.generationReset;
            aggregate.twoWorkerIsolation = aggregate.twoWorkerIsolation
                                        || current.twoWorkerIsolation;
            aggregate.channelIntegrity = aggregate.channelIntegrity || current.channelIntegrity;
            aggregate.blockInvariance = aggregate.blockInvariance || current.blockInvariance;
            if (current.parentRtAllocations >= 0)
                aggregate.parentRtAllocations = current.parentRtAllocations;
            if (current.workerRtAllocations >= 0)
                aggregate.workerRtAllocations = current.workerRtAllocations;
            aggregate.allocationProofDelegated = aggregate.allocationProofDelegated
                                               || current.allocationProofDelegated;
            aggregate.staleOutputsAccepted += current.staleOutputsAccepted;
            aggregate.staleOutputsRejected += current.staleOutputsRejected;
            aggregate.submitMisses += current.submitMisses;
            aggregate.deadlineMisses += current.deadlineMisses;
            aggregate.orphanWorkers += current.orphanWorkers;
            aggregate.mappedBytes = juce::jmax(aggregate.mappedBytes, current.mappedBytes);
            aggregate.latencyBlocks = current.latencyBlocks;
            aggregate.maxCallbackMicros = juce::jmax(aggregate.maxCallbackMicros,
                                                     current.maxCallbackMicros);
            if (aggregate.firstGeneration == 0)
                aggregate.firstGeneration = current.firstGeneration;
            aggregate.finalGeneration = current.finalGeneration;
            if (aggregate.error.isEmpty())
                aggregate.error = current.error;
            if (iterationPass)
                ++aggregate.iterationsCompleted;
        }

        juce::DynamicObject::Ptr report = new juce::DynamicObject();
        report->setProperty("scenario", scenario);
        report->setProperty("protocol", static_cast<int>(APEX_PLUGIN_SANDBOX_PROTOCOL_V1));
        report->setProperty("pass", aggregate.pass);
        report->setProperty("sharedMemory", aggregate.sharedMemory);
        report->setProperty("sharedMemoryReleased", aggregate.sharedMemoryReleased);
        report->setProperty("deterministicAudio", aggregate.deterministicAudio);
        report->setProperty("blockMatrix", aggregate.blockMatrix);
        report->setProperty("variableBlock", aggregate.variableBlock);
        report->setProperty("deadlineFallback", aggregate.deadlineFallback);
        report->setProperty("recoveredAfterDeadline", aggregate.recoveredAfterDeadline);
        report->setProperty("workerLossFallback", aggregate.workerLossFallback);
        report->setProperty("generationReset", aggregate.generationReset);
        report->setProperty("twoWorkerIsolation", aggregate.twoWorkerIsolation);
        report->setProperty("channelIntegrity", aggregate.channelIntegrity);
        report->setProperty("blockInvariance", aggregate.blockInvariance);
        report->setProperty("allocationProofDelegated",
                            aggregate.allocationProofDelegated);
        report->setProperty("parentRtAllocations", aggregate.parentRtAllocations);
        report->setProperty("workerRtAllocations", aggregate.workerRtAllocations);
        report->setProperty("staleOutputsAccepted", aggregate.staleOutputsAccepted);
        report->setProperty("staleOutputsRejected", aggregate.staleOutputsRejected);
        report->setProperty("submitMisses", aggregate.submitMisses);
        report->setProperty("deadlineMisses", aggregate.deadlineMisses);
        report->setProperty("orphanWorkers", aggregate.orphanWorkers);
        report->setProperty("iterationsCompleted", aggregate.iterationsCompleted);
        report->setProperty("mappedBytes", aggregate.mappedBytes);
        report->setProperty("latencyBlocks", aggregate.latencyBlocks);
        report->setProperty("maxCallbackMicros", aggregate.maxCallbackMicros);
        report->setProperty("firstGeneration",
                            static_cast<juce::int64>(aggregate.firstGeneration));
        report->setProperty("finalGeneration",
                            static_cast<juce::int64>(aggregate.finalGeneration));
        report->setProperty("error", aggregate.error);
        std::cout << "APEX_PLUGIN_SANDBOX_RESULT "
                  << juce::JSON::toString(juce::var(report.get()), false)
                  << std::endl;
        return aggregate.pass ? 0 : 1;
    }
};

} // namespace DAW
