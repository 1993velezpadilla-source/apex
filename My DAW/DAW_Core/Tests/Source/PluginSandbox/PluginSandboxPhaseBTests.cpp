#include <JuceHeader.h>
#include "../../../Source/PluginSandboxCore/PluginSandboxAudioTransportCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxProcessCore.h"
#include "../../../Source/PluginSandboxCore/PluginWorkerAudioTransportCore.h"

#if JUCE_WINDOWS
#include <Psapi.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr const char* kSelfTestFlag = "--apex-plugin-sandbox-self-test";
constexpr int kParentTimeoutMs = 120000;

struct PhaseBScenarioResult
{
    bool launched = false;
    bool timedOut = false;
    int exitCode = -1;
    juce::String output;
    juce::var report;
    juce::String error;
};

juce::File findPhaseBDawCoreExecutable()
{
    auto directory = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                         .getParentDirectory();
    for (int i = 0; i < 6; ++i)
        directory = directory.getParentDirectory();

    const auto testExecutable =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
    const auto configuration = testExecutable.containsIgnoreCase("\\Release\\")
                                 ? "Release" : "Debug";

    return directory.getChildFile("Builds")
                    .getChildFile("VisualStudio2026")
                    .getChildFile("x64")
                    .getChildFile(configuration)
                    .getChildFile("App")
                    .getChildFile("DAW_Core.exe");
}

PhaseBScenarioResult runPhaseBScenario(const juce::String& scenario)
{
    PhaseBScenarioResult result;
    const auto executable = findPhaseBDawCoreExecutable();
    if (! executable.existsAsFile())
    {
        result.error = "DAW_Core.exe does not exist: " + executable.getFullPathName();
        return result;
    }

    juce::StringArray arguments;
    arguments.add(executable.getFullPathName());
    arguments.add(kSelfTestFlag);
    arguments.add("--scenario=" + scenario);

    juce::ChildProcess process;
    result.launched = process.start(arguments,
                                    juce::ChildProcess::wantStdOut
                                      | juce::ChildProcess::wantStdErr);
    if (! result.launched)
    {
        result.error = "Failed to launch Phase B parent self-test";
        return result;
    }

    if (! process.waitForProcessToFinish(kParentTimeoutMs))
    {
        result.timedOut = true;
        process.kill();
        process.waitForProcessToFinish(3000);
        result.error = "Phase B parent self-test exceeded its bounded timeout";
        return result;
    }

    result.exitCode = static_cast<int>(process.getExitCode());
    result.output = process.readAllProcessOutput();

    constexpr auto prefix = "APEX_PLUGIN_SANDBOX_RESULT ";
    const auto reportStart = result.output.indexOf(prefix);
    if (reportStart >= 0)
        result.report = juce::JSON::parse(
            result.output.substring(reportStart + (int) std::strlen(prefix)).trim());

    if (! result.report.isObject())
        result.error = "Phase B parent returned no structured result. Output: " + result.output;
    return result;
}

bool reportBool(const PhaseBScenarioResult& result, const juce::Identifier& property)
{
    if (auto* object = result.report.getDynamicObject())
        return static_cast<bool>(object->getProperty(property));
    return false;
}

int reportInt(const PhaseBScenarioResult& result, const juce::Identifier& property)
{
    if (auto* object = result.report.getDynamicObject())
        return static_cast<int>(object->getProperty(property));
    return -1;
}

class PluginSandboxPhaseBTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseBTest(const juce::String& testName,
                            const juce::String& scenario)
        : juce::UnitTest(testName, "PluginSandbox"), scenario_(scenario)
    {
    }

    void runTest() override
    {
        if (scenario_ == "audio-zero-rt-allocation")
            runAllocationInterceptionProof();

        beginTest(scenario_);
        const auto result = runPhaseBScenario(scenario_);

        expect(result.launched, result.error);
        expect(! result.timedOut, result.error);
        expectEquals(result.exitCode, 0, result.output + "\n" + result.error);
        expect(result.report.isObject(), result.error);
        expect(reportBool(result, "pass"), result.output);
        expect(reportBool(result, "sharedMemory"), result.output);
        expect(reportBool(result, "sharedMemoryReleased"), result.output);
        expectEquals(reportInt(result, "staleOutputsAccepted"), 0, result.output);
        expectEquals(reportInt(result, "orphanWorkers"), 0, result.output);

        if (scenario_ == "audio-shared-memory")
            expect(reportBool(result, "deterministicAudio"), result.output);
        else if (scenario_ == "audio-block-matrix")
        {
            expect(reportBool(result, "blockMatrix"), result.output);
            expectEquals(reportInt(result, "deadlineMisses"), 0, result.output);
            expectEquals(reportInt(result, "submitMisses"), 0, result.output);
        }
        else if (scenario_ == "audio-variable-block")
        {
            expect(reportBool(result, "variableBlock"), result.output);
            expectEquals(reportInt(result, "deadlineMisses"), 0, result.output);
            expectEquals(reportInt(result, "submitMisses"), 0, result.output);
        }
        else if (scenario_ == "audio-zero-rt-allocation")
        {
            expect(reportBool(result, "allocationProofDelegated"), result.output);
            expectEquals(reportInt(result, "parentRtAllocations"), -1, result.output);
            expectEquals(reportInt(result, "workerRtAllocations"), -1, result.output);
        }
        else if (scenario_ == "audio-deadline-fallback")
        {
            expect(reportBool(result, "deadlineFallback"), result.output);
            expect(reportBool(result, "recoveredAfterDeadline"), result.output);
            expect(reportInt(result, "deadlineMisses") > 0, result.output);
            expect(reportInt(result, "staleOutputsRejected") > 0, result.output);
        }
        else if (scenario_ == "audio-worker-loss")
            expect(reportBool(result, "workerLossFallback"), result.output);
        else if (scenario_ == "audio-generation-reset")
        {
            expect(reportBool(result, "generationReset"), result.output);
            expect(reportInt(result, "staleOutputsRejected") > 0, result.output);
        }
        else if (scenario_ == "audio-two-worker-isolation")
            expect(reportBool(result, "twoWorkerIsolation"), result.output);
        else if (scenario_ == "audio-channel-integrity")
            expect(reportBool(result, "channelIntegrity"), result.output);
        else if (scenario_ == "audio-block-invariance")
            expect(reportBool(result, "blockInvariance"), result.output);
    }

private:
    void runAllocationInterceptionProof()
    {
        beginTest("JUCE allocation hooks wrap exact parent and worker steady-state paths");

       #if JUCE_WINDOWS && JUCE_ENABLE_ALLOCATION_HOOKS
        constexpr std::uint32_t maxChannels = 2;
        constexpr std::uint32_t maxSamples = 2048;
        const auto session = juce::Uuid().toString().removeCharacters("-");

        DAW::PluginSandboxAudioTransportCore parent;
        DAW::PluginSandboxAudioTransportCore::Configuration configuration;
        configuration.maxInputChannels = maxChannels;
        configuration.maxOutputChannels = maxChannels;
        configuration.maxSamples = maxSamples;
        configuration.nominalBlockSamples = 512;
        configuration.sampleRate = 48000.0;
        juce::String error;
        expect(parent.create(session, configuration, error), error);

        DAW::PluginWorkerCommandLine commandLine;
        commandLine.valid = true;
        commandLine.sessionToken = session;
        commandLine.audioTransportRequested = true;
        commandLine.audioMappingName = parent.mappingName();
        commandLine.audioDspMode = DAW::PluginSandboxAudioTestDspMode::Identity;
        DAW::PluginWorkerAudioTransportCore worker;
        expect(worker.open(commandLine, error), error);
        parent.markWorkerAvailable(true);
        parent.openRealtimeGate();

        std::array<std::array<float, maxSamples>, maxChannels> input {};
        std::array<std::array<float, maxSamples>, maxChannels> output {};
        std::array<const float*, maxChannels> inputPointers {
            input[0].data(), input[1].data()
        };
        std::array<float*, maxChannels> outputPointers {
            output[0].data(), output[1].data()
        };

        const auto fill = [&](std::uint32_t samples, float seed)
        {
            for (std::uint32_t channel = 0; channel < maxChannels; ++channel)
                for (std::uint32_t sample = 0; sample < samples; ++sample)
                    input[channel][sample] = seed + static_cast<float>(channel)
                                           + static_cast<float>(sample) * 0.0001f;
        };

        // Warm up every exact path before enabling the thread-local JUCE hook.
        fill(512, 1.0f);
        auto warm = parent.exchangeBlock(inputPointers.data(), maxChannels,
                                         outputPointers.data(), maxChannels,
                                         512, maxSamples);
        expect(warm.submitted);
        expect(worker.processOneAvailable());
        warm = parent.exchangeBlock(inputPointers.data(), maxChannels,
                                    outputPointers.data(), maxChannels,
                                    512, maxSamples);
        expect(warm.remoteOutput && warm.submitted);
        expect(worker.processOneAvailable());

        static constexpr std::uint32_t measuredSizes[] = {
            64, 128, 256, 480, 512, 1024, 2048,
            480, 1024, 128, 2048, 256, 512, 64, 2048
        };

        std::uint64_t expectedSequence = warm.submittedSequence + 1;
        for (std::size_t iteration = 0; iteration < std::size(measuredSizes); ++iteration)
        {
            const auto samples = measuredSizes[iteration];
            fill(samples, 10.0f + static_cast<float>(iteration));

            DAW::PluginSandboxAudioTransportCore::ExchangeResult parentResult;
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                parentResult = parent.exchangeBlock(inputPointers.data(), maxChannels,
                                                    outputPointers.data(), maxChannels,
                                                    samples, maxSamples);
            }
            expect(parentResult.submitted,
                   "parent failed to publish measured sequence "
                       + juce::String(static_cast<juce::int64>(expectedSequence)));
            expectEquals(static_cast<juce::int64>(parentResult.submittedSequence),
                         static_cast<juce::int64>(expectedSequence));

            bool workerProcessed = false;
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                workerProcessed = worker.processOneAvailable();
            }
            expect(workerProcessed,
                   "worker failed measured sequence "
                       + juce::String(static_cast<juce::int64>(expectedSequence)));
            ++expectedSequence;
        }

        parent.closeRealtimeGate();
        expect(parent.waitForRealtimeDrain(3000));
        parent.markWorkerAvailable(false);
        worker.stop();
        worker.close();
        parent.releaseAfterWorkerStopped();
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(
            "Local\\APEX.PluginSandbox.Audio." + session));
       #else
        expect(false, "JUCE allocation hooks are required for Phase B allocation proof");
       #endif
    }

    juce::String scenario_;
};

class PluginSandboxPhaseBFinalEvidenceTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseBFinalEvidenceTest()
        : juce::UnitTest("plugin.sandbox.audio-final-evidence.v1", "PluginSandbox")
    {
    }

    void runTest() override
    {
       #if JUCE_WINDOWS && JUCE_ENABLE_ALLOCATION_HOOKS
        runSpontaneousUnpolledDeathTest();
        runParentAndWorkerTimingTest();
        runIdleWorkerResourceTest();
       #else
        beginTest("Windows Phase B final evidence is available");
        expect(false, "Windows and JUCE allocation hooks are required");
       #endif
    }

private:
   #if JUCE_WINDOWS && JUCE_ENABLE_ALLOCATION_HOOKS
    static constexpr std::uint32_t kChannels = 2;
    static constexpr std::uint32_t kMaxSamples = 2048;

    struct AudioStorage
    {
        AudioStorage()
        {
            inputPointers = { input[0].data(), input[1].data() };
            outputPointers = { output[0].data(), output[1].data() };
        }

        void fillInput(std::uint32_t samples, float seed) noexcept
        {
            for (std::uint32_t channel = 0; channel < kChannels; ++channel)
                for (std::uint32_t sample = 0; sample < samples; ++sample)
                    input[channel][sample] = seed + static_cast<float>(channel)
                                           + static_cast<float>(sample) * 0.0001f;
        }

        void fillOutput(float value) noexcept
        {
            for (auto& channel : output)
                channel.fill(value);
        }

        bool outputIsDry(std::uint32_t samples) const noexcept
        {
            for (std::uint32_t channel = 0; channel < kChannels; ++channel)
                for (std::uint32_t sample = 0; sample < samples; ++sample)
                    if (output[channel][sample] != input[channel][sample])
                        return false;
            return true;
        }

        std::array<std::array<float, kMaxSamples>, kChannels> input {};
        std::array<std::array<float, kMaxSamples>, kChannels> output {};
        std::array<const float*, kChannels> inputPointers {};
        std::array<float*, kChannels> outputPointers {};
    };

    struct TimingStats
    {
        double average = 0.0;
        double median = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        double maximum = 0.0;
    };

    template <std::size_t Capacity>
    static TimingStats calculateStats(std::array<double, Capacity>& values,
                                      std::size_t count)
    {
        TimingStats result;
        if (count == 0)
            return result;

        long double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i)
            sum += values[i];
        result.average = static_cast<double>(sum / static_cast<long double>(count));

        std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
        const auto percentile = [&](double fraction)
        {
            const auto rank = static_cast<std::size_t>(
                std::ceil(fraction * static_cast<double>(count)));
            return values[juce::jmin(count - 1, rank > 0 ? rank - 1 : 0)];
        };
        result.median = percentile(0.50);
        result.p95 = percentile(0.95);
        result.p99 = percentile(0.99);
        result.maximum = values[count - 1];
        return result;
    }

    static juce::String statsText(const juce::String& label,
                                  std::size_t samples,
                                  const TimingStats& stats)
    {
        return "APEX_PHASE_B_TIMING path=" + label
             + " samples=" + juce::String(static_cast<juce::int64>(samples))
             + " average_us=" + juce::String(stats.average, 6)
             + " median_us=" + juce::String(stats.median, 6)
             + " p95_us=" + juce::String(stats.p95, 6)
             + " p99_us=" + juce::String(stats.p99, 6)
             + " max_us=" + juce::String(stats.maximum, 6);
    }

    static bool waitForSequence(DAW::PluginSandboxAudioTransportCore& transport,
                                std::uint64_t sequence,
                                DWORD timeoutMs)
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

    void runSpontaneousUnpolledDeathTest()
    {
        beginTest("worker death before health poll remains bounded and dry");

        DAW::PluginSandboxProcessCore process;
        DAW::PluginSandboxProcessCore::Options options;
        options.enableAudioTransport = true;
        options.audioConfiguration.maxInputChannels = kChannels;
        options.audioConfiguration.maxOutputChannels = kChannels;
        options.audioConfiguration.maxSamples = kMaxSamples;
        options.audioConfiguration.nominalBlockSamples = 512;
        options.audioConfiguration.sampleRate = 48000.0;
        options.audioDspMode = DAW::PluginSandboxAudioTestDspMode::DeadlineIdentity;
        options.audioDelaySequence = 2;
        options.audioDelayMilliseconds = 10000;
        options.workerExecutablePathForTest = findPhaseBDawCoreExecutable().getFullPathName();
        expect(process.start(options) == DAW::PluginSandboxProcessCore::StartResult::Started,
               process.diagnostics().error);
        if (! process.diagnostics().handshake)
            return;

        AudioStorage audio;
        audio.fillInput(512, 1.0f);
        auto first = process.audioTransport().exchangeBlock(
            audio.inputPointers.data(), kChannels,
            audio.outputPointers.data(), kChannels, 512, kMaxSamples);
        expect(first.fallback && first.submitted);
        expect(waitForSequence(process.audioTransport(), first.submittedSequence, 5000));

        audio.fillInput(512, 2.0f);
        const auto warmRemote = process.audioTransport().exchangeBlock(
            audio.inputPointers.data(), kChannels,
            audio.outputPointers.data(), kChannels, 512, kMaxSamples);
        expect(warmRemote.remoteOutput && warmRemote.submitted,
               "remote output did not become valid before external termination");

        const auto mappingName = process.diagnostics().sharedMemoryName;
        const auto generation = process.audioTransport().generation();
        DAW::PluginSandboxWin32::UniqueHandle externalProcess(OpenProcess(
            PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, process.diagnostics().workerProcessId));
        expect(externalProcess.isValid(), "OpenProcess failed for external worker kill");
        const bool externallyKilled = externalProcess.isValid()
            && TerminateProcess(externalProcess.get(), 0xA9E1F00D) != FALSE
            && WaitForSingleObject(externalProcess.get(), 5000) == WAIT_OBJECT_0;
        expect(externallyKilled, "external worker termination/reap observation failed");
        expect(DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "parent mapping disappeared before health detection");

        constexpr std::size_t postDeathCalls = 64;
        std::array<double, postDeathCalls> durations {};
        LARGE_INTEGER frequency {};
        QueryPerformanceFrequency(&frequency);
        bool boundedDry = externallyKilled;
        bool remoteAccepted = false;
        bool generationAccepted = false;
        for (std::size_t call = 0; call < postDeathCalls; ++call)
        {
            audio.fillInput(512, 10.0f + static_cast<float>(call));
            audio.fillOutput(-9876.5f);
            LARGE_INTEGER before {};
            LARGE_INTEGER after {};
            QueryPerformanceCounter(&before);
            const auto result = process.audioTransport().exchangeBlock(
                audio.inputPointers.data(), kChannels,
                audio.outputPointers.data(), kChannels, 512, kMaxSamples);
            QueryPerformanceCounter(&after);
            durations[call] = static_cast<double>(after.QuadPart - before.QuadPart)
                            * 1000000.0 / static_cast<double>(frequency.QuadPart);
            remoteAccepted = remoteAccepted || result.remoteOutput;
            generationAccepted = generationAccepted || result.completedGeneration != 0;
            boundedDry = boundedDry && result.fallback && ! result.remoteOutput
                      && audio.outputIsDry(512)
                      && process.audioTransport().generation() == generation
                      && DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName);
        }

        auto sortedDurations = durations;
        const auto stats = calculateStats(sortedDurations, postDeathCalls);
        logMessage(statsText("post-death-parent", postDeathCalls, stats));
        expect(boundedDry, "post-death exchange did not remain deterministic dry fallback");
        expect(! remoteAccepted, "post-death remote output was accepted before health polling");
        expect(! generationAccepted, "post-death completed generation was accepted");
        expect(stats.maximum < 1000000.0,
               "post-death exchange approached a blocking control timeout");

        // This is deliberately the first PluginSandboxProcessCore health action after
        // all post-death exchangeBlock calls above.
        expect(! process.pollWorkerHealth(), "health poll did not detect dead worker");
        expect(process.diagnostics().workerUnavailableDetected);
        expect(process.diagnostics().sharedMemoryRetainedAfterWorkerExit);
        expect(DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName));
        expect(process.cleanupTerminatedWorkerForTest(), process.diagnostics().error);
        expect(process.diagnostics().reaped);
        expect(process.diagnostics().sharedMemoryReleased);
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName));
        logMessage("APEX_PHASE_B_UNPOLLED_DEATH post_death_calls=64"
                   " poll_before_calls=false dry_fallback=true remote_accepted=false"
                   " generation_accepted=false mapping_retained=true reap_before_unmap=true");
    }

    void runParentAndWorkerTimingTest()
    {
        beginTest("preallocated parent and worker timing distributions");

        static constexpr std::array<std::uint32_t, 7> blockSizes {
            64, 128, 256, 480, 512, 1024, 2048
        };
        static constexpr std::size_t samplesPerSize = 1000;
        static constexpr std::size_t totalSamples = blockSizes.size() * samplesPerSize;

        DAW::PluginSandboxAudioTransportCore parent;
        DAW::PluginSandboxAudioTransportCore::Configuration configuration;
        configuration.maxInputChannels = kChannels;
        configuration.maxOutputChannels = kChannels;
        configuration.maxSamples = kMaxSamples;
        configuration.nominalBlockSamples = 512;
        configuration.sampleRate = 48000.0;
        const auto session = juce::Uuid().toString().removeCharacters("-");
        juce::String error;
        expect(parent.create(session, configuration, error), error);

        DAW::PluginWorkerCommandLine commandLine;
        commandLine.valid = true;
        commandLine.sessionToken = session;
        commandLine.audioTransportRequested = true;
        commandLine.audioMappingName = parent.mappingName();
        commandLine.audioDspMode = DAW::PluginSandboxAudioTestDspMode::Identity;
        DAW::PluginWorkerAudioTransportCore worker;
        expect(worker.open(commandLine, error), error);
        parent.markWorkerAvailable(true);
        parent.openRealtimeGate();

        AudioStorage audio;
        audio.fillInput(512, 1.0f);
        auto warm = parent.exchangeBlock(audio.inputPointers.data(), kChannels,
                                         audio.outputPointers.data(), kChannels,
                                         512, kMaxSamples);
        expect(warm.submitted && worker.processOneAvailable());
        warm = parent.exchangeBlock(audio.inputPointers.data(), kChannels,
                                    audio.outputPointers.data(), kChannels,
                                    512, kMaxSamples);
        expect(warm.remoteOutput && warm.submitted && worker.processOneAvailable());

        std::array<double, totalSamples> parentMicros {};
        std::array<double, totalSamples> workerMicros {};
        LARGE_INTEGER frequency {};
        QueryPerformanceFrequency(&frequency);
        bool valid = true;
        std::size_t measurement = 0;
        for (const auto blockSize : blockSizes)
        {
            for (std::size_t iteration = 0; iteration < samplesPerSize; ++iteration)
            {
                audio.fillInput(blockSize,
                                20.0f + static_cast<float>(measurement % 100));
                LARGE_INTEGER before {};
                LARGE_INTEGER after {};
                QueryPerformanceCounter(&before);
                const auto parentResult = parent.exchangeBlock(
                    audio.inputPointers.data(), kChannels,
                    audio.outputPointers.data(), kChannels,
                    blockSize, kMaxSamples);
                QueryPerformanceCounter(&after);
                parentMicros[measurement] =
                    static_cast<double>(after.QuadPart - before.QuadPart)
                    * 1000000.0 / static_cast<double>(frequency.QuadPart);

                QueryPerformanceCounter(&before);
                const bool workerProcessed = worker.processOneAvailable();
                QueryPerformanceCounter(&after);
                workerMicros[measurement] =
                    static_cast<double>(after.QuadPart - before.QuadPart)
                    * 1000000.0 / static_cast<double>(frequency.QuadPart);
                valid = valid && parentResult.submitted && parentResult.remoteOutput
                              && workerProcessed;
                ++measurement;
            }
        }
        expect(valid, "normal timing run lost a parent or worker transport operation");
        const auto counters = parent.counters();
        expectEquals(static_cast<juce::int64>(counters.submitMisses), juce::int64(0));
        expectEquals(static_cast<juce::int64>(counters.deadlineMisses), juce::int64(0));

        std::array<double, samplesPerSize> scratch {};
        for (std::size_t sizeIndex = 0; sizeIndex < blockSizes.size(); ++sizeIndex)
        {
            const auto start = sizeIndex * samplesPerSize;
            std::copy_n(parentMicros.begin() + static_cast<std::ptrdiff_t>(start),
                        samplesPerSize, scratch.begin());
            const auto parentStats = calculateStats(scratch, samplesPerSize);
            logMessage(statsText("parent-" + juce::String(blockSizes[sizeIndex]),
                                 samplesPerSize, parentStats));

            std::copy_n(workerMicros.begin() + static_cast<std::ptrdiff_t>(start),
                        samplesPerSize, scratch.begin());
            const auto workerStats = calculateStats(scratch, samplesPerSize);
            logMessage(statsText("worker-" + juce::String(blockSizes[sizeIndex]),
                                 samplesPerSize, workerStats));
        }

        const auto parentStats = calculateStats(parentMicros, totalSamples);
        const auto workerStats = calculateStats(workerMicros, totalSamples);
        logMessage(statsText("parent-all", totalSamples, parentStats));
        logMessage(statsText("worker-all", totalSamples, workerStats));

        parent.closeRealtimeGate();
        expect(parent.waitForRealtimeDrain(3000));
        parent.markWorkerAvailable(false);
        worker.stop();
        worker.close();
        const auto mappingName = parent.mappingName();
        parent.releaseAfterWorkerStopped();
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName));
    }

    static std::uint64_t fileTimeValue(const FILETIME& value) noexcept
    {
        ULARGE_INTEGER combined {};
        combined.LowPart = value.dwLowDateTime;
        combined.HighPart = value.dwHighDateTime;
        return combined.QuadPart;
    }

    void runIdleWorkerResourceTest()
    {
        beginTest("external idle worker CPU and RAM characterization");

        DAW::PluginSandboxProcessCore process;
        DAW::PluginSandboxProcessCore::Options options;
        options.enableAudioTransport = true;
        options.audioConfiguration.maxInputChannels = kChannels;
        options.audioConfiguration.maxOutputChannels = kChannels;
        options.audioConfiguration.maxSamples = kMaxSamples;
        options.audioConfiguration.nominalBlockSamples = 512;
        options.audioConfiguration.sampleRate = 48000.0;
        options.workerExecutablePathForTest = findPhaseBDawCoreExecutable().getFullPathName();
        expect(process.start(options) == DAW::PluginSandboxProcessCore::StartResult::Started,
               process.diagnostics().error);
        if (! process.diagnostics().handshake)
            return;

        DAW::PluginSandboxWin32::UniqueHandle externalProcess(OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
            FALSE, process.diagnostics().workerProcessId));
        expect(externalProcess.isValid(), "OpenProcess failed for resource sampling");
        if (! externalProcess.isValid())
            return;

        Sleep(500);
        FILETIME creation {};
        FILETIME exit {};
        FILETIME kernelBefore {};
        FILETIME userBefore {};
        FILETIME kernelAfter {};
        FILETIME userAfter {};
        PROCESS_MEMORY_COUNTERS_EX memory {};
        memory.cb = sizeof(memory);
        const bool beforeOk = GetProcessTimes(externalProcess.get(), &creation, &exit,
                                              &kernelBefore, &userBefore) != FALSE;
        LARGE_INTEGER frequency {};
        LARGE_INTEGER wallBefore {};
        LARGE_INTEGER wallAfter {};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&wallBefore);
        constexpr DWORD intervalMs = 3000;
        Sleep(intervalMs);
        QueryPerformanceCounter(&wallAfter);
        const bool afterOk = GetProcessTimes(externalProcess.get(), &creation, &exit,
                                             &kernelAfter, &userAfter) != FALSE;
        const bool memoryOk = K32GetProcessMemoryInfo(
            externalProcess.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory)) != FALSE;
        expect(beforeOk && afterOk, "GetProcessTimes failed");
        expect(memoryOk, "K32GetProcessMemoryInfo failed");

        const auto cpuTicks = (fileTimeValue(kernelAfter) - fileTimeValue(kernelBefore))
                            + (fileTimeValue(userAfter) - fileTimeValue(userBefore));
        const auto cpuSeconds = static_cast<double>(cpuTicks) / 10000000.0;
        const auto wallSeconds = static_cast<double>(wallAfter.QuadPart - wallBefore.QuadPart)
                               / static_cast<double>(frequency.QuadPart);
        SYSTEM_INFO systemInfo {};
        GetSystemInfo(&systemInfo);
        const auto logicalProcessors = juce::jmax<DWORD>(1, systemInfo.dwNumberOfProcessors);
        const auto singleCorePercent = wallSeconds > 0.0
            ? cpuSeconds * 100.0 / wallSeconds : 0.0;
        const auto normalizedPercent = singleCorePercent
                                     / static_cast<double>(logicalProcessors);

        logMessage("APEX_PHASE_B_IDLE interval_ms=" + juce::String(intervalMs)
                   + " cpu_delta_seconds=" + juce::String(cpuSeconds, 6)
                   + " single_core_percent=" + juce::String(singleCorePercent, 6)
                   + " normalized_percent=" + juce::String(normalizedPercent, 6)
                   + " logical_processors=" + juce::String(logicalProcessors)
                   + " working_set_bytes="
                   + juce::String(static_cast<juce::int64>(memory.WorkingSetSize))
                   + " private_bytes="
                   + juce::String(static_cast<juce::int64>(memory.PrivateUsage))
                   + " shared_mapping_bytes="
                   + juce::String(static_cast<juce::int64>(
                         process.diagnostics().sharedMemoryBytes)));

        const auto mappingName = process.diagnostics().sharedMemoryName;
        expect(process.shutdown(3000), process.diagnostics().error);
        expect(process.diagnostics().reaped);
        expect(process.diagnostics().sharedMemoryReleased);
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName));
    }
   #endif
};

class PluginSandboxAudioDepthWindowTest final : public juce::UnitTest
{
public:
    PluginSandboxAudioDepthWindowTest()
        : juce::UnitTest("plugin.sandbox.audio-depth-window.v1", "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("depth-aware transport preserves back-to-back quanta (d=1,2,4)");
        runDepthPreservationCase(1);
        runDepthPreservationCase(2);
        runDepthPreservationCase(4);

        beginTest("depth-4 deterministic miss and stale-window discard");
        runDepthMissCase(4);
       #else
        beginTest("Windows-only depth window contract");
        expect(true);
       #endif
    }

private:
    struct DepthHarness
    {
        DAW::PluginSandboxAudioTransportCore parent;
        DAW::PluginWorkerAudioTransportCore worker;
        juce::String session;

        bool setup(std::uint32_t depth, std::uint32_t quantum, juce::String& error)
        {
            session = juce::Uuid().toString().removeCharacters("-");
            DAW::PluginSandboxAudioTransportCore::Configuration configuration;
            configuration.maxInputChannels = 2;
            configuration.maxOutputChannels = 2;
            configuration.maxSamples = quantum;
            configuration.nominalBlockSamples = quantum;
            configuration.sampleRate = 48000.0;
            configuration.outstandingQuantumDepth = depth;
            if (! parent.create(session, configuration, error))
                return false;

            DAW::PluginWorkerCommandLine commandLine;
            commandLine.valid = true;
            commandLine.sessionToken = session;
            commandLine.audioTransportRequested = true;
            commandLine.audioMappingName = parent.mappingName();
            commandLine.audioDspMode = DAW::PluginSandboxAudioTestDspMode::Identity;
            if (! worker.open(commandLine, error))
                return false;
            parent.markWorkerAvailable(true);
            parent.openRealtimeGate();
            return true;
        }

        void teardown()
        {
            parent.closeRealtimeGate();
            parent.waitForRealtimeDrain(3000);
            parent.markWorkerAvailable(false);
            worker.stop();
            worker.close();
            parent.releaseAfterWorkerStopped();
        }
    };

    void fillQuantum(std::array<std::array<float, 2048>, 2>& input,
                     std::uint32_t quantum, std::uint32_t quantumIndex)
    {
        const float seed = 1.0f + static_cast<float>(quantumIndex) * 10.0f;
        for (std::uint32_t channel = 0; channel < 2; ++channel)
            for (std::uint32_t sample = 0; sample < quantum; ++sample)
                input[channel][sample] = seed + static_cast<float>(channel) * 100.0f
                                       + static_cast<float>(sample) * 0.0001f;
    }

    void runDepthPreservationCase(std::uint32_t depth)
    {
        constexpr std::uint32_t kQuantum = 512;
        DepthHarness harness;
        juce::String error;
        expect(harness.setup(depth, kQuantum, error),
               "depth " + juce::String(depth) + " setup failed: " + error);
        if (! harness.parent.isPrepared())
            return;

        std::array<std::array<float, 2048>, 2> input {};
        std::array<std::array<float, 2048>, 2> output {};
        std::array<const float*, 2> inputPointers { input[0].data(), input[1].data() };
        std::array<float*, 2> outputPointers { output[0].data(), output[1].data() };

        // One host callback: submit `depth` quanta back-to-back with no worker
        // servicing in between. No result may be consumed yet.
        for (std::uint32_t q = 0; q < depth; ++q)
        {
            fillQuantum(input, kQuantum, q);
            const auto result = harness.parent.exchangeBlock(
                inputPointers.data(), 2, outputPointers.data(), 2,
                kQuantum, 2048);
            expect(result.submitted,
                   "depth " + juce::String(depth) + " submit failed at quantum "
                       + juce::String(q + 1));
            expect(! result.remoteOutput,
                   "depth " + juce::String(depth) + " consumed before its resolution point");
        }

        // Worker services every outstanding quantum during the inter-callback
        // gap (deterministic in-process servicing; models >= 1 ms completion).
        for (std::uint32_t q = 0; q < depth; ++q)
            expect(harness.worker.processOneAvailable(),
                   "worker failed to service outstanding quantum "
                       + juce::String(q + 1) + " at depth " + juce::String(depth));

        // Next host callback: each exchange must resolve its k-depth quantum
        // remotely with the exact sequence and content — the historical
        // impossible case now preserved with zero misses and zero stale drops.
        std::uint64_t expectedRemote = 1;
        for (std::uint32_t q = depth; q < 2 * depth; ++q)
        {
            fillQuantum(input, kQuantum, q);
            const auto result = harness.parent.exchangeBlock(
                inputPointers.data(), 2, outputPointers.data(), 2,
                kQuantum, 2048);
            expect(result.submitted, "depth " + juce::String(depth) + " follow-up submit failed");
            expect(result.remoteOutput,
                   "depth " + juce::String(depth) + " lost quantum "
                       + juce::String(static_cast<juce::int64>(expectedRemote))
                       + " inside the outstanding window");
            expectEquals(static_cast<juce::int64>(result.completedSequence),
                         static_cast<juce::int64>(expectedRemote));
            expectEquals(static_cast<juce::int64>(result.outputSamples),
                         static_cast<juce::int64>(kQuantum));
            expectEquals(static_cast<juce::int64>(result.outputChannels),
                         juce::int64(2));

            const float expectedSeed = 1.0f + static_cast<float>(expectedRemote - 1) * 10.0f;
            expectWithinAbsoluteError(output[0][0], expectedSeed, 0.0002f,
                                      "slot collision returned the wrong quantum content");
            expectWithinAbsoluteError(output[1][0], expectedSeed + 100.0f, 0.0002f,
                                      "slot collision corrupted channel 1 content");
            ++expectedRemote;

            expect(harness.worker.processOneAvailable(),
                   "worker failed to service follow-up quantum");
        }

        const auto counters = harness.parent.counters();
        expectEquals(static_cast<juce::int64>(counters.deadlineMisses), juce::int64(0));
        expectEquals(static_cast<juce::int64>(counters.staleOutputsRejected), juce::int64(0));
        expectEquals(static_cast<juce::int64>(counters.submitMisses), juce::int64(0));
        expectEquals(static_cast<juce::int64>(counters.completed), juce::int64(depth));

        const auto mappingName = harness.parent.mappingName();
        harness.teardown();
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "depth window mapping was not released");
    }

    void runDepthMissCase(std::uint32_t depth)
    {
        constexpr std::uint32_t kQuantum = 512;
        DepthHarness harness;
        juce::String error;
        expect(harness.setup(depth, kQuantum, error), error);
        if (! harness.parent.isPrepared())
            return;

        std::array<std::array<float, 2048>, 2> input {};
        std::array<std::array<float, 2048>, 2> output {};
        std::array<const float*, 2> inputPointers { input[0].data(), input[1].data() };
        std::array<float*, 2> outputPointers { output[0].data(), output[1].data() };

        // Submit four quanta with NO worker servicing: quantum 1 will
        // genuinely miss its deterministic resolution point.
        for (std::uint32_t q = 0; q < depth; ++q)
        {
            fillQuantum(input, kQuantum, q);
            const auto result = harness.parent.exchangeBlock(
                inputPointers.data(), 2, outputPointers.data(), 2,
                kQuantum, 2048);
            expect(result.submitted);
        }

        // Exchange depth+1: quantum 1's resolution point — it is unavailable.
        fillQuantum(input, kQuantum, depth);
        auto result = harness.parent.exchangeBlock(
            inputPointers.data(), 2, outputPointers.data(), 2,
            kQuantum, 2048);
        expect(result.submitted);
        expect(! result.remoteOutput,
               "unserviced quantum resolved remotely before the worker ran");
        expectEquals(static_cast<juce::int64>(
                         harness.parent.counters().deadlineMisses),
                     juce::int64(1));

        // Worker completes every quantum LATE (after quantum 1's window).
        for (std::uint32_t q = 0; q < depth + 1; ++q)
            expect(harness.worker.processOneAvailable(),
                   "late worker servicing failed at quantum " + juce::String(q + 1));

        // Exchange depth+2 consumes quantum 2 remotely; quantum 1's late result
        // is now outside the window and must be discarded as stale.
        fillQuantum(input, kQuantum, depth + 1);
        result = harness.parent.exchangeBlock(
            inputPointers.data(), 2, outputPointers.data(), 2,
            kQuantum, 2048);
        expect(result.submitted);
        expect(result.remoteOutput, "quantum 2 should resolve remotely");
        expectEquals(static_cast<juce::int64>(result.completedSequence), juce::int64(2));

        const auto counters = harness.parent.counters();
        expect(counters.staleOutputsRejected >= 1,
               "late quantum-1 result was not discarded once outside its window");
        expectEquals(static_cast<juce::int64>(counters.deadlineMisses), juce::int64(1));

        const auto mappingName = harness.parent.mappingName();
        harness.teardown();
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "depth miss-case mapping was not released");
    }
};

PluginSandboxAudioDepthWindowTest audioDepthWindowTest;

PluginSandboxPhaseBTest sharedMemoryTest(
    "plugin.sandbox.audio-shared-memory.v1", "audio-shared-memory");PluginSandboxPhaseBTest blockMatrixTest(
    "plugin.sandbox.audio-block-matrix.v1", "audio-block-matrix");
PluginSandboxPhaseBTest variableBlockTest(
    "plugin.sandbox.audio-variable-block.v1", "audio-variable-block");
PluginSandboxPhaseBTest zeroRtAllocationTest(
    "plugin.sandbox.audio-zero-rt-allocation.v1", "audio-zero-rt-allocation");
PluginSandboxPhaseBTest deadlineFallbackTest(
    "plugin.sandbox.audio-deadline-fallback.v1", "audio-deadline-fallback");
PluginSandboxPhaseBTest workerLossTest(
    "plugin.sandbox.audio-worker-loss.v1", "audio-worker-loss");
PluginSandboxPhaseBTest generationResetTest(
    "plugin.sandbox.audio-generation-reset.v1", "audio-generation-reset");
PluginSandboxPhaseBTest twoWorkerIsolationTest(
    "plugin.sandbox.audio-two-worker-isolation.v1", "audio-two-worker-isolation");
PluginSandboxPhaseBTest channelIntegrityTest(
    "plugin.sandbox.audio-channel-integrity.v1", "audio-channel-integrity");
PluginSandboxPhaseBTest blockInvarianceTest(
    "plugin.sandbox.audio-block-invariance.v1", "audio-block-invariance");
PluginSandboxPhaseBFinalEvidenceTest finalEvidenceTest;
} // namespace
