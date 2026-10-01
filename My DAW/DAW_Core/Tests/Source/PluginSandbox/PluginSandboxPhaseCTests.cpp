#include <JuceHeader.h>

#include "../../../Source/PluginSandboxCore/PluginSandboxProcessCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxDiagnosticsCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxEnumOnlyDiagnosticCore.h"
#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"
#include "../../../Source/PluginSandboxCore/PluginWorkerHostedPluginCore.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
juce::File productionWorkerExecutable()
{
    const auto testExecutable = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getFullPathName();
    const juce::String configuration = testExecutable.containsIgnoreCase("\\Release\\")
        ? "Release" : "Debug";

    auto cursor = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    for (int level = 0; level < 8; ++level)
    {
        const auto candidate = cursor.getChildFile("Builds")
            .getChildFile("VisualStudio2026")
            .getChildFile("x64")
            .getChildFile(configuration)
            .getChildFile("App")
            .getChildFile("DAW_Core.exe");
        if (candidate.existsAsFile())
            return candidate;
        cursor = cursor.getParentDirectory();
    }
    return {};
}

// JUCE derives these PluginDescription IDs from the VST3 component CID
// ABCDEF019182FAEB4150585441545633. They are cached metadata; the parent does
// not load the module to obtain them.
constexpr int kFixtureUniqueId = 2021071337;
constexpr int kFixtureDeprecatedUid = 1612553221;
constexpr int kFixtureLatencySamples = 32;
constexpr int kBlockSamples = 512;

juce::PluginDescription fixtureDescription()
{
    const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_VST3_PATH", {}));
    juce::PluginDescription description;
    description.name = "APEX Test VST3";
    description.descriptiveName = description.name;
    description.manufacturerName = "APEX";
    description.version = "1.0.0";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = fixture.getFullPathName();
    description.uniqueId = kFixtureUniqueId;
    description.deprecatedUid = kFixtureDeprecatedUid;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
    return description;
}

class PluginSandboxVst3PathFormTest final : public juce::UnitTest
{
public:
    PluginSandboxVst3PathFormTest()
        : juce::UnitTest("plugin.sandbox.vst3-path-form.v1", "PluginSandbox") {}

    void runTest() override
    {
        beginTest("VST3 path form accepts directories and regular files, rejects invalid paths");

        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("APEX-PluginSandbox-VST3PathForm-"
                          + juce::String(juce::Time::getHighResolutionTicks()));
        struct DirectoryGuard
        {
            juce::File directory;
            ~DirectoryGuard() { directory.deleteRecursively(); }
        } guard { root };

        expect(root.createDirectory().wasOk(),
               "could not create temporary VST3 path-form test directory");
        if (! root.isDirectory())
            return;

        const auto validFormat = juce::String("VST3");
        const auto validName = juce::String("Some Plugin");
        const auto validDirectory = root.getChildFile("SomePlugin.vst3");
        const auto validFile = root.getChildFile("SomePluginFile.vst3");
        const auto uppercaseFilePath = validFile.getFullPathName().dropLastCharacters(5)
            + ".VST3";
        const auto wrongExtensionFile = root.getChildFile("SomePlugin.txt");
        const auto wrongExtensionDirectory = root.getChildFile("SomePlugin");
        const auto missingVst3 = root.getChildFile("MissingPlugin.vst3");

        expect(validDirectory.createDirectory().wasOk(),
               "could not create directory-form VST3 fixture");
        expect(validFile.replaceWithText("not a VST3 binary"),
               "could not create regular-file VST3 fixture");
        expect(wrongExtensionFile.replaceWithText("not a VST3 binary"),
               "could not create wrong-extension file fixture");
        expect(wrongExtensionDirectory.createDirectory().wasOk(),
               "could not create wrong-extension directory fixture");

        expect(DAW::PluginWorkerHostedPluginCore::isValidVst3PathForm(
                    validDirectory.getFullPathName(), validFormat, validName),
               "directory-form .vst3 path was rejected");
        expect(DAW::PluginWorkerHostedPluginCore::isValidVst3PathForm(
                    validFile.getFullPathName(), validFormat, validName),
               "regular-file .vst3 path was rejected before binary validation");
        expect(DAW::PluginWorkerHostedPluginCore::isValidVst3PathForm(
                    uppercaseFilePath, validFormat, validName),
               "uppercase .VST3 extension was rejected on Windows");

        expect(! DAW::PluginWorkerHostedPluginCore::isValidVst3PathForm(
                    missingVst3.getFullPathName(), validFormat, validName),
               "nonexistent .vst3 path was accepted");
        expect(! DAW::PluginWorkerHostedPluginCore::isValidVst3PathForm(
                    wrongExtensionFile.getFullPathName(), validFormat, validName),
               "wrong-extension regular file was accepted");
        expect(! DAW::PluginWorkerHostedPluginCore::isValidVst3PathForm(
                    wrongExtensionDirectory.getFullPathName(), validFormat, validName),
               "wrong-extension directory was accepted");
        expect(! DAW::PluginWorkerHostedPluginCore::isValidVst3PathForm(
                    validFile.getFullPathName(), "VST2", validName),
               "non-VST3 format was accepted");
        expect(! DAW::PluginWorkerHostedPluginCore::isValidVst3PathForm(
                    validFile.getFullPathName(), validFormat, {}),
               "empty plugin identity was accepted");
    }
};

class PluginSandboxEnumOnlyDiagnosticTest final : public juce::UnitTest
{
public:
    PluginSandboxEnumOnlyDiagnosticTest()
        : juce::UnitTest("plugin.sandbox.enum-only-diagnostic.v1", "PluginSandbox") {}

    void runTest() override
    {
        beginTest("enum-only command line parser is explicit and bounded");
        {
            juce::StringArray arguments {
                "--apex-plugin-worker",
                "--apex-vst3-enum-diagnostic",
                "--apex-enum-only=C:\\APEX\\Fixture.vst3",
                "--apex-enum-ceiling-ms=120000"
            };
            const auto parsed = DAW::PluginSandboxCommandLineCore::parseEnumOnly(arguments);
            expect(parsed.requested, "enum-only mode was not recognized");
            expect(parsed.valid, parsed.error);
            expectEquals(parsed.pluginPath,
                         juce::String("C:\\APEX\\Fixture.vst3"));
            expectEquals(static_cast<juce::int64>(parsed.ceilingMilliseconds),
                         static_cast<juce::int64>(120000));
        }

        {
            juce::StringArray arguments {
                "--apex-plugin-worker",
                "--apex-vst3-enum-diagnostic",
                "--apex-enum-only",
                "C:\\APEX\\Fixture.vst3"
            };
            const auto parsed = DAW::PluginSandboxCommandLineCore::parseEnumOnly(arguments);
            expect(parsed.valid, parsed.error);
            expectEquals(static_cast<juce::int64>(parsed.ceilingMilliseconds),
                         static_cast<juce::int64>(
                             DAW::PluginSandboxCommandLineCore::kDefaultEnumOnlyCeilingMilliseconds));
        }

        {
            juce::StringArray missingPath {
                "--apex-plugin-worker",
                "--apex-vst3-enum-diagnostic",
                "--apex-enum-only",
                "--apex-enum-ceiling-ms=120000"
            };
            const auto parsed = DAW::PluginSandboxCommandLineCore::parseEnumOnly(missingPath);
            expect(parsed.requested, "missing-path input was not recognized as a request");
            expect(! parsed.valid, "missing enum-only path was accepted");

            juce::StringArray missingDiagnostic {
                "--apex-plugin-worker",
                "--apex-enum-only=C:\\APEX\\Fixture.vst3"
            };
            const auto withoutDiagnostic =
                DAW::PluginSandboxCommandLineCore::parseEnumOnly(missingDiagnostic);
            expect(! withoutDiagnostic.valid,
                   "enum-only mode was accepted without the diagnostic flag");

            juce::StringArray excessiveCeiling {
                "--apex-plugin-worker",
                "--apex-vst3-enum-diagnostic",
                "--apex-enum-only=C:\\APEX\\Fixture.vst3",
                "--apex-enum-ceiling-ms=600001"
            };
            const auto tooLarge =
                DAW::PluginSandboxCommandLineCore::parseEnumOnly(excessiveCeiling);
            expect(! tooLarge.valid, "enum-only ceiling exceeded its diagnostic bound");
        }

        beginTest("normal PluginCreate startup deadline remains five seconds");
        expectEquals(static_cast<juce::int64>(
                          DAW::PluginSandboxProcessCore::Options {}.startupTimeoutMs),
                      static_cast<juce::int64>(5000));
        beginTest("PluginCreate has a separate bounded sixty-second deadline");
        expectEquals(static_cast<juce::int64>(
                          DAW::PluginSandboxProcessCore::Options {}.pluginCreateTimeoutMs),
                      static_cast<juce::int64>(60000));

       #if JUCE_WINDOWS
        beginTest("enum-only mode launches the production worker entry with a fixture");
        const auto fixture = juce::File(juce::SystemStats::getEnvironmentVariable(
            "APEX_TEST_VST3_PATH", {}));
        const auto worker = productionWorkerExecutable();
        expect(fixture.isDirectory(),
               "deterministic VST3 fixture is missing: " + fixture.getFullPathName());
        expect(worker.existsAsFile(),
               "production DAW_Core executable is missing: " + worker.getFullPathName());
        if (fixture.isDirectory() && worker.existsAsFile())
        {
            juce::ChildProcess process;
            juce::StringArray arguments {
                worker.getFullPathName(),
                "--apex-plugin-worker",
                "--apex-vst3-enum-diagnostic",
                "--apex-enum-only",
                fixture.getFullPathName(),
                "--apex-enum-ceiling-ms=15000"
            };
            expect(process.start(arguments,
                                 juce::ChildProcess::wantStdOut
                                   | juce::ChildProcess::wantStdErr),
                   "could not launch enum-only worker diagnostic");
            if (process.isRunning())
            {
                expect(process.waitForProcessToFinish(30000),
                       "enum-only fixture diagnostic exceeded its test bound");
                const auto output = process.readAllProcessOutput();
                expectEquals(static_cast<int>(process.getExitCode()),
                             0,
                             output);
            }

            const auto diagnosticLog = DAW::PluginScanAuditLogCore::getLogFile(
                "vst3_enumeration_diagnostic.log").loadFileAsString();
            expect(diagnosticLog.contains(
                       "[VST3-ENUM-HARNESS] event=ENUMERATION_HARNESS_RESULT state=Success"),
                   "enum-only fixture did not report success");
            expect(diagnosticLog.contains("context=SANDBOX_WORKER"),
                   "enum-only fixture was not labeled as a sandbox worker");
            expect(diagnosticLog.contains("descriptions=1"),
                   "enum-only fixture did not report one description");
        }

        beginTest("enum-only mode reports a missing path explicitly");
        if (worker.existsAsFile())
        {
            const auto missing = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("APEX-Missing-EnumOnly-Target.vst3");
            missing.deleteFile();

            juce::ChildProcess process;
            juce::StringArray arguments {
                worker.getFullPathName(),
                "--apex-plugin-worker",
                "--apex-vst3-enum-diagnostic",
                "--apex-enum-only",
                missing.getFullPathName(),
                "--apex-enum-ceiling-ms=15000"
            };
            expect(process.start(arguments,
                                 juce::ChildProcess::wantStdOut
                                   | juce::ChildProcess::wantStdErr),
                   "could not launch missing-path enum-only diagnostic");
            if (process.isRunning())
            {
                expect(process.waitForProcessToFinish(30000),
                       "missing-path enum-only diagnostic exceeded its test bound");
                const auto output = process.readAllProcessOutput();
                expectEquals(static_cast<int>(process.getExitCode()),
                             DAW::PluginSandboxEnumOnlyDiagnosticCore::kNoDescriptionsExitCode,
                             output);
            }

            const auto diagnosticLog = DAW::PluginScanAuditLogCore::getLogFile(
                "vst3_enumeration_diagnostic.log").loadFileAsString();
            expect(diagnosticLog.contains(
                       "[VST3-ENUM-HARNESS] event=ENUMERATION_HARNESS_RESULT state=Failure"),
                   "missing-path enum-only diagnostic did not report failure");
            expect(diagnosticLog.contains("reason=PathDoesNotExist"),
                   "missing-path enum-only diagnostic did not identify the missing path");
        }
       #else
        beginTest("Windows-only enum-only worker diagnostic");
        expect(true);
       #endif
    }
};

class PluginSandboxPhaseCTestBase : public juce::UnitTest
{
public:
    explicit PluginSandboxPhaseCTestBase(const juce::String& name)
        : juce::UnitTest(name, "PluginSandbox")
    {
    }

protected:
    DAW::PluginSandboxProcessCore::Options fixtureOptions() const
    {
        const auto worker = productionWorkerExecutable();
        DAW::PluginSandboxProcessCore::Options options;
        options.enableAudioTransport = true;
        options.createVst3Plugin = true;
        options.pluginDescription = fixtureDescription();
        options.audioConfiguration.maxInputChannels = 2;
        options.audioConfiguration.maxOutputChannels = 2;
        options.audioConfiguration.maxSamples = kBlockSamples;
        options.audioConfiguration.nominalBlockSamples = kBlockSamples;
        options.audioConfiguration.sampleRate = 48000.0;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        options.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        return options;
    }

    void useTestWorkerForDiagnostic(
        DAW::PluginSandboxProcessCore::Options& options) const
    {
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        const auto injectedWorker = juce::SystemStats::getEnvironmentVariable(
            "APEX_TEST_PLUGIN_WORKER_PATH", {});
        if (injectedWorker.isNotEmpty())
            options.workerExecutablePathForTest = injectedWorker;
       #else
        juce::ignoreUnused(options);
       #endif
    }

    void expectAuditPhase(const DAW::PluginSandboxProcessCore::Diagnostics& diagnostics,
                          const char* phase,
                          std::uint32_t processId,
                          std::uint64_t generation)
    {
        const auto needle = juce::String("op=PluginCreate phase=") + phase
            + " pid=" + juce::String(static_cast<juce::int64>(processId))
            + " generation=" + juce::String(static_cast<juce::int64>(generation))
            + " session=" + diagnostics.sessionToken;
        const auto log = DAW::PluginScanAuditLogCore::getLogFile(
            "plugin_ui_flow.log").loadFileAsString();
        expect(log.contains(needle), "missing PluginCreate audit phase: " + needle);
    }

    void expectNormalPluginCreateMilestones(
        const DAW::PluginSandboxProcessCore::Diagnostics& diagnostics)
    {
        static constexpr const char* parentPhases[] {
            "PLUGINCREATE_CONTEXT",
            "PLUGINCREATE_WRITE_BEGIN",
            "PLUGINCREATE_WRITE_COMPLETE",
            "PLUGINCREATE_RESPONSE_READ_BEGIN",
            "PLUGINCREATE_RESPONSE_READ_COMPLETE",
            "PLUGINCREATE_COMPLETE"
        };

        const auto log = DAW::PluginScanAuditLogCore::getLogFile(
            "plugin_ui_flow.log").loadFileAsString();
        int previous = -1;
        for (const auto* phase : parentPhases)
        {
            const auto needle = juce::String("op=PluginCreate phase=") + phase
                + " pid=" + juce::String(static_cast<juce::int64>(
                    diagnostics.pluginCreateWorkerProcessId))
                + " generation=" + juce::String(static_cast<juce::int64>(
                    diagnostics.pluginCreateWorkerGeneration))
                + " session=" + diagnostics.sessionToken;
            const auto position = log.indexOf(needle);
            expect(position >= 0, "missing parent PluginCreate milestone: " + needle);
            expect(position > previous, "parent PluginCreate milestones are out of order: " + needle);
            previous = position;
        }

        expectAuditPhase(diagnostics, "PLUGINCREATE_RECEIVED",
                          diagnostics.workerProcessId, diagnostics.audioGeneration);
        expectAuditPhase(diagnostics, "VST3_ENUMERATION_COMPLETE",
                          diagnostics.workerProcessId, diagnostics.audioGeneration);
        expectAuditPhase(diagnostics, "PARAMETER_BIND_COMPLETE",
                          diagnostics.workerProcessId, diagnostics.audioGeneration);
        expectAuditPhase(diagnostics, "PLUGINCREATE_RESPONSE_SEND_COMPLETE",
                          diagnostics.workerProcessId, diagnostics.audioGeneration);
    }

    bool startFixture(DAW::PluginSandboxProcessCore& process)
    {
        const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
            "APEX_TEST_VST3_PATH", {}));
        expect(fixture.isDirectory(),
               "fixture bundle is missing: " + fixture.getFullPathName());
        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(),
               "production worker executable is missing: " + worker.getFullPathName());
        if (! fixture.isDirectory() || ! worker.existsAsFile())
            return false;

        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module was already loaded in the parent test process");

        const auto options = fixtureOptions();

        const auto start = process.start(options);
        const auto& diagnostics = process.diagnostics();
        expectEquals(static_cast<int>(start),
                     static_cast<int>(DAW::PluginSandboxProcessCore::StartResult::Started),
                     diagnostics.error);
        expect(diagnostics.sameExecutable, "worker is not the requested DAW_Core executable");
        expect(diagnostics.hostedPluginCreated, diagnostics.error);
        expectEquals(diagnostics.hostedPluginUniqueId, kFixtureUniqueId,
                     "worker resolved a different VST3 component identity");
        expectEquals(diagnostics.hostedPluginDeprecatedUid, kFixtureDeprecatedUid,
                     "worker resolved a different deprecated VST3 identity");
        expectEquals(diagnostics.hostedPluginInputChannels, 2,
                     "worker negotiated a non-stereo input layout");
        expectEquals(diagnostics.hostedPluginOutputChannels, 2,
                     "worker negotiated a non-stereo output layout");
        expectEquals(diagnostics.hostedPluginLatencySamples, kFixtureLatencySamples,
                     "fixture latency was not reported by the worker");
        expectEquals(diagnostics.hostedPluginLatencySamples
                         + process.audioTransport().getTransportLatencySamples(),
                      kFixtureLatencySamples + kBlockSamples,
                      "effective sandbox latency must include plugin plus transport");
        expect(diagnostics.pluginCreateFailurePhase.isEmpty(),
               "successful PluginCreate retained a failure phase");
        expectEquals(diagnostics.pluginCreateLastPhase,
                     juce::String("PLUGINCREATE_COMPLETE"));
        expectEquals(diagnostics.pluginCreateWorkerProcessId,
                     diagnostics.workerProcessId);
        expect(diagnostics.pluginCreateWorkerGeneration > 0,
               "PluginCreate diagnostics did not record a worker generation");
        expectNormalPluginCreateMilestones(diagnostics);
        return start == DAW::PluginSandboxProcessCore::StartResult::Started;
    }

    void expectCleanShutdown(DAW::PluginSandboxProcessCore& process)
    {
        expect(process.shutdown(5000), process.diagnostics().error);
        const auto& diagnostics = process.diagnostics();
        expect(diagnostics.reaped, "worker was not reaped");
        expectEquals(diagnostics.activeProcessesAfter, 0,
                     "worker Job Object still owns an active process");
        expect(diagnostics.endpointReleased, "control endpoint was not released");
        expect(diagnostics.handlesClosed, "worker/control handles were not closed");
        expect(diagnostics.sharedMemoryReleased,
               "shared memory was not released after worker teardown");
    }
};

class PluginSandboxRealVst3CreateTest final : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxRealVst3CreateTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.real-vst3-create.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("worker exclusively creates, prepares, reports, and destroys exact VST3");
        DAW::PluginSandboxProcessCore process;
        if (! startFixture(process))
            return;

        const auto& diagnostics = process.diagnostics();
        expect(process.isRunning(), "sandbox worker did not remain alive after creation");
        expect(diagnostics.workerProcessId != diagnostics.parentProcessId,
               "worker acknowledgement came from the parent PID");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "parent loaded the fixture execution module");
        expectCleanShutdown(process);
       #else
        beginTest("Windows-only Phase C worker contract");
        expect(true);
       #endif
    }
};

class PluginSandboxStartupTimeoutSeparationTest final
    : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxStartupTimeoutSeparationTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.startup-timeout-separation.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("worker startup remains bounded by the short startup deadline");
        auto options = fixtureOptions();
        useTestWorkerForDiagnostic(options);
        options.createVst3Plugin = false;
        options.startupTimeoutMs = 100;
        options.pluginCreateTimeoutMs = 10000;
        options.workerStartupDelayMilliseconds = 250;

        DAW::PluginSandboxProcessCore process;
        const auto start = process.start(options);
        const auto& diagnostics = process.diagnostics();
        expectEquals(static_cast<int>(start),
                     static_cast<int>(DAW::PluginSandboxProcessCore::StartResult::Failed),
                     "delayed worker startup unexpectedly succeeded");
        expect(! diagnostics.handshake,
               "startup timeout test reached a completed worker handshake");
        expect(diagnostics.reaped, "startup-timeout worker was not reaped");
        expect(diagnostics.handlesClosed,
               "startup-timeout worker/control handles were not closed");
       #else
        beginTest("Windows test-hook startup timeout separation seam");
        expect(true);
       #endif
    }
};

class PluginSandboxSlowFinitePluginCreateTest final
    : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxSlowFinitePluginCreateTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.plugin-create-slow-finite.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("PluginCreate may exceed startup timeout within its heavy deadline");
        auto options = fixtureOptions();
        useTestWorkerForDiagnostic(options);
        options.pluginCreateTimeoutMs = 10000;
        options.pluginCreateResponseDelayMilliseconds = 6000;

        DAW::PluginSandboxProcessCore process;
        const auto startedAt = GetTickCount64();
        const auto start = process.start(options);
        const auto elapsed = GetTickCount64() - startedAt;
        const auto& diagnostics = process.diagnostics();
        expectEquals(static_cast<int>(start),
                     static_cast<int>(DAW::PluginSandboxProcessCore::StartResult::Started),
                     diagnostics.error);
        expect(elapsed >= 5000,
               "slow finite PluginCreate did not exceed the startup budget");
        expect(diagnostics.hostedPluginCreated,
               "slow finite PluginCreate did not complete");
        expect(diagnostics.pluginCreateFailurePhase.isEmpty(),
               "slow finite PluginCreate retained a failure phase");
        expectEquals(diagnostics.pluginCreateLastPhase,
                     juce::String("PLUGINCREATE_COMPLETE"));
        expect(process.isRunning(), "worker did not remain alive after slow PluginCreate");
        expectCleanShutdown(process);
       #else
        beginTest("Windows test-hook slow finite PluginCreate seam");
        expect(true);
       #endif
    }
};

class PluginSandboxRealVst3ModuleIsolationTest final : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxRealVst3ModuleIsolationTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.real-vst3-module-isolation.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("real VST3 module exists only in worker process");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module was loaded before sandbox creation");
        DAW::PluginSandboxProcessCore process;
        if (! startFixture(process))
            return;

        const auto& diagnostics = process.diagnostics();
        expect(diagnostics.hostedPluginModuleLoadedInWorker,
               "worker module inventory did not contain the fixture VST3 binary");
        expect(diagnostics.workerProcessId != GetCurrentProcessId(),
               "fixture execution PID equals parent PID");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module is present in parent while worker is alive");
        expectCleanShutdown(process);
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into parent after teardown");
       #else
        beginTest("Windows-only Phase C module isolation contract");
        expect(true);
       #endif
    }
};

class PluginSandboxRealVst3AudioTest final : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxRealVst3AudioTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.real-vst3-audio.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("shared memory round trip executes fixture gain and 32-sample delay");
        DAW::PluginSandboxProcessCore process;
        if (! startFixture(process))
            return;

        constexpr int samples = kBlockSamples;
        juce::AudioBuffer<float> input(2, samples);
        juce::AudioBuffer<float> output(2, samples);
        input.clear();
        input.setSample(0, 0, 1.0f);
        input.setSample(1, 0, -1.0f);
        auto first = process.audioTransport().exchangeBlock(
            input.getArrayOfReadPointers(), 2,
            output.getArrayOfWritePointers(), 2, samples, samples);
        expect(first.submitted, "fixture impulse was not submitted");

        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (process.audioTransport().workerCompletedSequence() < first.submittedSequence
               && juce::Time::getMillisecondCounterHiRes() < deadline)
            Sleep(1);
        expect(process.audioTransport().workerCompletedSequence() >= first.submittedSequence,
               "worker did not complete the real VST3 block before timeout");

        input.clear();
        output.clear();
        const auto second = process.audioTransport().exchangeBlock(
            input.getArrayOfReadPointers(), 2,
            output.getArrayOfWritePointers(), 2, samples, samples);
        expect(second.remoteOutput, "parent did not adopt the worker VST3 output");
        expectWithinAbsoluteError(output.getSample(0, kFixtureLatencySamples), 0.5f, 0.0f,
                                  "left impulse does not match fixture DSP");
        expectWithinAbsoluteError(output.getSample(1, kFixtureLatencySamples), -0.5f, 0.0f,
                                  "right impulse does not match fixture DSP");
        expectWithinAbsoluteError(output.getSample(0, kFixtureLatencySamples - 1), 0.0f, 0.0f,
                                  "fixture latency is one sample early");
        expectWithinAbsoluteError(output.getSample(0, kFixtureLatencySamples + 1), 0.0f, 0.0f,
                                  "fixture impulse is wider than one sample");

        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture VST3 module leaked into the parent process");
        expectCleanShutdown(process);
       #else
        beginTest("Windows-only Phase C audio contract");
        expect(true);
       #endif
    }
};

class PluginSandboxRealVst3ProxyTest final : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxRealVst3ProxyTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.real-vst3-proxy.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("standalone parent proxy delegates fixed-quantum realtime DSP to worker");
        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(juce::File(description.fileOrIdentifier).isDirectory(),
               "fixture bundle is missing: " + description.fileOrIdentifier);
        expect(worker.existsAsFile(),
               "production worker executable is missing: " + worker.getFullPathName());
        if (! juce::File(description.fileOrIdentifier).isDirectory()
            || ! worker.existsAsFile())
            return;

        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module was loaded before proxy preparation");

        const juce::String stableInstanceId = "phase-c-real-vst3-proxy-fixture";
        DAW::SandboxedPluginProxyCore proxy(description, stableInstanceId);
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.sampleRate = 48000.0;
        preparation.blockSamples = kBlockSamples;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif

        const auto prepareResult = proxy.prepare(preparation);
        expectEquals(static_cast<int>(prepareResult),
                     static_cast<int>(DAW::SandboxedPluginProxyCore::PrepareResult::Prepared),
                     proxy.processDiagnostics().error);
        if (prepareResult != DAW::SandboxedPluginProxyCore::PrepareResult::Prepared)
            return;

        const auto prepared = proxy.lifecycleDiagnostics();
        expect(prepared.prepared && prepared.active,
               "proxy did not publish prepared/active lifecycle state");
        expectEquals(static_cast<int>(prepared.lifecycle),
                     static_cast<int>(DAW::SandboxedPluginProxyCore::LifecycleState::Active));
        expect(! prepared.bypassed, "proxy unexpectedly started bypassed");
        expectEquals(proxy.getPluginInstanceId(), stableInstanceId,
                     "proxy did not retain its stable instance ID");
        expectEquals(proxy.getDescription().name, description.name);
        expectEquals(proxy.getDescription().fileOrIdentifier,
                     description.fileOrIdentifier);
        expectEquals(proxy.getDescription().uniqueId, kFixtureUniqueId);
        expectEquals(proxy.getDescription().deprecatedUid, kFixtureDeprecatedUid);
        expectEquals(proxy.getMainInputChannels(), 2);
        expectEquals(proxy.getMainOutputChannels(), 2);
        expectEquals(proxy.getWorkerPluginLatencySamples(), kFixtureLatencySamples);
        expectEquals(proxy.getPhaseBQuantumLatencySamples(), kBlockSamples);
        expectEquals(proxy.getReblockTransportLatencySamples(), 2 * kBlockSamples);
        expectEquals(proxy.getEffectiveLatencySamples(),
                     kFixtureLatencySamples + 2 * kBlockSamples);

        const auto& workerPrepared = proxy.processDiagnostics();
        expect(workerPrepared.hostedPluginCreated,
               "worker did not create and prepare the requested fixture");
        expect(workerPrepared.hostedPluginModuleLoadedInWorker,
               "worker did not report the fixture module in its process");
        expectEquals(workerPrepared.hostedPluginUniqueId, kFixtureUniqueId);
        expectEquals(workerPrepared.hostedPluginDeprecatedUid, kFixtureDeprecatedUid);
        expect(workerPrepared.workerProcessId != workerPrepared.parentProcessId,
               "proxy worker PID equals the parent PID");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "proxy loaded the fixture module in the parent");

        static constexpr int partitions[] { 256, 512, 256, 128, 384, 512 };
        constexpr int renderedSamples = 2048;
        juce::AudioBuffer<float> block(2, kBlockSamples);
        juce::AudioBuffer<float> rendered(2, renderedSamples);
        rendered.clear();
        int streamOffset = 0;
        for (std::size_t index = 0; index < std::size(partitions); ++index)
        {
            const int samples = partitions[index];
            block.clear();
            if (streamOffset == 0)
            {
                block.setSample(0, 0, 1.0f);
                block.setSample(1, 0, -1.0f);
            }

            DAW::PluginSandboxFixedQuantumReblockerCore::ProcessSummary summary;
            if (index == 0)
            {
                summary = proxy.processBlock(block, samples);
            }
            else
            {
               #if JUCE_ENABLE_ALLOCATION_HOOKS
                juce::UnitTestAllocationChecker allocationChecker(*this);
                summary = proxy.processBlock(block, samples);
               #else
                expect(false, "JUCE allocation hooks are required for parent proxy allocation proof");
               #endif
            }

            expect(! summary.contractFault, "proxy reblocker entered its fault state");
            rendered.copyFrom(0, streamOffset, block, 0, 0, samples);
            rendered.copyFrom(1, streamOffset, block, 1, 0, samples);
            streamOffset += samples;

            if (summary.lastSubmittedSequence != 0)
            {
                const auto deadline = GetTickCount64() + 5000;
                while (proxy.workerCompletedSequence() < summary.lastSubmittedSequence
                       && GetTickCount64() < deadline)
                    Sleep(1);
                expect(proxy.workerCompletedSequence() >= summary.lastSubmittedSequence,
                       "worker did not complete a submitted exact quantum before timeout");
            }
        }
        expectEquals(streamOffset, renderedSamples);
        expectWithinAbsoluteError(rendered.getSample(0, 1056),
                                  0.5f, 0.0f,
                                  "left proxy impulse does not match fixture gain/delay");
        expectWithinAbsoluteError(rendered.getSample(1, 1056),
                                  -0.5f, 0.0f,
                                  "right proxy impulse does not match fixture gain/delay");
        expectWithinAbsoluteError(rendered.getSample(0, 1055),
                                  0.0f, 0.0f,
                                  "proxy fixture output arrived one sample early");
        expectWithinAbsoluteError(rendered.getSample(0, 1057),
                                  0.0f, 0.0f,
                                  "proxy fixture impulse is wider than one sample");

        const auto realtimeDiagnostics = proxy.lifecycleDiagnostics();
        expectEquals(static_cast<juce::int64>(realtimeDiagnostics.transport.calls),
                     juce::int64(4));
        expectEquals(static_cast<juce::int64>(realtimeDiagnostics.transport.boundsRejected),
                     juce::int64(0));
        expectEquals(static_cast<juce::int64>(realtimeDiagnostics.reblocker.remoteQuanta),
                     juce::int64(3));
        expectEquals(static_cast<int>(realtimeDiagnostics.activeRealtimeCalls), 0);
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into parent after proxy processing");

        expect(proxy.release(5000), proxy.processDiagnostics().error);
        const auto released = proxy.lifecycleDiagnostics();
        expect(! released.prepared && ! released.active,
               "proxy retained prepared/active state after release");
        expectEquals(static_cast<int>(released.lifecycle),
                     static_cast<int>(DAW::SandboxedPluginProxyCore::LifecycleState::Released));
        const auto& shutdown = proxy.processDiagnostics();
        expect(shutdown.gracefulShutdown, "worker/plugin shutdown was not graceful");
        expect(shutdown.reaped, "proxy worker was not reaped");
        expectEquals(shutdown.activeProcessesAfter, 0,
                     "proxy left an orphan worker in its Job Object");
        expect(shutdown.endpointReleased, "proxy control endpoint was not released");
        expect(shutdown.handlesClosed, "proxy worker/control handles were not closed");
        expect(shutdown.sharedMemoryReleased, "proxy shared memory was not released");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into parent after proxy release");
        logMessage("APEX_PHASE_C_PROXY worker_create_prepare=true channels=2"
                   " plugin_latency=32 phase_b_quantum=512 transport_latency=1024"
                   " effective_latency=1056 variable_partitions=true"
                   " gain=0.5 plugin_delay=32 parent_allocations=0 orphan_workers=0");
       #else
        beginTest("Windows-only standalone Phase C proxy contract");
        expect(true);
       #endif
    }
};

class PluginSandboxRealVst3IdentityMismatchTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3IdentityMismatchTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-identity-mismatch.v1",
                         "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("valid VST3 path form does not bypass exact worker identity matching");

        const auto worker = productionWorkerExecutable();
        auto description = fixtureDescription();
        description.uniqueId += 1;

        expect(juce::File(description.fileOrIdentifier).isDirectory(),
               "fixture bundle is missing: " + description.fileOrIdentifier);
        expect(worker.existsAsFile(),
               "production worker executable is missing: " + worker.getFullPathName());
        if (! juce::File(description.fileOrIdentifier).isDirectory()
            || ! worker.existsAsFile())
            return;

        DAW::PluginSandboxProcessCore process;
        DAW::PluginSandboxProcessCore::Options options;
        options.enableAudioTransport = true;
        options.createVst3Plugin = true;
        options.pluginDescription = description;
        options.audioConfiguration.maxInputChannels = 2;
        options.audioConfiguration.maxOutputChannels = 2;
        options.audioConfiguration.maxSamples = kBlockSamples;
        options.audioConfiguration.nominalBlockSamples = kBlockSamples;
        options.audioConfiguration.sampleRate = 48000.0;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        options.workerExecutablePathForTest = worker.getFullPathName();
       #endif

        const auto start = process.start(options);
        const auto& diagnostics = process.diagnostics();
        expectEquals(static_cast<int>(start),
                     static_cast<int>(DAW::PluginSandboxProcessCore::StartResult::Failed),
                     "mismatched VST3 identity unexpectedly started");
        expect(! diagnostics.hostedPluginCreated,
               "worker created a plugin despite requested identity mismatch");
        expect(diagnostics.error.contains("exactly one requested VST3 component"),
               "unexpected identity-mismatch error: " + diagnostics.error);
        expectEquals(diagnostics.pluginCreateFailurePhase,
                     juce::String("PLUGINCREATE_WORKER_REJECT"));
        expectEquals(diagnostics.pluginCreateWorkerProcessId,
                     diagnostics.workerProcessId);
        expect(diagnostics.pluginCreateWorkerGeneration > 0,
               "identity failure omitted worker generation");
        expect(diagnostics.reaped, "identity-mismatch worker was not reaped");
       #else
        beginTest("Windows-only VST3 identity matching contract");
        expect(true);
       #endif
    }
};

class PluginSandboxPluginCreateWriteTimeoutDiagnosticTest final
    : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxPluginCreateWriteTimeoutDiagnosticTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.plugin-create-write-timeout.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("PluginCreate write timeout is classified before response read");
        const auto options = fixtureOptions();
        expect(juce::File(options.pluginDescription.fileOrIdentifier).isDirectory(),
               "fixture bundle is missing");
        expect(productionWorkerExecutable().existsAsFile(),
               "production worker executable is missing");
        if (! juce::File(options.pluginDescription.fileOrIdentifier).isDirectory()
            || ! productionWorkerExecutable().existsAsFile())
            return;

        auto faultedOptions = options;
        useTestWorkerForDiagnostic(faultedOptions);
        faultedOptions.pluginCreateControlFaultPoint =
            DAW::PluginSandboxControlTestFaultPoint::PluginCreateWriteTimeout;
        DAW::PluginSandboxProcessCore process;
        const auto start = process.start(faultedOptions);
        const auto& diagnostics = process.diagnostics();
        expectEquals(static_cast<int>(start),
                     static_cast<int>(DAW::PluginSandboxProcessCore::StartResult::Failed),
                     "write-timeout fixture unexpectedly started");
        expectEquals(diagnostics.pluginCreateFailurePhase,
                     juce::String("PLUGINCREATE_WRITE_TIMEOUT"));
        expectEquals(diagnostics.pluginCreateLastPhase,
                     juce::String("PLUGINCREATE_WRITE_TIMEOUT"));
        expectEquals(diagnostics.error,
                     juce::String("PluginCreate request write timeout"));
        expectEquals(diagnostics.pluginCreateWorkerProcessId,
                     diagnostics.workerProcessId);
        expect(diagnostics.pluginCreateWorkerGeneration > 0,
               "write timeout omitted worker generation");
        expect(diagnostics.reaped, "write-timeout worker was not reaped");
        expectAuditPhase(diagnostics, "PLUGINCREATE_WRITE_BEGIN",
                         diagnostics.workerProcessId,
                         diagnostics.pluginCreateWorkerGeneration);
        expectAuditPhase(diagnostics, "PLUGINCREATE_WRITE_TIMEOUT",
                         diagnostics.workerProcessId,
                         diagnostics.pluginCreateWorkerGeneration);
       #else
        beginTest("Windows test-hook PluginCreate write timeout seam");
        expect(true);
       #endif
    }
};

class PluginSandboxPluginCreateResponseReadTimeoutDiagnosticTest final
    : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxPluginCreateResponseReadTimeoutDiagnosticTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.plugin-create-response-read-timeout.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("PluginCreate response timeout is classified after request write");
        const auto options = fixtureOptions();
        expect(juce::File(options.pluginDescription.fileOrIdentifier).isDirectory(),
               "fixture bundle is missing");
        expect(productionWorkerExecutable().existsAsFile(),
               "production worker executable is missing");
        if (! juce::File(options.pluginDescription.fileOrIdentifier).isDirectory()
            || ! productionWorkerExecutable().existsAsFile())
            return;

        auto delayedOptions = options;
        useTestWorkerForDiagnostic(delayedOptions);
        delayedOptions.pluginCreateTimeoutMs = 1000;
        delayedOptions.pluginCreateResponseDelayMilliseconds = 2000;
        DAW::PluginSandboxProcessCore process;
        const auto start = process.start(delayedOptions);
        const auto& diagnostics = process.diagnostics();
        expectEquals(static_cast<int>(start),
                     static_cast<int>(DAW::PluginSandboxProcessCore::StartResult::Failed),
                     "response-delay fixture unexpectedly started");
        expectEquals(diagnostics.pluginCreateFailurePhase,
                     juce::String("PLUGINCREATE_RESPONSE_READ_TIMEOUT"));
        expectEquals(diagnostics.pluginCreateLastPhase,
                     juce::String("PLUGINCREATE_RESPONSE_READ_TIMEOUT"));
        expectEquals(diagnostics.error,
                     juce::String("PluginCreate response read timeout"));
        expectEquals(diagnostics.pluginCreateWorkerProcessId,
                     diagnostics.workerProcessId);
        expect(diagnostics.pluginCreateWorkerGeneration > 0,
               "response timeout omitted worker generation");
        expect(diagnostics.reaped, "response-timeout worker was not reaped");
        expect(! diagnostics.hostedPluginCreated,
               "heavy PluginCreate timeout silently retained a hosted plugin");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "heavy PluginCreate timeout loaded the fixture into the parent");
        expectAuditPhase(diagnostics, "PLUGINCREATE_WRITE_COMPLETE",
                         diagnostics.workerProcessId,
                         diagnostics.pluginCreateWorkerGeneration);
        expectAuditPhase(diagnostics, "PLUGINCREATE_RESPONSE_READ_BEGIN",
                         diagnostics.workerProcessId,
                         diagnostics.pluginCreateWorkerGeneration);
        expectAuditPhase(diagnostics, "PLUGINCREATE_RESPONSE_READ_TIMEOUT",
                         diagnostics.workerProcessId,
                         diagnostics.pluginCreateWorkerGeneration);
       #else
        beginTest("Windows test-hook PluginCreate response-read timeout seam");
        expect(true);
       #endif
    }
};

class PluginSandboxPluginCreateFailureMilestoneTest final
    : public PluginSandboxPhaseCTestBase
{
public:
    PluginSandboxPluginCreateFailureMilestoneTest()
        : PluginSandboxPhaseCTestBase("plugin.sandbox.plugin-create-failure-milestone.v1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("worker create failure records begin without complete");
        const auto options = fixtureOptions();
        expect(juce::File(options.pluginDescription.fileOrIdentifier).isDirectory(),
               "fixture bundle is missing");
        expect(productionWorkerExecutable().existsAsFile(),
               "production worker executable is missing");
        if (! juce::File(options.pluginDescription.fileOrIdentifier).isDirectory()
            || ! productionWorkerExecutable().existsAsFile())
            return;

        auto failingOptions = options;
        useTestWorkerForDiagnostic(failingOptions);
        failingOptions.pluginCreateControlFaultPoint =
            DAW::PluginSandboxControlTestFaultPoint::PluginCreateFailBeforeInstance;
        DAW::PluginSandboxProcessCore process;
        const auto start = process.start(failingOptions);
        const auto& diagnostics = process.diagnostics();
        expectEquals(static_cast<int>(start),
                     static_cast<int>(DAW::PluginSandboxProcessCore::StartResult::Failed),
                     "injected create failure unexpectedly started");
        expectEquals(diagnostics.pluginCreateFailurePhase,
                     juce::String("PLUGINCREATE_WORKER_REJECT"));
        expect(diagnostics.error.contains("test-injected PluginCreate instance failure"),
               "worker create failure was not preserved: " + diagnostics.error);
        expect(diagnostics.reaped, "create-failure worker was not reaped");
        expectAuditPhase(diagnostics, "CREATE_INSTANCE_BEGIN",
                         diagnostics.workerProcessId,
                         diagnostics.audioGeneration);
        expectAuditPhase(diagnostics, "CREATE_INSTANCE_FAILURE",
                         diagnostics.workerProcessId,
                         diagnostics.audioGeneration);
        expectAuditPhase(diagnostics, "PLUGINCREATE_RESPONSE_SEND_COMPLETE",
                         diagnostics.workerProcessId,
                         diagnostics.audioGeneration);
        const auto log = DAW::PluginScanAuditLogCore::getLogFile(
            "plugin_ui_flow.log").loadFileAsString();
        const auto completeNeedle = juce::String(
            "op=PluginCreate phase=CREATE_INSTANCE_COMPLETE pid=")
            + juce::String(static_cast<juce::int64>(diagnostics.workerProcessId))
            + " generation=" + juce::String(static_cast<juce::int64>(diagnostics.audioGeneration))
            + " session=" + diagnostics.sessionToken;
        expect(! log.contains(completeNeedle),
               "create-failure fixture reported an instance-complete milestone");
       #else
        beginTest("Windows test-hook PluginCreate failure milestone seam");
        expect(true);
       #endif
    }
};

PluginSandboxVst3PathFormTest pluginSandboxVst3PathFormTest;
PluginSandboxEnumOnlyDiagnosticTest pluginSandboxEnumOnlyDiagnosticTest;
PluginSandboxRealVst3CreateTest pluginSandboxRealVst3CreateTest;
PluginSandboxStartupTimeoutSeparationTest pluginSandboxStartupTimeoutSeparationTest;
PluginSandboxSlowFinitePluginCreateTest pluginSandboxSlowFinitePluginCreateTest;
PluginSandboxRealVst3ModuleIsolationTest pluginSandboxRealVst3ModuleIsolationTest;
PluginSandboxRealVst3AudioTest pluginSandboxRealVst3AudioTest;
PluginSandboxRealVst3ProxyTest pluginSandboxRealVst3ProxyTest;
PluginSandboxRealVst3IdentityMismatchTest pluginSandboxRealVst3IdentityMismatchTest;
PluginSandboxPluginCreateWriteTimeoutDiagnosticTest pluginSandboxPluginCreateWriteTimeoutDiagnosticTest;
PluginSandboxPluginCreateResponseReadTimeoutDiagnosticTest pluginSandboxPluginCreateResponseReadTimeoutDiagnosticTest;
PluginSandboxPluginCreateFailureMilestoneTest pluginSandboxPluginCreateFailureMilestoneTest;
} // namespace
