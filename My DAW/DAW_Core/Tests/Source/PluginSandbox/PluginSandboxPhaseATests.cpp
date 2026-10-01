#include <JuceHeader.h>

namespace
{
constexpr const char* kSelfTestFlag = "--apex-plugin-sandbox-self-test";
constexpr int kParentTimeoutMs = 15000;

struct ScenarioResult
{
    bool launched = false;
    bool timedOut = false;
    int exitCode = -1;
    juce::String output;
    juce::var report;
    juce::String error;
};

juce::File findDawCoreExecutable()
{
    auto directory = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                         .getParentDirectory();

    // APEXTests.exe lives under
    // Tests/Builds/VisualStudio2026/x64/<Configuration>/ConsoleApp.
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

bool binaryContainsSelfTestFlag(const juce::File& executable)
{
    juce::MemoryBlock bytes;
    if (! executable.loadFileAsData(bytes))
        return false;

    const auto* begin = static_cast<const char*>(bytes.getData());
    const auto* end = begin + bytes.getSize();
    const std::string token(kSelfTestFlag);
    return std::search(begin, end, token.begin(), token.end()) != end;
}

ScenarioResult runScenario(const juce::File& executable,
                           const juce::String& scenario,
                           int iterations = 1)
{
    ScenarioResult result;
    if (! executable.existsAsFile())
    {
        result.error = "DAW_Core.exe does not exist: " + executable.getFullPathName();
        return result;
    }

    if (! binaryContainsSelfTestFlag(executable))
    {
        result.error = "Phase A parent self-test command is not present in DAW_Core.exe";
        return result;
    }

    juce::StringArray arguments;
    arguments.add(executable.getFullPathName());
    arguments.add(kSelfTestFlag);
    arguments.add("--scenario=" + scenario);
    arguments.add("--iterations=" + juce::String(iterations));

    juce::ChildProcess process;
    result.launched = process.start(arguments,
                                    juce::ChildProcess::wantStdOut
                                      | juce::ChildProcess::wantStdErr);
    if (! result.launched)
    {
        result.error = "Failed to launch Phase A parent self-test";
        return result;
    }

    if (! process.waitForProcessToFinish(kParentTimeoutMs))
    {
        result.timedOut = true;
        process.kill();
        process.waitForProcessToFinish(3000);
        result.error = "Phase A parent self-test exceeded its bounded timeout";
        return result;
    }

    result.exitCode = static_cast<int>(process.getExitCode());
    result.output = process.readAllProcessOutput();

    constexpr auto prefix = "APEX_PLUGIN_SANDBOX_RESULT ";
    const auto reportStart = result.output.indexOf(prefix);
    if (reportStart >= 0)
        result.report = juce::JSON::parse(
            result.output.substring(reportStart + (int)std::strlen(prefix)).trim());

    if (! result.report.isObject())
        result.error = "Phase A parent returned no structured result. Output: " + result.output;
    return result;
}

bool reportBool(const ScenarioResult& result, const juce::Identifier& property)
{
    if (auto* object = result.report.getDynamicObject())
        return static_cast<bool>(object->getProperty(property));
    return false;
}

int reportInt(const ScenarioResult& result, const juce::Identifier& property)
{
    if (auto* object = result.report.getDynamicObject())
        return static_cast<int>(object->getProperty(property));
    return -1;
}

juce::String reportString(const ScenarioResult& result, const juce::Identifier& property)
{
    if (auto* object = result.report.getDynamicObject())
        return object->getProperty(property).toString();
    return {};
}

class PluginSandboxPhaseATest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseATest(const juce::String& testName,
                            const juce::String& scenario)
        : juce::UnitTest(testName, "PluginSandbox"), scenario_(scenario)
    {
    }

    void runTest() override
    {
        beginTest(scenario_);

        auto executable = findDawCoreExecutable();
        juce::File temporaryDirectory;

        if (scenario_ == "single-executable-distribution")
        {
            temporaryDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getNonexistentChildFile("apex-sandbox-phase-a-", {}, false);
            expect(temporaryDirectory.createDirectory().wasOk(),
                   "Could not create isolated distribution directory");

            const auto copiedExecutable = temporaryDirectory.getChildFile("DAW_Core.exe");
            expect(executable.copyFileTo(copiedExecutable),
                   "Could not copy the single APEX executable");
            executable = copiedExecutable;

            expectEquals(temporaryDirectory.findChildFiles(juce::File::findFiles,
                                                            false,
                                                            "*.exe").size(),
                         1,
                         "The isolated directory must contain exactly one executable");
        }

        const int iterations = scenario_ == "worker-clean-shutdown" ? 20 : 1;
        const auto result = runScenario(executable, scenario_, iterations);

        expect(result.launched, result.error);
        expect(! result.timedOut, result.error);
        expectEquals(result.exitCode, 0, result.output + "\n" + result.error);
        expect(result.report.isObject(), result.error);
        expect(reportBool(result, "pass"), result.output);
        expectEquals(reportString(result, "scenario"), scenario_);
        expect(reportBool(result, "sameExecutable"), result.output);
        expect(reportBool(result, "reaped"), result.output);
        expectEquals(reportInt(result, "activeProcessesAfter"), 0, result.output);
        expect(reportBool(result, "endpointReleased"), result.output);
        expect(reportBool(result, "handlesClosed"), result.output);
        expect(reportBool(result, "handleAuditAvailable"), result.output);
        expectEquals(reportInt(result, "osHandleDelta"), 0, result.output);

        if (scenario_ == "worker-handshake" || scenario_ == "no-recursive-worker"
            || scenario_ == "single-executable-distribution")
        {
            expect(reportBool(result, "handshake"), result.output);
            expect(reportBool(result, "workerMode"), result.output);
            expect(! reportBool(result, "normalGuiCreated"), result.output);
            expectEquals(reportInt(result, "maxJobActiveProcesses"), 1, result.output);
        }

        if (scenario_ == "protocol-mismatch")
            expect(reportBool(result, "protocolMismatchRejected"), result.output);

        if (scenario_ == "worker-clean-shutdown")
        {
            expect(reportBool(result, "gracefulShutdown"), result.output);
            expectEquals(reportInt(result, "iterationsCompleted"), iterations, result.output);
        }

        if (scenario_ == "worker-forced-termination")
            expect(reportBool(result, "forcedTermination"), result.output);

        if (scenario_ == "no-recursive-worker")
            expect(reportBool(result, "recursionGuardEnabled"), result.output);

        if (scenario_ == "single-executable-distribution")
            expect(juce::File(reportString(result, "executablePath")) == executable,
                   "The copied parent did not resolve itself as the current executable");

        if (temporaryDirectory != juce::File())
        {
            expectEquals(temporaryDirectory.findChildFiles(juce::File::findFiles,
                                                            false,
                                                            "*.exe").size(),
                         1,
                         "Self-worker mode must not create a helper executable");
            expect(temporaryDirectory.deleteRecursively(),
                   "Could not remove isolated distribution directory");
        }
    }

private:
    juce::String scenario_;
};

PluginSandboxPhaseATest singleExecutableDistributionTest(
    "plugin.sandbox.single-executable-distribution.v1", "single-executable-distribution");
PluginSandboxPhaseATest workerHandshakeTest(
    "plugin.sandbox.worker-handshake.v1", "worker-handshake");
PluginSandboxPhaseATest protocolMismatchTest(
    "plugin.sandbox.protocol-mismatch.v1", "protocol-mismatch");
PluginSandboxPhaseATest cleanShutdownTest(
    "plugin.sandbox.worker-clean-shutdown.v1", "worker-clean-shutdown");
PluginSandboxPhaseATest forcedTerminationTest(
    "plugin.sandbox.worker-forced-termination.v1", "worker-forced-termination");
PluginSandboxPhaseATest noRecursiveWorkerTest(
    "plugin.sandbox.no-recursive-worker.v1", "no-recursive-worker");
} // namespace
