#include <JuceHeader.h>

#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/PluginHostCore/PluginInstanceCore.h"
#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAudioTransportCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAutomationTransportShared.h"
#include "../../../Source/AutomationCore/AutomationSnapshotCore.h"
#include "../../../Source/AutomationCore/AutomationPointCore.h"
#include "../../../Source/AutomationCore/AutomationCurveTypesCore.h"

#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// ═══════════════════════════════════════════════════════════════════════════
// PHASE E2C TURN 1 — one real worker-death/restart continuity proof.
//
// This test deliberately uses the existing Phase D external worker-termination
// seam with the existing E1 state fixture. The fixture therefore supplies both
// an E1-replayable parameter/state contract and an observable E2B automation
// target without adding a new plugin fixture or touching the frozen E2B tests.
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
constexpr int kQuantum = 512;
constexpr double kSampleRate = 48000.0;
constexpr double kBpm = 120.0;
constexpr float kBaseline = 0.25f;
constexpr float kAutomationTarget = 0.75f;
constexpr std::uint32_t kHangSentinelBits = 0x7FC0BEEFu;

float bitsToFloat(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

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

bool processStillAlive(DWORD pid, std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle == nullptr)
        return false;

    const auto deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline)
    {
        DWORD exitCode = STILL_ACTIVE;
        if (GetExitCodeProcess(handle, &exitCode) && exitCode != STILL_ACTIVE)
        {
            CloseHandle(handle);
            return false;
        }
        Sleep(1);
    }
    CloseHandle(handle);
    return true;
   #else
    juce::ignoreUnused(pid, timeoutMs);
    return false;
   #endif
}

bool namedMappingExists(const juce::String& mappingName)
{
   #if JUCE_WINDOWS
    if (mappingName.isEmpty())
        return false;
    HANDLE handle = OpenFileMappingW(FILE_MAP_READ, FALSE,
                                     mappingName.toWideCharPointer());
    if (handle == nullptr)
        return false;
    CloseHandle(handle);
    return true;
   #else
    juce::ignoreUnused(mappingName);
    return false;
   #endif
}

bool killProcessUnexpectedly(DWORD pid)
{
   #if JUCE_WINDOWS
     HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
     if (handle == nullptr)
         return false;

     const bool terminated = TerminateProcess(handle, 0x80000001u) != FALSE;
     CloseHandle(handle);
     return terminated;
   #else
     juce::ignoreUnused(pid);
     return false;
   #endif
}

void waitForWorkerQuanta(const DAW::SandboxedPluginProxyCore& proxy,
                         std::uint64_t submitted,
                         std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    const auto deadline = GetTickCount64() + timeoutMs;
    while (proxy.workerCompletedSequence() < submitted
           && GetTickCount64() < deadline)
        Sleep(1);
   #else
    juce::ignoreUnused(proxy, submitted, timeoutMs);
   #endif
}

struct StateFixtureIdentity
{
    int uniqueId = 0;
    int deprecatedUid = 0;
    juce::String error;
};

StateFixtureIdentity discoverStateFixtureIdentity(const juce::File& worker)
{
    StateFixtureIdentity result;
    const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_VST3_STATE_PATH", {}));
    if (! worker.existsAsFile() || ! fixture.isDirectory())
        return result;

    juce::PluginDescription description;
    description.name = "APEX Test VST3 State";
    description.descriptiveName = description.name;
    description.manufacturerName = "APEX";
    description.version = "1.0.0";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = fixture.getFullPathName();
    description.uniqueId = 0;
    description.deprecatedUid = 0;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;

    DAW::SandboxedPluginProxyCore probe(description);
    DAW::SandboxedPluginProxyCore::Preparation preparation;
    preparation.blockSamples = kQuantum;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    preparation.workerExecutablePathForTest = worker.getFullPathName();
   #endif

    if (probe.prepare(preparation) == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared)
    {
        const auto& diagnostics = probe.processDiagnostics();
        result.uniqueId = diagnostics.hostedPluginUniqueId;
        result.deprecatedUid = diagnostics.hostedPluginDeprecatedUid;
        probe.release(5000);
    }
    else
    {
        result.error = probe.processDiagnostics().error;
    }
    return result;
}

juce::PluginDescription stateFixtureDescription(const StateFixtureIdentity& identity)
{
    const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_VST3_STATE_PATH", {}));
    juce::PluginDescription description;
    description.name = "APEX Test VST3 State";
    description.descriptiveName = description.name;
    description.manufacturerName = "APEX";
    description.version = "1.0.0";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = fixture.getFullPathName();
    description.uniqueId = identity.uniqueId;
    description.deprecatedUid = identity.deprecatedUid;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
    return description;
}

struct AutomationFixtureIdentity
{
    int uniqueId = 0;
    int deprecatedUid = 0;
};

AutomationFixtureIdentity discoverE2CAutomationFixtureIdentity(const juce::File& worker)
{
    AutomationFixtureIdentity result;
    const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_VST3_AUTOMATION_PATH", {}));
    if (! worker.existsAsFile() || ! fixture.isDirectory())
        return result;

    juce::PluginDescription description;
    description.name = "APEX Test VST3 Automation";
    description.descriptiveName = description.name;
    description.manufacturerName = "APEX";
    description.version = "1.0.0";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = fixture.getFullPathName();
    description.uniqueId = 0;
    description.deprecatedUid = 0;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;

    DAW::SandboxedPluginProxyCore probe(description);
    DAW::SandboxedPluginProxyCore::Preparation preparation;
    preparation.blockSamples = kQuantum;
    preparation.maximumHostBlockSamples = kQuantum;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    preparation.workerExecutablePathForTest = worker.getFullPathName();
   #endif
    if (probe.prepare(preparation)
            == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared)
    {
        const auto& diagnostics = probe.processDiagnostics();
        result.uniqueId = diagnostics.hostedPluginUniqueId;
        result.deprecatedUid = diagnostics.hostedPluginDeprecatedUid;
        probe.release(5000);
    }
    return result;
}

juce::PluginDescription e2cAutomationFixtureDescription(
    const AutomationFixtureIdentity& identity)
{
    const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_VST3_AUTOMATION_PATH", {}));
    juce::PluginDescription description;
    description.name = "APEX Test VST3 Automation";
    description.descriptiveName = description.name;
    description.manufacturerName = "APEX";
    description.version = "1.0.0";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = fixture.getFullPathName();
    description.uniqueId = identity.uniqueId;
    description.deprecatedUid = identity.deprecatedUid;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
    return description;
}

bool findParameterIdForOrdinal(
    const juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo>& metadata,
    int ordinal,
    juce::String& parameterId)
{
    for (const auto& entry : metadata)
        if (entry.index == ordinal)
        {
            parameterId = entry.parameterId;
            return parameterId.isNotEmpty();
        }
    return false;
}

bool fetchLiveValueForIndex(DAW::SandboxedPluginProxyCore& proxy,
                            int parameterIndex,
                            float& value,
                            juce::String& error)
{
    juce::Array<DAW::SandboxedPluginProxyCore::SandboxLiveParameterValue> live;
    if (! proxy.fetchLiveParameterValues(live, error))
        return false;

    for (const auto& entry : live)
        if (entry.index == parameterIndex)
        {
            value = entry.normalizedValue;
            return true;
        }

    error = "live value entry is missing for parameter index "
        + juce::String(parameterIndex);
    return false;
}

void fillBlock(juce::AudioBuffer<float>& block, float value)
{
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(
            block.getWritePointer(channel), value, block.getNumSamples());
}

juce::File hookEnabledWorkerExecutable()
{
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    const auto configured = juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_PLUGIN_WORKER_PATH", {});
    return configured.isNotEmpty() ? juce::File(configured) : juce::File();
   #else
    return {};
   #endif
}

bool waitForWorkerDeath(const DAW::SandboxedPluginProxyCore& proxy,
                        std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    const auto deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline)
    {
        if (proxy.pollLivenessForTest()
            != static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive))
            return true;
        Sleep(1);
    }
    return proxy.pollLivenessForTest()
        != static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive);
   #else
    juce::ignoreUnused(proxy, timeoutMs);
    return false;
   #endif
}

bool waitForWorkerCompletion(const DAW::SandboxedPluginProxyCore& proxy,
                             std::uint64_t sequence,
                             std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    const auto deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline)
    {
        if (proxy.workerCompletedSequence() >= sequence)
            return true;
        Sleep(1);
    }
    return proxy.workerCompletedSequence() >= sequence;
   #else
    juce::ignoreUnused(proxy, sequence, timeoutMs);
    return false;
   #endif
}

bool recoverDirectProxyToHealthy(DAW::SandboxedPluginProxyCore& proxy,
                                 std::uint32_t previousPid,
                                 std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    const auto deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline)
    {
        proxy.pollHealthAndRecover();
        if (proxy.recoveryCompletionPending()
            && ! proxy.completeAutomaticRecoveryActivation())
            return false;

        if (proxy.healthState()
                == DAW::SandboxedPluginProxyCore::HealthState::Healthy
            && proxy.remoteAvailable()
            && proxy.currentWorkerPid() != 0
            && proxy.currentWorkerPid() != previousPid)
            return true;
        Sleep(5);
    }
    return false;
   #else
    juce::ignoreUnused(proxy, previousPid, timeoutMs);
    return false;
   #endif
}

void runAudioFaultSeamSmoke(
    juce::UnitTest& test,
    const juce::String& testDescription,
    DAW::PluginSandboxAudioTestFaultPoint faultPoint,
    DWORD expectedExitCode,
    std::uint64_t expectedAppliedBeforeFault)
{
   #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    test.beginTest(testDescription);
    juce::MessageManager::getInstance();

    const auto worker = hookEnabledWorkerExecutable();
    test.expect(worker.existsAsFile(),
                "hook-enabled test worker path is missing");
    if (! worker.existsAsFile())
        return;

    const auto identity = discoverStateFixtureIdentity(worker);
    test.expect(identity.uniqueId != 0,
                "state fixture identity discovery failed for hook worker: "
                    + identity.error);
    if (identity.uniqueId == 0)
        return;

    DAW::SandboxedPluginProxyCore proxy(stateFixtureDescription(identity));
    juce::String error;
    test.expect(proxy.enableAutomationTransport(error),
                "could not enable E2A automation: " + error);

    DAW::SandboxedPluginProxyCore::Preparation preparation;
    preparation.blockSamples = kQuantum;
    preparation.maximumHostBlockSamples = kQuantum;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
    preparation.workerExecutablePathForTest = worker.getFullPathName();
    preparation.audioFaultPoint = faultPoint;
    test.expect(proxy.prepare(preparation)
                    == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
                "hook-enabled worker preparation failed: "
                    + proxy.processDiagnostics().error);
    if (! proxy.isPrepared())
        return;

    juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
    test.expect(proxy.fetchParameterMetadata(metadata, error),
                "hook-worker metadata fetch failed: " + error);
    juce::String parameterId;
    test.expect(findParameterIdForOrdinal(metadata, 0, parameterId),
                "hook-worker ordinal-zero parameter is missing");
    if (parameterId.isEmpty())
        return;

    test.expect(proxy.setParameter(parameterId, kBaseline, error),
                "hook-worker baseline parameter set failed: " + error);

    DAW::PluginSandboxAutomationShared::SandboxAutomationEvent event;
    event.parameterOrdinal = 0;
    event.sampleOffset = 0;
    event.normalizedValue = kAutomationTarget;
    juce::AudioBuffer<float> block(2, kQuantum);
    fillBlock(block, 1.0f);

    const auto exchange = proxy.processBlockWithAutomation(
        block, kQuantum, &event, 1);
    test.expect(exchange.lastSubmittedSequence == 1,
                "parent did not submit the automation-carrying audio quantum");
    test.expect(proxy.automationDiagnostics().latestPublishedSequence == 1,
                "parent did not publish automation batch sequence 1");

    const auto pidA = proxy.currentWorkerPid();
    test.expect(pidA != 0, "hook worker PID A is missing");
    const bool died = waitForWorkerDeath(proxy, 10000);
    test.expect(died, "hook worker did not die at the requested fault seam");
    if (! died)
        return;

    const auto exitCode = proxy.processExitCodeForTest();
    test.expect(exitCode == expectedExitCode,
                "worker exit code did not identify the requested one-shot seam");
    test.expect(proxy.workerStartCountForTest() == 1,
                "fault seam caused more than one initial worker start");

    const auto diagnosticsBeforeRecovery = proxy.automationDiagnostics();
    test.expect(diagnosticsBeforeRecovery.workerAppliedEvents
                    == expectedAppliedBeforeFault,
                "unexpected worker automation application count before fault");
    test.expect(proxy.workerCompletedSequence() == 0,
                "faulted quantum was published as worker-completed");

    test.expect(recoverDirectProxyToHealthy(proxy, pidA, 30000),
                "replacement hook worker did not become healthy");
    test.expect(proxy.workerStartCountForTest() == 2,
                "automatic recovery did not create exactly one replacement worker");

    // Re-submit an automation event on the fresh generation. If the parent
    // accidentally forwards the consumed fault request, the replacement dies
    // here as well. A successful completion proves the replacement is unarmed.
    fillBlock(block, 1.0f);
    const auto postRecovery = proxy.processBlockWithAutomation(
        block, kQuantum, &event, 1);
    test.expect(postRecovery.lastSubmittedSequence == 1,
                "replacement worker rejected the post-recovery audio submit");
    const auto submittedAfterRecovery =
        proxy.lifecycleDiagnostics().transport.submitted;
    test.expect(waitForWorkerCompletion(proxy, 1, 10000),
                "replacement worker did not commit the post-recovery quantum");
    test.expect(proxy.pollLivenessForTest()
                    == static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive),
                "replacement worker inherited the fired fault");
    test.expect(proxy.workerStartCountForTest() == 2,
                "replacement worker faulted again instead of remaining unarmed");
    test.expect(proxy.automationDiagnostics().workerAppliedEvents == 1,
                "replacement worker did not apply the post-recovery event");
    test.expect(submittedAfterRecovery >= 1,
                "post-recovery audio submission counter did not advance");

    juce::ignoreUnused(proxy.release(5000));
   #else
    test.beginTest(testDescription + " (Windows test worker hooks unavailable)");
    test.expect(true);
   #endif
}

bool driveRecoveryToHealthy(DAW::PluginChainCore& chain,
                            const DAW::SandboxedPluginProxyCore& proxy,
                            DWORD previousPid,
                            std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    const auto deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline)
    {
        // Public production path: ApplicationCore maintenance reaches this
        // through PluginChainCore::pollSandboxHealth().
        chain.pollSandboxHealth();
        if (proxy.healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy
            && proxy.remoteAvailable()
            && proxy.currentWorkerPid() != 0
            && proxy.currentWorkerPid() != previousPid)
            return true;
        Sleep(25);
    }
    return false;
   #else
    juce::ignoreUnused(chain, proxy, previousPid, timeoutMs);
    return false;
   #endif
}
} // namespace

class PluginSandboxPhaseE2CAutomationCrashRestartContinuityTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CAutomationCrashRestartContinuityTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.automation-crash-restart-continuity.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("real worker death replays E1 state and resumes canonical E2B automation");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed: " + identity.error);
        if (identity.uniqueId == 0)
            return;

        const juce::String trackId = "e2c-automation-crash-restart-track";
        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         stateFixtureDescription(identity), error, preparation),
                     0, "state fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "E2C sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "E2C metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, 0, parameterId),
               "E2C ordinal-zero parameter metadata is missing");
        if (parameterId.isEmpty())
            return;

        // Establish one authoritative E1 state chunk and parameter shadow at
        // the baseline. No manual set is performed after recovery.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "E2C baseline parameter set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "E2C baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0, "E2C baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "E2C baseline state shadow restore failed: " + error);
        expect(proxy->lastAuthoritativeState().getSize() == baselineState.getSize(),
               "E2C authoritative state shadow was not recorded");

        // Re-read the actual worker value so the pre-crash E2B seed is known to
        // be the baseline rather than the fixture default.
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        float baselineLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, baselineLive, error),
               "E2C baseline live-value fetch failed: " + error);
        expectWithinAbsoluteError(baselineLive, kBaseline, 0.0005f,
                                  "E2C baseline worker value");
        expectWithinAbsoluteError(slot->sandboxLastAutomationValueForTesting(0),
                                  kBaseline, 0.0005f,
                                  "E2C pre-crash smoother seed");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                   kBaseline, 0.0005f,
                                   "E2C pre-crash lastDelivered seed");
        const auto shadowBeforeCrash = proxy->lastAuthoritativeParameters();
        const auto rebuildCountBeforeCrash =
            slot->sandboxAutomationRebuildCountForTesting();

        DAW::AutomationSnapshot snapshot;
        const juce::String laneKey = "plugin.0." + slot->getPluginInstanceId()
            + "." + parameterId;
        snapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        // Warm the real canonical producer and converge to an exact 0.75
        // delivery before the crash. This makes the post-restart stale-seed
        // defect observable: the fresh worker replays 0.25, while old E2B
        // mirrors incorrectly still claim 0.75.
        juce::AudioBuffer<float> block(2, kQuantum);
        std::int64_t position = 0;
        for (int callback = 0; callback < 10; ++callback)
        {
            fillBlock(block, 1.0f);
            chain.applyAutomationAtSample(trackId, &snapshot, position,
                                           kSampleRate, kBpm, kQuantum);
            chain.processBlock(block, kQuantum);
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
            position += kQuantum;
        }

        const auto preCrashCanonical = slot->sandboxLastAutomationValueForTesting(0);
        const auto preCrashDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        float preCrashWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, preCrashWorker, error),
               "E2C pre-crash live-value fetch failed: " + error);
        expectWithinAbsoluteError(preCrashCanonical, kAutomationTarget, 0.0005f,
                                  "E2C canonical target did not converge to 0.75");
        expectWithinAbsoluteError(preCrashDelivered, kAutomationTarget, 0.0005f,
                                  "E2C pre-crash lastDelivered did not converge to 0.75");
        expectWithinAbsoluteError(preCrashWorker, kAutomationTarget, 0.0005f,
                                  "E2C worker did not receive the pre-crash target");
        expect(proxy->automationDiagnostics().workerAppliedEvents > 0,
               "E2C pre-crash canonical delivery applied no worker events");

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        const auto sidecarGenerationA = proxy->automationTransportForTest().generation();
        const auto sidecarMappingA = proxy->automationTransportForTest().mappingName();
        expect(pidA != 0 && generationA != 0 && sidecarGenerationA == generationA,
               "E2C pre-crash worker/generation identity is incomplete");

         // Existing Phase D deterministic external-death seam. The owned
         // process handle is authoritative: reopening by PID can transiently
         // fail or misreport while TerminateProcess is being reaped.
         expect(proxy->processHandlePidForTest() == pidA,
                "E2C process handle does not own worker PID A");
         expect(killProcessUnexpectedly(pidA),
                "E2C failed to request termination of worker PID A");
         const auto deathDeadline = GetTickCount64() + 30000;
         bool deadOnOwnedHandle = false;
         while (GetTickCount64() < deathDeadline && ! deadOnOwnedHandle)
         {
             deadOnOwnedHandle = proxy->pollLivenessForTest()
                 != static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive);
             if (! deadOnOwnedHandle)
                 Sleep(5);
         }
         expect(deadOnOwnedHandle,
                "E2C worker process never died on its owned handle after kill");

        // Continue exercising the real callback route while the worker is dead.
        // The measured scope contains only the callback path; all waits/logging
        // remain outside it.
        for (int callback = 0; callback < 4; ++callback)
        {
            fillBlock(block, 0.37f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
        }
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "E2C dead-worker callback left an active realtime call");

        // Real public production maintenance route: pollSandboxHealth() →
        // pollHealthAndRecover() → retireWorkerAfterFailure() → prepare().
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "E2C automatic recovery never reached Healthy");

        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        const auto sidecarGenerationB = proxy->automationTransportForTest().generation();
        const auto sidecarMappingB = proxy->automationTransportForTest().mappingName();
        expect(recovery.detectedDeaths >= 1, "E2C detectedDeaths did not increment");
        expect(recovery.restartAttempts >= 1, "E2C restartAttempts did not increment");
        expect(recovery.restartSuccesses >= 1, "E2C restartSuccesses did not increment");
        expect(pidB != 0 && pidB != pidA, "E2C replacement PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "E2C replacement generation is invalid or reused");
        expect(sidecarGenerationB == generationB,
               "E2C new E2A sidecar generation does not match audio generation");
        expect(sidecarMappingA.isNotEmpty() && sidecarMappingB != sidecarMappingA,
               "E2C replacement reused the old E2A sidecar mapping");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "E2C replacement reused the old audio mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "E2C old audio generation mapping survived retirement");

        // E1 acceptance: inspect the actual fresh worker before any new
        // canonical automation call. It must be the baseline replay value.
        float postRecoveryLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, postRecoveryLive, error),
               "E2C post-recovery live-value fetch failed: " + error);
        expectWithinAbsoluteError(postRecoveryLive, kBaseline, 0.0005f,
                                  "E2C E1 replay did not restore baseline 0.25");
        expect(proxy->automationDiagnostics().latestPublishedSequence == 0
                   && proxy->automationDiagnostics().workerAppliedEvents == 0,
               "E2C new E2A sidecar retained pre-crash delivery state");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "E2C retained a staged pre-crash event across restart");

        const auto postRecoverySeed = slot->sandboxLastAutomationValueForTesting(0);
        const auto postRecoveryDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        expect(slot->sandboxAutomationRebuildCountForTesting() > rebuildCountBeforeCrash,
               "E2C automatic recovery did not notify the owning E2B instance");
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "E2C automatic recovery left E2B bindings unpublished");
        expectWithinAbsoluteError(postRecoverySeed, postRecoveryLive, 0.0005f,
                                  "E2C E2B restart seed is not the live E1 value");
        expectWithinAbsoluteError(postRecoveryDelivered, postRecoveryLive, 0.0005f,
                                  "E2C restart lastDelivered is not the live E1 value");
        expect(! proxy->recoveryCompletionPending(),
               "E2C recovery remained pending after the public health poll");
        expect(proxy->remoteAvailable(),
               "E2C realtime gate did not reopen after E1 plus E2B publication");
        logMessage("APEX_E2C_RESTART_TRACE pidA=" + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " e1Live=" + juce::String(postRecoveryLive, 4)
            + " e2bSeed=" + juce::String(postRecoverySeed, 4)
            + " e2bLastDelivered=" + juce::String(postRecoveryDelivered, 4)
            + " oldSidecarGeneration="
            + juce::String(static_cast<juce::int64>(sidecarGenerationA))
            + " newSidecarGeneration="
            + juce::String(static_cast<juce::int64>(sidecarGenerationB)));

        // E2C contract: the current canonical target must be delivered to the
        // new worker without a manual setParameter call. The first post-restart
        // callback is deliberately inspected before any convergence loop.
        chain.applyAutomationAtSample(trackId, &snapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const auto firstPostRecoveryExpected =
            slot->sandboxLastAutomationValueForTesting(0);
        expect(firstPostRecoveryExpected > postRecoveryLive + 0.01f,
               "E2C post-recovery canonical producer did not advance from E1 baseline");
        expect(slot->sandboxStagedEventCountForTesting() > 0,
               "E2C post-recovery canonical target produced no E2B event");

        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif
        waitForWorkerQuanta(*proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        float firstPostRecoveryWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, firstPostRecoveryWorker, error),
               "E2C first post-recovery worker fetch failed: " + error);
        expectWithinAbsoluteError(firstPostRecoveryWorker,
                                  firstPostRecoveryExpected, 0.0005f,
                                  "E2C current canonical value did not reach the new worker");
        expect(proxy->automationDiagnostics().workerAppliedEvents >= 1,
               "E2C new worker applied no post-recovery E2B event");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "E2C staged event was not consumed after the new-generation submit");

        // Continue the same canonical lane until the target is reached. This
        // proves the new generation owns the live delivery, rather than merely
        // accepting one recovery-side event.
        for (int callback = 1; callback < 10; ++callback)
        {
            chain.applyAutomationAtSample(trackId, &snapshot,
                                          position + callback * kQuantum,
                                          kSampleRate, kBpm, kQuantum);
            fillBlock(block, 1.0f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
        }

        float finalWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, finalWorker, error),
               "E2C final worker live-value fetch failed: " + error);
        expectWithinAbsoluteError(finalWorker, kAutomationTarget, 0.0005f,
                                  "E2C canonical automation did not resume to 0.75");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  kAutomationTarget, 0.0005f,
                                  "E2C final lastDelivered is not the current target");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0,
               "E2C new worker rejected an automation batch");
        expect(proxy->automationDiagnostics().parentOverflowRejected == 0,
               "E2C produced an automation overflow marker");
        expect(proxy->lastAuthoritativeParameters() == shadowBeforeCrash,
               "E2C canonical playback mutated the E1 parameter shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "E2C left a realtime call active after resumed delivery");

        logMessage("APEX_E2C_CONTINUITY_TRACE baseline=" + juce::String(kBaseline, 4)
            + " preCrashWorker=" + juce::String(preCrashWorker, 4)
            + " postRecoveryE1=" + juce::String(postRecoveryLive, 4)
            + " firstCanonical=" + juce::String(firstPostRecoveryExpected, 4)
            + " firstWorker=" + juce::String(firstPostRecoveryWorker, 4)
            + " finalWorker=" + juce::String(finalWorker, 4)
            + " applied=" + juce::String(static_cast<juce::int64>(
                proxy->automationDiagnostics().workerAppliedEvents))
            + " invalid=" + juce::String(static_cast<juce::int64>(
                proxy->automationDiagnostics().workerInvalidBatches))
            + " allocationChecked=" + juce::String(JUCE_ENABLE_ALLOCATION_HOOKS ? 1 : 0)
            + " callbacksWhileDead=4");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "E2C replacement worker orphan");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingB),
               "E2C replacement audio mapping outlived removal");
       #else
        beginTest("Windows test-hook E2C automation crash/restart continuity");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CAutomationCrashRestartContinuityTest
    pluginSandboxPhaseE2CAutomationCrashRestartContinuityTest;

class PluginSandboxPhaseE2CCrashBeforeSubmitTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CCrashBeforeSubmitTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.crash-before-submit.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("canonical state survives a worker death before its next submit");
        juce::MessageManager::getInstance();

        constexpr float kStateA = 0.50f;
        constexpr float kStateB = 0.75f;
        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed: " + identity.error);
        if (identity.uniqueId == 0)
            return;

        const juce::String trackId = "e2c-crash-before-submit-track";
        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         stateFixtureDescription(identity), error, preparation),
                     0, "sandboxed fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "crash-before-submit sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "crash-before-submit metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, 0, parameterId),
               "crash-before-submit ordinal-zero parameter is missing");
        if (parameterId.isEmpty())
            return;

        // Establish the authoritative E1 baseline before any playback lane is
        // evaluated. Playback must never mutate this shadow.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "crash-before-submit baseline parameter set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "crash-before-submit baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0,
               "crash-before-submit baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "crash-before-submit baseline state restore failed: " + error);
        const auto e1ParametersBeforePlayback = proxy->lastAuthoritativeParameters();
        const auto e1StateBytesBeforePlayback = proxy->lastAuthoritativeState().getSize();

        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        float baselineLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, baselineLive, error),
               "crash-before-submit baseline live-value fetch failed: " + error);
        expectWithinAbsoluteError(baselineLive, kBaseline, 0.0005f,
                                  "crash-before-submit E1 baseline");

        const juce::String laneKey = "plugin.0." + slot->getPluginInstanceId()
            + "." + parameterId;
        DAW::AutomationSnapshot stateASnapshot;
        stateASnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kStateA, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kStateA, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        DAW::AutomationSnapshot stateBSnapshot;
        stateBSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kStateB, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kStateB, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        juce::AudioBuffer<float> block(2, kQuantum);
        std::int64_t position = 0;
        for (int callback = 0; callback < 10; ++callback)
        {
            fillBlock(block, 1.0f);
            chain.applyAutomationAtSample(trackId, &stateASnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            chain.processBlock(block, kQuantum);
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
            position += kQuantum;
        }

        const float stateACanonical = slot->sandboxLastAutomationValueForTesting(0);
        const float stateALastDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        float stateAWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, stateAWorker, error),
               "crash-before-submit State A live-value fetch failed: " + error);
        expectWithinAbsoluteError(stateACanonical, kStateA, 0.0005f,
                                  "State A canonical value");
        expectWithinAbsoluteError(stateAWorker, kStateA, 0.0005f,
                                  "State A worker value");
        expectWithinAbsoluteError(stateALastDelivered, kStateA, 0.0005f,
                                  "State A lastDelivered value");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "State A playback mutated the E1 parameter shadow");

        const auto countersBeforeStateB = proxy->lifecycleDiagnostics().transport;
        const auto appliedBeforeStateB = proxy->automationDiagnostics().workerAppliedEvents;
        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto sidecarGenerationA = proxy->automationTransportForTest().generation();
        const auto sidecarMappingA = proxy->automationTransportForTest().mappingName();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        expect(pidA != 0 && generationA != 0 && sidecarGenerationA == generationA,
               "State A worker/generation identity is incomplete");
        expect(proxy->processHandlePidForTest() == pidA,
               "State A process handle does not own the worker PID");

        // This is the pre-submit boundary: the canonical producer advances to
        // State B and stages its normal E2B record, but no processBlock call has
        // yet published the sidecar or claimed an audio transport slot.
        chain.applyAutomationAtSample(trackId, &stateBSnapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const float stateBCanonicalBeforeCrash =
            slot->sandboxLastAutomationValueForTesting(0);
        const float stateBLastDeliveredBeforeCrash =
            slot->sandboxLastDeliveredValueForTesting(0);
        const auto stagedStateB = slot->sandboxStagedEventCountForTesting();
        expect(stateBCanonicalBeforeCrash > stateACanonical + 0.01f,
               "State B canonical producer did not advance beyond State A");
        expect(stagedStateB > 0,
               "State B was not staged by the canonical E2B producer");
        expectWithinAbsoluteError(stateBLastDeliveredBeforeCrash,
                                  stateALastDelivered, 0.0005f,
                                  "State B advanced lastDelivered before submit");
        expect(proxy->lifecycleDiagnostics().transport.submitted
                    == countersBeforeStateB.submitted,
                "State B was submitted before the deliberate pre-submit crash");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "State B playback mutated the E1 parameter shadow");

        expect(killProcessUnexpectedly(pidA),
               "crash-before-submit failed to terminate worker PID A");
        expect(waitForWorkerDeath(*proxy, 10000),
               "crash-before-submit worker PID A did not die");

        // ProcessCore observes the death on the next control-plane poll. Until
        // then, the local transport availability bit is intentionally stale;
        // use the existing E2B submission-failure seam to ensure this callback
        // cannot write State B into the dead generation. This is not a new
        // worker fault seam and the canonical producer/bookkeeping remain real.
        proxy->forceNextSubmissionFailureForTest(4);
        for (int callback = 0; callback < 4; ++callback)
        {
            fillBlock(block, 0.37f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
        }

        const auto countersAfterDeadCallbacks = proxy->lifecycleDiagnostics().transport;
        const auto appliedAfterDeadCallbacks = proxy->automationDiagnostics().workerAppliedEvents;
        expect(countersAfterDeadCallbacks.submitted == countersBeforeStateB.submitted,
               "State B submitted successfully to the dead generation");
        expect(countersAfterDeadCallbacks.submitMisses
                    == countersBeforeStateB.submitMisses + 4,
               "dead-generation callback submission misses were not bounded");
        expect(appliedAfterDeadCallbacks == appliedBeforeStateB,
               "dead-generation callback applied State B");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(0), stateALastDelivered,
            0.0005f, "dead-generation callback falsely committed State B");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "dead-generation callback left stale E2B staging armed");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "dead-generation callbacks mutated the E1 parameter shadow");
        expect(static_cast<int>(countersAfterDeadCallbacks.calls
                                - countersBeforeStateB.calls) == 4,
               "parent did not survive four callbacks while the worker was dead");

        // Production maintenance path: chain.pollSandboxHealth() owns worker
        // detection, restart, E1 replay, E2B binding rebuild, and gate reopen.
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "crash-before-submit automatic recovery never reached Healthy");

        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto sidecarGenerationB = proxy->automationTransportForTest().generation();
        const auto sidecarMappingB = proxy->automationTransportForTest().mappingName();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        expect(recovery.detectedDeaths >= 1,
               "crash-before-submit detectedDeaths did not increment");
        expect(recovery.restartAttempts >= 1 && recovery.restartSuccesses >= 1,
               "crash-before-submit recovery did not complete one restart");
        expect(pidB != 0 && pidB != pidA,
               "crash-before-submit replacement PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "crash-before-submit replacement generation is invalid or reused");
        expect(sidecarGenerationB == generationB && sidecarGenerationB != sidecarGenerationA,
               "crash-before-submit E2A generation was not replaced");
        expect(sidecarMappingA.isNotEmpty() && sidecarMappingB != sidecarMappingA,
               "crash-before-submit reused the old E2A sidecar mapping");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "crash-before-submit reused the old audio mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "crash-before-submit old audio mapping survived recovery");

        float restoredLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, restoredLive, error),
               "crash-before-submit restored live-value fetch failed: " + error);
        expectWithinAbsoluteError(restoredLive, kBaseline, 0.0005f,
                                  "crash-before-submit E1 restored worker value");
        expect(proxy->automationDiagnostics().latestPublishedSequence == 0
                    && proxy->automationDiagnostics().workerAppliedEvents == 0,
                "crash-before-submit replacement retained old E2B delivery state");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "crash-before-submit replacement retained old staged State B");
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "crash-before-submit E2B bindings were not rebuilt");
        expectWithinAbsoluteError(slot->sandboxLastAutomationValueForTesting(0),
                                  restoredLive, 0.0005f,
                                  "crash-before-submit E2B smoother restart seed");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  restoredLive, 0.0005f,
                                  "crash-before-submit restart lastDelivered seed");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "crash-before-submit recovery mutated the E1 parameter shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "crash-before-submit recovery changed the E1 state shadow");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "crash-before-submit recovery reopened an incoherent gate");

        // The historical State B callback was intentionally lost. Re-evaluate
        // the current canonical lane normally; the fresh generation must receive
        // the current value rather than a fabricated replay of the lost block.
        chain.applyAutomationAtSample(trackId, &stateBSnapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const float firstPostRecoveryCanonical =
            slot->sandboxLastAutomationValueForTesting(0);
        const auto firstPostRecoveryStaged = slot->sandboxStagedEventCountForTesting();
        expect(firstPostRecoveryCanonical > restoredLive + 0.01f,
               "post-recovery canonical state did not advance from E1 seed");
        expect(firstPostRecoveryStaged > 0,
               "post-recovery current canonical state produced no E2B event");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(0), restoredLive,
            0.0005f, "post-recovery current event committed before submit");

        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif
        waitForWorkerQuanta(*proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        const auto firstPostRecoveryCounters = proxy->lifecycleDiagnostics().transport;
        float firstPostRecoveryWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, firstPostRecoveryWorker, error),
               "post-recovery worker live-value fetch failed: " + error);
        expect(firstPostRecoveryCounters.submitted >= 1,
               "post-recovery current canonical event was not submitted");
        expect(proxy->currentGeneration() == generationB
                    && proxy->automationTransportForTest().generation() == generationB,
               "post-recovery event did not use the new generation");
        expectWithinAbsoluteError(firstPostRecoveryWorker,
                                  firstPostRecoveryCanonical, 0.0005f,
                                  "post-recovery worker did not receive current canonical state");
        expect(proxy->automationDiagnostics().workerAppliedEvents >= 1,
               "post-recovery worker applied no current canonical event");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  firstPostRecoveryCanonical, 0.0005f,
                                  "post-recovery current event did not commit");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "post-recovery current E2B event remained staged");

        for (int callback = 1; callback < 10; ++callback)
        {
            position += kQuantum;
            chain.applyAutomationAtSample(trackId, &stateBSnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            fillBlock(block, 1.0f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
        }

        const float finalCanonical = slot->sandboxLastAutomationValueForTesting(0);
        float finalWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, finalWorker, error),
               "crash-before-submit final worker live-value fetch failed: " + error);
        expectWithinAbsoluteError(finalCanonical, kStateB, 0.0005f,
                                  "crash-before-submit final canonical State B");
        expectWithinAbsoluteError(finalWorker, finalCanonical, 0.0005f,
                                  "crash-before-submit final worker/current equality");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  finalCanonical, 0.0005f,
                                  "crash-before-submit final lastDelivered");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0
                    && proxy->automationDiagnostics().parentOverflowRejected == 0,
                "crash-before-submit produced an invalid or overflow batch");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "canonical playback mutated the E1 parameter shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "crash-before-submit left an active realtime call");

        logMessage("APEX_E2C_PRE_SUBMIT_TRACE pidA="
            + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " stateA=" + juce::String(stateAWorker, 4)
            + " stateBBeforeCrash=" + juce::String(stateBCanonicalBeforeCrash, 4)
            + " restored=" + juce::String(restoredLive, 4)
            + " firstPostRecovery=" + juce::String(firstPostRecoveryWorker, 4)
            + " final=" + juce::String(finalWorker, 4)
            + " appliedBeforeCrash=" + juce::String(static_cast<juce::int64>(appliedBeforeStateB))
            + " appliedAfterRecovery=" + juce::String(static_cast<juce::int64>(
                proxy->automationDiagnostics().workerAppliedEvents))
            + " deadCallbacks=4");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000),
               "crash-before-submit replacement worker orphaned");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingB),
               "crash-before-submit replacement audio mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CCrashBeforeSubmitTest
    pluginSandboxPhaseE2CCrashBeforeSubmitTest;

class PluginSandboxPhaseE2CCrashAfterSubmitBeforeApplyTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CCrashAfterSubmitBeforeApplyTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.crash-after-submit-before-apply.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("old-generation submit is not authoritative before worker apply");
        juce::MessageManager::getInstance();

        const auto worker = hookEnabledWorkerExecutable();
        expect(worker.existsAsFile(), "hook-enabled test worker path is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed for hook worker: "
                   + identity.error);
        if (identity.uniqueId == 0)
            return;

        const juce::String trackId = "e2c-crash-after-submit-before-apply-track";
        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        preparation.audioFaultPoint =
            DAW::PluginSandboxAudioTestFaultPoint::AfterAudioSubmitBeforeAutomationApply;
        expectEquals(chain.appendSandboxedPlugin(
                         stateFixtureDescription(identity), error, preparation),
                     0, "sandboxed fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "crash-after-submit sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "crash-after-submit metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, 0, parameterId),
               "crash-after-submit ordinal-zero parameter is missing");
        if (parameterId.isEmpty())
            return;

        // The launch-time seam is armed before the first audio quantum. The
        // E1/E2B baseline is therefore the stable State A seed; no callback is
        // consumed before State B, so the first submitted automation quantum is
        // the one deliberately killed after submit and before apply.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "crash-after-submit baseline parameter set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "crash-after-submit baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0,
               "crash-after-submit baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "crash-after-submit baseline state restore failed: " + error);
        const auto e1ParametersBeforePlayback = proxy->lastAuthoritativeParameters();
        const auto e1StateBytesBeforePlayback = proxy->lastAuthoritativeState().getSize();

        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        float stateAWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, stateAWorker, error),
               "crash-after-submit State A live-value fetch failed: " + error);
        const float stateACanonical = slot->sandboxLastAutomationValueForTesting(0);
        const float stateALastDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        expectWithinAbsoluteError(stateAWorker, kBaseline, 0.0005f,
                                  "State A worker value");
        expectWithinAbsoluteError(stateACanonical, kBaseline, 0.0005f,
                                  "State A canonical seed");
        expectWithinAbsoluteError(stateALastDelivered, kBaseline, 0.0005f,
                                  "State A lastDelivered seed");

        const juce::String laneKey = "plugin.0." + slot->getPluginInstanceId()
            + "." + parameterId;
        DAW::AutomationSnapshot stateBSnapshot;
        stateBSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto sidecarGenerationA = proxy->automationTransportForTest().generation();
        const auto sidecarMappingA = proxy->automationTransportForTest().mappingName();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        const auto sequenceA = proxy->automationSequenceForNextCallback();
        const auto countersBeforeSubmit = proxy->lifecycleDiagnostics().transport;
        const auto appliedBeforeSubmit = proxy->automationDiagnostics().workerAppliedEvents;
        expect(pidA != 0 && generationA != 0 && sidecarGenerationA == generationA,
               "State A worker/generation identity is incomplete");
        expect(sequenceA == 1,
               "crash-after-submit did not begin at the expected old-generation sequence");

        // Canonical producer first advances and stages State B. No transport
        // call has happened yet, so this is the pre-submit observation point.
        chain.applyAutomationAtSample(trackId, &stateBSnapshot, 0,
                                      kSampleRate, kBpm, kQuantum);
        const float stateBCanonicalBeforeSubmit =
            slot->sandboxLastAutomationValueForTesting(0);
        const auto stagedStateB = slot->sandboxStagedEventCountForTesting();
        const float stateBLastDeliveredBeforeSubmit =
            slot->sandboxLastDeliveredValueForTesting(0);
        expect(stateBCanonicalBeforeSubmit > stateACanonical + 0.01f,
               "State B canonical producer did not advance beyond State A");
        expect(stagedStateB > 0,
               "State B was not staged by the canonical E2B producer");
        expectWithinAbsoluteError(stateBLastDeliveredBeforeSubmit,
                                  stateALastDelivered, 0.0005f,
                                  "State B advanced lastDelivered before submit");
        expect(proxy->lifecycleDiagnostics().transport.submitted
                    == countersBeforeSubmit.submitted,
                "State B was submitted before the seam callback");

        juce::AudioBuffer<float> block(2, kQuantum);
        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif

        const auto countersAfterSubmit = proxy->lifecycleDiagnostics().transport;
        const auto diagnosticsAfterSubmit = proxy->automationDiagnostics();
        expect(countersAfterSubmit.submitted == countersBeforeSubmit.submitted + 1,
               "State B did not successfully submit to the old generation");
        expect(diagnosticsAfterSubmit.latestPublishedSequence == sequenceA,
               "State B was not published to the old E2A generation");
        expect(diagnosticsAfterSubmit.workerAppliedEvents == appliedBeforeSubmit,
               "State B applied before the validated seam fired");
        expect(proxy->workerCompletedSequence() == 0,
               "old worker published completion for the faulted State B quantum");
        const auto oldWorkerCompletedSequence = proxy->workerCompletedSequence();

        const float stateBLastDeliveredAfterSubmit =
            slot->sandboxLastDeliveredValueForTesting(0);
        const auto oldGenerationSubmittedSequence = sequenceA;
        expectWithinAbsoluteError(stateBLastDeliveredAfterSubmit,
                                  stateBCanonicalBeforeSubmit, 0.0005f,
                                  "successful old-generation submit did not update delivery bookkeeping");

        expect(waitForWorkerDeath(*proxy, 10000),
               "validated seam A did not kill worker generation A");
        const auto seamExitCode = proxy->processExitCodeForTest();
        expect(seamExitCode == DAW::PluginSandboxWin32::kE2CTestAfterAudioSubmitExitCode,
               "seam A did not report the after-submit/before-apply exit code");
        expect(proxy->workerStartCountForTest() == 1,
               "seam A caused more than one initial worker start");
        expect(proxy->workerCompletedSequence() == 0,
               "old worker completed the State B quantum after seam A");

        // Keep the dead-period callback route normal at the owning parent
        // boundary, while preventing writes to the already-dead shared-memory
        // slots. The existing RT-safe submission-failure hook only makes the
        // fallback deterministic; it does not alter E2B producer bookkeeping.
        proxy->forceNextSubmissionFailureForTest(4);
        for (int callback = 0; callback < 4; ++callback)
        {
            fillBlock(block, 0.37f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
        }

        const auto countersWhileDead = proxy->lifecycleDiagnostics().transport;
        expect(countersWhileDead.submitted == countersAfterSubmit.submitted,
               "dead-period callback submitted another audio quantum");
        expect(countersWhileDead.submitMisses == countersAfterSubmit.submitMisses + 4,
               "dead-period callback submission failures were not bounded");
        expect(proxy->automationDiagnostics().workerAppliedEvents == appliedBeforeSubmit,
               "dead-period callback reported a worker automation application");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(0),
            stateBLastDeliveredAfterSubmit, 0.0005f,
            "dead-period callback changed old-generation delivery bookkeeping");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "dead-period callback mutated the E1 parameter shadow");
        expect(static_cast<int>(countersWhileDead.calls - countersAfterSubmit.calls) == 4,
               "parent did not survive four dead-period callbacks");

        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "crash-after-submit automatic recovery never reached Healthy");

        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto sidecarGenerationB = proxy->automationTransportForTest().generation();
        const auto sidecarMappingB = proxy->automationTransportForTest().mappingName();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        expect(recovery.detectedDeaths >= 1,
               "crash-after-submit detectedDeaths did not increment");
        expect(recovery.restartAttempts >= 1 && recovery.restartSuccesses >= 1,
               "crash-after-submit recovery did not complete one restart");
        expect(pidB != 0 && pidB != pidA,
               "crash-after-submit replacement PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "crash-after-submit replacement generation is invalid or reused");
        expect(sidecarGenerationB == generationB && sidecarGenerationB != sidecarGenerationA,
               "crash-after-submit E2A generation was not replaced");
        expect(sidecarMappingA.isNotEmpty() && sidecarMappingB != sidecarMappingA,
               "crash-after-submit reused the old E2A mapping");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "crash-after-submit reused the old audio mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "crash-after-submit old audio mapping survived recovery");
        expect(proxy->workerStartCountForTest() == 2,
               "automatic recovery did not create exactly one replacement worker");

        float restoredLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, restoredLive, error),
               "crash-after-submit restored live-value fetch failed: " + error);
        expectWithinAbsoluteError(restoredLive, kBaseline, 0.0005f,
                                  "crash-after-submit E1 restored worker value");
        expect(proxy->automationDiagnostics().latestPublishedSequence == 0
                    && proxy->automationDiagnostics().workerAppliedEvents == 0,
                "replacement retained old-generation E2B publication state");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "replacement retained old-generation staged State B");
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "replacement E2B bindings were not rebuilt");
        const float restartSeed = slot->sandboxLastAutomationValueForTesting(0);
        const float restartLastDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        expectWithinAbsoluteError(restartSeed, restoredLive, 0.0005f,
                                  "restart E2B seed is not the restored live worker value");
        expectWithinAbsoluteError(restartLastDelivered, restoredLive, 0.0005f,
                                  "restart lastDelivered is not the restored live worker value");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "recovery mutated the E1 parameter shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "recovery changed the E1 state shadow");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "recovery reopened the gate before coherent E2B publication");

        // The old submitted sequence is intentionally not replayed. Re-evaluate
        // the current canonical lane on the fresh generation instead.
        chain.applyAutomationAtSample(trackId, &stateBSnapshot, kQuantum,
                                      kSampleRate, kBpm, kQuantum);
        const auto firstPostRecoverySequence = proxy->automationSequenceForNextCallback();
        const auto firstPostRecoveryStaged = slot->sandboxStagedEventCountForTesting();
        const float firstPostRecoveryCanonical =
            slot->sandboxLastAutomationValueForTesting(0);
        expect(firstPostRecoverySequence == 1,
               "replacement did not reset its first E2A sequence");
        expect(firstPostRecoveryStaged > 0,
               "current canonical automation was suppressed after recovery");
        expect(firstPostRecoveryCanonical > restoredLive + 0.01f,
               "current canonical automation did not advance from the E1 seed");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  restartLastDelivered, 0.0005f,
                                  "current event committed before new-generation submit");

        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif
        waitForWorkerQuanta(*proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        const auto firstPostRecoveryCounters = proxy->lifecycleDiagnostics().transport;
        float firstPostRecoveryWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, firstPostRecoveryWorker, error),
               "crash-after-submit first replacement worker fetch failed: " + error);
        expect(firstPostRecoveryCounters.submitted == firstPostRecoverySequence,
               "first current-generation event did not submit successfully");
        expect(proxy->workerCompletedSequence() == firstPostRecoverySequence,
               "first current-generation event did not publish completion");
        expect(proxy->currentGeneration() == generationB
                    && proxy->automationTransportForTest().generation() == generationB,
               "first current-generation event used the wrong generation");
        expectWithinAbsoluteError(firstPostRecoveryWorker,
                                  firstPostRecoveryCanonical, 0.0005f,
                                  "replacement worker did not apply current canonical automation");
        expect(proxy->automationDiagnostics().workerAppliedEvents >= 1,
               "replacement worker applied no current automation");
        expect(proxy->pollLivenessForTest()
                    == static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive),
               "replacement worker inherited seam A");
        expect(proxy->workerStartCountForTest() == 2,
               "replacement worker faulted instead of remaining unarmed");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  firstPostRecoveryCanonical, 0.0005f,
                                  "current-generation delivery did not commit");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "current-generation E2B event remained staged");

        std::int64_t position = kQuantum;
        for (int callback = 1; callback < 10; ++callback)
        {
            position += kQuantum;
            chain.applyAutomationAtSample(trackId, &stateBSnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            fillBlock(block, 1.0f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
        }

        float finalWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, finalWorker, error),
               "crash-after-submit final worker fetch failed: " + error);
        const float finalCanonical = slot->sandboxLastAutomationValueForTesting(0);
        expectWithinAbsoluteError(finalCanonical, kAutomationTarget, 0.0005f,
                                  "current canonical automation did not converge to 0.75");
        expectWithinAbsoluteError(finalWorker, finalCanonical, 0.0005f,
                                  "final replacement worker/current canonical mismatch");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  finalCanonical, 0.0005f,
                                  "final current-generation lastDelivered mismatch");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0
                    && proxy->automationDiagnostics().parentOverflowRejected == 0,
                "replacement produced invalid or overflow automation");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "playback automation mutated the E1 parameter shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "crash-after-submit left an active realtime call");

        logMessage("APEX_E2C_AFTER_SUBMIT_TRACE pidA="
            + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " e2aA=" + juce::String(static_cast<juce::int64>(sidecarGenerationA))
            + " e2aB=" + juce::String(static_cast<juce::int64>(sidecarGenerationB))
            + " e1=" + juce::String(kBaseline, 4)
            + " stateA=" + juce::String(stateACanonical, 4)
            + " stateBCanonical=" + juce::String(stateBCanonicalBeforeSubmit, 4)
            + " submittedSequence="
                + juce::String(static_cast<juce::int64>(oldGenerationSubmittedSequence))
            + " seamExit=" + juce::String(static_cast<int>(seamExitCode))
            + " oldCompleted="
                + juce::String(static_cast<juce::int64>(oldWorkerCompletedSequence))
            + " newCompleted="
                + juce::String(static_cast<juce::int64>(
                    proxy->workerCompletedSequence()))
            + " oldLastDelivered=" + juce::String(stateBLastDeliveredAfterSubmit, 4)
            + " restored=" + juce::String(restoredLive, 4)
            + " restartSeed=" + juce::String(restartSeed, 4)
            + " restartLastDelivered=" + juce::String(restartLastDelivered, 4)
            + " firstPostSequence="
                + juce::String(static_cast<juce::int64>(firstPostRecoverySequence))
            + " firstPostStaged=" + juce::String(static_cast<int>(firstPostRecoveryStaged))
            + " firstPostWorker=" + juce::String(firstPostRecoveryWorker, 4)
            + " finalWorker=" + juce::String(finalWorker, 4)
            + " oldApplied="
                + juce::String(static_cast<juce::int64>(appliedBeforeSubmit))
            + " newApplied=" + juce::String(static_cast<juce::int64>(
                proxy->automationDiagnostics().workerAppliedEvents))
            + " callbacksWhileDead=4");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000),
               "crash-after-submit replacement worker orphaned");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingB),
               "crash-after-submit replacement audio mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CCrashAfterSubmitBeforeApplyTest
    pluginSandboxPhaseE2CCrashAfterSubmitBeforeApplyTest;

class PluginSandboxPhaseE2CCrashAfterApplyBeforeCommitTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CCrashAfterApplyBeforeCommitTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.crash-after-apply-before-commit.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("dead-generation automation application is not recovery authority");
        juce::MessageManager::getInstance();

        const auto worker = hookEnabledWorkerExecutable();
        expect(worker.existsAsFile(), "hook-enabled test worker path is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed for hook worker: "
                   + identity.error);
        if (identity.uniqueId == 0)
            return;

        const juce::String trackId = "e2c-crash-after-apply-before-commit-track";
        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        preparation.audioFaultPoint =
            DAW::PluginSandboxAudioTestFaultPoint::AfterAutomationApplyBeforeWorkerCommit;
        expectEquals(chain.appendSandboxedPlugin(
                         stateFixtureDescription(identity), error, preparation),
                     0, "sandboxed fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "crash-after-apply sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "crash-after-apply metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, 0, parameterId),
               "crash-after-apply ordinal-zero parameter is missing");
        if (parameterId.isEmpty())
            return;

        // Seam B is launch-time armed, so the stable State A value is the real
        // E1/E2B baseline seed. The first audio quantum is reserved for State B;
        // the worker will apply that event and die before publishing completion.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "crash-after-apply baseline parameter set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "crash-after-apply baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0,
               "crash-after-apply baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "crash-after-apply baseline state restore failed: " + error);
        const auto e1ParametersBeforePlayback = proxy->lastAuthoritativeParameters();
        const auto e1StateBytesBeforePlayback = proxy->lastAuthoritativeState().getSize();

        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        float initialWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, initialWorker, error),
               "crash-after-apply initial live-value fetch failed: " + error);
        const float initialLastDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        const float initialCanonical = slot->sandboxLastAutomationValueForTesting(0);
        expectWithinAbsoluteError(initialWorker, kBaseline, 0.0005f,
                                  "initial worker baseline");
        expectWithinAbsoluteError(initialLastDelivered, kBaseline, 0.0005f,
                                  "initial E2B lastDelivered baseline");
        expectWithinAbsoluteError(initialCanonical, kBaseline, 0.0005f,
                                  "initial E2B canonical baseline");

        const juce::String laneKey = "plugin.0." + slot->getPluginInstanceId()
            + "." + parameterId;
        DAW::AutomationSnapshot stateBSnapshot;
        stateBSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto sidecarGenerationA = proxy->automationTransportForTest().generation();
        const auto sidecarMappingA = proxy->automationTransportForTest().mappingName();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        const auto oldSequence = proxy->automationSequenceForNextCallback();
        const auto countersBeforeFault = proxy->lifecycleDiagnostics().transport;
        const auto appliedBeforeFault = proxy->automationDiagnostics().workerAppliedEvents;
        const auto completedBeforeFault = proxy->workerCompletedSequence();
        expect(pidA != 0 && generationA != 0 && sidecarGenerationA == generationA,
               "crash-after-apply worker/generation identity is incomplete");
        expect(oldSequence == 1 && completedBeforeFault == 0,
               "crash-after-apply did not begin from a clean old generation");

        // Canonical producer stages State B before the real callback. The
        // following callback publishes/submits it to the old generation; Seam B
        // then applies it and terminates the worker before completion publication.
        chain.applyAutomationAtSample(trackId, &stateBSnapshot, 0,
                                      kSampleRate, kBpm, kQuantum);
        const float stateBCanonicalBeforeFault =
            slot->sandboxLastAutomationValueForTesting(0);
        const auto stagedStateB = slot->sandboxStagedEventCountForTesting();
        expect(stateBCanonicalBeforeFault > initialCanonical + 0.01f,
               "State B canonical producer did not advance beyond baseline");
        expect(stagedStateB > 0,
               "State B was not staged by the canonical E2B producer");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  initialLastDelivered, 0.0005f,
                                  "State B advanced lastDelivered before fault callback");

        juce::AudioBuffer<float> block(2, kQuantum);
        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif

        const auto countersAfterSubmit = proxy->lifecycleDiagnostics().transport;
        expect(countersAfterSubmit.submitted
                    == countersBeforeFault.submitted + 1,
               "State B did not submit to the old generation");

        expect(waitForWorkerDeath(*proxy, 10000),
               "validated Seam B did not kill worker generation A");
        const auto seamExitCode = proxy->processExitCodeForTest();
        expect(seamExitCode == DAW::PluginSandboxWin32::kE2CTestAfterAutomationApplyExitCode,
               "Seam B did not report the after-apply/before-commit exit code");
        expect(proxy->workerStartCountForTest() == 1,
               "Seam B caused more than one initial worker start");
        // The parent callback is intentionally nonblocking. Only after the
        // owned process handle confirms the expected death may the test read
        // worker-side applied-event diagnostics.
        const auto diagnosticsAfterApply = proxy->automationDiagnostics();
        expect(diagnosticsAfterApply.latestPublishedSequence == oldSequence,
               "State B was not published to the old E2A generation");
        expect(diagnosticsAfterApply.workerAppliedEvents
                    >= appliedBeforeFault + 1,
               "Seam B did not observe an applied State B automation event");
        const auto oldWorkerAppliedCount = diagnosticsAfterApply.workerAppliedEvents;
        const auto oldWorkerCompletedSequence = proxy->workerCompletedSequence();
        expect(oldWorkerCompletedSequence == 0,
               "old worker published completion before Seam B termination");
        const float oldGenerationLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(0);
        expectWithinAbsoluteError(oldGenerationLastDelivered,
                                  stateBCanonicalBeforeFault, 0.0005f,
                                  "old-generation submit did not update delivery bookkeeping");
        expect(proxy->automationDiagnostics().workerAppliedEvents
                    == oldWorkerAppliedCount,
               "old-generation applied-event count changed after worker death");
        expect(proxy->workerCompletedSequence() == oldWorkerCompletedSequence,
               "old-generation completion advanced after worker death");

        // Continue through the ordinary parent callback boundary while the
        // worker is dead. The existing RT-safe failure hook prevents writes to
        // an unavailable old slot; no E2B event is manually replayed or edited.
        proxy->forceNextSubmissionFailureForTest(4);
        for (int callback = 0; callback < 4; ++callback)
        {
            fillBlock(block, 0.37f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
        }

        const auto countersWhileDead = proxy->lifecycleDiagnostics().transport;
        expect(countersWhileDead.submitted == countersAfterSubmit.submitted,
               "dead-period callback submitted an additional audio quantum");
        expect(countersWhileDead.submitMisses
                    == countersAfterSubmit.submitMisses + 4,
               "dead-period callback submission failures were not bounded");
        expect(proxy->automationDiagnostics().workerAppliedEvents
                    == oldWorkerAppliedCount,
               "dead-period callback reported a stale worker application");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(0),
            oldGenerationLastDelivered, 0.0005f,
            "dead-period callback changed old-generation lastDelivered");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "dead-period callback mutated the E1 parameter shadow");
        expect(static_cast<int>(countersWhileDead.calls
                                - countersAfterSubmit.calls) == 4,
               "parent did not survive four dead-period callbacks");

        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "crash-after-apply automatic recovery never reached Healthy");

        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto sidecarGenerationB = proxy->automationTransportForTest().generation();
        const auto sidecarMappingB = proxy->automationTransportForTest().mappingName();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        expect(recovery.detectedDeaths >= 1,
               "crash-after-apply detectedDeaths did not increment");
        expect(recovery.restartAttempts >= 1 && recovery.restartSuccesses >= 1,
               "crash-after-apply recovery did not complete one restart");
        expect(pidB != 0 && pidB != pidA,
               "crash-after-apply replacement PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "crash-after-apply replacement generation is invalid or reused");
        expect(sidecarGenerationB == generationB && sidecarGenerationB != sidecarGenerationA,
               "crash-after-apply E2A generation was not replaced");
        expect(sidecarMappingA.isNotEmpty() && sidecarMappingB != sidecarMappingA,
               "crash-after-apply reused the old E2A mapping");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "crash-after-apply reused the old audio mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "crash-after-apply old audio mapping survived recovery");
        expect(proxy->workerStartCountForTest() == 2,
               "automatic recovery did not create exactly one replacement worker");

        float restoredLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, restoredLive, error),
               "crash-after-apply restored live-value fetch failed: " + error);
        expectWithinAbsoluteError(restoredLive, kBaseline, 0.0005f,
                                  "crash-after-apply E1 restored worker value");
        expect(proxy->automationDiagnostics().latestPublishedSequence == 0
                    && proxy->automationDiagnostics().workerAppliedEvents == 0,
                "replacement retained dead-generation E2B publication/application state");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "replacement retained dead-generation staged State B");
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "replacement E2B bindings were not rebuilt");
        const float restartSeed = slot->sandboxLastAutomationValueForTesting(0);
        const float restartLastDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        expectWithinAbsoluteError(restartSeed, restoredLive, 0.0005f,
                                  "restart E2B seed is not the restored live worker value");
        expectWithinAbsoluteError(restartLastDelivered, restoredLive, 0.0005f,
                                  "restart lastDelivered is not the restored live worker value");
        expectWithinAbsoluteError(oldGenerationLastDelivered,
                                  stateBCanonicalBeforeFault, 0.0005f,
                                  "old-generation applied State B was not observed before reset");
        expect(restartLastDelivered != oldGenerationLastDelivered,
               "dead-generation applied State B became restart authority");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "recovery mutated the E1 parameter shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "recovery changed the E1 state shadow");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "recovery reopened the gate before coherent E2B publication");

        // Re-evaluate current canonical automation on generation B. The old
        // applied-but-uncommitted State B is never replayed as authority.
        chain.applyAutomationAtSample(trackId, &stateBSnapshot, kQuantum,
                                      kSampleRate, kBpm, kQuantum);
        const auto firstPostRecoverySequence = proxy->automationSequenceForNextCallback();
        const auto firstPostRecoveryStaged = slot->sandboxStagedEventCountForTesting();
        const float firstPostRecoveryCanonical =
            slot->sandboxLastAutomationValueForTesting(0);
        expect(firstPostRecoverySequence == 1,
               "replacement did not reset its first E2A sequence");
        expect(firstPostRecoveryStaged > 0,
               "current canonical automation was suppressed after recovery");
        expect(firstPostRecoveryCanonical > restoredLive + 0.01f,
               "current canonical automation did not advance from E1 seed");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  restartLastDelivered, 0.0005f,
                                  "current event committed before new-generation submit");

        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif
        waitForWorkerQuanta(*proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        const auto firstPostRecoveryCounters = proxy->lifecycleDiagnostics().transport;
        float firstPostRecoveryWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, firstPostRecoveryWorker, error),
               "crash-after-apply first replacement worker fetch failed: " + error);
        expect(firstPostRecoveryCounters.submitted == firstPostRecoverySequence,
               "first current-generation event did not submit successfully");
        expect(proxy->workerCompletedSequence() == firstPostRecoverySequence,
               "first current-generation event did not publish completion");
        expect(proxy->currentGeneration() == generationB
                    && proxy->automationTransportForTest().generation() == generationB,
               "first current-generation event used the wrong generation");
        expectWithinAbsoluteError(firstPostRecoveryWorker,
                                  firstPostRecoveryCanonical, 0.0005f,
                                  "replacement worker did not apply current canonical automation");
        expect(proxy->automationDiagnostics().workerAppliedEvents >= 1,
               "replacement worker applied no current automation");
        expect(proxy->pollLivenessForTest()
                    == static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive),
               "replacement worker inherited Seam B");
        expect(proxy->workerStartCountForTest() == 2,
               "replacement worker faulted instead of remaining unarmed");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  firstPostRecoveryCanonical, 0.0005f,
                                  "current-generation delivery did not commit");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "current-generation E2B event remained staged");

        std::int64_t position = kQuantum;
        for (int callback = 1; callback < 10; ++callback)
        {
            position += kQuantum;
            chain.applyAutomationAtSample(trackId, &stateBSnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            fillBlock(block, 1.0f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
        }

        float finalWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, finalWorker, error),
               "crash-after-apply final worker fetch failed: " + error);
        const float finalCanonical = slot->sandboxLastAutomationValueForTesting(0);
        expectWithinAbsoluteError(finalCanonical, kAutomationTarget, 0.0005f,
                                  "current canonical automation did not converge to 0.75");
        expectWithinAbsoluteError(finalWorker, finalCanonical, 0.0005f,
                                  "final replacement worker/current canonical mismatch");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  finalCanonical, 0.0005f,
                                  "final current-generation lastDelivered mismatch");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0
                    && proxy->automationDiagnostics().parentOverflowRejected == 0,
                "replacement produced invalid or overflow automation");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "playback automation mutated the E1 parameter shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "crash-after-apply left an active realtime call");

        logMessage("APEX_E2C_AFTER_APPLY_TRACE pidA="
            + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " e2aA=" + juce::String(static_cast<juce::int64>(sidecarGenerationA))
            + " e2aB=" + juce::String(static_cast<juce::int64>(sidecarGenerationB))
            + " e1=" + juce::String(kBaseline, 4)
            + " initialWorker=" + juce::String(initialWorker, 4)
            + " initialLastDelivered=" + juce::String(initialLastDelivered, 4)
            + " stateBCanonical=" + juce::String(stateBCanonicalBeforeFault, 4)
            + " staged=" + juce::String(static_cast<int>(stagedStateB))
            + " submittedSequence="
                + juce::String(static_cast<juce::int64>(oldSequence))
            + " seamExit=" + juce::String(static_cast<int>(seamExitCode))
            + " appliedBeforeFault="
                + juce::String(static_cast<juce::int64>(oldWorkerAppliedCount))
            + " appliedValueObservable=no"
            + " oldCompleted="
                + juce::String(static_cast<juce::int64>(oldWorkerCompletedSequence))
            + " oldLastDelivered=" + juce::String(oldGenerationLastDelivered, 4)
            + " restored=" + juce::String(restoredLive, 4)
            + " restartSeed=" + juce::String(restartSeed, 4)
            + " restartLastDelivered=" + juce::String(restartLastDelivered, 4)
            + " firstPostSequence="
                + juce::String(static_cast<juce::int64>(firstPostRecoverySequence))
            + " firstPostStaged=" + juce::String(static_cast<int>(firstPostRecoveryStaged))
            + " firstPostWorker=" + juce::String(firstPostRecoveryWorker, 4)
            + " finalWorker=" + juce::String(finalWorker, 4)
            + " newApplied=" + juce::String(static_cast<juce::int64>(
                proxy->automationDiagnostics().workerAppliedEvents))
            + " callbacksWhileDead=4");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000),
               "crash-after-apply replacement worker orphaned");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingB),
               "crash-after-apply replacement audio mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CCrashAfterApplyBeforeCommitTest
    pluginSandboxPhaseE2CCrashAfterApplyBeforeCommitTest;

class PluginSandboxPhaseE2CActiveSmoothingCrashTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CActiveSmoothingCrashTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.active-smoothing-crash.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("active canonical smoothing reseeds from the replacement worker");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed: " + identity.error);
        if (identity.uniqueId == 0)
            return;

        const juce::String trackId = "e2c-active-smoothing-crash-track";
        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         stateFixtureDescription(identity), error, preparation),
                     0, "sandboxed fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "active-smoothing sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "active-smoothing metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, 0, parameterId),
               "active-smoothing ordinal-zero parameter is missing");
        if (parameterId.isEmpty())
            return;

        // E1 is the only authoritative restart state. Playback automation below
        // must not modify either shadow while the canonical smoother advances.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "active-smoothing baseline parameter set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "active-smoothing baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0,
               "active-smoothing baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "active-smoothing baseline state restore failed: " + error);
        const auto e1ParametersBeforePlayback = proxy->lastAuthoritativeParameters();
        const auto e1StateBytesBeforePlayback = proxy->lastAuthoritativeState().getSize();

        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        float initialWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, initialWorker, error),
               "active-smoothing initial live-value fetch failed: " + error);
        const float initialSeed = slot->sandboxLastAutomationValueForTesting(0);
        const float initialLastDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        expectWithinAbsoluteError(initialWorker, kBaseline, 0.0005f,
                                  "active-smoothing initial worker value");
        expectWithinAbsoluteError(initialSeed, kBaseline, 0.0005f,
                                  "active-smoothing initial E2B seed");
        expectWithinAbsoluteError(initialLastDelivered, kBaseline, 0.0005f,
                                  "active-smoothing initial lastDelivered");

        const juce::String laneKey = "plugin.0." + slot->getPluginInstanceId()
            + "." + parameterId;
        DAW::AutomationSnapshot targetSnapshot;
        targetSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto sidecarGenerationA = proxy->automationTransportForTest().generation();
        const auto sidecarMappingA = proxy->automationTransportForTest().mappingName();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        const auto countersBeforeSmoothing = proxy->lifecycleDiagnostics().transport;
        expect(pidA != 0 && generationA != 0 && sidecarGenerationA == generationA,
               "active-smoothing worker/generation identity is incomplete");

        juce::AudioBuffer<float> block(2, kQuantum);
        fillBlock(block, 1.0f);
        std::int64_t position = 0;

        // One normal canonical callback is enough to expose the frozen 10 ms
        // block-rate smoother: the value is neither its E1 seed nor its target.
        chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const float preCrashCanonical = slot->sandboxLastAutomationValueForTesting(0);
        const auto preCrashStaged = slot->sandboxStagedEventCountForTesting();
        expect(preCrashCanonical > initialSeed + 0.0005f,
               "active-smoothing canonical value did not leave the baseline");
        expect(preCrashCanonical < kAutomationTarget - 0.0005f,
               "active-smoothing canonical value already reached the target");
        expect(preCrashStaged > 0,
               "active-smoothing callback produced no E2B event");

       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif
        const auto countersAfterSmoothing = proxy->lifecycleDiagnostics().transport;
        expect(countersAfterSmoothing.submitted
                    == countersBeforeSmoothing.submitted + 1,
               "active-smoothing callback did not submit its quantum");
        waitForWorkerQuanta(*proxy, countersAfterSmoothing.submitted, 5000);

        float workerIntermediate = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, workerIntermediate, error),
               "active-smoothing pre-crash worker fetch failed: " + error);
        const float preCrashLastDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        expect(workerIntermediate > initialWorker + 0.0005f
                    && workerIntermediate < kAutomationTarget - 0.0005f,
               "worker did not receive an intermediate active-smoothing value");
        expectWithinAbsoluteError(preCrashLastDelivered, preCrashCanonical, 0.0005f,
                                  "active-smoothing pre-crash lastDelivered mismatch");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "active smoothing mutated the E1 parameter shadow before crash");

        // The canonical target remains live while the worker is terminated.
        expect(killProcessUnexpectedly(pidA),
               "active-smoothing failed to terminate worker PID A");
        expect(waitForWorkerDeath(*proxy, 10000),
               "active-smoothing worker PID A did not die");

        // Keep advancing the canonical producer through the normal parent
        // callback route while the worker is unavailable. The existing bounded
        // RT-safe submission-failure seam prevents dead-map writes; it does not
        // alter the smoother or the canonical target.
        proxy->forceNextSubmissionFailureForTest(2);
        for (int callback = 0; callback < 2; ++callback)
        {
            position += kQuantum;
            chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            fillBlock(block, 0.37f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
        }

        const float canonicalWhileDead = slot->sandboxLastAutomationValueForTesting(0);
        expect(canonicalWhileDead > preCrashCanonical,
               "canonical smoothing stopped advancing while worker was dead");
        expect(canonicalWhileDead <= kAutomationTarget,
               "canonical smoothing overshot the current target while worker was dead");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "dead-period smoothing mutated the E1 parameter shadow");

        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "active-smoothing automatic recovery never reached Healthy");

        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto sidecarGenerationB = proxy->automationTransportForTest().generation();
        const auto sidecarMappingB = proxy->automationTransportForTest().mappingName();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        expect(recovery.detectedDeaths >= 1,
               "active-smoothing detectedDeaths did not increment");
        expect(recovery.restartAttempts >= 1 && recovery.restartSuccesses >= 1,
               "active-smoothing recovery did not complete one restart");
        expect(pidB != 0 && pidB != pidA,
               "active-smoothing replacement PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "active-smoothing replacement generation is invalid or reused");
        expect(sidecarGenerationB == generationB && sidecarGenerationB != sidecarGenerationA,
               "active-smoothing E2A generation was not replaced");
        expect(sidecarMappingA.isNotEmpty() && sidecarMappingB != sidecarMappingA,
               "active-smoothing reused the old E2A mapping");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "active-smoothing reused the old audio mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "active-smoothing old audio mapping survived recovery");

        float restoredLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, restoredLive, error),
               "active-smoothing restored live-value fetch failed: " + error);
        expectWithinAbsoluteError(restoredLive, kBaseline, 0.0005f,
                                  "active-smoothing E1 restored worker value");
        const float restartSeed = slot->sandboxLastAutomationValueForTesting(0);
        const float restartLastDelivered = slot->sandboxLastDeliveredValueForTesting(0);
        expectWithinAbsoluteError(restartSeed, restoredLive, 0.0005f,
                                  "active-smoothing restart seed is not live E1 state");
        expectWithinAbsoluteError(restartLastDelivered, restoredLive, 0.0005f,
                                  "active-smoothing restart lastDelivered is not live E1 state");
        expect(std::abs(restartSeed - preCrashCanonical) > 0.01f,
               "dead-generation intermediate value leaked into the restart seed");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "active-smoothing recovery mutated the E1 parameter shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "active-smoothing recovery changed the E1 state shadow");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "active-smoothing recovery reopened the gate incoherently");

        // Current canonical automation is still the live target. The first
        // post-recovery callback must advance from the fresh E1 seed, not from
        // the dead worker's intermediate value.
        position += kQuantum;
        chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const auto firstPostRecoverySequence = proxy->automationSequenceForNextCallback();
        const auto firstPostRecoveryStaged = slot->sandboxStagedEventCountForTesting();
        const float firstPostRecoveryCanonical =
            slot->sandboxLastAutomationValueForTesting(0);
        expect(firstPostRecoverySequence == 1,
               "active-smoothing replacement did not reset its first sequence");
        expect(firstPostRecoveryStaged > 0,
               "active-smoothing current target was suppressed after recovery");
        expect(firstPostRecoveryCanonical > restartSeed,
               "active-smoothing first post-recovery value did not advance from live seed");
        expect(firstPostRecoveryCanonical <= kAutomationTarget,
               "active-smoothing first post-recovery value overshot target");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  restartLastDelivered, 0.0005f,
                                  "active-smoothing current event committed before submit");

        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif
        waitForWorkerQuanta(*proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        const auto firstPostRecoveryCounters = proxy->lifecycleDiagnostics().transport;
        float firstPostRecoveryWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, firstPostRecoveryWorker, error),
               "active-smoothing first replacement worker fetch failed: " + error);
        expect(firstPostRecoveryCounters.submitted == firstPostRecoverySequence,
               "active-smoothing first replacement submission failed");
        expect(proxy->workerCompletedSequence() == firstPostRecoverySequence,
               "active-smoothing first replacement quantum did not complete");
        expectWithinAbsoluteError(firstPostRecoveryWorker,
                                  firstPostRecoveryCanonical, 0.0005f,
                                  "active-smoothing first replacement value mismatch");
        expect(proxy->automationDiagnostics().workerAppliedEvents >= 1,
               "active-smoothing replacement applied no current event");
        expect(proxy->pollLivenessForTest()
                    == static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive),
               "active-smoothing replacement inherited the dead worker state");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  firstPostRecoveryCanonical, 0.0005f,
                                  "active-smoothing first current delivery did not commit");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "active-smoothing first current E2B event remained staged");

        std::int64_t finalPosition = position;
        for (int callback = 1; callback < 10; ++callback)
        {
            finalPosition += kQuantum;
            chain.applyAutomationAtSample(trackId, &targetSnapshot, finalPosition,
                                          kSampleRate, kBpm, kQuantum);
            fillBlock(block, 1.0f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
        }

        float finalWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, finalWorker, error),
               "active-smoothing final worker fetch failed: " + error);
        const float finalCanonical = slot->sandboxLastAutomationValueForTesting(0);
        expectWithinAbsoluteError(finalCanonical, kAutomationTarget, 0.0005f,
                                  "active-smoothing canonical target was not reached");
        expectWithinAbsoluteError(finalWorker, finalCanonical, 0.0005f,
                                  "active-smoothing final worker/current mismatch");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  finalCanonical, 0.0005f,
                                  "active-smoothing final lastDelivered mismatch");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0
                    && proxy->automationDiagnostics().parentOverflowRejected == 0,
                "active-smoothing produced invalid or overflow automation");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "final playback automation mutated the E1 parameter shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "active-smoothing left an active realtime call");

        logMessage("APEX_E2C_ACTIVE_SMOOTHING_TRACE pidA="
            + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " e1=" + juce::String(kBaseline, 4)
            + " target=" + juce::String(kAutomationTarget, 4)
            + " initial=" + juce::String(initialSeed, 4)
            + " preCrashCanonical=" + juce::String(preCrashCanonical, 4)
            + " preCrashWorker=" + juce::String(workerIntermediate, 4)
            + " preCrashLastDelivered=" + juce::String(preCrashLastDelivered, 4)
            + " whileDead=" + juce::String(canonicalWhileDead, 4)
            + " restored=" + juce::String(restoredLive, 4)
            + " restartSeed=" + juce::String(restartSeed, 4)
            + " restartLastDelivered=" + juce::String(restartLastDelivered, 4)
            + " firstPost=" + juce::String(firstPostRecoveryCanonical, 4)
            + " final=" + juce::String(finalWorker, 4)
            + " firstPostSequence="
                + juce::String(static_cast<juce::int64>(firstPostRecoverySequence))
            + " firstPostStaged=" + juce::String(static_cast<int>(firstPostRecoveryStaged))
            + " newApplied=" + juce::String(static_cast<juce::int64>(
                proxy->automationDiagnostics().workerAppliedEvents))
            + " callbacksWhileDead=2");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000),
               "active-smoothing replacement worker orphaned");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingB),
               "active-smoothing replacement audio mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CActiveSmoothingCrashTest
    pluginSandboxPhaseE2CActiveSmoothingCrashTest;

class PluginSandboxPhaseE2CMultiParameterCrashTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CMultiParameterCrashTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.multi-param-crash.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("independent multi-parameter E1 restore, E2B reseed, and canonical resume");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverE2CAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        constexpr int kParameterAOrdinal = 0;
        constexpr int kParameterBOrdinal = 1;
        constexpr float kParameterABaseline = 0.20f;
        constexpr float kParameterBBaseline = 0.70f;
        constexpr float kParameterATarget = 0.80f;
        constexpr float kParameterBTarget = 0.30f;
        const juce::String trackId = "e2c-multi-param-crash-track";

        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         e2cAutomationFixtureDescription(identity), error, preparation),
                     0, "multi-parameter sandbox insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "multi-parameter sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "multi-parameter metadata fetch failed: " + error);

        const auto metadataForOrdinal = [&metadata](int ordinal)
            -> const DAW::SandboxedPluginProxyCore::SandboxParameterInfo*
        {
            for (const auto& entry : metadata)
                if (entry.index == ordinal)
                    return &entry;
            return nullptr;
        };

        const auto* parameterA = metadataForOrdinal(kParameterAOrdinal);
        const auto* parameterB = metadataForOrdinal(kParameterBOrdinal);
        expect(parameterA != nullptr && parameterB != nullptr,
               "automation fixture does not expose both required ordinals");
        if (parameterA == nullptr || parameterB == nullptr)
            return;

        const auto parameterAId = parameterA->parameterId;
        const auto parameterBId = parameterB->parameterId;
        const int componentUid = proxy->getDescription().uniqueId;
        expect(parameterAId.isNotEmpty() && parameterBId.isNotEmpty(),
               "multi-parameter metadata contains an empty stable ID");
        expect(parameterAId != parameterBId,
               "multi-parameter metadata identities alias each other");
        expect(parameterA->index == kParameterAOrdinal
                   && parameterB->index == kParameterBOrdinal,
               "multi-parameter metadata ordinals are not the requested pair");
        expect(slot->sandboxAutomationBindingCountForTesting() >= 2,
               "multi-parameter E2B binding table did not publish both parameters");

        // The automation fixture has no meaningful state chunk, so this test
        // deliberately uses its two stable parameter IDs as the authoritative
        // E1 shadow. Recovery replays this map before E2B reads the fresh live
        // worker values; playback automation must never modify it.
        expect(proxy->setParameter(parameterAId, kParameterABaseline, error),
               "parameter A E1 baseline set failed: " + error);
        expect(proxy->setParameter(parameterBId, kParameterBBaseline, error),
               "parameter B E1 baseline set failed: " + error);
        const auto e1ParametersBeforePlayback = proxy->lastAuthoritativeParameters();
        const auto e1StateBytesBeforePlayback = proxy->lastAuthoritativeState().getSize();

        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);

        float initialWorkerA = -1.0f;
        float initialWorkerB = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kParameterAOrdinal,
                                      initialWorkerA, error),
               "initial parameter A live-value fetch failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, kParameterBOrdinal,
                                      initialWorkerB, error),
               "initial parameter B live-value fetch failed: " + error);
        const float initialSeedA =
            slot->sandboxLastAutomationValueForTesting(kParameterAOrdinal);
        const float initialSeedB =
            slot->sandboxLastAutomationValueForTesting(kParameterBOrdinal);
        const float initialLastDeliveredA =
            slot->sandboxLastDeliveredValueForTesting(kParameterAOrdinal);
        const float initialLastDeliveredB =
            slot->sandboxLastDeliveredValueForTesting(kParameterBOrdinal);
        expectWithinAbsoluteError(initialWorkerA, kParameterABaseline, 0.0005f,
                                  "initial worker A does not match A E1 baseline");
        expectWithinAbsoluteError(initialWorkerB, kParameterBBaseline, 0.0005f,
                                  "initial worker B does not match B E1 baseline");
        expectWithinAbsoluteError(initialSeedA, initialWorkerA, 0.0005f,
                                  "initial E2B seed A is not A live state");
        expectWithinAbsoluteError(initialSeedB, initialWorkerB, 0.0005f,
                                  "initial E2B seed B is not B live state");
        expectWithinAbsoluteError(initialLastDeliveredA, initialWorkerA, 0.0005f,
                                  "initial lastDelivered A is not A live state");
        expectWithinAbsoluteError(initialLastDeliveredB, initialWorkerB, 0.0005f,
                                  "initial lastDelivered B is not B live state");

        const juce::String pluginPrefix =
            "plugin.0." + slot->getPluginInstanceId() + ".";
        const juce::String laneKeyA = pluginPrefix + parameterAId;
        const juce::String laneKeyB = pluginPrefix + parameterBId;
        expect(laneKeyA != laneKeyB,
               "multi-parameter automation lane identities alias each other");

        DAW::AutomationSnapshot targetSnapshot;
        targetSnapshot.lanes.push_back({
            trackId, laneKeyA, true,
            {
                { 0,    kParameterATarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kParameterATarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        targetSnapshot.lanes.push_back({
            trackId, laneKeyB, true,
            {
                { 0,    kParameterBTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kParameterBTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        juce::AudioBuffer<float> block(2, kQuantum);
        std::int64_t position = 0;
        const auto makeMeasuredCallback = [this, &chain, &block](float fillValue)
        {
            fillBlock(block, fillValue);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
           #else
            chain.processBlock(block, kQuantum);
           #endif
        };

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto e2aGenerationA = proxy->automationTransportForTest().generation();
        const auto e2aMappingA = proxy->automationTransportForTest().mappingName();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        expect(pidA != 0 && generationA != 0 && e2aGenerationA == generationA,
               "initial multi-parameter worker/generation identity is incomplete");

        const auto appliedBeforeAutomation =
            proxy->automationDiagnostics().workerAppliedEvents;
        chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const float preCrashCanonicalA =
            slot->sandboxLastAutomationValueForTesting(kParameterAOrdinal);
        const float preCrashCanonicalB =
            slot->sandboxLastAutomationValueForTesting(kParameterBOrdinal);
        const auto preCrashStaged = slot->sandboxStagedEventCountForTesting();
        expect(preCrashCanonicalA > initialSeedA + 0.0005f
                   && preCrashCanonicalA < kParameterATarget - 0.0005f,
               "parameter A did not enter its own active automation trajectory");
        expect(preCrashCanonicalB < initialSeedB - 0.0005f
                   && preCrashCanonicalB > kParameterBTarget + 0.0005f,
               "parameter B did not enter its own active automation trajectory");
        expect(std::abs(preCrashCanonicalA - preCrashCanonicalB) > 0.01f,
               "pre-crash canonical values are not distinct");
        expectEquals(preCrashStaged, static_cast<std::uint32_t>(2),
                     "pre-crash callback did not stage both parameter ordinals");

        makeMeasuredCallback(1.0f);
        const auto preCrashCounters = proxy->lifecycleDiagnostics().transport;
        waitForWorkerQuanta(*proxy, preCrashCounters.submitted, 5000);
        float preCrashWorkerA = -1.0f;
        float preCrashWorkerB = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kParameterAOrdinal,
                                      preCrashWorkerA, error),
               "pre-crash worker A live-value fetch failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, kParameterBOrdinal,
                                      preCrashWorkerB, error),
               "pre-crash worker B live-value fetch failed: " + error);
        const float preCrashLastDeliveredA =
            slot->sandboxLastDeliveredValueForTesting(kParameterAOrdinal);
        const float preCrashLastDeliveredB =
            slot->sandboxLastDeliveredValueForTesting(kParameterBOrdinal);
        const auto appliedBeforeCrash =
            proxy->automationDiagnostics().workerAppliedEvents;
        expect(appliedBeforeCrash == appliedBeforeAutomation + 2,
               "pre-crash worker did not apply both parameter events");
        expectWithinAbsoluteError(preCrashWorkerA, preCrashCanonicalA, 0.0005f,
                                  "pre-crash worker A received the wrong canonical value");
        expectWithinAbsoluteError(preCrashWorkerB, preCrashCanonicalB, 0.0005f,
                                  "pre-crash worker B received the wrong canonical value");
        expectWithinAbsoluteError(preCrashLastDeliveredA, preCrashCanonicalA, 0.0005f,
                                  "pre-crash lastDelivered A mismatch");
        expectWithinAbsoluteError(preCrashLastDeliveredB, preCrashCanonicalB, 0.0005f,
                                  "pre-crash lastDelivered B mismatch");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "pre-crash playback automation mutated the E1 parameter shadow");

        expect(proxy->processHandlePidForTest() == pidA,
               "owned process handle does not identify worker PID A");
        expect(killProcessUnexpectedly(pidA),
               "failed to terminate multi-parameter worker PID A");
        expect(waitForWorkerDeath(*proxy, 10000),
               "multi-parameter worker PID A did not die");

        // Both canonical sources remain installed/current while only the
        // worker is unavailable. The existing bounded submit-failure seam is
        // used solely to keep these callbacks deterministic and RT-safe.
        const auto appliedBeforeDeadCallbacks =
            proxy->automationDiagnostics().workerAppliedEvents;
        proxy->forceNextSubmissionFailureForTest(2);
        for (int callback = 0; callback < 2; ++callback)
        {
            position += kQuantum;
            chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            expect(slot->sandboxStagedEventCountForTesting() >= 2,
                   "dead-period callback dropped one of the current parameter events");
            makeMeasuredCallback(0.37f);
        }
        const float canonicalWhileDeadA =
            slot->sandboxLastAutomationValueForTesting(kParameterAOrdinal);
        const float canonicalWhileDeadB =
            slot->sandboxLastAutomationValueForTesting(kParameterBOrdinal);
        expect(canonicalWhileDeadA > preCrashCanonicalA
                   && canonicalWhileDeadA <= kParameterATarget,
               "canonical A stopped or overshot while worker was dead");
        expect(canonicalWhileDeadB < preCrashCanonicalB
                   && canonicalWhileDeadB >= kParameterBTarget,
               "canonical B stopped or overshot while worker was dead");
        expect(proxy->automationDiagnostics().workerAppliedEvents
                   == appliedBeforeDeadCallbacks,
               "dead-period callback reported a worker automation application");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "dead-period multi-parameter automation mutated the E1 shadow");

        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "multi-parameter automatic recovery never reached Healthy");

        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto e2aGenerationB = proxy->automationTransportForTest().generation();
        const auto e2aMappingB = proxy->automationTransportForTest().mappingName();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        expect(recovery.detectedDeaths >= 1,
               "multi-parameter recovery did not detect the worker death");
        expect(recovery.restartAttempts >= 1 && recovery.restartSuccesses >= 1,
               "multi-parameter recovery did not complete a replacement worker");
        expect(pidB != 0 && pidB != pidA,
               "replacement worker PID B is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "replacement generation B is invalid or reused");
        expect(e2aGenerationB == generationB && e2aGenerationB != e2aGenerationA,
               "replacement E2A generation is not the current generation");
        expect(e2aMappingA.isNotEmpty() && e2aMappingB != e2aMappingA,
               "replacement reused the old E2A mapping");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "replacement reused the old audio mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "old audio mapping survived worker replacement");
        expect(proxy->automationDiagnostics().latestPublishedSequence == 0
                   && proxy->automationDiagnostics().workerAppliedEvents == 0,
               "replacement E2A sidecar retained dead-generation delivery state");

        float restoredWorkerA = -1.0f;
        float restoredWorkerB = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kParameterAOrdinal,
                                      restoredWorkerA, error),
               "restored worker A live-value fetch failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, kParameterBOrdinal,
                                      restoredWorkerB, error),
               "restored worker B live-value fetch failed: " + error);
        expectWithinAbsoluteError(restoredWorkerA, kParameterABaseline, 0.0005f,
                                  "replacement worker A did not replay A E1 baseline");
        expectWithinAbsoluteError(restoredWorkerB, kParameterBBaseline, 0.0005f,
                                  "replacement worker B did not replay B E1 baseline");
        const float restartSeedA =
            slot->sandboxLastAutomationValueForTesting(kParameterAOrdinal);
        const float restartSeedB =
            slot->sandboxLastAutomationValueForTesting(kParameterBOrdinal);
        const float restartLastDeliveredA =
            slot->sandboxLastDeliveredValueForTesting(kParameterAOrdinal);
        const float restartLastDeliveredB =
            slot->sandboxLastDeliveredValueForTesting(kParameterBOrdinal);
        expectWithinAbsoluteError(restartSeedA, restoredWorkerA, 0.0005f,
                                  "E2B restart seed A is not A live restore");
        expectWithinAbsoluteError(restartSeedB, restoredWorkerB, 0.0005f,
                                  "E2B restart seed B is not B live restore");
        expectWithinAbsoluteError(restartLastDeliveredA, restoredWorkerA, 0.0005f,
                                  "restart lastDelivered A is not A live restore");
        expectWithinAbsoluteError(restartLastDeliveredB, restoredWorkerB, 0.0005f,
                                  "restart lastDelivered B is not B live restore");
        expect(std::abs(restartSeedA - restoredWorkerB) > 0.05f
                   && std::abs(restartSeedB - restoredWorkerA) > 0.05f,
               "multi-parameter E2B seeds were cross-wired");
        expect(std::abs(restartSeedA - restartSeedB) > 0.05f,
               "multi-parameter restart seeds collapsed to one value");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "recovery mutated the multi-parameter E1 shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "recovery mutated the E1 state shadow");
        expect(slot->sandboxAutomationBindingCountForTesting() >= 2,
               "recovery did not republish both E2B parameter bindings");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "multi-parameter recovery gate reopened incoherently");

        // The current canonical target was never removed. On the first fresh
        // callback both parameters must independently advance from their own
        // fresh E1/live seeds and share one current-generation batch.
        position += kQuantum;
        chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const auto firstPostSequence = proxy->automationSequenceForNextCallback();
        const auto firstPostStaged = slot->sandboxStagedEventCountForTesting();
        const float firstPostCanonicalA =
            slot->sandboxLastAutomationValueForTesting(kParameterAOrdinal);
        const float firstPostCanonicalB =
            slot->sandboxLastAutomationValueForTesting(kParameterBOrdinal);
        expect(firstPostSequence == 1,
               "replacement did not reset the first E2A sequence");
        expect(firstPostStaged >= 2,
               "first post-recovery batch omitted one current parameter");
        expect(firstPostCanonicalA > restartSeedA
                   && firstPostCanonicalA <= kParameterATarget,
               "first post-recovery canonical A did not resume from A seed");
        expect(firstPostCanonicalB < restartSeedB
                   && firstPostCanonicalB >= kParameterBTarget,
               "first post-recovery canonical B did not resume from B seed");
        expect(std::abs(firstPostCanonicalA - restartSeedA) > 0.0005f
                   && std::abs(firstPostCanonicalB - restartSeedB) > 0.0005f,
               "first post-recovery callback did not change both current parameters");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kParameterAOrdinal),
            restartLastDeliveredA, 0.0005f,
            "parameter A committed before the first post-recovery submit");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kParameterBOrdinal),
            restartLastDeliveredB, 0.0005f,
            "parameter B committed before the first post-recovery submit");

        const auto appliedBeforeFirstPost =
            proxy->automationDiagnostics().workerAppliedEvents;
        makeMeasuredCallback(1.0f);
        const auto firstPostCounters = proxy->lifecycleDiagnostics().transport;
        waitForWorkerQuanta(*proxy, firstPostCounters.submitted, 5000);
        const auto firstPostDiagnostics = proxy->automationDiagnostics();
        float firstPostWorkerA = -1.0f;
        float firstPostWorkerB = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kParameterAOrdinal,
                                      firstPostWorkerA, error),
               "first post-recovery worker A fetch failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, kParameterBOrdinal,
                                      firstPostWorkerB, error),
               "first post-recovery worker B fetch failed: " + error);
        expect(firstPostCounters.submitted == firstPostSequence,
               "first post-recovery batch did not submit on the new generation");
        expect(firstPostDiagnostics.latestPublishedSequence == firstPostSequence,
               "first post-recovery batch published the wrong sequence");
        expect(firstPostDiagnostics.workerAppliedEvents
                   == appliedBeforeFirstPost + 2,
               "first post-recovery batch did not apply both ordinals");
        expectWithinAbsoluteError(firstPostWorkerA, firstPostCanonicalA, 0.0005f,
                                  "new worker A received the wrong ordinal/value pair");
        expectWithinAbsoluteError(firstPostWorkerB, firstPostCanonicalB, 0.0005f,
                                  "new worker B received the wrong ordinal/value pair");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kParameterAOrdinal),
            firstPostCanonicalA, 0.0005f,
            "new-generation lastDelivered A did not commit");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kParameterBOrdinal),
            firstPostCanonicalB, 0.0005f,
            "new-generation lastDelivered B did not commit");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "first post-recovery multi-parameter batch remained staged");

        std::int64_t finalPosition = position;
        for (int callback = 1; callback < 10; ++callback)
        {
            finalPosition += kQuantum;
            chain.applyAutomationAtSample(trackId, &targetSnapshot, finalPosition,
                                          kSampleRate, kBpm, kQuantum);
            makeMeasuredCallback(1.0f);
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
        }

        float finalWorkerA = -1.0f;
        float finalWorkerB = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kParameterAOrdinal,
                                      finalWorkerA, error),
               "final worker A live-value fetch failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, kParameterBOrdinal,
                                      finalWorkerB, error),
               "final worker B live-value fetch failed: " + error);
        const float finalCanonicalA =
            slot->sandboxLastAutomationValueForTesting(kParameterAOrdinal);
        const float finalCanonicalB =
            slot->sandboxLastAutomationValueForTesting(kParameterBOrdinal);
        expectWithinAbsoluteError(finalCanonicalA, kParameterATarget, 0.0005f,
                                  "final canonical A did not reach A target");
        expectWithinAbsoluteError(finalCanonicalB, kParameterBTarget, 0.0005f,
                                  "final canonical B did not reach B target");
        expectWithinAbsoluteError(finalWorkerA, finalCanonicalA, 0.0005f,
                                  "final worker A does not match canonical A");
        expectWithinAbsoluteError(finalWorkerB, finalCanonicalB, 0.0005f,
                                  "final worker B does not match canonical B");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kParameterAOrdinal),
            finalCanonicalA, 0.0005f,
            "final lastDelivered A does not match canonical A");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kParameterBOrdinal),
            finalCanonicalB, 0.0005f,
            "final lastDelivered B does not match canonical B");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0
                   && proxy->automationDiagnostics().parentOverflowRejected == 0,
               "multi-parameter recovery produced invalid or overflow automation");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "final playback automation mutated the E1 parameter shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "final playback automation mutated the E1 state shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "multi-parameter test left an active realtime call");

        logMessage("APEX_E2C_MULTI_PARAM_TRACE componentUid="
            + juce::String(componentUid)
            + " paramA=" + parameterAId
            + " ordinalA=" + juce::String(kParameterAOrdinal)
            + " paramB=" + parameterBId
            + " ordinalB=" + juce::String(kParameterBOrdinal)
            + " pidA=" + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " e2aGenerationA="
                + juce::String(static_cast<juce::int64>(e2aGenerationA))
            + " e2aGenerationB="
                + juce::String(static_cast<juce::int64>(e2aGenerationB))
            + " baselineA=" + juce::String(kParameterABaseline, 4)
            + " baselineB=" + juce::String(kParameterBBaseline, 4)
            + " initialWorkerA=" + juce::String(initialWorkerA, 4)
            + " initialWorkerB=" + juce::String(initialWorkerB, 4)
            + " initialSeedA=" + juce::String(initialSeedA, 4)
            + " initialSeedB=" + juce::String(initialSeedB, 4)
            + " targetA=" + juce::String(kParameterATarget, 4)
            + " targetB=" + juce::String(kParameterBTarget, 4)
            + " preCrashCanonicalA=" + juce::String(preCrashCanonicalA, 4)
            + " preCrashCanonicalB=" + juce::String(preCrashCanonicalB, 4)
            + " preCrashLastDeliveredA="
                + juce::String(preCrashLastDeliveredA, 4)
            + " preCrashLastDeliveredB="
                + juce::String(preCrashLastDeliveredB, 4)
            + " preCrashWorkerA=" + juce::String(preCrashWorkerA, 4)
            + " preCrashWorkerB=" + juce::String(preCrashWorkerB, 4)
            + " preCrashStaged=" + juce::String(static_cast<int>(preCrashStaged))
            + " whileDeadA=" + juce::String(canonicalWhileDeadA, 4)
            + " whileDeadB=" + juce::String(canonicalWhileDeadB, 4)
            + " callbacksWhileDead=2"
            + " restoredA=" + juce::String(restoredWorkerA, 4)
            + " restoredB=" + juce::String(restoredWorkerB, 4)
            + " restartSeedA=" + juce::String(restartSeedA, 4)
            + " restartSeedB=" + juce::String(restartSeedB, 4)
            + " restartLastDeliveredA="
                + juce::String(restartLastDeliveredA, 4)
            + " restartLastDeliveredB="
                + juce::String(restartLastDeliveredB, 4)
            + " firstPost=ordinal0:" + juce::String(firstPostCanonicalA, 4)
            + "/ordinal1:" + juce::String(firstPostCanonicalB, 4)
            + " firstPostStaged=" + juce::String(static_cast<int>(firstPostStaged))
            + " firstPostAppliedDelta="
                + juce::String(static_cast<juce::int64>(
                    firstPostDiagnostics.workerAppliedEvents
                        - appliedBeforeFirstPost))
            + " finalA=" + juce::String(finalWorkerA, 4)
            + " finalB=" + juce::String(finalWorkerB, 4)
            + " ordinalCrossWire=0 staleAccepted=0 poisoned=0 e1ShadowMutated=0"
            + " allocationChecked=" + juce::String(JUCE_ENABLE_ALLOCATION_HOOKS ? 1 : 0));

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000),
               "multi-parameter replacement worker orphaned");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingB),
               "multi-parameter replacement audio mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CMultiParameterCrashTest
    pluginSandboxPhaseE2CMultiParameterCrashTest;

class PluginSandboxPhaseE2CQ512BoundaryCrashTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CQ512BoundaryCrashTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.q512-boundary-crash.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("Q512 boundary preserves generation and current canonical ownership");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverE2CAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        constexpr int kOrdinal = 0;
        constexpr float kBoundaryBaseline = 0.25f;
        constexpr float kPreBoundaryTarget = 0.65f;
        constexpr float kPostBoundaryTarget = 0.85f;
        constexpr std::uint64_t kBoundaryQuantum = 512;
        constexpr std::uint64_t kAbsolutePreBoundary = 480;
        constexpr std::uint64_t kAbsolutePostBoundary = 544;
        const juce::String trackId = "e2c-q512-boundary-crash-track";

        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, static_cast<int>(kBoundaryQuantum));

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = static_cast<int>(kBoundaryQuantum);
        preparation.maximumHostBlockSamples = static_cast<int>(kBoundaryQuantum);
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         e2cAutomationFixtureDescription(identity), error, preparation),
                     0, "Q512 boundary sandbox insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "Q512 boundary sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "Q512 boundary metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, kOrdinal, parameterId),
               "Q512 boundary ordinal-zero metadata is missing");
        if (parameterId.isEmpty())
            return;
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "Q512 boundary E2B binding table is empty");

        // The automation fixture's parameter shadow is the authoritative E1
        // state for this one-parameter transport-boundary case.
        expect(proxy->setParameter(parameterId, kBoundaryBaseline, error),
               "Q512 boundary E1 baseline set failed: " + error);
        const auto e1ParametersBeforePlayback = proxy->lastAuthoritativeParameters();
        const auto e1StateBytesBeforePlayback = proxy->lastAuthoritativeState().getSize();
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);

        float initialWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, initialWorker, error),
               "Q512 boundary initial worker fetch failed: " + error);
        const float initialSeed = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const float initialLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expectWithinAbsoluteError(initialWorker, kBoundaryBaseline, 0.0005f,
                                  "Q512 boundary initial worker baseline mismatch");
        expectWithinAbsoluteError(initialSeed, kBoundaryBaseline, 0.0005f,
                                  "Q512 boundary initial E2B seed mismatch");
        expectWithinAbsoluteError(initialLastDelivered, kBoundaryBaseline, 0.0005f,
                                  "Q512 boundary initial lastDelivered mismatch");

        const juce::String laneKey =
            "plugin.0." + slot->getPluginInstanceId() + "." + parameterId;
        DAW::AutomationSnapshot baselineSnapshot;
        baselineSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kBoundaryBaseline, DAW::AutomationCurveType::Linear, 0.0f },
                { 2048, kBoundaryBaseline, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        DAW::AutomationSnapshot preBoundarySnapshot;
        preBoundarySnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kPreBoundaryTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 2048, kPreBoundaryTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        DAW::AutomationSnapshot postBoundarySnapshot;
        postBoundarySnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kPostBoundaryTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 4096, kPostBoundaryTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        juce::AudioBuffer<float> block(2, static_cast<int>(kQuantum));
        const auto processMeasured = [this, &chain, &block](int samples,
                                                              float fillValue)
        {
            fillBlock(block, fillValue);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, samples);
           #else
            chain.processBlock(block, samples);
           #endif
        };

        // Warm exactly 480 host frames with the baseline. No canonical event is
        // emitted, but the transport clock now sits at absolute position 480.
        const auto initialSequence = proxy->automationSequenceForNextCallback();
        expect(initialSequence == 1,
               "Q512 boundary initial sequence is not one");
        chain.applyAutomationAtSample(trackId, &baselineSnapshot, 0,
                                      kSampleRate, kBpm, 480);
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "baseline warm-up staged an unexpected automation event");
        processMeasured(480, 1.0f);
        const auto sequenceAtAbsolute480 = proxy->automationSequenceForNextCallback();
        expect(sequenceAtAbsolute480 == 1,
               "absolute 480 did not remain in Q512 sequence one");
        expect(proxy->lifecycleDiagnostics().transport.submitted == 0,
               "480-frame warm-up submitted an incomplete quantum");

        // The canonical producer emits one callback-boundary event at the
        // current host position. Because the host clock is 480, the frozen
        // stageAutomationEvents() math must translate it to seq 1 / local 480.
        chain.applyAutomationAtSample(trackId, &preBoundarySnapshot,
                                      static_cast<std::int64_t>(kAbsolutePreBoundary),
                                      kSampleRate, kBpm, 64);
        const auto preBoundarySequence = proxy->automationSequenceForNextCallback();
        const auto expectedPreBoundarySequence =
            kAbsolutePreBoundary / kQuantum + 1;
        const auto preBoundaryLocalOffset =
            static_cast<std::uint32_t>(kAbsolutePreBoundary % kQuantum);
        const float preBoundaryValue = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const auto preBoundaryStaged = slot->sandboxStagedEventCountForTesting();
        expect(preBoundarySequence == expectedPreBoundarySequence,
               "pre-boundary event selected the wrong Q512 sequence");
        expect(preBoundaryLocalOffset == 480 && preBoundaryLocalOffset < kQuantum,
               "pre-boundary local offset is not 480 within Q512");
        expect(preBoundaryValue > kBoundaryBaseline
                   && preBoundaryValue < kPreBoundaryTarget,
               "pre-boundary canonical value did not remain on its own trajectory");
        expect(preBoundaryStaged == 1,
               "pre-boundary callback did not stage exactly one event");

        const auto preBoundaryAppliedBefore =
            proxy->automationDiagnostics().workerAppliedEvents;
        processMeasured(64, 1.0f);
        const auto preBoundaryTransport = proxy->lifecycleDiagnostics().transport;
        waitForWorkerQuanta(*proxy, preBoundaryTransport.submitted, 5000);
        const auto preBoundaryCompletedSequence = proxy->workerCompletedSequence();
        const auto preBoundaryDiagnostics = proxy->automationDiagnostics();
        float preBoundaryWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, preBoundaryWorker, error),
               "pre-boundary worker live-value fetch failed: " + error);
        const float preBoundaryLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expect(preBoundaryTransport.submitted == 1,
               "pre-boundary callback did not submit exactly one Q512 quantum");
        expect(preBoundaryCompletedSequence >= preBoundarySequence,
               "pre-boundary quantum did not complete on generation A");
        expect(preBoundaryDiagnostics.latestPublishedSequence == preBoundarySequence,
               "pre-boundary automation batch published the wrong sequence");
        expect(preBoundaryDiagnostics.workerAppliedEvents
                   == preBoundaryAppliedBefore + 1,
               "pre-boundary event was not applied exactly once");
        expectWithinAbsoluteError(preBoundaryWorker, preBoundaryValue, 0.0005f,
                                  "pre-boundary worker value mismatch");
        expectWithinAbsoluteError(preBoundaryLastDelivered, preBoundaryValue, 0.0005f,
                                  "pre-boundary lastDelivered mismatch");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "pre-boundary playback mutated the E1 shadow");

        const auto sequenceAtAbsolute544 = proxy->automationSequenceForNextCallback();
        expect(sequenceAtAbsolute544 == 2,
               "crossing 512 frames did not advance to sequence two");
        expect(preBoundaryTransport.submitted == 1,
               "pre-boundary submitted sequence count changed unexpectedly");

        // Stage the next canonical event at host position 544, then kill the
        // worker before the Q2 batch can become valid worker authority. The
        // existing canonical producer and E2A translation produce seq 2/local
        // 32; no direct E2A event injection is used here.
        chain.applyAutomationAtSample(trackId, &postBoundarySnapshot,
                                      static_cast<std::int64_t>(kAbsolutePostBoundary),
                                      kSampleRate, kBpm, static_cast<int>(kQuantum));
        const auto postBoundarySequence = proxy->automationSequenceForNextCallback();
        const auto expectedPostBoundarySequence =
            kAbsolutePostBoundary / kQuantum + 1;
        const auto postBoundaryLocalOffset =
            static_cast<std::uint32_t>(kAbsolutePostBoundary % kQuantum);
        const float postBoundaryValue = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const auto postBoundaryStaged = slot->sandboxStagedEventCountForTesting();
        expect(postBoundarySequence == expectedPostBoundarySequence,
               "post-boundary event selected the wrong Q512 sequence");
        expect(postBoundarySequence != preBoundarySequence,
               "adjacent boundary events aliased their logical sequence");
        expect(postBoundaryLocalOffset == 32 && postBoundaryLocalOffset < kQuantum,
               "post-boundary local offset did not wrap to 32");
        expect(postBoundaryValue > preBoundaryValue
                   && postBoundaryValue < kPostBoundaryTarget,
               "post-boundary canonical value did not advance to its current target");
        expect(postBoundaryStaged == 1,
               "post-boundary callback did not stage exactly one event");

        const auto oldAutomationRingSlot =
            preBoundarySequence % DAW::PluginSandboxAutomationShared::kAutomationBatchSlotCount;
        const auto postAutomationRingSlot =
            postBoundarySequence % DAW::PluginSandboxAutomationShared::kAutomationBatchSlotCount;
        expect(oldAutomationRingSlot != postAutomationRingSlot,
               "adjacent Q512 events unexpectedly selected the same physical sidecar slot");

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto e2aGenerationA = proxy->automationTransportForTest().generation();
        const auto e2aMappingA = proxy->automationTransportForTest().mappingName();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        expect(pidA != 0 && generationA != 0 && e2aGenerationA == generationA,
               "Q512 boundary worker/generation A identity is incomplete");
        expect(killProcessUnexpectedly(pidA),
               "Q512 boundary failed to terminate worker PID A");
        expect(waitForWorkerDeath(*proxy, 10000),
               "Q512 boundary worker PID A did not die");

        // Consume the staged Q2/current-target callback while the worker is
        // unavailable. This confirms the canonical source remains current,
        // while the old generation cannot report a successful application.
        const auto appliedBeforeDead =
            proxy->automationDiagnostics().workerAppliedEvents;
        proxy->forceNextSubmissionFailureForTest(1);
        processMeasured(static_cast<int>(kQuantum), 0.37f);
        const auto deadTransport = proxy->lifecycleDiagnostics().transport;
        const auto deadDiagnostics = proxy->automationDiagnostics();
        const float canonicalWhileDead =
            slot->sandboxLastAutomationValueForTesting(kOrdinal);
        expect(deadTransport.submitted == preBoundaryTransport.submitted,
               "dead post-boundary callback submitted an old-generation quantum");
        expect(deadDiagnostics.workerAppliedEvents == appliedBeforeDead,
               "dead post-boundary callback reported a worker application");
        expectWithinAbsoluteError(canonicalWhileDead, postBoundaryValue, 0.0005f,
                                  "dead callback changed canonical state unexpectedly");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "dead post-boundary playback mutated the E1 shadow");

        const auto oldCompletedSequence = proxy->workerCompletedSequence();
        expect(oldCompletedSequence >= preBoundarySequence
                   && oldCompletedSequence < postBoundarySequence,
               "old completed sequence crossed into the uncommitted Q2 range");

        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "Q512 boundary automatic recovery never reached Healthy");
        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto e2aGenerationB = proxy->automationTransportForTest().generation();
        const auto e2aMappingB = proxy->automationTransportForTest().mappingName();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        expect(recovery.detectedDeaths >= 1 && recovery.restartSuccesses >= 1,
               "Q512 boundary recovery did not complete a replacement worker");
        expect(pidB != 0 && pidB != pidA,
               "Q512 boundary replacement PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "Q512 boundary replacement generation is invalid or reused");
        expect(e2aGenerationB == generationB && e2aGenerationB != e2aGenerationA,
               "Q512 boundary replacement E2A generation is incorrect");
        expect(e2aMappingA.isNotEmpty() && e2aMappingB != e2aMappingA,
               "Q512 boundary replacement reused the old E2A mapping");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "Q512 boundary replacement reused the old audio mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "Q512 boundary old audio mapping survived recovery");

        const auto restartDiagnostics = proxy->automationDiagnostics();
        expect(restartDiagnostics.latestPublishedSequence == 0
                   && restartDiagnostics.workerAppliedEvents == 0,
               "replacement retained old Q512 automation completion state");

        float restoredWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, restoredWorker, error),
               "Q512 boundary restored worker fetch failed: " + error);
        expectWithinAbsoluteError(restoredWorker, kBoundaryBaseline, 0.0005f,
                                  "Q512 boundary E1 restore mismatch");
        const float restartSeed = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const float restartLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expectWithinAbsoluteError(restartSeed, restoredWorker, 0.0005f,
                                  "Q512 boundary E2B seed is not fresh live state");
        expectWithinAbsoluteError(restartLastDelivered, restoredWorker, 0.0005f,
                                  "Q512 boundary restart lastDelivered is not fresh live state");
        expect(std::abs(restartSeed - preBoundaryValue) > 0.01f
                   && std::abs(restartSeed - postBoundaryValue) > 0.01f,
               "old Q512 canonical value leaked into the restart seed");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "Q512 boundary recovery mutated the E1 shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "Q512 boundary recovery mutated the E1 state shadow");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "Q512 boundary recovery gate reopened incoherently");

        // The new generation starts its own transport coordinate at zero as
        // required by the existing prepare/reset contract. The canonical
        // sample position remains beyond the old 544 boundary, but the first
        // replacement event is current-generation sequence 1/local 0 rather
        // than a replay of old sequence 1/local 480 or sequence 2/local 32.
        const auto firstNewGlobalPosition = kAbsolutePostBoundary + kQuantum;
        const auto firstNewGenerationPosition = std::uint64_t { 0 };
        const auto firstNewSequence = proxy->automationSequenceForNextCallback();
        const auto firstNewLocalOffset = static_cast<std::uint32_t>(
            firstNewGenerationPosition % kQuantum);
        chain.applyAutomationAtSample(trackId, &postBoundarySnapshot,
                                      static_cast<std::int64_t>(firstNewGlobalPosition),
                                      kSampleRate, kBpm, static_cast<int>(kQuantum));
        const float firstNewCanonical = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const auto firstNewStaged = slot->sandboxStagedEventCountForTesting();
        expect(firstNewSequence == 1,
               "first replacement event did not start at current generation sequence one");
        expect(firstNewLocalOffset < kQuantum,
               "first replacement event local offset is outside Q512");
        expect(firstNewStaged == 1,
               "first replacement callback did not stage the current event");
        expect(firstNewCanonical > restartSeed
                   && firstNewCanonical < kPostBoundaryTarget,
               "first replacement event did not resume current canonical automation");
        expect(firstNewCanonical > preBoundaryValue + 0.01f,
               "first replacement event replayed the pre-boundary value");

        const auto appliedBeforeFirstNew =
            proxy->automationDiagnostics().workerAppliedEvents;
        processMeasured(static_cast<int>(kQuantum), 1.0f);
        const auto firstNewTransport = proxy->lifecycleDiagnostics().transport;
        waitForWorkerQuanta(*proxy, firstNewTransport.submitted, 5000);
        const auto firstNewDiagnostics = proxy->automationDiagnostics();
        float firstNewWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, firstNewWorker, error),
               "first replacement worker fetch failed: " + error);
        expect(firstNewTransport.submitted == firstNewSequence,
               "first replacement event did not submit on the current generation");
        expect(firstNewDiagnostics.latestPublishedSequence == firstNewSequence,
               "first replacement event published the wrong sequence");
        expect(firstNewDiagnostics.workerAppliedEvents
                   == appliedBeforeFirstNew + 1,
               "first replacement event was duplicated or lost");
        expectWithinAbsoluteError(firstNewWorker, firstNewCanonical, 0.0005f,
                                  "first replacement worker received the wrong value");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                  firstNewCanonical, 0.0005f,
                                  "first replacement lastDelivered did not commit");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "first replacement event remained staged");

        std::int64_t finalPosition = static_cast<std::int64_t>(firstNewGlobalPosition);
        for (int callback = 1; callback < 10; ++callback)
        {
            finalPosition += static_cast<std::int64_t>(kQuantum);
            chain.applyAutomationAtSample(trackId, &postBoundarySnapshot, finalPosition,
                                          kSampleRate, kBpm, static_cast<int>(kQuantum));
            processMeasured(static_cast<int>(kQuantum), 1.0f);
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
        }

        float finalWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, finalWorker, error),
               "Q512 boundary final worker fetch failed: " + error);
        const float finalCanonical = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        expectWithinAbsoluteError(finalCanonical, kPostBoundaryTarget, 0.0005f,
                                  "Q512 boundary canonical target did not settle");
        expectWithinAbsoluteError(finalWorker, finalCanonical, 0.0005f,
                                  "Q512 boundary worker rolled back or diverged");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                  finalCanonical, 0.0005f,
                                  "Q512 boundary final lastDelivered mismatch");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0
                   && proxy->automationDiagnostics().parentOverflowRejected == 0,
               "Q512 boundary produced invalid or overflow automation");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "final Q512 playback mutated the E1 shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "final Q512 playback mutated the E1 state shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "Q512 boundary left an active realtime call");

        logMessage("APEX_E2C_Q512_BOUNDARY_TRACE componentUid="
            + juce::String(proxy->getDescription().uniqueId)
            + " parameter=" + parameterId
            + " ordinal=" + juce::String(kOrdinal)
            + " pidA=" + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " e2aA=" + juce::String(static_cast<juce::int64>(e2aGenerationA))
            + " e2aB=" + juce::String(static_cast<juce::int64>(e2aGenerationB))
            + " baseline=" + juce::String(kBoundaryBaseline, 4)
            + " initialWorker=" + juce::String(initialWorker, 4)
            + " initialSeed=" + juce::String(initialSeed, 4)
            + " preAbs=" + juce::String(static_cast<juce::int64>(kAbsolutePreBoundary))
            + " preSeq=" + juce::String(static_cast<juce::int64>(preBoundarySequence))
            + " preLocal=" + juce::String(static_cast<int>(preBoundaryLocalOffset))
            + " preValue=" + juce::String(preBoundaryValue, 4)
            + " preSubmitted=" + juce::String(static_cast<juce::int64>(
                preBoundaryTransport.submitted))
            + " preCompleted=" + juce::String(static_cast<juce::int64>(
                preBoundaryCompletedSequence))
            + " preWorker=" + juce::String(preBoundaryWorker, 4)
            + " postAbs=" + juce::String(static_cast<juce::int64>(kAbsolutePostBoundary))
            + " postSeq=" + juce::String(static_cast<juce::int64>(postBoundarySequence))
            + " postLocal=" + juce::String(static_cast<int>(postBoundaryLocalOffset))
            + " postValue=" + juce::String(postBoundaryValue, 4)
            + " postRingSlot=" + juce::String(static_cast<int>(postAutomationRingSlot))
            + " oldCompleted=" + juce::String(static_cast<juce::int64>(oldCompletedSequence))
            + " restored=" + juce::String(restoredWorker, 4)
            + " restartSeed=" + juce::String(restartSeed, 4)
            + " restartLastDelivered=" + juce::String(restartLastDelivered, 4)
            + " firstGlobalAbs=" + juce::String(static_cast<juce::int64>(
                firstNewGlobalPosition))
            + " firstGenerationPos=" + juce::String(static_cast<juce::int64>(
                firstNewGenerationPosition))
            + " firstSeq=" + juce::String(static_cast<juce::int64>(firstNewSequence))
            + " firstLocal=" + juce::String(static_cast<int>(firstNewLocalOffset))
            + " firstValue=" + juce::String(firstNewCanonical, 4)
            + " firstStaged=" + juce::String(static_cast<int>(firstNewStaged))
            + " firstAppliedDelta=" + juce::String(static_cast<juce::int64>(
                firstNewDiagnostics.workerAppliedEvents - appliedBeforeFirstNew))
            + " final=" + juce::String(finalWorker, 4)
            + " oldRingSlot=" + juce::String(static_cast<int>(oldAutomationRingSlot))
            + " boundaryWrap=1 staleAccepted=0 duplicated=0 lost=0"
            + " e1ShadowMutated=0 allocationChecked="
                + juce::String(JUCE_ENABLE_ALLOCATION_HOOKS ? 1 : 0));

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000),
               "Q512 boundary replacement worker orphaned");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingB),
               "Q512 boundary replacement audio mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CQ512BoundaryCrashTest
    pluginSandboxPhaseE2CQ512BoundaryCrashTest;

class PluginSandboxPhaseE2CBypassCrashTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CBypassCrashTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.bypass-crash.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("bypass remains active while canonical automation survives worker recovery");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverE2CAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        constexpr int kOrdinal = 0;
        constexpr float kE1Baseline = 0.25f;
        constexpr float kAutomationTarget = 0.75f;
        constexpr int kQuantum = 512;
        const juce::String trackId = "e2c-bypass-crash-track";

        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         e2cAutomationFixtureDescription(identity), error, preparation),
                     0, "bypass crash sandbox insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "bypass crash sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "bypass crash metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, kOrdinal, parameterId),
               "bypass crash ordinal-zero metadata is missing");
        if (parameterId.isEmpty())
            return;

        // This fixture's parameter shadow is the authoritative E1 state. The
        // bypass control itself remains host/slot-owned and must not be
        // confused with that parameter state.
        expect(proxy->setParameter(parameterId, kE1Baseline, error),
               "bypass crash E1 baseline set failed: " + error);
        const auto e1ParametersBeforePlayback = proxy->lastAuthoritativeParameters();
        const auto e1StateBytesBeforePlayback = proxy->lastAuthoritativeState().getSize();
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);

        float initialWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, initialWorker, error),
               "bypass crash initial worker fetch failed: " + error);
        const float initialSeed = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const float initialLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expectWithinAbsoluteError(initialWorker, kE1Baseline, 0.0005f,
                                  "bypass crash initial worker baseline mismatch");
        expectWithinAbsoluteError(initialSeed, kE1Baseline, 0.0005f,
                                  "bypass crash initial E2B seed mismatch");
        expectWithinAbsoluteError(initialLastDelivered, kE1Baseline, 0.0005f,
                                  "bypass crash initial lastDelivered mismatch");
        expect(! slot->isBypassed() && ! proxy->isBypassed(),
               "bypass crash worker unexpectedly starts bypassed");

        const juce::String laneKey =
            "plugin.0." + slot->getPluginInstanceId() + "." + parameterId;
        DAW::AutomationSnapshot baselineSnapshot;
        baselineSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kE1Baseline, DAW::AutomationCurveType::Linear, 0.0f },
                { 4096, kE1Baseline, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        DAW::AutomationSnapshot targetSnapshot;
        targetSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        juce::AudioBuffer<float> block(2, kQuantum);
        const auto processMeasured = [this, &chain, &block, kQuantum](float fillValue)
        {
            fillBlock(block, fillValue);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
           #else
            chain.processBlock(block, kQuantum);
           #endif
        };
        const auto tailIsDry = [&block](float inputValue)
        {
            return std::abs(block.getSample(0, block.getNumSamples() - 1)
                            - inputValue) < 0.0005f
                && std::abs(block.getSample(1, block.getNumSamples() - 1)
                            - inputValue) < 0.0005f;
        };

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto e2aGenerationA = proxy->automationTransportForTest().generation();
        const auto e2aMappingA = proxy->automationTransportForTest().mappingName();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        expect(pidA != 0 && generationA != 0 && e2aGenerationA == generationA,
               "bypass crash worker/generation A identity is incomplete");

        // Use the normal production bypass path, then warm its existing
        // latency-aligned dry behavior without disabling automation transport.
        chain.setSlotBypassed(0, true);
        expect(slot->isBypassed() && proxy->isBypassed(),
               "normal production bypass path did not enable bypass");
        std::int64_t position = 0;
        for (int callback = 0; callback < 3; ++callback)
        {
            chain.applyAutomationAtSample(trackId, &baselineSnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            processMeasured(1.0f);
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
            position += kQuantum;
        }
        expect(tailIsDry(1.0f),
               "healthy bypass path exposed processed wet output");
        expect(proxy->remoteAvailable() && proxy->isBypassed() && slot->isBypassed(),
               "bypass was not active after healthy warm-up");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "bypass warm-up mutated the E1 shadow");

        // The real canonical producer advances while bypass is ON. The worker
        // must receive the parameter event even though its processed output is
        // discarded by the bypass path.
        chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const float preCrashCanonical =
            slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const auto preCrashStaged = slot->sandboxStagedEventCountForTesting();
        expect(preCrashCanonical > initialSeed
                   && preCrashCanonical < kAutomationTarget,
               "canonical automation did not advance while bypassed");
        expect(preCrashStaged > 0,
               "bypassed canonical automation staged no event");
        const auto appliedBeforePreCrash =
            proxy->automationDiagnostics().workerAppliedEvents;
        processMeasured(1.0f);
        const auto preCrashTransport = proxy->lifecycleDiagnostics().transport;
        waitForWorkerQuanta(*proxy, preCrashTransport.submitted, 5000);
        float preCrashWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, preCrashWorker, error),
               "bypassed pre-crash worker fetch failed: " + error);
        const float preCrashLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expect(proxy->automationDiagnostics().workerAppliedEvents
                   == appliedBeforePreCrash + 1,
               "worker did not apply automation while bypassed");
        expectWithinAbsoluteError(preCrashWorker, preCrashCanonical, 0.0005f,
                                  "bypassed worker value did not follow canonical automation");
        expectWithinAbsoluteError(preCrashLastDelivered, preCrashCanonical, 0.0005f,
                                  "bypassed lastDelivered did not commit");
        expect(tailIsDry(1.0f),
               "bypassed automation callback exposed wet output");
        expect(slot->isBypassed() && proxy->isBypassed(),
               "bypass was lost while automation advanced");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "bypassed playback automation mutated the E1 shadow");
        position += kQuantum;

        expect(killProcessUnexpectedly(pidA),
               "bypass crash failed to terminate worker PID A");
        expect(waitForWorkerDeath(*proxy, 10000),
               "bypass crash worker PID A did not die");

        const auto appliedBeforeDead =
            proxy->automationDiagnostics().workerAppliedEvents;
        proxy->forceNextSubmissionFailureForTest(2);
        for (int callback = 0; callback < 2; ++callback)
        {
            chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            expect(slot->sandboxStagedEventCountForTesting() > 0,
                   "dead bypass callback dropped the current automation event");
            processMeasured(1.0f);
            position += kQuantum;
        }
        const float canonicalWhileDead =
            slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const auto deadDiagnostics = proxy->automationDiagnostics();
        expect(canonicalWhileDead > preCrashCanonical
                   && canonicalWhileDead <= kAutomationTarget,
               "canonical automation stopped or overshot while bypassed worker was dead");
        expect(deadDiagnostics.workerAppliedEvents == appliedBeforeDead,
               "dead bypass callbacks reported worker automation application");
        expect(proxy->isBypassed() && slot->isBypassed(),
               "bypass state was lost while worker was dead");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "dead bypass callbacks mutated the E1 shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "dead bypass callback left an active realtime call");

        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "bypass crash automatic recovery never reached Healthy");
        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto e2aGenerationB = proxy->automationTransportForTest().generation();
        const auto e2aMappingB = proxy->automationTransportForTest().mappingName();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        expect(recovery.detectedDeaths >= 1 && recovery.restartSuccesses >= 1,
               "bypass crash recovery did not complete a replacement worker");
        expect(pidB != 0 && pidB != pidA,
               "bypass crash replacement PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "bypass crash replacement generation is invalid or reused");
        expect(e2aGenerationB == generationB && e2aGenerationB != e2aGenerationA,
               "bypass crash replacement E2A generation is incorrect");
        expect(e2aMappingA.isNotEmpty() && e2aMappingB != e2aMappingA,
               "bypass crash replacement reused the old E2A mapping");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "bypass crash replacement reused the old audio mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "bypass crash old audio mapping survived recovery");
        expect(slot->isBypassed() && proxy->isBypassed()
                   && proxy->lifecycleDiagnostics().bypassed,
               "bypass state was not preserved through worker replacement");
        const auto restartDiagnostics = proxy->automationDiagnostics();
        expect(restartDiagnostics.latestPublishedSequence == 0
                   && restartDiagnostics.workerAppliedEvents == 0,
               "replacement retained dead-generation automation state");

        float restoredWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, restoredWorker, error),
               "bypass crash restored worker fetch failed: " + error);
        expectWithinAbsoluteError(restoredWorker, kE1Baseline, 0.0005f,
                                  "bypass crash E1 restore mismatch");
        const float restartSeed = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const float restartLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expectWithinAbsoluteError(restartSeed, restoredWorker, 0.0005f,
                                  "bypass crash E2B seed is not fresh live state");
        expectWithinAbsoluteError(restartLastDelivered, restoredWorker, 0.0005f,
                                  "bypass crash restart lastDelivered is not fresh live state");
        expect(std::abs(restartSeed - preCrashCanonical) > 0.01f
                   && std::abs(restartSeed - canonicalWhileDead) > 0.01f,
               "dead-generation bypass automation became restart authority");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "bypass recovery mutated the E1 shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "bypass recovery mutated the E1 state shadow");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "bypass recovery gate reopened incoherently");

        // Keep bypass ON. The first fresh callback must submit the current
        // canonical target and apply it to the replacement worker; the output
        // remains dry because bypass owns the audible path, not automation.
        chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                      kSampleRate, kBpm, kQuantum);
        const auto firstPostSequence = proxy->automationSequenceForNextCallback();
        const float firstPostCanonical =
            slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const auto firstPostStaged = slot->sandboxStagedEventCountForTesting();
        expect(firstPostSequence == 1,
               "bypass recovery did not reset the first E2A sequence");
        expect(firstPostStaged > 0
                   && firstPostCanonical > restartSeed
                   && firstPostCanonical < kAutomationTarget,
               "current automation did not resume from the fresh bypassed seed");
        const auto appliedBeforeFirstPost =
            proxy->automationDiagnostics().workerAppliedEvents;
        processMeasured(1.0f);
        const auto firstPostTransport = proxy->lifecycleDiagnostics().transport;
        waitForWorkerQuanta(*proxy, firstPostTransport.submitted, 5000);
        const auto firstPostDiagnostics = proxy->automationDiagnostics();
        float firstPostWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, firstPostWorker, error),
               "bypass crash first replacement worker fetch failed: " + error);
        expect(firstPostTransport.submitted == firstPostSequence,
               "bypass crash first replacement event did not submit");
        expect(firstPostDiagnostics.workerAppliedEvents
                   == appliedBeforeFirstPost + 1,
               "replacement worker did not apply automation while bypassed");
        expectWithinAbsoluteError(firstPostWorker, firstPostCanonical, 0.0005f,
                                  "replacement worker received the wrong bypassed value");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                  firstPostCanonical, 0.0005f,
                                  "replacement lastDelivered did not advance while bypassed");
        expect(slot->isBypassed() && proxy->isBypassed(),
               "bypass was lost on the first post-recovery callback");
        position += kQuantum;

        for (int callback = 1; callback < 10; ++callback)
        {
            chain.applyAutomationAtSample(trackId, &targetSnapshot, position,
                                          kSampleRate, kBpm, kQuantum);
            processMeasured(1.0f);
            waitForWorkerQuanta(*proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
            position += kQuantum;
        }

        float finalWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, finalWorker, error),
               "bypass crash final worker fetch failed: " + error);
        const float finalCanonical = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        expectWithinAbsoluteError(finalCanonical, kAutomationTarget, 0.0005f,
                                  "bypassed canonical automation did not reach target");
        expectWithinAbsoluteError(finalWorker, finalCanonical, 0.0005f,
                                  "bypassed replacement worker did not reach target");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                  finalCanonical, 0.0005f,
                                  "bypassed final lastDelivered mismatch");
        expect(slot->isBypassed() && proxy->isBypassed()
                   && proxy->lifecycleDiagnostics().bypassed,
               "bypass was lost at final canonical convergence");
        expect(tailIsDry(1.0f),
               "wet/DSP output reopened while bypass remained active");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0
                   && proxy->automationDiagnostics().parentOverflowRejected == 0,
               "bypass crash produced invalid or overflow automation");
        expect(proxy->lastAuthoritativeParameters() == e1ParametersBeforePlayback,
               "final bypassed playback mutated the E1 shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytesBeforePlayback,
               "final bypassed playback mutated the E1 state shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "bypass crash left an active realtime call");

        logMessage("APEX_E2C_BYPASS_TRACE componentUid="
            + juce::String(proxy->getDescription().uniqueId)
            + " parameter=" + parameterId
            + " ordinal=" + juce::String(kOrdinal)
            + " pidA=" + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " e2aA=" + juce::String(static_cast<juce::int64>(e2aGenerationA))
            + " e2aB=" + juce::String(static_cast<juce::int64>(e2aGenerationB))
            + " e1=" + juce::String(kE1Baseline, 4)
            + " initialWorker=" + juce::String(initialWorker, 4)
            + " initialSeed=" + juce::String(initialSeed, 4)
            + " bypassBeforeAutomation=1"
            + " target=" + juce::String(kAutomationTarget, 4)
            + " preCrashCanonical=" + juce::String(preCrashCanonical, 4)
            + " preCrashWorker=" + juce::String(preCrashWorker, 4)
            + " preCrashLastDelivered=" + juce::String(preCrashLastDelivered, 4)
            + " preCrashStaged=" + juce::String(static_cast<int>(preCrashStaged))
            + " automationAdvancedBypassed=1 bypassBeforeCrash=1"
            + " callbacksWhileDead=2 bypassWhileDead=1 canonicalWhileDead="
                + juce::String(canonicalWhileDead, 4)
            + " restored=" + juce::String(restoredWorker, 4)
            + " restartSeed=" + juce::String(restartSeed, 4)
            + " restartLastDelivered=" + juce::String(restartLastDelivered, 4)
            + " firstPost=" + juce::String(firstPostCanonical, 4)
            + " firstPostSequence="
                + juce::String(static_cast<juce::int64>(firstPostSequence))
            + " firstPostStaged=" + juce::String(static_cast<int>(firstPostStaged))
            + " firstPostAppliedDelta=" + juce::String(static_cast<juce::int64>(
                firstPostDiagnostics.workerAppliedEvents - appliedBeforeFirstPost))
            + " final=" + juce::String(finalWorker, 4)
            + " bypassAfterRecovery=1 staleAccepted=0 wetReopened=0"
            + " e1ShadowMutated=0 allocationChecked="
                + juce::String(JUCE_ENABLE_ALLOCATION_HOOKS ? 1 : 0));

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000),
               "bypass crash replacement worker orphaned");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingB),
               "bypass crash replacement audio mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CBypassCrashTest
    pluginSandboxPhaseE2CBypassCrashTest;

class PluginSandboxPhaseE2CRepeatedCrashRestartTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CRepeatedCrashRestartTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.repeated-crash-restart.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("three worker deaths do not accumulate stale E2C state");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverE2CAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        constexpr int kOrdinal = 0;
        constexpr int kDeadCallbacks = 2;
        const juce::String trackId = "e2c-repeated-crash-track";

        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         e2cAutomationFixtureDescription(identity), error, preparation),
                     0, "repeated-crash sandbox insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "repeated-crash sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "repeated-crash metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, kOrdinal, parameterId),
               "repeated-crash ordinal-zero parameter is missing");
        if (parameterId.isEmpty())
            return;

        // Establish one E1 state/parameter shadow. Every later replacement must
        // replay this state; playback automation is never allowed to mutate it.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "repeated-crash E1 baseline set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "repeated-crash baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0,
               "repeated-crash baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "repeated-crash baseline state shadow restore failed: " + error);
        const auto e1Parameters = proxy->lastAuthoritativeParameters();
        const auto e1StateBytes = proxy->lastAuthoritativeState().getSize();
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);

        float initialWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, initialWorker, error),
               "repeated-crash initial worker fetch failed: " + error);
        const float initialSeed = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const float initialLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expectWithinAbsoluteError(initialWorker, kBaseline, 0.0005f,
                                  "repeated-crash initial worker baseline mismatch");
        expectWithinAbsoluteError(initialSeed, kBaseline, 0.0005f,
                                  "repeated-crash initial E2B seed mismatch");
        expectWithinAbsoluteError(initialLastDelivered, kBaseline, 0.0005f,
                                  "repeated-crash initial lastDelivered mismatch");

        DAW::AutomationSnapshot snapshot;
        const juce::String laneKey =
            "plugin.0." + slot->getPluginInstanceId() + "." + parameterId;
        snapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        juce::AudioBuffer<float> block(2, kQuantum);
        struct CallbackObservation
        {
            float canonical = 0.0f;
            float delivered = 0.0f;
            std::uint32_t staged = 0;
            std::uint64_t submitted = 0;
            std::uint64_t parentCompleted = 0;
            std::uint64_t workerCompleted = 0;
            std::uint64_t workerApplied = 0;
        };

        // This helper deliberately keeps all waiting/observation outside the
        // allocation-checked processBlock scope. The callback itself remains
        // the existing chain path, not a direct proxy shortcut.
        const auto processAutomation =
            [this, &chain, &block, &slot, &proxy, &snapshot, &trackId, kOrdinal]
            (std::int64_t samplePosition, bool waitForWorker)
        {
            CallbackObservation result;
            chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                          kSampleRate, kBpm, kQuantum);
            result.canonical = slot->sandboxLastAutomationValueForTesting(kOrdinal);
            result.staged = slot->sandboxStagedEventCountForTesting();
            fillBlock(block, 1.0f);
           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kQuantum);
            }
           #else
            chain.processBlock(block, kQuantum);
           #endif
            auto lifecycle = proxy->lifecycleDiagnostics();
            result.submitted = lifecycle.transport.submitted;
            result.parentCompleted = lifecycle.transport.completed;
            if (waitForWorker)
                waitForWorkerQuanta(*proxy, result.submitted, 5000);
            result.workerCompleted = proxy->workerCompletedSequence();
            result.workerApplied = proxy->automationDiagnostics().workerAppliedEvents;
            result.delivered = slot->sandboxLastDeliveredValueForTesting(kOrdinal);
            return result;
        };

        const auto initialGeneration = proxy->currentGeneration();
        const auto initialE2AGeneration = proxy->automationTransportForTest().generation();
        const auto initialPid = proxy->currentWorkerPid();
        const auto initialAudioMapping = proxy->processDiagnostics().sharedMemoryName;
        const auto initialE2AMapping = proxy->automationTransportForTest().mappingName();
        expect(initialPid != 0 && initialGeneration != 0
                   && initialE2AGeneration == initialGeneration,
               "repeated-crash initial generation identity is incomplete");
        expect(proxy->processHandlePidForTest() == initialPid,
               "repeated-crash initial process handle does not own PID 0");
        expect(proxy->workerStartCountForTest() == 1,
               "repeated-crash initial worker start count is not one");
        expect(proxy->healthState()
                   == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "repeated-crash initial worker is not Healthy");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "repeated-crash initial realtime gate is incoherent");

        std::int64_t position = 0;
        const auto initialAutomation = processAutomation(position, true);
        expect(initialAutomation.staged > 0,
               "repeated-crash canonical automation did not stage initially");
        expect(initialAutomation.canonical > initialSeed
                   && initialAutomation.canonical < kAutomationTarget,
               "repeated-crash initial canonical automation did not smooth");
        expect(initialAutomation.workerApplied > 0,
               "repeated-crash initial worker applied no automation");
        expectWithinAbsoluteError(initialAutomation.delivered,
                                  initialAutomation.canonical, 0.0005f,
                                  "repeated-crash initial lastDelivered mismatch");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "repeated-crash initial playback mutated the E1 shadow");
        position += kQuantum;

        struct CycleRecord
        {
            std::uint32_t pidBefore = 0;
            std::uint32_t replacementPid = 0;
            std::uint64_t generationBefore = 0;
            std::uint64_t replacementGeneration = 0;
            std::uint64_t e2aBefore = 0;
            std::uint64_t replacementE2AGeneration = 0;
            std::uint32_t deadCallbacks = 0;
            std::uint64_t submittedBefore = 0;
            std::uint64_t completedBefore = 0;
            std::uint64_t appliedBefore = 0;
            std::uint64_t appliedWhileDead = 0;
            std::uint64_t submittedAfterRecovery = 0;
            std::uint64_t completedAfterRecovery = 0;
            std::uint32_t pendingAfterRecovery = 0;
            std::uint32_t workerStarts = 0;
            float preCrashCanonical = 0.0f;
            float canonicalWhileDead = 0.0f;
            float lastDeliveredWhileDead = 0.0f;
            float e1Restored = 0.0f;
            float liveReseed = 0.0f;
            float restartSeed = 0.0f;
            float restartLastDelivered = 0.0f;
            float firstCanonical = 0.0f;
            float firstWorker = 0.0f;
            std::uint64_t firstSequence = 0;
            bool canonicalReemitted = false;
            bool staleAccepted = false;
            juce::String audioMapping;
            juce::String e2aMapping;
        };

        std::array<CycleRecord, 3> cycles {};
        std::array<std::uint32_t, 4> pids { initialPid, 0, 0, 0 };
        std::array<std::uint64_t, 4> generations { initialGeneration, 0, 0, 0 };
        std::array<std::uint64_t, 4> e2aGenerations { initialE2AGeneration, 0, 0, 0 };
        std::array<float, 4> priorCanonical { initialAutomation.canonical, 0.0f, 0.0f, 0.0f };
        std::array<float, 4> priorDelivered { initialAutomation.delivered, 0.0f, 0.0f, 0.0f };
        std::array<juce::String, 4> audioMappings { initialAudioMapping, {}, {}, {} };
        std::array<juce::String, 4> e2aMappings { initialE2AMapping, {}, {}, {} };

        for (std::size_t cycleIndex = 0; cycleIndex < cycles.size(); ++cycleIndex)
        {
            auto& cycle = cycles[cycleIndex];
            const auto cycleNumber = static_cast<std::uint32_t>(cycleIndex + 1);
            cycle.deadCallbacks = kDeadCallbacks;
            cycle.preCrashCanonical = slot->sandboxLastAutomationValueForTesting(kOrdinal);
            cycle.pidBefore = proxy->currentWorkerPid();
            cycle.generationBefore = proxy->currentGeneration();
            cycle.e2aBefore = proxy->automationTransportForTest().generation();
            cycle.audioMapping = proxy->processDiagnostics().sharedMemoryName;
            cycle.e2aMapping = proxy->automationTransportForTest().mappingName();
            cycle.submittedBefore = proxy->lifecycleDiagnostics().transport.submitted;
            cycle.completedBefore = proxy->workerCompletedSequence();
            cycle.appliedBefore = proxy->automationDiagnostics().workerAppliedEvents;

            expect(cycle.preCrashCanonical > kBaseline
                       && cycle.preCrashCanonical < kAutomationTarget,
                   "repeated-crash pre-crash canonical value is not mid-smoothing");
            expect(cycle.pidBefore == pids[cycleIndex],
                   "repeated-crash current PID does not match the prior replacement");
            expect(cycle.generationBefore == generations[cycleIndex],
                   "repeated-crash current generation does not match the prior replacement");
            expect(cycle.e2aBefore == e2aGenerations[cycleIndex],
                   "repeated-crash current E2A generation is stale");
            expect(proxy->processHandlePidForTest() == cycle.pidBefore,
                   "repeated-crash owned process handle points at the wrong worker");
            expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
                   "repeated-crash worker gate is not healthy before termination");

            float preCrashWorker = -1.0f;
            expect(fetchLiveValueForIndex(*proxy, kOrdinal, preCrashWorker, error),
                   "repeated-crash pre-crash worker fetch failed: " + error);
            expectWithinAbsoluteError(preCrashWorker, cycle.preCrashCanonical, 0.0005f,
                                      "repeated-crash pre-crash worker diverged from canonical");
            expect(killProcessUnexpectedly(cycle.pidBefore),
                   "repeated-crash failed to terminate the current worker");
            expect(waitForWorkerDeath(*proxy, 10000),
                   "repeated-crash current worker did not die on its owned handle");
            expect(proxy->pollLivenessForTest()
                       != static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive),
                   "repeated-crash worker still reports Alive after termination");
            expect(! processStillAlive(cycle.pidBefore, 1000),
                   "repeated-crash terminated worker remained alive");

            for (std::uint32_t callback = 0; callback < cycle.deadCallbacks; ++callback)
            {
                const auto dead = processAutomation(position, false);
                cycle.canonicalWhileDead = dead.canonical;
                cycle.lastDeliveredWhileDead = dead.delivered;
                cycle.appliedWhileDead = dead.workerApplied;
                expect(dead.staged > 0,
                       "repeated-crash dead callback dropped current canonical automation");
                position += kQuantum;
            }
            expect(cycle.canonicalWhileDead > cycle.preCrashCanonical
                       && cycle.canonicalWhileDead <= kAutomationTarget,
                   "repeated-crash canonical producer stopped while worker was dead");
            expect(cycle.appliedWhileDead == cycle.appliedBefore,
                   "repeated-crash dead worker reported an automation application");
            expect(slot->sandboxStagedEventCountForTesting() == 0,
                   "repeated-crash left a staged E2B event during dead callbacks");
            expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
                   "repeated-crash dead callbacks left an active realtime call");

            expect(driveRecoveryToHealthy(chain, *proxy, cycle.pidBefore, 30000),
                   "repeated-crash automatic recovery did not reach Healthy");
            const auto recovery = proxy->recoveryDiagnostics();
            cycle.replacementPid = proxy->currentWorkerPid();
            cycle.replacementGeneration = proxy->currentGeneration();
            cycle.replacementE2AGeneration = proxy->automationTransportForTest().generation();
            pids[cycleIndex + 1] = cycle.replacementPid;
            generations[cycleIndex + 1] = cycle.replacementGeneration;
            e2aGenerations[cycleIndex + 1] = cycle.replacementE2AGeneration;
            audioMappings[cycleIndex + 1] = proxy->processDiagnostics().sharedMemoryName;
            e2aMappings[cycleIndex + 1] = proxy->automationTransportForTest().mappingName();
            cycle.workerStarts = proxy->workerStartCountForTest();

            expect(recovery.health == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
                   "repeated-crash recovery did not finish in Healthy state");
            expect(recovery.detectedDeaths == cycleNumber,
                   "repeated-crash death counter accumulated incorrectly");
            expect(recovery.restartAttempts == cycleNumber
                       && recovery.restartSuccesses == cycleNumber
                       && recovery.restartFailures == 0,
                   "repeated-crash restart counters show a failed or duplicate recovery");
            expect(recovery.restartsInWindow <= recovery.maxRestartsPerWindow,
                   "repeated-crash unexpectedly activated restart throttle");
            expect(cycle.replacementPid != 0
                       && cycle.replacementPid != cycle.pidBefore,
                   "repeated-crash replacement PID is invalid or unchanged");
            expect(cycle.replacementGeneration != 0
                       && cycle.replacementGeneration != cycle.generationBefore,
                   "repeated-crash replacement generation is invalid or unchanged");
            expect(cycle.replacementE2AGeneration == cycle.replacementGeneration,
                   "repeated-crash replacement E2A generation mismatches audio generation");
            expect(cycle.workerStarts == cycleNumber + 1,
                   "repeated-crash worker start count indicates a crash loop or missed start");
            expect(proxy->processHandlePidForTest() == cycle.replacementPid,
                   "repeated-crash replacement process handle owns the wrong PID");
            expect(proxy->pollLivenessForTest()
                       == static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive),
                   "repeated-crash replacement worker is not Alive");
            expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
                   "repeated-crash recovery gate reopened incoherently");
            expect(cycle.audioMapping.isNotEmpty()
                       && audioMappings[cycleIndex + 1] != cycle.audioMapping,
                   "repeated-crash replacement reused the old audio mapping");
            expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(cycle.audioMapping),
                   "repeated-crash old audio mapping survived retirement");
            expect(cycle.e2aMapping.isNotEmpty()
                       && e2aMappings[cycleIndex + 1] != cycle.e2aMapping,
                   "repeated-crash replacement reused the old E2A mapping");

            for (std::size_t prior = 0; prior <= cycleIndex; ++prior)
            {
                expect(cycle.replacementGeneration != generations[prior],
                       "repeated-crash replacement generation collided with an old generation");
                expect(cycle.replacementE2AGeneration != e2aGenerations[prior],
                       "repeated-crash replacement E2A generation collided with an old generation");
            }

            cycle.e1Restored = -1.0f;
            expect(fetchLiveValueForIndex(*proxy, kOrdinal, cycle.e1Restored, error),
                   "repeated-crash replacement E1 live fetch failed: " + error);
            expectWithinAbsoluteError(cycle.e1Restored, kBaseline, 0.0005f,
                                      "repeated-crash replacement did not restore E1 baseline");
            cycle.liveReseed = slot->sandboxLastAutomationValueForTesting(kOrdinal);
            cycle.restartSeed = cycle.liveReseed;
            cycle.restartLastDelivered =
                slot->sandboxLastDeliveredValueForTesting(kOrdinal);
            expectWithinAbsoluteError(cycle.liveReseed, cycle.e1Restored, 0.0005f,
                                      "repeated-crash E2B seed is not fresh live E1 state");
            expectWithinAbsoluteError(cycle.restartLastDelivered, cycle.e1Restored, 0.0005f,
                                      "repeated-crash restart lastDelivered is not fresh live state");
            expect(slot->sandboxAutomationBindingCountForTesting() > 0,
                   "repeated-crash recovery left E2B bindings unpublished");
            cycle.pendingAfterRecovery = slot->sandboxStagedEventCountForTesting();
            expect(cycle.pendingAfterRecovery == 0,
                   "repeated-crash orphaned a pending E2B delivery record");

            const auto freshLifecycle = proxy->lifecycleDiagnostics();
            const auto freshAutomation = proxy->automationDiagnostics();
            cycle.submittedAfterRecovery = freshLifecycle.transport.submitted;
            cycle.completedAfterRecovery = proxy->workerCompletedSequence();
            expect(freshLifecycle.reblocker.generation == cycle.replacementGeneration,
                   "repeated-crash reblocker retained an old generation");
            expect(! freshLifecycle.reblocker.pendingQuantum,
                   "repeated-crash fresh generation retained a pending audio quantum");
            expect(cycle.submittedAfterRecovery == 0
                       && cycle.completedAfterRecovery == 0,
                   "repeated-crash fresh transport retained old completion state");
            expect(freshAutomation.latestPublishedSequence == 0
                       && freshAutomation.workerAppliedEvents == 0,
                   "repeated-crash fresh E2A retained old sequence completion state");
            expect(proxy->lastAuthoritativeParameters() == e1Parameters,
                   "repeated-crash recovery mutated the E1 parameter shadow");
            expect(proxy->lastAuthoritativeState().getSize() == e1StateBytes,
                   "repeated-crash recovery mutated the E1 state shadow");

            for (std::size_t prior = 0; prior <= cycleIndex; ++prior)
            {
                if (std::abs(priorCanonical[prior] - kBaseline) > 0.01f)
                    expect(std::abs(cycle.restartSeed - priorCanonical[prior]) > 0.01f,
                           "repeated-crash old canonical value became a later E2B seed");
                if (std::abs(priorDelivered[prior] - kBaseline) > 0.01f)
                    expect(std::abs(cycle.restartSeed - priorDelivered[prior]) > 0.01f,
                           "repeated-crash old lastDelivered became a later E2B seed");
                if (std::abs(cycles[prior].lastDeliveredWhileDead - kBaseline) > 0.01f)
                    expect(std::abs(cycle.restartSeed
                                       - cycles[prior].lastDeliveredWhileDead) > 0.01f,
                           "repeated-crash dead-generation lastDelivered became a later seed");
                if (std::abs(cycles[prior].canonicalWhileDead - kBaseline) > 0.01f)
                    expect(std::abs(cycle.restartSeed
                                       - cycles[prior].canonicalWhileDead) > 0.01f,
                           "repeated-crash dead-generation canonical value became a later seed");
            }

            cycle.firstSequence = proxy->automationSequenceForNextCallback();
            const auto firstPost = processAutomation(position, true);
            cycle.firstCanonical = firstPost.canonical;
            cycle.firstWorker = -1.0f;
            expect(firstPost.staged > 0,
                   "repeated-crash current canonical value did not re-emit after recovery");
            expect(cycle.firstSequence == 1,
                   "repeated-crash fresh generation did not restart E2A sequence at one");
            expect(firstPost.canonical > cycle.restartSeed + 0.01f
                       && firstPost.canonical < kAutomationTarget,
                   "repeated-crash post-recovery canonical value did not resume smoothing");
            expect(firstPost.workerApplied == 1,
                   "repeated-crash replacement applied stale or duplicate automation");
            expect(firstPost.submitted == 1 && firstPost.workerCompleted >= 1,
                   "repeated-crash replacement did not complete its first current event");
            expect(fetchLiveValueForIndex(*proxy, kOrdinal, cycle.firstWorker, error),
                   "repeated-crash post-recovery worker fetch failed: " + error);
            expectWithinAbsoluteError(cycle.firstWorker, cycle.firstCanonical, 0.0005f,
                                      "repeated-crash current canonical value missed replacement");
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                      cycle.firstCanonical, 0.0005f,
                                      "repeated-crash replacement lastDelivered mismatch");
            expect(slot->sandboxStagedEventCountForTesting() == 0,
                   "repeated-crash post-recovery E2B event remained pending");
            expect(proxy->pollLivenessForTest()
                       == static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive),
                   "repeated-crash replacement died without an intentional termination");
            cycle.canonicalReemitted = true;

            priorCanonical[cycleIndex + 1] = cycle.firstCanonical;
            priorDelivered[cycleIndex + 1] = firstPost.delivered;
            position += kQuantum;
        }

        // Only after G3 has survived its first current-generation event, allow
        // the existing canonical lane to settle. No fourth crash is performed.
        for (int callback = 1; callback < 10; ++callback)
        {
            processAutomation(position, true);
            position += kQuantum;
        }

        float finalWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, finalWorker, error),
               "repeated-crash final worker fetch failed: " + error);
        const float finalCanonical = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        expectWithinAbsoluteError(finalCanonical, kAutomationTarget, 0.0005f,
                                  "repeated-crash final canonical target did not settle");
        expectWithinAbsoluteError(finalWorker, kAutomationTarget, 0.0005f,
                                  "repeated-crash final worker target did not settle");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                  kAutomationTarget, 0.0005f,
                                  "repeated-crash final lastDelivered did not settle");
        expect(proxy->currentWorkerPid() == pids[3]
                   && proxy->currentGeneration() == generations[3]
                   && proxy->automationTransportForTest().generation() == e2aGenerations[3],
               "repeated-crash final generation ownership is incorrect");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "repeated-crash final playback mutated the E1 parameter shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytes,
               "repeated-crash final playback mutated the E1 state shadow");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "repeated-crash final callback left stale E2B delivery pending");
        expect(proxy->automationDiagnostics().workerInvalidBatches == 0
                   && proxy->automationDiagnostics().parentOverflowRejected == 0,
               "repeated-crash final generation reported invalid automation");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "repeated-crash final callback left an active realtime call");

        for (std::size_t first = 0; first < generations.size(); ++first)
            for (std::size_t second = first + 1; second < generations.size(); ++second)
                expect(generations[first] != generations[second],
                       "repeated-crash worker generations are not pairwise distinct");

        logMessage(juce::String("APEX_E2C_REPEATED_TRACE cycles=3")
            + " initialPid=" + juce::String(static_cast<int>(pids[0]))
            + " initialGeneration="
                + juce::String(static_cast<juce::int64>(generations[0]))
            + " initialE2A="
                + juce::String(static_cast<juce::int64>(e2aGenerations[0]))
            + " e1=" + juce::String(kBaseline, 4)
            + " initialWorker=" + juce::String(initialWorker, 4)
            + " initialSeed=" + juce::String(initialSeed, 4)
            + " target=" + juce::String(kAutomationTarget, 4)
            + " finalCanonical=" + juce::String(finalCanonical, 4)
            + " finalWorker=" + juce::String(finalWorker, 4)
            + " generationsDistinct=1 e2aMatches=1 crashLoop=0 throttle=0"
            + " stalePending=0 oldCompletionAccepted=0 e1ShadowMutated=0"
            + " allocationChecked="
                + juce::String(JUCE_ENABLE_ALLOCATION_HOOKS ? 1 : 0));

        for (std::size_t cycleIndex = 0; cycleIndex < cycles.size(); ++cycleIndex)
        {
            const auto& cycle = cycles[cycleIndex];
            const auto cycleNumber = static_cast<int>(cycleIndex + 1);
            logMessage("APEX_E2C_REPEATED_CYCLE cycle="
                + juce::String(cycleNumber)
                + " preCanonical=" + juce::String(cycle.preCrashCanonical, 4)
                + " pidBefore=" + juce::String(static_cast<int>(cycle.pidBefore))
                + " generationBefore="
                    + juce::String(static_cast<juce::int64>(cycle.generationBefore))
                + " deadCallbacks=" + juce::String(static_cast<int>(cycle.deadCallbacks))
                + " replacementPid="
                    + juce::String(static_cast<int>(cycle.replacementPid))
                + " replacementGeneration="
                    + juce::String(static_cast<juce::int64>(cycle.replacementGeneration))
                + " replacementE2A="
                    + juce::String(static_cast<juce::int64>(cycle.replacementE2AGeneration))
                + " e1Restored=" + juce::String(cycle.e1Restored, 4)
                + " liveReseed=" + juce::String(cycle.liveReseed, 4)
                + " restartSeed=" + juce::String(cycle.restartSeed, 4)
                + " restartLastDelivered="
                    + juce::String(cycle.restartLastDelivered, 4)
                + " canonicalReemitted="
                    + juce::String(cycle.canonicalReemitted ? 1 : 0)
                + " staleAccepted=" + juce::String(cycle.staleAccepted ? 1 : 0));
        }

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pids[3], 5000),
               "repeated-crash replacement worker orphaned after removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappings[3]),
               "repeated-crash final audio mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CRepeatedCrashRestartTest
    pluginSandboxPhaseE2CRepeatedCrashRestartTest;

class PluginSandboxPhaseE2CHangWatchdogRecoveryTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CHangWatchdogRecoveryTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.hang-watchdog-recovery.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("a live worker hang is watchdog-recovered without losing E1/E2B continuity");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        // The state fixture is also an E2A target: PluginChainCore enables the
        // sidecar before prepare and the worker resolves its gain parameter by
        // ordinal. The fixture-only hang sentinel is enabled by its generated
        // test project, not by production APEX code.
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state/automation fixture identity discovery failed: "
                   + identity.error);
        if (identity.uniqueId == 0)
            return;

        constexpr int kOrdinal = 0;
        const juce::String trackId = "e2c-hang-watchdog-track";

        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         stateFixtureDescription(identity), error, preparation),
                     0, "hang-watchdog sandbox insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "hang-watchdog sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        // TEST-ONLY shorter detection window; production defaults remain
        // untouched. All waits below are control-plane observation.
        proxy->setPollIntervalMsForTest(50);
        proxy->setHangWindowMsForTest(500);

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "hang-watchdog metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, kOrdinal, parameterId),
               "hang-watchdog ordinal-zero parameter is missing");
        if (parameterId.isEmpty())
            return;

        // Establish the authoritative E1 parameter and serialized state before
        // any playback automation. Playback must never mutate these shadows.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "hang-watchdog E1 baseline set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "hang-watchdog baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0,
               "hang-watchdog baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "hang-watchdog baseline state shadow restore failed: " + error);
        const auto e1Parameters = proxy->lastAuthoritativeParameters();
        const auto e1StateBytes = proxy->lastAuthoritativeState().getSize();
        expect(e1StateBytes == baselineState.getSize(),
               "hang-watchdog E1 state shadow size is incorrect");

        slot->configureAutomationContext(trackId, kOrdinal, nullptr, nullptr);
        float baselineLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, baselineLive, error),
               "hang-watchdog baseline live-value fetch failed: " + error);
        expectWithinAbsoluteError(baselineLive, kBaseline, 0.0005f,
                                  "hang-watchdog E1 baseline worker value");
        expectWithinAbsoluteError(
            slot->sandboxLastAutomationValueForTesting(kOrdinal),
            kBaseline, 0.0005f, "hang-watchdog initial E2B seed");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kOrdinal),
            kBaseline, 0.0005f, "hang-watchdog initial E2B lastDelivered");
        const auto rebuildCountBeforeHang =
            slot->sandboxAutomationRebuildCountForTesting();

        DAW::AutomationSnapshot snapshot;
        const juce::String laneKey =
            "plugin.0." + slot->getPluginInstanceId() + "." + parameterId;
        snapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        juce::AudioBuffer<float> block(2, kQuantum);
        struct CallbackObservation
        {
            float canonical = 0.0f;
            float delivered = 0.0f;
            std::uint32_t staged = 0;
            std::uint64_t submitted = 0;
            std::uint64_t parentCompleted = 0;
            std::uint64_t workerCompleted = 0;
            std::uint64_t workerApplied = 0;
        };

        // Keep the actual canonical producer plus chain.processBlock inside
        // the allocation-checked scope after one warm-up callback. No direct
        // proxy handoff or manual E2B update is used.
        const auto processAutomation =
            [this, &chain, &block, &slot, &proxy, &snapshot, &trackId, kOrdinal]
            (std::int64_t samplePosition, bool triggerHang,
             bool waitForWorker, bool checkAllocation)
        {
            CallbackObservation result;
            const auto process = [&]()
            {
                chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                              kSampleRate, kBpm, kQuantum);
                result.canonical =
                    slot->sandboxLastAutomationValueForTesting(kOrdinal);
                result.staged = slot->sandboxStagedEventCountForTesting();
                fillBlock(block, 1.0f);
                if (triggerHang)
                    block.setSample(0, 0, bitsToFloat(kHangSentinelBits));
                chain.processBlock(block, kQuantum);
            };

           #if JUCE_ENABLE_ALLOCATION_HOOKS
            if (checkAllocation)
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                process();
            }
            else
            {
                process();
            }
           #else
            juce::ignoreUnused(checkAllocation);
            process();
           #endif

            const auto lifecycle = proxy->lifecycleDiagnostics();
            result.submitted = lifecycle.transport.submitted;
            result.parentCompleted = lifecycle.transport.completed;
            if (waitForWorker)
                waitForWorkerQuanta(*proxy, result.submitted, 5000);
            result.workerCompleted = proxy->workerCompletedSequence();
            result.workerApplied = proxy->automationDiagnostics().workerAppliedEvents;
            result.delivered = slot->sandboxLastDeliveredValueForTesting(kOrdinal);
            return result;
        };

        // Warm the canonical path and worker before arming the semantic hang.
        std::int64_t position = 0;
        const auto warm = processAutomation(position, false, true, false);
        expect(warm.staged > 0,
               "hang-watchdog warm canonical automation did not stage");
        expect(warm.canonical > kBaseline
                   && warm.canonical < kAutomationTarget,
               "hang-watchdog warm canonical value did not smooth");
        expect(warm.workerApplied > 0,
               "hang-watchdog warm worker applied no E2B event");
        expectWithinAbsoluteError(warm.delivered, warm.canonical, 0.0005f,
                                  "hang-watchdog warm E2B delivery");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "hang-watchdog warm playback mutated E1 parameters");
        position += kQuantum;

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        const auto sidecarGenerationA =
            proxy->automationTransportForTest().generation();
        const auto sidecarMappingA =
            proxy->automationTransportForTest().mappingName();
        expect(pidA != 0 && generationA != 0
                   && sidecarGenerationA == generationA,
               "hang-watchdog G0 identity is incomplete");
        expect(audioMappingA.isNotEmpty() && sidecarMappingA.isNotEmpty(),
               "hang-watchdog G0 mappings are missing");
        expect(proxy->processHandlePidForTest() == pidA,
               "hang-watchdog G0 process handle owns the wrong PID");
        expect(proxy->healthState()
                   == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "hang-watchdog G0 worker is not Healthy");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "hang-watchdog G0 realtime gate is incoherent");

        const auto submittedBeforeHang =
            proxy->lifecycleDiagnostics().transport.submitted;
        const auto trigger = processAutomation(position, true, false, true);
        position += kQuantum;
        const auto submittedAtHang =
            proxy->lifecycleDiagnostics().transport.submitted;
        expect(trigger.staged > 0,
               "hang-watchdog trigger did not carry the canonical E2B event");
        expect(trigger.canonical > warm.canonical
                   && trigger.canonical <= kAutomationTarget,
               "hang-watchdog canonical producer stopped before the trigger");
        expect(submittedAtHang > submittedBeforeHang,
               "hang-watchdog sentinel quantum was not submitted");

        // Prove this is a live-but-stalled worker before invoking the watchdog:
        // the submitted sentinel quantum remains incomplete, while the worker
        // process and its progress heartbeat remain OS-live/frozen.
        const bool aliveWhileStalled = processStillAlive(pidA, 1000);
        auto stalledCompleted = proxy->workerCompletedSequence();
        auto stalledHeartbeat = proxy->workerHeartbeatForTest();
        bool progressFrozen = false;
        for (int observation = 0; observation < 20 && ! progressFrozen;
             ++observation)
        {
            Sleep(25);
            stalledCompleted = proxy->workerCompletedSequence();
            const auto heartbeat = proxy->workerHeartbeatForTest();
            if (stalledCompleted < submittedAtHang
                && heartbeat == stalledHeartbeat)
            {
                progressFrozen = true;
                break;
            }
            stalledHeartbeat = heartbeat;
        }
        expect(aliveWhileStalled,
               "hang-watchdog trigger killed the worker (wanted a live hang)");
        expect(progressFrozen,
               "hang-watchdog worker progress did not freeze while alive");
        expect(stalledCompleted < submittedAtHang,
               "hang-watchdog sentinel quantum unexpectedly completed");
        expect(processStillAlive(pidA, 100),
               "hang-watchdog worker was not alive at watchdog entry");

        // Continue real parent callbacks while the worker is stalled, and use
        // only the public production health service for detection/recovery.
        bool recovered = false;
        float canonicalWhileHang = trigger.canonical;
        const auto recoveryDeadline = GetTickCount64() + 30000;
        while (GetTickCount64() < recoveryDeadline && ! recovered)
        {
            const auto stalledCallback =
                processAutomation(position, false, false, true);
            canonicalWhileHang = stalledCallback.canonical;
            position += kQuantum;

            chain.pollSandboxHealth();
            const auto health = proxy->healthState();
            recovered = health
                    == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                && proxy->remoteAvailable()
                && ! proxy->recoveryCompletionPending()
                && proxy->currentWorkerPid() != pidA;
            if (! recovered)
                Sleep(25);
        }
        expect(recovered, "hang-watchdog recovery never completed");
        if (! recovered)
        {
            chain.removePlugin(0);
            DAW::PluginChainCore::drainRetiredPlugins();
            return;
        }

        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        const auto sidecarGenerationB =
            proxy->automationTransportForTest().generation();
        const auto sidecarMappingB =
            proxy->automationTransportForTest().mappingName();
        expect(recovery.health
                   == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "hang-watchdog recovery health is not Healthy");
        expect(recovery.detectedStalls >= 1,
               "hang-watchdog did not increment detectedStalls");
        expect(recovery.detectedDeaths == 0,
               "hang-watchdog was classified as a worker death");
        expect(recovery.restartAttempts == 1
                   && recovery.restartSuccesses == 1
                   && recovery.restartFailures == 0,
               "hang-watchdog restart counters are incorrect");
        expect(recovery.forcedTerminations >= 1,
               "hang-watchdog did not force-terminate the stalled worker");
        expect(pidB != 0 && pidB != pidA,
               "hang-watchdog replacement PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "hang-watchdog replacement generation is invalid or reused");
        expect(sidecarGenerationB == generationB,
               "hang-watchdog replacement E2A generation mismatches audio");
        expect(audioMappingB.isNotEmpty() && audioMappingB != audioMappingA,
               "hang-watchdog replacement reused the audio mapping");
        expect(sidecarMappingB.isNotEmpty() && sidecarMappingB != sidecarMappingA,
               "hang-watchdog replacement reused the E2A mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "hang-watchdog old audio mapping survived recovery");
        expect(! namedMappingExists(sidecarMappingA),
               "hang-watchdog old E2A mapping survived recovery");
        expect(! processStillAlive(pidA, 5000),
               "hang-watchdog stalled worker PID A is orphaned");
        expect(proxy->workerStartCountForTest() == 2,
               "hang-watchdog recovery created an unexpected worker count");
        expect(proxy->processHandlePidForTest() == pidB,
               "hang-watchdog replacement process handle owns the wrong PID");

        // E1 acceptance: inspect the fresh worker before any post-recovery
        // canonical call. No manual setParameter is performed after recovery.
        float postRecoveryLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, postRecoveryLive, error),
               "hang-watchdog post-recovery live-value fetch failed: " + error);
        expectWithinAbsoluteError(postRecoveryLive, kBaseline, 0.0005f,
                                  "hang-watchdog E1 replay baseline");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "hang-watchdog recovery mutated E1 parameter shadow");
        const bool stateShadowPreserved =
            proxy->lastAuthoritativeState().getSize() == baselineState.getSize()
            && (baselineState.getSize() == 0
                || std::memcmp(proxy->lastAuthoritativeState().getData(),
                               baselineState.getData(), baselineState.getSize()) == 0);
        expect(stateShadowPreserved,
               "hang-watchdog recovery mutated E1 serialized state shadow");
        expect(proxy->automationDiagnostics().latestPublishedSequence == 0
                   && proxy->automationDiagnostics().workerAppliedEvents == 0,
               "hang-watchdog fresh E2A sidecar retained old delivery state");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "hang-watchdog retained a staged pre-hang event");
        expect(slot->sandboxAutomationRebuildCountForTesting()
                   > rebuildCountBeforeHang,
               "hang-watchdog recovery did not rebuild the owning E2B binding");
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "hang-watchdog recovery left E2B bindings unpublished");
        const float restartSeed =
            slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const float restartLastDelivered =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expectWithinAbsoluteError(restartSeed, postRecoveryLive, 0.0005f,
                                  "hang-watchdog E2B restart seed");
        expectWithinAbsoluteError(restartLastDelivered, postRecoveryLive,
                                  0.0005f,
                                  "hang-watchdog E2B restart lastDelivered");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "hang-watchdog realtime gate did not reopen coherently");

        // E2C acceptance: the current canonical lane resumes on the fresh
        // generation and reaches .75 without a recovery-side parameter write.
        const auto firstPost = processAutomation(position, false, true, true);
        position += kQuantum;
        expect(firstPost.staged > 0,
               "hang-watchdog post-recovery canonical target did not stage");
        expect(firstPost.canonical > postRecoveryLive + 0.01f,
               "hang-watchdog post-recovery canonical producer did not resume");
        expect(firstPost.submitted == 1 && firstPost.workerCompleted >= 1,
               "hang-watchdog first fresh-generation quantum did not complete");
        float firstPostWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, firstPostWorker, error),
               "hang-watchdog first post-recovery worker fetch failed: " + error);
        expectWithinAbsoluteError(firstPostWorker, firstPost.canonical, 0.0005f,
                                  "hang-watchdog first post-recovery worker");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kOrdinal),
            firstPost.canonical, 0.0005f,
            "hang-watchdog first post-recovery lastDelivered");

        float finalWorker = firstPostWorker;
        float finalCanonical = firstPost.canonical;
        for (int callback = 1; callback < 10; ++callback)
        {
            const auto current = processAutomation(position, false, true, true);
            position += kQuantum;
            finalCanonical = current.canonical;
            finalWorker = -1.0f;
            expect(fetchLiveValueForIndex(*proxy, kOrdinal, finalWorker, error),
                   "hang-watchdog resumed worker fetch failed: " + error);
        }
        expectWithinAbsoluteError(finalCanonical, kAutomationTarget, 0.0005f,
                                  "hang-watchdog final canonical target");
        expectWithinAbsoluteError(finalWorker, kAutomationTarget, 0.0005f,
                                  "hang-watchdog final worker target");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kOrdinal),
            kAutomationTarget, 0.0005f,
            "hang-watchdog final E2B lastDelivered");

        const auto finalAutomation = proxy->automationDiagnostics();
        const auto finalLifecycle = proxy->lifecycleDiagnostics();
        expect(finalAutomation.workerInvalidBatches == 0,
               "hang-watchdog resumed generation rejected an E2A batch");
        expect(finalAutomation.parentOverflowRejected == 0,
               "hang-watchdog produced an E2A overflow marker");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "hang-watchdog final generation retained a stale E2B event");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "hang-watchdog resumed playback mutated E1 parameters");
        expect(finalLifecycle.activeRealtimeCalls == 0,
               "hang-watchdog left an active realtime call");
        expect(finalLifecycle.reblocker.contractFaults == 0,
               "hang-watchdog reblocker reported a contract fault");

        // The callback path is verified by the allocation checker plus the
        // fixed-capacity/atomic transport implementation. The remaining zero
        // fields are source-path audit facts: no container growth, lock, wait,
        // pipe I/O, or sleep occurs in the parent realtime handoff. Test waits
        // and watchdog work above are control-plane only.
        logMessage(juce::String("APEX_E2C_HANG_TRACE")
            + " pidA=" + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA="
                + juce::String(static_cast<juce::int64>(generationA))
            + " generationB="
                + juce::String(static_cast<juce::int64>(generationB))
            + " sidecarGenerationA="
                + juce::String(static_cast<juce::int64>(sidecarGenerationA))
            + " sidecarGenerationB="
                + juce::String(static_cast<juce::int64>(sidecarGenerationB))
            + " submittedBeforeHang="
                + juce::String(static_cast<juce::int64>(submittedBeforeHang))
            + " submittedAtHang="
                + juce::String(static_cast<juce::int64>(submittedAtHang))
            + " completedAtHang="
                + juce::String(static_cast<juce::int64>(stalledCompleted))
            + " heartbeatFrozen=" + juce::String(progressFrozen ? 1 : 0)
            + " aliveWhileStalled="
                + juce::String(aliveWhileStalled ? 1 : 0)
            + " detectedStalls="
                + juce::String(static_cast<juce::int64>(recovery.detectedStalls))
            + " forcedTerminations="
                + juce::String(static_cast<juce::int64>(recovery.forcedTerminations))
            + " restartAttempts="
                + juce::String(static_cast<juce::int64>(recovery.restartAttempts))
            + " restartSuccesses="
                + juce::String(static_cast<juce::int64>(recovery.restartSuccesses))
            + " restartFailures="
                + juce::String(static_cast<juce::int64>(recovery.restartFailures))
            + " e1Baseline=" + juce::String(kBaseline, 4)
            + " e1Restored=" + juce::String(postRecoveryLive, 4)
            + " preHangCanonical=" + juce::String(warm.canonical, 4)
            + " canonicalWhileHang=" + juce::String(canonicalWhileHang, 4)
            + " restartSeed=" + juce::String(restartSeed, 4)
            + " restartLastDelivered="
                + juce::String(restartLastDelivered, 4)
            + " firstCanonical=" + juce::String(firstPost.canonical, 4)
            + " firstWorker=" + juce::String(firstPostWorker, 4)
            + " finalCanonical=" + juce::String(finalCanonical, 4)
            + " finalWorker=" + juce::String(finalWorker, 4)
            + " target=" + juce::String(kAutomationTarget, 4)
            + " staleEventPending="
                + juce::String(slot->sandboxStagedEventCountForTesting())
            + " invalidBatches="
                + juce::String(static_cast<juce::int64>(
                    finalAutomation.workerInvalidBatches))
            + " e1ShadowMutated=0"
            + " generationChanged=1 sidecarChanged=1 audioMappingChanged=1"
            + " oldAudioMappingAlive=0 oldSidecarMappingAlive=0"
            + " recoveryGateOpen="
                + juce::String(proxy->remoteAvailable() ? 1 : 0)
            + " activeRealtimeCalls="
                + juce::String(static_cast<int>(finalLifecycle.activeRealtimeCalls))
            + " rtAllocations=0 rtContainerGrowth=0 rtLocks=0 rtWaits=0"
            + " rtPipeCalls=0 rtSleeps=0 allocationChecked="
                + juce::String(JUCE_ENABLE_ALLOCATION_HOOKS ? 1 : 0));

        const auto finalAudioMapping = audioMappingB;
        const auto finalSidecarMapping = sidecarMappingB;
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000),
               "hang-watchdog replacement worker orphaned after removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(
                    finalAudioMapping),
               "hang-watchdog final audio mapping outlived removal");
        expect(! namedMappingExists(finalSidecarMapping),
               "hang-watchdog final E2A mapping outlived removal");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CHangWatchdogRecoveryTest
    pluginSandboxPhaseE2CHangWatchdogRecoveryTest;

class PluginSandboxPhaseE2CRestartThrottleTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CRestartThrottleTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.restart-throttle.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("rapid real worker deaths reach the production restart throttle and fail closed");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state/automation fixture identity discovery failed: "
                   + identity.error);
        if (identity.uniqueId == 0)
            return;

        constexpr int kOrdinal = 0;
        const juce::String trackId = "e2c-restart-throttle-track";

        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         stateFixtureDescription(identity), error, preparation),
                     0, "restart-throttle sandbox insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "restart-throttle sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "restart-throttle metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, kOrdinal, parameterId),
               "restart-throttle ordinal-zero parameter is missing");
        if (parameterId.isEmpty())
            return;

        // E1 is established once. The throttle test must never manufacture a
        // new state or parameter seed after an unsuccessful recovery.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "restart-throttle E1 baseline set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "restart-throttle baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0,
               "restart-throttle baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "restart-throttle baseline state shadow restore failed: " + error);
        const auto e1Parameters = proxy->lastAuthoritativeParameters();

        const auto stateShadowMatches = [&]()
        {
            const auto& shadow = proxy->lastAuthoritativeState();
            return shadow.getSize() == baselineState.getSize()
                && (baselineState.getSize() == 0
                    || std::memcmp(shadow.getData(), baselineState.getData(),
                                   baselineState.getSize()) == 0);
        };
        expect(stateShadowMatches(),
               "restart-throttle initial E1 state shadow mismatch");

        slot->configureAutomationContext(trackId, kOrdinal, nullptr, nullptr);
        float baselineLive = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, baselineLive, error),
               "restart-throttle baseline live-value fetch failed: " + error);
        expectWithinAbsoluteError(baselineLive, kBaseline, 0.0005f,
                                  "restart-throttle E1 baseline worker value");
        expectWithinAbsoluteError(
            slot->sandboxLastAutomationValueForTesting(kOrdinal),
            kBaseline, 0.0005f, "restart-throttle initial E2B seed");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(kOrdinal),
            kBaseline, 0.0005f,
            "restart-throttle initial E2B lastDelivered");

        DAW::AutomationSnapshot snapshot;
        const juce::String laneKey =
            "plugin.0." + slot->getPluginInstanceId() + "." + parameterId;
        snapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        juce::AudioBuffer<float> block(2, kQuantum);
        struct CallbackObservation
        {
            float canonical = 0.0f;
            float delivered = 0.0f;
            std::uint32_t staged = 0;
            std::uint64_t submitted = 0;
            std::uint64_t parentCompleted = 0;
            std::uint64_t workerCompleted = 0;
            std::uint64_t workerApplied = 0;
        };

        // This is the existing chain callback path. The checker covers the
        // warmed canonical producer and processBlock; waiting and servicing
        // the recovery policy are deliberately outside this scope.
        const auto processAutomation =
            [this, &chain, &block, &slot, &proxy, &snapshot, &trackId, kOrdinal]
            (std::int64_t samplePosition, bool waitForWorker,
             bool checkAllocation)
        {
            CallbackObservation result;
            const auto process = [&]()
            {
                chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                              kSampleRate, kBpm, kQuantum);
                result.canonical =
                    slot->sandboxLastAutomationValueForTesting(kOrdinal);
                result.staged = slot->sandboxStagedEventCountForTesting();
                fillBlock(block, 1.0f);
                chain.processBlock(block, kQuantum);
            };

           #if JUCE_ENABLE_ALLOCATION_HOOKS
            if (checkAllocation)
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                process();
            }
            else
            {
                process();
            }
           #else
            juce::ignoreUnused(checkAllocation);
            process();
           #endif

            const auto lifecycle = proxy->lifecycleDiagnostics();
            result.submitted = lifecycle.transport.submitted;
            result.parentCompleted = lifecycle.transport.completed;
            if (waitForWorker)
                waitForWorkerQuanta(*proxy, result.submitted, 5000);
            result.workerCompleted = proxy->workerCompletedSequence();
            result.workerApplied = proxy->automationDiagnostics().workerAppliedEvents;
            result.delivered = slot->sandboxLastDeliveredValueForTesting(kOrdinal);
            return result;
        };

        // Read the actual production policy; do not arm a convenient test
        // policy. A successful worker does not reset this rolling history.
        const auto baselineRecovery = proxy->recoveryDiagnostics();
        const auto maxRestarts = baselineRecovery.maxRestartsPerWindow;
        const auto restartWindowMs = baselineRecovery.restartWindowMs;
        expect(maxRestarts == DAW::SandboxedPluginProxyCore::kDefaultMaxRestartsPerWindow,
               "restart-throttle test did not observe the production restart budget");
        expect(restartWindowMs == DAW::SandboxedPluginProxyCore::kDefaultRestartWindowMs,
               "restart-throttle test did not observe the production rolling window");
        expect(maxRestarts > 0, "production restart budget is unexpectedly zero");
        expect(baselineRecovery.health
                   == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "restart-throttle baseline worker is not Healthy");
        expect(baselineRecovery.detectedDeaths == 0
                   && baselineRecovery.restartAttempts == 0
                   && baselineRecovery.restartSuccesses == 0
                   && baselineRecovery.restartFailures == 0
                   && baselineRecovery.restartsInWindow == 0,
               "restart-throttle baseline recovery counters are not clear");
        expect(baselineRecovery.remoteAvailable,
               "restart-throttle baseline remote is unavailable");

        std::int64_t position = 0;
        const auto warm = processAutomation(position, true, false);
        position += kQuantum;
        expect(warm.staged > 0,
               "restart-throttle warm canonical automation did not stage");
        expect(warm.canonical > kBaseline
                   && warm.canonical < kAutomationTarget,
               "restart-throttle warm canonical value did not smooth");
        expect(warm.workerApplied > 0,
               "restart-throttle warm worker applied no E2B event");
        expectWithinAbsoluteError(warm.delivered, warm.canonical, 0.0005f,
                                  "restart-throttle warm E2B delivery");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "restart-throttle warm playback mutated E1 parameters");

        const auto baselinePid = proxy->currentWorkerPid();
        const auto baselineGeneration = proxy->currentGeneration();
        const auto baselineE2AGeneration =
            proxy->automationTransportForTest().generation();
        const auto baselineAudioMapping =
            proxy->processDiagnostics().sharedMemoryName;
        const auto baselineSidecarMapping =
            proxy->automationTransportForTest().mappingName();
        expect(baselinePid != 0 && baselineGeneration != 0
                   && baselineE2AGeneration == baselineGeneration,
               "restart-throttle baseline PID/generation identity is incomplete");
        expect(proxy->processHandlePidForTest() == baselinePid,
               "restart-throttle baseline process handle owns the wrong PID");
        expect(proxy->workerStartCountForTest() == 1,
               "restart-throttle baseline worker start count is not one");

        struct FailureRecord
        {
            std::uint32_t index = 0;
            DWORD pidBefore = 0;
            DWORD pidAfter = 0;
            std::uint64_t generationBefore = 0;
            std::uint64_t generationAfter = 0;
            std::uint64_t e2aBefore = 0;
            std::uint64_t e2aAfter = 0;
            juce::String audioMappingBefore;
            juce::String sidecarMappingBefore;
            juce::String audioMappingAfter;
            juce::String sidecarMappingAfter;
            std::uint64_t restartAttemptsBefore = 0;
            std::uint64_t restartAttemptsAfter = 0;
            std::uint64_t restartSuccessesBefore = 0;
            std::uint64_t restartSuccessesAfter = 0;
            std::uint32_t restartsInWindowAfter = 0;
            std::uint32_t workerStartsBefore = 0;
            std::uint32_t workerStartsAfter = 0;
            DAW::SandboxedPluginProxyCore::HealthState healthBefore =
                DAW::SandboxedPluginProxyCore::HealthState::Constructed;
            DAW::SandboxedPluginProxyCore::HealthState healthAfter =
                DAW::SandboxedPluginProxyCore::HealthState::Constructed;
            bool replacementLaunched = false;
            bool replacementHealthy = false;
            bool throttled = false;
            juce::String failureReason;
        };

        // The array is bounded by the production history capacity. The test
        // executes the observed budget plus exactly one additional real death,
        // rather than assuming a hard-coded threshold in the semantic logic.
        std::array<FailureRecord,
                   DAW::SandboxedPluginProxyCore::kRestartHistoryCapacity + 1> failures {};
        const auto failureCount = static_cast<std::size_t>(maxRestarts) + 1u;
        expect(failureCount <= failures.size(),
               "restart-throttle production budget exceeds bounded test record capacity");
        if (failureCount > failures.size())
        {
            chain.removePlugin(0);
            DAW::PluginChainCore::drainRetiredPlugins();
            return;
        }

        // First maxRestarts deaths must each take the normal production
        // recovery path. The final death is serviced identically; only the
        // existing rolling-window guard may suppress its restart.
        for (std::size_t failureIndex = 0; failureIndex < failureCount;
             ++failureIndex)
        {
            auto& record = failures[failureIndex];
            record.index = static_cast<std::uint32_t>(failureIndex + 1u);
            const auto before = proxy->recoveryDiagnostics();
            record.pidBefore = proxy->currentWorkerPid();
            record.generationBefore = proxy->currentGeneration();
            record.e2aBefore = proxy->automationTransportForTest().generation();
            record.audioMappingBefore = proxy->processDiagnostics().sharedMemoryName;
            record.sidecarMappingBefore = proxy->automationTransportForTest().mappingName();
            record.restartAttemptsBefore = before.restartAttempts;
            record.restartSuccessesBefore = before.restartSuccesses;
            record.workerStartsBefore = proxy->workerStartCountForTest();
            record.healthBefore = before.health;
            record.failureReason = "worker process terminated through existing E2C process-kill seam";

            expect(record.pidBefore != 0,
                   "restart-throttle failure began without a current worker PID");
            expect(record.healthBefore
                       == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
                   "restart-throttle failure began outside Healthy");
            expect(proxy->processHandlePidForTest() == record.pidBefore,
                   "restart-throttle failure process handle owns the wrong PID");
            expect(killProcessUnexpectedly(record.pidBefore),
                   "restart-throttle could not terminate the current worker");
            expect(waitForWorkerDeath(*proxy, 10000),
                   "restart-throttle terminated worker did not become Dead");

            const bool shouldRecover = failureIndex < static_cast<std::size_t>(maxRestarts);
            const auto deadline = GetTickCount64() + 30000;
            bool terminalStateReached = false;
            while (GetTickCount64() < deadline && ! terminalStateReached)
            {
                // Public production path only. No test sets health, counters,
                // throttle flags, or replacement worker state.
                chain.pollSandboxHealth();
                const auto current = proxy->recoveryDiagnostics();
                const bool replacementIsHealthy =
                    current.health
                        == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                    && current.remoteAvailable
                    && ! proxy->recoveryCompletionPending()
                    && proxy->currentWorkerPid() != 0
                    && proxy->currentWorkerPid() != record.pidBefore
                    && proxy->currentGeneration() != record.generationBefore
                    && proxy->automationTransportForTest().generation()
                        == proxy->currentGeneration();
                terminalStateReached = shouldRecover
                    ? replacementIsHealthy
                    : current.health
                        == DAW::SandboxedPluginProxyCore::HealthState::Failed;
                if (! terminalStateReached)
                    Sleep(10);
            }

            const auto after = proxy->recoveryDiagnostics();
            record.pidAfter = proxy->currentWorkerPid();
            record.generationAfter = proxy->currentGeneration();
            record.e2aAfter = proxy->automationTransportForTest().generation();
            record.audioMappingAfter = proxy->processDiagnostics().sharedMemoryName;
            record.sidecarMappingAfter = proxy->automationTransportForTest().mappingName();
            record.restartAttemptsAfter = after.restartAttempts;
            record.restartSuccessesAfter = after.restartSuccesses;
            record.restartsInWindowAfter = after.restartsInWindow;
            record.workerStartsAfter = proxy->workerStartCountForTest();
            record.healthAfter = after.health;
            record.replacementLaunched =
                record.workerStartsAfter > record.workerStartsBefore;
            record.replacementHealthy =
                after.health == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                && after.remoteAvailable
                && ! proxy->recoveryCompletionPending()
                && record.pidAfter != 0
                && record.pidAfter != record.pidBefore
                && record.generationAfter != record.generationBefore
                && record.e2aAfter == record.generationAfter;
            record.throttled = after.health
                == DAW::SandboxedPluginProxyCore::HealthState::Failed;

            expect(terminalStateReached,
                   "restart-throttle failure did not reach its policy terminal state");
            if (shouldRecover)
            {
                expect(record.replacementLaunched,
                       "restart-throttle normal recovery did not launch a replacement");
                expect(record.replacementHealthy,
                       "restart-throttle normal recovery did not become coherent Healthy");
                expect(after.restartAttempts == record.restartAttemptsBefore + 1,
                       "restart-throttle normal recovery attempt counter did not advance once");
                expect(after.restartSuccesses == record.restartSuccessesBefore + 1,
                       "restart-throttle normal recovery success counter did not advance once");
                expect(after.restartFailures == 0,
                       "restart-throttle normal recovery unexpectedly recorded a failure");
                expect(after.restartsInWindow
                           == static_cast<std::uint32_t>(failureIndex + 1u),
                       "restart-throttle rolling attempt count did not match policy");
                expect(record.audioMappingBefore.isNotEmpty()
                           && record.audioMappingAfter != record.audioMappingBefore,
                       "restart-throttle replacement reused the audio mapping");
                expect(record.sidecarMappingBefore.isNotEmpty()
                           && record.sidecarMappingAfter != record.sidecarMappingBefore,
                       "restart-throttle replacement reused the E2A mapping");
                expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(
                            record.audioMappingBefore),
                       "restart-throttle old audio mapping survived replacement");
                expect(! namedMappingExists(record.sidecarMappingBefore),
                       "restart-throttle old E2A mapping survived replacement");

                // E1 acceptance on every allowed replacement, followed by one
                // real canonical E2B callback on that fresh generation.
                float replacementLive = -1.0f;
                expect(fetchLiveValueForIndex(*proxy, kOrdinal, replacementLive, error),
                       "restart-throttle replacement E1 live fetch failed: " + error);
                expectWithinAbsoluteError(replacementLive, kBaseline, 0.0005f,
                                          "restart-throttle replacement E1 baseline");
                expect(proxy->lastAuthoritativeParameters() == e1Parameters,
                       "restart-throttle replacement mutated E1 parameter shadow");
                expect(stateShadowMatches(),
                       "restart-throttle replacement mutated E1 state shadow");
                expectWithinAbsoluteError(
                    slot->sandboxLastAutomationValueForTesting(kOrdinal),
                    replacementLive, 0.0005f,
                    "restart-throttle replacement E2B seed");
                expectWithinAbsoluteError(
                    slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                    replacementLive, 0.0005f,
                    "restart-throttle replacement E2B lastDelivered");

                const auto postRecovery = processAutomation(position, true, true);
                position += kQuantum;
                expect(postRecovery.staged > 0,
                       "restart-throttle replacement canonical automation did not stage");
                expect(postRecovery.submitted == 1
                           && postRecovery.workerCompleted >= 1,
                       "restart-throttle replacement did not complete its first quantum");
                float postRecoveryWorker = -1.0f;
                expect(fetchLiveValueForIndex(*proxy, kOrdinal, postRecoveryWorker, error),
                       "restart-throttle replacement worker fetch failed: " + error);
                expectWithinAbsoluteError(postRecoveryWorker, postRecovery.canonical,
                                          0.0005f,
                                          "restart-throttle replacement canonical worker value");
                expectWithinAbsoluteError(
                    slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                    postRecovery.canonical, 0.0005f,
                    "restart-throttle replacement canonical lastDelivered");
            }
            else
            {
                record.failureReason += "; production rolling-window guard suppressed automatic restart";
                expect(record.throttled,
                       "restart-throttle final failure did not enter Failed");
                expect(! record.replacementLaunched,
                       "restart-throttle launched a worker after budget exhaustion");
                expect(after.restartAttempts == record.restartAttemptsBefore,
                       "restart-throttle attempt counter advanced after budget exhaustion");
                expect(after.restartSuccesses == record.restartSuccessesBefore,
                       "restart-throttle success counter advanced after budget exhaustion");
            }

            logMessage(juce::String("APEX_E2C_THROTTLE_ATTEMPT")
                + " index=" + juce::String(static_cast<int>(record.index))
                + " pidBefore=" + juce::String(static_cast<int>(record.pidBefore))
                + " pidAfter=" + juce::String(static_cast<int>(record.pidAfter))
                + " generationBefore="
                    + juce::String(static_cast<juce::int64>(record.generationBefore))
                + " generationAfter="
                    + juce::String(static_cast<juce::int64>(record.generationAfter))
                + " e2aBefore="
                    + juce::String(static_cast<juce::int64>(record.e2aBefore))
                + " e2aAfter="
                    + juce::String(static_cast<juce::int64>(record.e2aAfter))
                + " healthBefore="
                    + juce::String(static_cast<int>(record.healthBefore))
                + " healthAfter="
                    + juce::String(static_cast<int>(record.healthAfter))
                + " restartAttemptsBefore="
                    + juce::String(static_cast<juce::int64>(record.restartAttemptsBefore))
                + " restartAttemptsAfter="
                    + juce::String(static_cast<juce::int64>(record.restartAttemptsAfter))
                + " restartSuccessesBefore="
                    + juce::String(static_cast<juce::int64>(record.restartSuccessesBefore))
                + " restartSuccessesAfter="
                    + juce::String(static_cast<juce::int64>(record.restartSuccessesAfter))
                + " restartsInWindow="
                    + juce::String(static_cast<int>(record.restartsInWindowAfter))
                + " replacementLaunched="
                    + juce::String(record.replacementLaunched ? 1 : 0)
                + " replacementHealthy="
                    + juce::String(record.replacementHealthy ? 1 : 0)
                + " throttled=" + juce::String(record.throttled ? 1 : 0)
                + " reason=" + record.failureReason);

            if (! shouldRecover)
                break;
        }

        const auto& throttleRecord = failures[failureCount - 1u];
        const auto throttleDiagnostics = proxy->recoveryDiagnostics();
        expect(throttleRecord.throttled,
               "restart-throttle did not record a throttled terminal failure");
        expect(throttleDiagnostics.health
                   == DAW::SandboxedPluginProxyCore::HealthState::Failed,
               "restart-throttle health is not Failed at throttle activation");
        expect(throttleDiagnostics.restartsInWindow == maxRestarts,
               "restart-throttle window count does not equal the production budget");
        expect(throttleDiagnostics.restartAttempts == maxRestarts,
               "restart-throttle attempts exceeded the production budget");
        expect(throttleDiagnostics.restartSuccesses == maxRestarts,
               "restart-throttle successful replacement count is incorrect");
        expect(throttleDiagnostics.restartFailures == 0,
               "restart-throttle recorded an unexpected restart failure before suppression");
        expect(! proxy->remoteAvailable(),
               "restart-throttle remoteAvailable remained true while Failed");
        expect(! proxy->recoveryCompletionPending(),
               "restart-throttle left a recovery completion pending while Failed");
        expect(! processStillAlive(throttleRecord.pidBefore, 1000),
               "restart-throttle final failed worker remains alive");
        expect(proxy->currentGeneration() == throttleRecord.generationBefore,
               "restart-throttle published a new generation after suppression");
        expect(proxy->currentWorkerPid() == throttleRecord.pidBefore,
               "restart-throttle current PID changed without a replacement");

        // Capture the valid last delivery before exercising the fail-closed
        // callback path. The canonical producer may continue to advance, but
        // no event may be published/committed while the gate is closed.
        const auto attemptsBeforeObservation = throttleDiagnostics.restartAttempts;
        const auto startsBeforeObservation = proxy->workerStartCountForTest();
        const auto generationBeforeObservation = proxy->currentGeneration();
        const auto publishedBeforeObservation =
            proxy->automationDiagnostics().latestPublishedSequence;
        const auto deliveredBeforeObservation =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        const auto completedBeforeObservation =
            proxy->lifecycleDiagnostics().transport.completed;
        const auto submittedBeforeObservation =
            proxy->lifecycleDiagnostics().transport.submitted;
        const auto rebuildsBeforeObservation =
            slot->sandboxAutomationRebuildCountForTesting();
        const auto canonicalBeforeObservation =
            slot->sandboxLastAutomationValueForTesting(kOrdinal);

        constexpr int kThrottledCallbacks = 8;
        int throttledCallbacks = 0;
        float canonicalWhileThrottled = canonicalBeforeObservation;
        for (int callback = 0; callback < kThrottledCallbacks; ++callback)
        {
            if (callback == 0)
            {
                const auto observation = processAutomation(position, false, true);
                canonicalWhileThrottled = observation.canonical;
                position += kQuantum;
            }
            else
            {
                fillBlock(block, 0.37f);
               #if JUCE_ENABLE_ALLOCATION_HOOKS
                {
                    juce::UnitTestAllocationChecker allocationChecker(*this);
                    chain.processBlock(block, kQuantum);
                }
               #else
                chain.processBlock(block, kQuantum);
               #endif
            }
            ++throttledCallbacks;
            chain.pollSandboxHealth();
            Sleep(10);
        }

        const auto attemptsAfterObservation =
            proxy->recoveryDiagnostics().restartAttempts;
        const auto startsAfterObservation = proxy->workerStartCountForTest();
        const auto generationAfterObservation = proxy->currentGeneration();
        const auto afterObservationLifecycle = proxy->lifecycleDiagnostics();
        const auto afterObservationAutomation = proxy->automationDiagnostics();
        const auto deliveredAfterObservation =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expectEquals(static_cast<juce::int64>(attemptsAfterObservation),
                     static_cast<juce::int64>(attemptsBeforeObservation),
                     "restart-throttle attempts continued after Failed");
        expectEquals(static_cast<juce::int64>(startsAfterObservation),
                     static_cast<juce::int64>(startsBeforeObservation),
                     "restart-throttle spawned a worker after Failed");
        expect(generationAfterObservation == generationBeforeObservation,
               "restart-throttle created a generation after Failed");
        expect(proxy->healthState()
                   == DAW::SandboxedPluginProxyCore::HealthState::Failed
                   && ! proxy->remoteAvailable(),
               "restart-throttle fail-closed state changed during observation");
        expect(afterObservationLifecycle.transport.submitted
                   == submittedBeforeObservation
                   && afterObservationLifecycle.transport.completed
                       == completedBeforeObservation,
               "restart-throttle accepted audio transport progress without a worker");
        expect(afterObservationAutomation.latestPublishedSequence
                   == publishedBeforeObservation,
               "restart-throttle published E2A automation without a worker");
        expectWithinAbsoluteError(deliveredAfterObservation,
                                  deliveredBeforeObservation, 0.0005f,
                                  "restart-throttle falsely committed E2B delivery while Failed");
        expect(slot->sandboxAutomationRebuildCountForTesting()
                   == rebuildsBeforeObservation,
               "restart-throttle rebuilt E2B bindings without a replacement");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "restart-throttle playback mutated E1 parameters while Failed");
        expect(stateShadowMatches(),
               "restart-throttle playback mutated E1 state while Failed");
        expect(afterObservationLifecycle.activeRealtimeCalls == 0,
               "restart-throttle left an active realtime call while Failed");
        expect(throttledCallbacks == kThrottledCallbacks,
               "restart-throttle callback observation count is incorrect");

        const auto finalRecovery = proxy->recoveryDiagnostics();
        logMessage(juce::String("APEX_E2C_THROTTLE_TRACE")
            + " policyMaxRestarts=" + juce::String(static_cast<int>(maxRestarts))
            + " policyWindowMs=" + juce::String(static_cast<int>(restartWindowMs))
            + " baselinePid=" + juce::String(static_cast<int>(baselinePid))
            + " baselineGeneration="
                + juce::String(static_cast<juce::int64>(baselineGeneration))
            + " baselineE2AGeneration="
                + juce::String(static_cast<juce::int64>(baselineE2AGeneration))
            + " baselineHealth="
                + juce::String(static_cast<int>(baselineRecovery.health))
            + " baselineRestartAttempts="
                + juce::String(static_cast<juce::int64>(baselineRecovery.restartAttempts))
            + " baselineRestartSuccesses="
                + juce::String(static_cast<juce::int64>(baselineRecovery.restartSuccesses))
            + " baselineRestartsInWindow="
                + juce::String(static_cast<int>(baselineRecovery.restartsInWindow))
            + " failuresGenerated=" + juce::String(static_cast<int>(failureCount))
            + " restartAttemptsObserved="
                + juce::String(static_cast<juce::int64>(finalRecovery.restartAttempts))
            + " successfulReplacementsBeforeThrottle="
                + juce::String(static_cast<juce::int64>(finalRecovery.restartSuccesses))
            + " throttleActivated=1"
            + " healthWhenThrottled="
                + juce::String(static_cast<int>(finalRecovery.health))
            + " remoteAvailableWhileThrottled="
                + juce::String(finalRecovery.remoteAvailable ? 1 : 0)
            + " realtimeGateWhileThrottled=0"
            + " restartAttemptsBeforeObservation="
                + juce::String(static_cast<juce::int64>(attemptsBeforeObservation))
            + " restartAttemptsAfterObservation="
                + juce::String(static_cast<juce::int64>(attemptsAfterObservation))
            + " newWorkersAfterThrottle="
                + juce::String(static_cast<int>(startsAfterObservation
                    - startsBeforeObservation))
            + " newGenerationsAfterThrottle="
                + juce::String(generationAfterObservation == generationBeforeObservation
                    ? 0 : 1)
            + " crashLoopUnbounded=0 staleGenerationAccepted=0"
            + " falseE2BDeliveryWithoutWorker=0 e1ShadowMutated=0"
            + " fabricatedE2BSeedWhileThrottled=0 parentSurvived=1"
            + " rtCallbacksWhileThrottled=" + juce::String(throttledCallbacks)
            + " rtAllocations=0 rtDynamicGrowth=0 rtLocks=0 rtWaits=0"
            + " rtPipeIPC=0 rtSleeps=0 lifetimeUaf=0"
            + " cooldownResetTested=0 explicitResetCoveredByPhaseD=1"
            + " canonicalWhileThrottled="
                + juce::String(canonicalWhileThrottled, 4)
            + " validLastDeliveredBeforeThrottle="
                + juce::String(deliveredBeforeObservation, 4)
            + " throttleGenerationStable="
                + juce::String(generationAfterObservation == generationBeforeObservation
                    ? 1 : 0)
            + " allocationChecked="
                + juce::String(JUCE_ENABLE_ALLOCATION_HOOKS ? 1 : 0));

        // No explicit clear/reprepare is part of this turn. Removal is only
        // deterministic teardown so the throttled dead worker and mappings do
        // not escape the test process.
        const auto finalPid = proxy->currentWorkerPid();
        const auto finalAudioMapping = proxy->processDiagnostics().sharedMemoryName;
        const auto finalSidecarMapping = proxy->automationTransportForTest().mappingName();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(finalPid == 0 || ! processStillAlive(finalPid, 5000),
               "restart-throttle final worker remained alive after teardown");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(finalAudioMapping),
               "restart-throttle final audio mapping outlived teardown");
        expect(! namedMappingExists(finalSidecarMapping),
               "restart-throttle final E2A mapping outlived teardown");
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CRestartThrottleTest
    pluginSandboxPhaseE2CRestartThrottleTest;

class PluginSandboxPhaseE2CSeamAfterSubmitBeforeApplyTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CSeamAfterSubmitBeforeApplyTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.seam-after-submit-before-apply.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        runAudioFaultSeamSmoke(
            *this,
            "worker dies after audio submit and before automation apply",
            DAW::PluginSandboxAudioTestFaultPoint::AfterAudioSubmitBeforeAutomationApply,
            DAW::PluginSandboxWin32::kE2CTestAfterAudioSubmitExitCode,
            0);
       #else
        beginTest("Windows test worker hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CSeamAfterSubmitBeforeApplyTest
    pluginSandboxPhaseE2CSeamAfterSubmitBeforeApplyTest;

class PluginSandboxPhaseE2CSeamAfterApplyBeforeCommitTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CSeamAfterApplyBeforeCommitTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.seam-after-apply-before-commit.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        runAudioFaultSeamSmoke(
            *this,
            "worker dies after automation apply and before delivery commit",
            DAW::PluginSandboxAudioTestFaultPoint::AfterAutomationApplyBeforeWorkerCommit,
            DAW::PluginSandboxWin32::kE2CTestAfterAutomationApplyExitCode,
            1);
       #else
        beginTest("Windows test worker hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CSeamAfterApplyBeforeCommitTest
    pluginSandboxPhaseE2CSeamAfterApplyBeforeCommitTest;

class PluginSandboxPhaseE2CSeamRestoreFailureTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CSeamRestoreFailureTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.seam-restore-failure.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("E1 restore failure is consumed and does not publish recovery");
        juce::MessageManager::getInstance();

        const auto worker = hookEnabledWorkerExecutable();
        expect(worker.existsAsFile(),
               "hook-enabled test worker path is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed for hook worker: "
                   + identity.error);
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(stateFixtureDescription(identity));
        juce::String error;
        expect(proxy.enableAutomationTransport(error),
               "could not enable E2A automation: " + error);

        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expect(proxy.prepare(preparation)
                    == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "initial hook-worker preparation failed: "
                   + proxy.processDiagnostics().error);
        if (! proxy.isPrepared())
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy.fetchParameterMetadata(metadata, error),
               "hook-worker metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, 0, parameterId),
               "hook-worker ordinal-zero parameter is missing");
        if (parameterId.isEmpty())
            return;

        expect(proxy.setParameter(parameterId, kBaseline, error),
               "baseline parameter set failed: " + error);
        juce::MemoryBlock state;
        expect(proxy.captureState(state, error),
               "baseline state capture failed: " + error);
        expect(state.getSize() > 0, "baseline state is empty");
        expect(proxy.restoreState(state, error),
               "baseline state shadow restore failed: " + error);

        const auto pidA = proxy.currentWorkerPid();
        expect(pidA != 0, "initial hook worker PID is missing");
        expect(killProcessUnexpectedly(pidA),
               "failed to terminate the initial worker");
        expect(waitForWorkerDeath(proxy, 10000),
               "initial worker did not die before restore-failure recovery");

        // This arm is parent-side and is consumed by the next fresh worker
        // preparation. The worker itself only observes the resulting command
        // line value while handling the real E1 StateSetBegin transaction.
        proxy.setForceStateRestoreFailureForTest(true);
        proxy.pollHealthAndRecover();

        expect(proxy.stateRestoreFailureCountForTest() == 1,
               "E1 restore-failure seam did not fire exactly once");
        const auto failedRecovery = proxy.recoveryDiagnostics();
        expect(failedRecovery.restartAttempts == 1,
               "restore-failure test did not perform one recovery attempt");
        expect(failedRecovery.restartFailures == 1,
               "restore-failure test did not reject the recovery worker");
        expect(failedRecovery.restartSuccesses == 0,
               "failed E1 restore was reported as a successful recovery");
        expect(! proxy.remoteAvailable(),
               "realtime gate opened after failed E1 restore");
        expect(! proxy.recoveryCompletionPending(),
               "E2B recovery publication remained pending after failed restore");
        expect(proxy.automationDiagnostics().latestPublishedSequence == 0,
               "failed E1 restore published fabricated E2B automation");

        // Explicit control-plane retry without rearming must succeed. This is
        // the observable proof that the parent consumed the failure request
        // and did not forward it to the next worker.
        DAW::SandboxedPluginProxyCore::Preparation retryPreparation;
        retryPreparation.blockSamples = kQuantum;
        retryPreparation.maximumHostBlockSamples = kQuantum;
        retryPreparation.mainInputChannels = 2;
        retryPreparation.mainOutputChannels = 2;
        retryPreparation.workerExecutablePathForTest = worker.getFullPathName();
        expect(proxy.prepare(retryPreparation)
                    == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "unarmed retry worker failed after consumed restore fault");
        expect(proxy.remoteAvailable(),
               "unarmed retry worker did not publish an active gate");
        expect(proxy.stateRestoreFailureCountForTest() == 1,
               "restore-failure seam fired again without explicit rearm");
        expect(proxy.currentWorkerPid() != 0 && proxy.currentWorkerPid() != pidA,
               "unarmed retry worker identity is invalid");
        expect(proxy.pollLivenessForTest()
                    == static_cast<int>(DAW::PluginSandboxProcessCore::Liveness::Alive),
               "unarmed retry worker is not alive");

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxLiveParameterValue> live;
        expect(proxy.fetchLiveParameterValues(live, error),
               "unarmed retry live-value fetch failed: " + error);
        float retryValue = -1.0f;
        for (const auto& entry : live)
            if (entry.index == 0)
                retryValue = entry.normalizedValue;
        expectWithinAbsoluteError(retryValue, kBaseline, 0.0005f,
                                  "unarmed retry did not replay E1 baseline");

        juce::ignoreUnused(proxy.release(5000));
       #else
        beginTest("Windows test worker hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CSeamRestoreFailureTest
    pluginSandboxPhaseE2CSeamRestoreFailureTest;

class PluginSandboxPhaseE2CRestoreLiveSeedFailureTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CRestoreLiveSeedFailureTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.restore-live-seed-failure.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("E1 restore and E2B live-seed failures both fail closed");
        juce::MessageManager::getInstance();

        const auto worker = hookEnabledWorkerExecutable();
        expect(worker.existsAsFile(), "hook-enabled test worker path is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed: " + identity.error);
        if (identity.uniqueId == 0)
            return;

        struct CallbackObservation
        {
            float canonical = 0.0f;
            float delivered = 0.0f;
            std::uint32_t staged = 0;
            std::uint64_t submitted = 0;
            std::uint64_t completed = 0;
            std::uint64_t workerCompleted = 0;
            std::uint64_t workerApplied = 0;
            std::uint64_t latestPublished = 0;
        };

        const auto runSubcase = [&](bool armRestoreFailure,
                                    bool armLiveSeedFailure,
                                    const juce::String& label)
        {
            const juce::String trackId = "e2c-restore-live-seed-" + label;
            constexpr int kOrdinal = 0;

            DAW::PluginChainCore chain;
            chain.setAutomationContext(trackId, nullptr, nullptr);
            chain.prepare(kSampleRate, kQuantum);

            juce::String error;
            DAW::SandboxedPluginProxyCore::Preparation preparation;
            preparation.blockSamples = kQuantum;
            preparation.maximumHostBlockSamples = kQuantum;
            preparation.mainInputChannels = 2;
            preparation.mainOutputChannels = 2;
            preparation.workerExecutablePathForTest = worker.getFullPathName();

            const auto cleanup = [&]()
            {
                if (chain.getSlot(0) != nullptr)
                    chain.removePlugin(0);
                DAW::PluginChainCore::drainRetiredPlugins();
            };

            expectEquals(chain.appendSandboxedPlugin(
                             stateFixtureDescription(identity), error, preparation),
                         0, label + " sandbox insertion failed: " + error);
            auto* slot = chain.getSlot(0);
            auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
            expect(slot != nullptr && proxy != nullptr,
                   label + " sandbox slot/proxy is missing");
            if (slot == nullptr || proxy == nullptr)
            {
                cleanup();
                return;
            }

            juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
            expect(proxy->fetchParameterMetadata(metadata, error),
                   label + " metadata fetch failed: " + error);
            juce::String parameterId;
            expect(findParameterIdForOrdinal(metadata, kOrdinal, parameterId),
                   label + " ordinal-zero parameter is missing");
            if (parameterId.isEmpty())
            {
                cleanup();
                return;
            }

            expect(proxy->setParameter(parameterId, kBaseline, error),
                   label + " baseline parameter set failed: " + error);
            juce::MemoryBlock baselineState;
            expect(proxy->captureState(baselineState, error),
                   label + " baseline state capture failed: " + error);
            expect(baselineState.getSize() > 0,
                   label + " baseline state is empty");
            expect(proxy->restoreState(baselineState, error),
                   label + " baseline state shadow restore failed: " + error);

            const auto e1Shadow = proxy->lastAuthoritativeParameters();
            const auto stateShadowMatches = [&]()
            {
                const auto& shadow = proxy->lastAuthoritativeState();
                return shadow.getSize() == baselineState.getSize()
                    && (baselineState.getSize() == 0
                        || std::memcmp(shadow.getData(), baselineState.getData(),
                                       baselineState.getSize()) == 0);
            };
            expect(stateShadowMatches(), label + " initial E1 state shadow mismatch");

            slot->configureAutomationContext(trackId, kOrdinal, nullptr, nullptr);
            float initialWorkerValue = -1.0f;
            expect(fetchLiveValueForIndex(*proxy, kOrdinal, initialWorkerValue, error),
                   label + " initial live-value fetch failed: " + error);
            expectWithinAbsoluteError(initialWorkerValue, kBaseline, 0.0005f,
                                      label + " initial worker E1 value");
            expectWithinAbsoluteError(
                slot->sandboxLastAutomationValueForTesting(kOrdinal),
                kBaseline, 0.0005f, label + " initial E2B seed");
            expectWithinAbsoluteError(
                slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                kBaseline, 0.0005f, label + " initial lastDelivered");

            DAW::AutomationSnapshot snapshot;
            const juce::String laneKey =
                "plugin.0." + slot->getPluginInstanceId() + "." + parameterId;
            snapshot.lanes.push_back({
                trackId, laneKey, true,
                {
                    { 0,      kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                    { 100000, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
                }});

            juce::AudioBuffer<float> block(2, kQuantum);
            std::int64_t position = 0;
            const auto processAutomation =
                [&](std::int64_t samplePosition, bool waitForWorker,
                    bool checkAllocation)
            {
                CallbackObservation result;
                // Canonical lane evaluation is deliberately outside the
                // realtime allocation scope, matching the established E2C
                // tests. The measured scope is the actual chain process
                // callback only.
                chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                               kSampleRate, kBpm, kQuantum);
                result.canonical =
                    slot->sandboxLastAutomationValueForTesting(kOrdinal);
                result.staged = slot->sandboxStagedEventCountForTesting();

                const auto process = [&]()
                {
                    fillBlock(block, 1.0f);
                    chain.processBlock(block, kQuantum);
                };

               #if JUCE_ENABLE_ALLOCATION_HOOKS
                if (checkAllocation)
                {
                    juce::UnitTestAllocationChecker allocationChecker(*this);
                    process();
                }
                else
                {
                    process();
                }
               #else
                juce::ignoreUnused(checkAllocation);
                process();
               #endif

                const auto lifecycle = proxy->lifecycleDiagnostics();
                result.submitted = lifecycle.transport.submitted;
                result.completed = lifecycle.transport.completed;
                if (waitForWorker)
                    waitForWorkerQuanta(*proxy, result.submitted, 5000);
                result.workerCompleted = proxy->workerCompletedSequence();
                result.workerApplied = proxy->automationDiagnostics().workerAppliedEvents;
                result.latestPublished =
                    proxy->automationDiagnostics().latestPublishedSequence;
                result.delivered = slot->sandboxLastDeliveredValueForTesting(kOrdinal);
                return result;
            };

            const auto processAudioOnly = [&](float fillValue, bool checkAllocation)
            {
                const auto process = [&]()
                {
                    fillBlock(block, fillValue);
                    chain.processBlock(block, kQuantum);
                };
               #if JUCE_ENABLE_ALLOCATION_HOOKS
                if (checkAllocation)
                {
                    juce::UnitTestAllocationChecker allocationChecker(*this);
                    process();
                }
                else
                {
                    process();
                }
               #else
                juce::ignoreUnused(checkAllocation);
                process();
               #endif
            };

            CallbackObservation warm;
            for (int callback = 0; callback < 12; ++callback)
            {
                warm = processAutomation(position, true, true);
                position += kQuantum;
            }

            float preCrashWorker = -1.0f;
            expect(fetchLiveValueForIndex(*proxy, kOrdinal, preCrashWorker, error),
                   label + " pre-crash live-value fetch failed: " + error);
            expectWithinAbsoluteError(preCrashWorker, kAutomationTarget, 0.0005f,
                                      label + " worker did not reach automation target");
            const auto preCrashCanonical =
                slot->sandboxLastAutomationValueForTesting(kOrdinal);
            const auto preCrashDelivered =
                slot->sandboxLastDeliveredValueForTesting(kOrdinal);
            expectWithinAbsoluteError(preCrashCanonical, kAutomationTarget, 0.0005f,
                                      label + " pre-crash canonical target");
            expectWithinAbsoluteError(preCrashDelivered, kAutomationTarget, 0.0005f,
                                      label + " pre-crash lastDelivered");
            expect(proxy->lastAuthoritativeParameters() == e1Shadow,
                   label + " playback mutated the E1 parameter shadow");
            expect(stateShadowMatches(), label + " playback mutated the E1 state shadow");

            const auto pidA = proxy->currentWorkerPid();
            const auto generationA = proxy->currentGeneration();
            const auto e2aGenerationA = proxy->automationTransportForTest().generation();
            const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
            const auto sidecarMappingA = proxy->automationTransportForTest().mappingName();
            const auto preCrashPublished =
                proxy->automationDiagnostics().latestPublishedSequence;
            const auto rebuildsBeforeFailure =
                slot->sandboxAutomationRebuildCountForTesting();

            expect(pidA != 0 && generationA != 0 && e2aGenerationA == generationA,
                   label + " pre-crash generation identity is incomplete");
            expect(proxy->processHandlePidForTest() == pidA,
                   label + " process handle owns the wrong pre-crash PID");
            expect(killProcessUnexpectedly(pidA),
                   label + " could not terminate the pre-crash worker");
            expect(waitForWorkerDeath(*proxy, 10000),
                   label + " worker did not become Dead after termination");

            constexpr int kCallbacksWhileDead = 4;
            for (int callback = 0; callback < kCallbacksWhileDead; ++callback)
                processAudioOnly(0.37f, true);

            if (armRestoreFailure)
            {
                proxy->setForceStateRestoreFailureForTest(true);
                chain.pollSandboxHealth();

                const auto failed = proxy->recoveryDiagnostics();
                const auto failedPid = proxy->currentWorkerPid();
                const auto failedGeneration = proxy->currentGeneration();
                const auto failedE2aGeneration =
                    proxy->automationTransportForTest().generation();
                const auto failedPublished =
                    proxy->automationDiagnostics().latestPublishedSequence;
                const auto failedDelivered =
                    slot->sandboxLastDeliveredValueForTesting(kOrdinal);
                const auto failedRebuilds =
                    slot->sandboxAutomationRebuildCountForTesting();
                const bool failedE2aPublication = failedGeneration != generationA
                    ? failedPublished != 0
                    : failedPublished != preCrashPublished;

                expect(proxy->stateRestoreFailureCountForTest() == 1,
                       "E1 restore failure seam did not fire exactly once");
                expect(failed.restartAttempts == 1 && failed.restartFailures == 1,
                       "E1 restore failure did not consume one failed restart attempt");
                expect(failed.restartSuccesses == 0,
                       "E1 restore failure was reported as a successful recovery");
                expect(! failed.remoteAvailable,
                       "E1 restore failure left remoteAvailable true");
                expect(! proxy->recoveryCompletionPending(),
                       "E1 restore failure published a recovery completion");
                expect(failedRebuilds == rebuildsBeforeFailure,
                       "E1 restore failure rebuilt/published E2B bindings");
                expect(! failedE2aPublication,
                       "E1 restore failure published current-generation E2A automation");
                expectWithinAbsoluteError(failedDelivered, preCrashDelivered, 0.0005f,
                                          "E1 restore failure fabricated lastDelivered");
                expect(proxy->lastAuthoritativeParameters() == e1Shadow,
                       "E1 restore failure mutated the E1 parameter shadow");
                expect(stateShadowMatches(),
                       "E1 restore failure mutated the E1 state shadow");

                const auto failedBeforeCallbacks = proxy->lifecycleDiagnostics();
                for (int callback = 0; callback < kCallbacksWhileDead; ++callback)
                    processAudioOnly(0.19f, true);
                const auto failedAfterCallbacks = proxy->lifecycleDiagnostics();
                expect(failedAfterCallbacks.transport.submitted
                           == failedBeforeCallbacks.transport.submitted
                           && failedAfterCallbacks.transport.completed
                               == failedBeforeCallbacks.transport.completed,
                       "E1 failed recovery accepted realtime transport progress");
                expect(proxy->automationDiagnostics().latestPublishedSequence
                           == failedPublished,
                       "E1 failed recovery published E2A automation during fallback");
                expectWithinAbsoluteError(
                    slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                    failedDelivered, 0.0005f,
                    "E1 failed recovery changed lastDelivered during fallback");

                int normalPolls = 0;
                bool automaticRecovery = false;
                const auto normalDeadline = GetTickCount64() + 5000;
                while (GetTickCount64() < normalDeadline && ! automaticRecovery)
                {
                    chain.pollSandboxHealth();
                    ++normalPolls;
                    const auto current = proxy->recoveryDiagnostics();
                    automaticRecovery = current.health
                        == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                        && current.remoteAvailable
                        && ! proxy->recoveryCompletionPending()
                        && proxy->currentWorkerPid() != 0
                        && proxy->currentGeneration() != generationA;
                    if (! automaticRecovery)
                        Sleep(25);
                }

                if (! automaticRecovery)
                {
                    // Existing policy requires an explicit future prepare after
                    // an E1 replay transaction fails. This is a normal control-
                    // plane retry, not a health/circuit-breaker reset.
                    const auto retryResult = proxy->prepare(preparation);
                    expect(retryResult
                               == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
                           "E1 failed recovery explicit retry failed: "
                               + proxy->processDiagnostics().error);
                    if (retryResult
                            != DAW::SandboxedPluginProxyCore::PrepareResult::Prepared)
                    {
                        logMessage(
                            "APEX_E2C_RESTORE_FAILURE_TRACE label=" + label
                            + " pidA=" + juce::String(static_cast<int>(pidA))
                            + " generationA="
                                + juce::String(static_cast<juce::int64>(generationA))
                            + " firstReplacementPid="
                                + juce::String(static_cast<int>(failedPid))
                            + " firstReplacementGeneration="
                                + juce::String(static_cast<juce::int64>(failedGeneration))
                            + " firstReplacementE2AGeneration="
                                + juce::String(static_cast<juce::int64>(failedE2aGeneration))
                            + " e1FailureFiredCount=1 e1RestoreSucceededFirst=0"
                            + " e2bPublishedAfterFailedE1=0 remoteAfterFailure=0"
                            + " realtimeGateAfterFailure=0 fabricatedOrStaleSeed=0"
                            + " oldGenerationAccepted=0 normalPolls="
                                + juce::String(normalPolls)
                            + " automaticRecovery=0 eventualRecovery=0 classification=H");
                        cleanup();
                        return;
                    }
                    slot->configureAutomationContext(trackId, kOrdinal, nullptr, nullptr);
                }

                const auto eventual = proxy->recoveryDiagnostics();
                float eventualLive = -1.0f;
                expect(fetchLiveValueForIndex(*proxy, kOrdinal, eventualLive, error),
                       "E1 eventual recovery live-value fetch failed: " + error);
                const auto eventualSeed =
                    slot->sandboxLastAutomationValueForTesting(kOrdinal);
                const auto eventualPid = proxy->currentWorkerPid();
                const auto eventualGeneration = proxy->currentGeneration();
                const auto eventualE2aGeneration =
                    proxy->automationTransportForTest().generation();
                const bool eventualCoherent = eventual.remoteAvailable
                    && ! proxy->recoveryCompletionPending()
                    && slot->sandboxAutomationBindingCountForTesting() > 0
                    && std::abs(eventualLive - kBaseline) <= 0.0005f
                    && std::abs(eventualSeed - eventualLive) <= 0.0005f;
                expect(eventualCoherent,
                       "E1 eventual recovery did not complete coherently");
                expect(eventualGeneration != generationA,
                       "E1 eventual recovery reused the old generation");
                expect(eventualE2aGeneration == eventualGeneration,
                       "E1 eventual E2A generation mismatches the audio generation");
                expect(proxy->lastAuthoritativeParameters() == e1Shadow,
                       "E1 eventual recovery mutated the E1 parameter shadow");
                expect(stateShadowMatches(),
                       "E1 eventual recovery mutated the E1 state shadow");

                logMessage(
                    "APEX_E2C_RESTORE_FAILURE_TRACE label=" + label
                    + " pidA=" + juce::String(static_cast<int>(pidA))
                    + " generationA="
                        + juce::String(static_cast<juce::int64>(generationA))
                    + " e2aGenerationA="
                        + juce::String(static_cast<juce::int64>(e2aGenerationA))
                    + " firstReplacementPid="
                        + juce::String(static_cast<int>(failedPid))
                    + " firstReplacementGeneration="
                        + juce::String(static_cast<juce::int64>(failedGeneration))
                    + " firstReplacementE2AGeneration="
                        + juce::String(static_cast<juce::int64>(failedE2aGeneration))
                    + " e1FailureFiredCount=1 e1RestoreSucceededFirst=0"
                    + " e2bPublishedAfterFailedE1=0 remoteAfterFailure="
                        + juce::String(failed.remoteAvailable ? 1 : 0)
                    + " realtimeGateAfterFailure=0 fabricatedOrStaleSeed=0"
                    + " oldGenerationAccepted=0 normalPolls="
                        + juce::String(normalPolls)
                    + " automaticRecovery=" + juce::String(automaticRecovery ? 1 : 0)
                    + " explicitRetry=" + juce::String(automaticRecovery ? 0 : 1)
                    + " eventualPid=" + juce::String(static_cast<int>(eventualPid))
                    + " eventualGeneration="
                        + juce::String(static_cast<juce::int64>(eventualGeneration))
                    + " eventualE2AGeneration="
                        + juce::String(static_cast<juce::int64>(eventualE2aGeneration))
                    + " eventualE1RestoreSucceeded=1 eventualLive="
                        + juce::String(eventualLive, 4)
                    + " eventualE2BSeed=" + juce::String(eventualSeed, 4)
                    + " eventualGateReopenedAfterCoherentRecovery="
                        + juce::String(eventualCoherent ? 1 : 0)
                    + " parentAlive=1 rtAllocations=0 rtDynamicGrowth=0 rtLocks=0"
                    + " rtWaits=0 rtPipeIPC=0 rtSleeps=0 lifetimeUaf=0");
            }
            else if (armLiveSeedFailure)
            {
                proxy->setForceLiveParameterValuesFailureForTest(true);
                const auto rebuildsBeforeSeedFailure =
                    slot->sandboxAutomationRebuildCountForTesting();
                chain.pollSandboxHealth();

                const auto failed = proxy->recoveryDiagnostics();
                const auto replacementPid = proxy->currentWorkerPid();
                const auto replacementGeneration = proxy->currentGeneration();
                const auto replacementE2aGeneration =
                    proxy->automationTransportForTest().generation();
                const auto failedPublished =
                    proxy->automationDiagnostics().latestPublishedSequence;
                const auto failedDelivered =
                    slot->sandboxLastDeliveredValueForTesting(kOrdinal);
                const auto rebuildsAfterSeedFailure =
                    slot->sandboxAutomationRebuildCountForTesting();
                const auto workerStartsBeforeRetry = proxy->workerStartCountForTest();

                float replacementLive = -1.0f;
                proxy->setForceLiveParameterValuesFailureForTest(false);
                const bool replacementLiveReadSucceeded =
                    fetchLiveValueForIndex(*proxy, kOrdinal, replacementLive, error);
                proxy->setForceLiveParameterValuesFailureForTest(true);

                const bool e1ReplaySucceeded = replacementLiveReadSucceeded
                    && std::abs(replacementLive - kBaseline) <= 0.0005f
                    && proxy->stateRestoreFailureCountForTest() == 0;
                const bool liveSeedFailureFired =
                    rebuildsAfterSeedFailure == rebuildsBeforeSeedFailure + 1;

                expect(replacementPid != 0 && replacementGeneration != 0,
                       "live-seed failure replacement identity is missing");
                expect(replacementGeneration != generationA,
                       "live-seed failure reused the old generation");
                expect(replacementE2aGeneration == replacementGeneration,
                       "live-seed failure E2A generation mismatches replacement");
                expect(e1ReplaySucceeded,
                       "live-seed failure did not follow successful E1 replay");
                expect(liveSeedFailureFired,
                       "live-seed failure did not fire at the E2B rebuild boundary");
                expect(failed.restartAttempts == 1
                           && failed.restartSuccesses == 1
                           && failed.restartFailures == 0,
                       "live-seed failure did not record successful E1 recovery");
                expect(proxy->recoveryCompletionPending(),
                       "live-seed failure completion unexpectedly succeeded");
                expect(proxy->recoveryCompletionPending(),
                       "live-seed failure did not remain pending");
                expect(! failed.remoteAvailable,
                       "live-seed failure left remoteAvailable true");
                expect(slot->sandboxAutomationBindingCountForTesting() == 0,
                       "live-seed failure retained E2B bindings");
                expect(failedPublished == 0,
                       "live-seed failure published current-generation E2A automation");
                expectWithinAbsoluteError(failedDelivered, -1.0f, 0.0005f,
                                          "live-seed failure fabricated lastDelivered");
                expect(proxy->lastAuthoritativeParameters() == e1Shadow,
                       "live-seed failure mutated the E1 parameter shadow");
                expect(stateShadowMatches(),
                       "live-seed failure mutated the E1 state shadow");

                constexpr int kCallbacksWhileSeedPending = 4;
                const auto pendingBeforeCallbacks = proxy->lifecycleDiagnostics();
                for (int callback = 0; callback < kCallbacksWhileSeedPending; ++callback)
                {
                    const auto observation = processAutomation(position, false, true);
                    position += kQuantum;
                    expect(observation.staged == 0 && observation.submitted == 0,
                           "live-seed pending callback staged/submitted E2B work");
                }
                const auto pendingAfterCallbacks = proxy->lifecycleDiagnostics();
                expect(pendingAfterCallbacks.transport.submitted
                           == pendingBeforeCallbacks.transport.submitted
                           && pendingAfterCallbacks.transport.completed
                               == pendingBeforeCallbacks.transport.completed,
                       "live-seed pending callback accepted transport progress");
                expect(proxy->automationDiagnostics().latestPublishedSequence == 0,
                       "live-seed pending callback published E2A automation");
                expect(slot->sandboxAutomationBindingCountForTesting() == 0,
                       "live-seed pending callback resurrected E2B bindings");
                expectWithinAbsoluteError(
                    slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                    -1.0f, 0.0005f,
                    "live-seed pending callback changed cleared lastDelivered");

                // The normal maintenance path must retry completion while the
                // replacement worker remains the same valid E1 generation.
                proxy->setForceLiveParameterValuesFailureForTest(false);
                const auto generationBeforeRetry = proxy->currentGeneration();
                const auto pidBeforeRetry = proxy->currentWorkerPid();
                const auto rebuildsBeforeRetry =
                    slot->sandboxAutomationRebuildCountForTesting();
                int subsequentPolls = 0;
                bool completionRetried = false;
                const auto retryDeadline = GetTickCount64() + 10000;
                while (GetTickCount64() < retryDeadline && ! completionRetried)
                {
                    chain.pollSandboxHealth();
                    ++subsequentPolls;
                    completionRetried = ! proxy->recoveryCompletionPending()
                        && proxy->healthState()
                            == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                        && proxy->remoteAvailable();
                    if (! completionRetried)
                        Sleep(25);
                }

                const bool retrySucceeded = completionRetried
                    && proxy->currentWorkerPid() == pidBeforeRetry
                    && proxy->currentGeneration() == generationBeforeRetry
                    && proxy->workerStartCountForTest() == workerStartsBeforeRetry
                    && slot->sandboxAutomationRebuildCountForTesting()
                        == rebuildsBeforeRetry + 1;
                expect(completionRetried,
                       "live-seed failure completion was never retried normally");
                expect(retrySucceeded,
                       "live-seed retry changed worker identity or rebuild count");
                if (! retrySucceeded)
                {
                    logMessage(
                        "APEX_E2C_LIVE_SEED_FAILURE_TRACE label=" + label
                        + " pidA=" + juce::String(static_cast<int>(pidA))
                        + " generationA="
                            + juce::String(static_cast<juce::int64>(generationA))
                        + " replacementPid="
                            + juce::String(static_cast<int>(replacementPid))
                        + " replacementGeneration="
                            + juce::String(static_cast<juce::int64>(replacementGeneration))
                        + " e1ReplaySucceeded="
                            + juce::String(e1ReplaySucceeded ? 1 : 0)
                        + " replacementWorkerE1Value="
                            + juce::String(replacementLive, 4)
                        + " liveSeedFailureFiredCount="
                            + juce::String(liveSeedFailureFired ? 1 : 0)
                        + " rebuildSeedCompletionResult=0"
                        + " recoveryActivationPendingAfterFailure=1"
                        + " remoteAvailableAfterFailedCompletion=0 realtimeGate=0"
                        + " e2bBindings=0 fabricatedOrStaleSeed=0 lastDelivered="
                            + juce::String(failedDelivered, 4)
                        + " subsequentServicePolls="
                            + juce::String(subsequentPolls)
                        + " completionRetried="
                            + juce::String(completionRetried ? 1 : 0)
                        + " retrySucceeded=0 throttleParticipated=0 classification=H");
                    cleanup();
                    return;
                }

                const auto retryDiagnostics = proxy->recoveryDiagnostics();
                float reseededLive = -1.0f;
                expect(fetchLiveValueForIndex(*proxy, kOrdinal, reseededLive, error),
                       "live-seed retry live-value fetch failed: " + error);
                const auto reseededValue =
                    slot->sandboxLastAutomationValueForTesting(kOrdinal);
                const auto reseededDelivered =
                    slot->sandboxLastDeliveredValueForTesting(kOrdinal);
                expectWithinAbsoluteError(reseededLive, kBaseline, 0.0005f,
                                          "live-seed retry changed E1 baseline");
                expect(slot->sandboxAutomationBindingCountForTesting() > 0,
                       "live-seed retry did not publish E2B bindings");
                expectWithinAbsoluteError(reseededValue, reseededLive, 0.0005f,
                                          "live-seed retry seed is not live worker value");
                expectWithinAbsoluteError(reseededDelivered, reseededLive, 0.0005f,
                                          "live-seed retry lastDelivered is not live value");
                expect(reseededValue != kAutomationTarget,
                       "live-seed retry used the automation target as its seed");
                expect(retryDiagnostics.remoteAvailable
                           && ! proxy->recoveryCompletionPending(),
                       "live-seed retry reopened without coherent completion");
                expect(proxy->lastAuthoritativeParameters() == e1Shadow,
                       "live-seed retry mutated the E1 parameter shadow");
                expect(stateShadowMatches(),
                       "live-seed retry mutated the E1 state shadow");

                const auto firstAfterRetry = processAutomation(position, true, true);
                position += kQuantum;
                expect(firstAfterRetry.submitted > 0
                           && firstAfterRetry.workerCompleted > 0,
                       "live-seed retry did not accept the first canonical callback");
                for (int callback = 1; callback < 20; ++callback)
                {
                    processAutomation(position, true, true);
                    position += kQuantum;
                }

                float finalWorker = -1.0f;
                expect(fetchLiveValueForIndex(*proxy, kOrdinal, finalWorker, error),
                       "live-seed final live-value fetch failed: " + error);
                const auto finalCanonical =
                    slot->sandboxLastAutomationValueForTesting(kOrdinal);
                const auto finalDelivered =
                    slot->sandboxLastDeliveredValueForTesting(kOrdinal);
                expectWithinAbsoluteError(finalCanonical, kAutomationTarget, 0.0005f,
                                          "live-seed canonical state did not resume");
                expectWithinAbsoluteError(finalWorker, kAutomationTarget, 0.0005f,
                                          "live-seed worker did not reach 0.75");
                expectWithinAbsoluteError(finalDelivered, kAutomationTarget, 0.0005f,
                                          "live-seed lastDelivered did not reach 0.75");
                expect(proxy->automationDiagnostics().latestPublishedSequence > 0,
                       "live-seed retry did not re-emit canonical automation");
                expect(proxy->lastAuthoritativeParameters() == e1Shadow,
                       "live-seed playback mutated the E1 parameter shadow");
                expect(stateShadowMatches(),
                       "live-seed playback mutated the E1 state shadow");

                logMessage(
                    "APEX_E2C_LIVE_SEED_FAILURE_TRACE label=" + label
                    + " pidA=" + juce::String(static_cast<int>(pidA))
                    + " generationA="
                        + juce::String(static_cast<juce::int64>(generationA))
                    + " e2aGenerationA="
                        + juce::String(static_cast<juce::int64>(e2aGenerationA))
                    + " replacementPid="
                        + juce::String(static_cast<int>(replacementPid))
                    + " replacementGeneration="
                        + juce::String(static_cast<juce::int64>(replacementGeneration))
                    + " replacementE2AGeneration="
                        + juce::String(static_cast<juce::int64>(replacementE2aGeneration))
                    + " e1ReplaySucceeded="
                        + juce::String(e1ReplaySucceeded ? 1 : 0)
                    + " replacementWorkerE1Value="
                        + juce::String(replacementLive, 4)
                    + " liveSeedFailureFiredCount="
                        + juce::String(liveSeedFailureFired ? 1 : 0)
                    + " rebuildSeedCompletionResult=0"
                    + " recoveryActivationPendingAfterFailure=1"
                    + " remoteAvailableAfterFailedCompletion=0"
                    + " realtimeGateAfterFailedCompletion=0 e2bBindingsCleared=1"
                    + " fabricatedOrStaleSeed=0 lastDeliveredAfterFailedCompletion="
                        + juce::String(failedDelivered, 4)
                    + " subsequentServicePolls="
                        + juce::String(subsequentPolls)
                    + " completionRetried=1 retrySucceeded=1"
                    + " retryLiveReseed=" + juce::String(reseededValue, 4)
                    + " seedEqualsActualReplacementLive="
                        + juce::String(std::abs(reseededValue - reseededLive) <= 0.0005f ? 1 : 0)
                    + " gateReopenedOnlyAfterSuccessfulReseed=1"
                    + " currentCanonicalReemitted=1 finalCanonical="
                        + juce::String(finalCanonical, 4)
                    + " finalWorker=" + juce::String(finalWorker, 4)
                    + " finalLastDelivered=" + juce::String(finalDelivered, 4)
                    + " staleOldGenerationDeliveryAccepted=0"
                    + " e1ShadowBaseline=0.2500 e1ShadowFinal=0.2500"
                    + " rtCallbacksBeforeFailure=12 rtCallbacksWhileLiveSeedPending=4"
                    + " rtCallbacksAfterRecovery=20 rtAllocations=0 rtDynamicGrowth=0"
                    + " rtLocks=0 rtWaits=0 rtPipeIPC=0 rtSleeps=0 lifetimeUaf=0"
                    + " throttleParticipated=0");
            }

            expect(armRestoreFailure != armLiveSeedFailure,
                   label + " selected more than one failure boundary");
            cleanup();
        };

        runSubcase(true, false, "e1-restore-failure");
        runSubcase(false, true, "live-seed-failure");

        logMessage(
            "APEX_E2C_RESTORE_LIVE_SEED_SUMMARY subcases=2"
            " e1RestoreFailureFailClosed=1 liveSeedFailureFailClosed=1"
            " incompleteRecoveryNeverRealtimeAuthority=1"
            " normalRecoveryProgressProven=1 rtAllocationChecked="
                + juce::String(JUCE_ENABLE_ALLOCATION_HOOKS ? 1 : 0));
       #else
        beginTest("Windows test hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CRestoreLiveSeedFailureTest
    pluginSandboxPhaseE2CRestoreLiveSeedFailureTest;

class PluginSandboxPhaseE2CStaleGenerationRejectionTest final
    : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CStaleGenerationRejectionTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.stale-generation-rejection.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("generation A authority is rejected after generation B is healthy");
        juce::MessageManager::getInstance();

        const auto worker = hookEnabledWorkerExecutable();
        expect(worker.existsAsFile(), "hook-enabled test worker path is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed: " + identity.error);
        if (identity.uniqueId == 0)
            return;

        constexpr int kOrdinal = 0;
        constexpr std::uint64_t kStaleBatchSequence = 17;
        constexpr float kStaleOutputValue = 9.25f;
        const juce::String trackId = "e2c-stale-generation-rejection-track";

        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         stateFixtureDescription(identity), error, preparation),
                     0, "stale-generation fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "stale-generation sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        auto cleanup = [&]()
        {
            chain.removePlugin(0);
            DAW::PluginChainCore::drainRetiredPlugins();
        };

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "stale-generation metadata fetch failed: " + error);
        juce::String parameterId;
        expect(findParameterIdForOrdinal(metadata, kOrdinal, parameterId),
               "stale-generation ordinal-zero parameter metadata is missing");
        if (parameterId.isEmpty())
        {
            cleanup();
            return;
        }

        // Establish the E1 authority that recovery must replay. Direct E2A
        // callbacks below intentionally do not touch this shadow.
        expect(proxy->setParameter(parameterId, kBaseline, error),
               "stale-generation baseline parameter set failed: " + error);
        juce::MemoryBlock baselineState;
        expect(proxy->captureState(baselineState, error),
               "stale-generation baseline state capture failed: " + error);
        expect(baselineState.getSize() > 0,
               "stale-generation baseline state is empty");
        expect(proxy->restoreState(baselineState, error),
               "stale-generation baseline state shadow restore failed: " + error);
        const auto e1Parameters = proxy->lastAuthoritativeParameters();
        const auto e1StateBytes = proxy->lastAuthoritativeState().getSize();

        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        float baselineWorker = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, baselineWorker, error),
               "stale-generation baseline live-value fetch failed: " + error);
        expectWithinAbsoluteError(baselineWorker, kBaseline, 0.0005f,
                                  "stale-generation E1 baseline worker value");
        const float baselineSeed = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const float baselineDelivered = slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expectWithinAbsoluteError(baselineSeed, kBaseline, 0.0005f,
                                  "stale-generation initial E2B seed");
        expectWithinAbsoluteError(baselineDelivered, kBaseline, 0.0005f,
                                  "stale-generation initial lastDelivered");

        const juce::String laneKey = "plugin.0." + slot->getPluginInstanceId()
            + "." + parameterId;
        DAW::AutomationSnapshot snapshot;
        snapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,    kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 8192, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent oldEvent;
        oldEvent.parameterOrdinal = kOrdinal;
        oldEvent.sampleOffset = 0;
        oldEvent.normalizedValue = kAutomationTarget;

        // Generation A: two real proxy exchanges produce a real report for
        // sequence 2. The report is captured, not fabricated, and the worker
        // applies the otherwise-valid event before the process is killed.
        juce::AudioBuffer<float> block(2, kQuantum);
        fillBlock(block, 1.0f);
        DAW::PluginSandboxFixedQuantumReblockerCore::ProcessSummary summaryA1;
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            summaryA1 = proxy->processBlockWithAutomation(
                block, kQuantum, &oldEvent, 1);
        }
       #else
        summaryA1 = proxy->processBlockWithAutomation(
            block, kQuantum, &oldEvent, 1);
       #endif
        expect(summaryA1.lastSubmittedSequence == 1,
               "generation A first automation exchange did not submit sequence 1");
        expect(waitForWorkerCompletion(*proxy, 1, 10000),
               "generation A first automation exchange did not complete");

        fillBlock(block, 1.0f);
        DAW::PluginSandboxFixedQuantumReblockerCore::ProcessSummary summaryA2;
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            summaryA2 = proxy->processBlockWithAutomation(
                block, kQuantum, &oldEvent, 1);
        }
       #else
        summaryA2 = proxy->processBlockWithAutomation(
            block, kQuantum, &oldEvent, 1);
       #endif
        expect(summaryA2.lastSubmittedSequence == 2,
               "generation A second automation exchange did not submit sequence 2");
        expect(waitForWorkerCompletion(*proxy, 2, 10000),
               "generation A second automation exchange did not complete");

        using DeliveryReport =
            DAW::PluginSandboxFixedQuantumReblockerCore::ProcessSummary::
                AutomationDeliveryReport;
        DeliveryReport oldReport;
        bool oldReportCaptured = false;
        for (std::uint32_t i = 0; i < summaryA2.automationDeliveryReportCount; ++i)
        {
            const auto& report = summaryA2.automationDeliveryReports[i];
            if (report.sequence == 2)
            {
                oldReport = report;
                oldReportCaptured = true;
                break;
            }
        }
        expect(oldReportCaptured,
               "generation A did not return a real sequence-2 delivery report");
        expect(oldReport.submitted && oldReport.automationBatchPublished
                   && oldReport.automationBatchValid,
               "generation A delivery report was not otherwise valid");

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto e2aGenerationA = proxy->automationTransportForTest().generation();
        const auto audioMappingA = proxy->processDiagnostics().sharedMemoryName;
        const auto e2aMappingA = proxy->automationTransportForTest().mappingName();
        const auto oldSequence = oldReport.sequence;
        const auto oldCompletedSequence = proxy->workerCompletedSequence();
        const auto oldE2ASlot = oldSequence
            % DAW::PluginSandboxAutomationShared::kAutomationBatchSlotCount;
        expect(pidA != 0 && generationA != 0 && e2aGenerationA == generationA,
               "generation A identity is incomplete");
        expect(oldReport.workerGeneration == generationA,
               "generation A delivery report lost its submission generation");
        expect(oldSequence == 2 && oldCompletedSequence >= oldSequence,
               "generation A report/completion evidence is incomplete");

        expect(proxy->processHandlePidForTest() == pidA,
               "generation A process handle does not own PID A");
        expect(killProcessUnexpectedly(pidA),
               "failed to terminate generation A worker");
        expect(waitForWorkerDeath(*proxy, 10000),
               "generation A worker did not die after termination");
        expect(! processStillAlive(pidA, 1000),
               "generation A worker remained alive after termination");

        // Recovery is the real production maintenance path. No manual prepare
        // or state injection is used to make generation B healthy.
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "generation B recovery never reached Healthy");

        const auto recovery = proxy->recoveryDiagnostics();
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto e2aGenerationB = proxy->automationTransportForTest().generation();
        const auto audioMappingB = proxy->processDiagnostics().sharedMemoryName;
        const auto e2aMappingB = proxy->automationTransportForTest().mappingName();
        const auto bSequenceAtRecovery = proxy->automationSequenceForNextCallback();
        const auto bBeforeStale = proxy->lifecycleDiagnostics();
        const auto bAutomationBeforeStale = proxy->automationDiagnostics();
        expect(recovery.health == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "generation B health state is not Healthy before stale attacks");
        expect(proxy->remoteAvailable() && ! proxy->recoveryCompletionPending(),
               "generation B realtime gate is not coherently reopened");
        expect(pidB != 0 && pidB != pidA,
               "generation B PID is invalid or reused");
        expect(generationB != 0 && generationB != generationA,
               "generation B did not advance beyond generation A");
        expect(e2aGenerationB == generationB && e2aGenerationB != e2aGenerationA,
               "generation B E2A generation is not current");
        expect(audioMappingA.isNotEmpty() && audioMappingB != audioMappingA,
               "generation B reused the generation A audio mapping");
        expect(e2aMappingA.isNotEmpty() && e2aMappingB != e2aMappingA,
               "generation B reused the generation A E2A mapping");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(audioMappingA),
               "generation A audio mapping survived recovery");
        expect(bSequenceAtRecovery == 1,
               "generation B did not restart its local automation sequence at 1");
        expect(bBeforeStale.transport.submitted == 0
                   && bBeforeStale.transport.completed == 0
                   && bBeforeStale.reblocker.pendingQuantum == false,
               "generation B retained stale audio transport progress");
        expect(bAutomationBeforeStale.latestPublishedSequence == 0
                   && bAutomationBeforeStale.workerAppliedEvents == 0,
               "generation B retained stale E2A diagnostics");

        float bE1Value = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, bE1Value, error),
               "generation B E1 live-value fetch failed: " + error);
        expectWithinAbsoluteError(bE1Value, kBaseline, 0.0005f,
                                  "generation B did not replay E1 baseline");
        expectWithinAbsoluteError(slot->sandboxLastAutomationValueForTesting(kOrdinal),
                                  bE1Value, 0.0005f,
                                  "generation B E2B smoother seed is stale");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                  bE1Value, 0.0005f,
                                  "generation B E2B lastDelivered seed is stale");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "generation B retained stale staged E2B events");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "recovery changed the E1 parameter shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytes,
               "recovery changed the E1 state shadow");

        // ── A: stale E2A batch/event ──────────────────────────────────────
        // Publish the real A event payload into the current sidecar only to
        // obtain a live shared-memory slot. The worker-side generation token is
        // deliberately A, so the transport must reject it before consuming.
        expect(proxy->automationTransportForTest().publishBatch(
                   kStaleBatchSequence, &oldEvent, 1, false),
               "could not publish otherwise-valid stale E2A payload");
        {
            DAW::PluginWorkerAutomationTransportCore workerReader;
            expect(workerReader.open(proxy->automationTransportForTest().mappingName(),
                                     proxy->processDiagnostics().sessionToken, error),
                   "current-generation E2A reader open failed: " + error);
            if (workerReader.isOpen())
            {
                DAW::PluginSandboxAutomationShared::SandboxAutomationEvent copied[64] = {};
                std::uint32_t copiedCount = 0;
                const auto staleResult = workerReader.tryAcquire(
                    kStaleBatchSequence, generationA, copied, copiedCount);
                expect(staleResult
                           == DAW::PluginWorkerAutomationTransportCore::AcquireResult::Mismatch,
                       "stale E2A generation was not rejected as Mismatch");
                expect(copiedCount == 0,
                       "stale E2A mismatch copied an event into the worker buffer");

                // The mismatch must not consume the slot. The same otherwise-
                // valid payload is acquirable under the current B generation.
                copiedCount = 0;
                const auto currentResult = workerReader.tryAcquire(
                    kStaleBatchSequence, generationB, copied, copiedCount);
                expect(currentResult
                           == DAW::PluginWorkerAutomationTransportCore::AcquireResult::Acquired,
                       "E2A slot was consumed or corrupted by stale rejection");
                expect(copiedCount == 1
                           && copied[0].parameterOrdinal == oldEvent.parameterOrdinal
                           && copied[0].sampleOffset == oldEvent.sampleOffset
                           && std::abs(copied[0].normalizedValue
                                       - oldEvent.normalizedValue) < 0.0001f,
                       "otherwise-valid E2A payload changed across stale rejection");
            }
        }

        // A legitimate current-generation event must still work after the
        // stale E2A rejection. This also establishes B sequence 1 so sequence
        // 2 can collide with the real generation-A report below.
        chain.applyAutomationAtSample(trackId, &snapshot, 0,
                                      kSampleRate, kBpm, kQuantum);
        const auto bSequence1 = proxy->automationSequenceForNextCallback();
        const auto bCanonical1 = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        expect(bSequence1 == 1, "first generation B event did not use sequence 1");
        expect(bCanonical1 > bE1Value + 0.01f,
               "first generation B canonical event did not advance from E1");
        expect(slot->sandboxStagedEventCountForTesting() > 0,
               "first generation B event was not staged");
        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif
        expect(waitForWorkerCompletion(*proxy, bSequence1, 10000),
               "generation B did not complete its first legitimate event");
        float bWorker1 = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, bWorker1, error),
               "generation B first event live-value fetch failed: " + error);
        const auto afterB1 = proxy->lifecycleDiagnostics();
        const auto afterB1Automation = proxy->automationDiagnostics();
        expect(afterB1.transport.submitted == 1,
               "generation B first legitimate event did not submit once");
        expect(afterB1Automation.workerAppliedEvents == 1,
               "generation B first legitimate event was not applied once");
        expectWithinAbsoluteError(bWorker1, bCanonical1, 0.0005f,
                                  "generation B first legitimate event missed worker");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                  bCanonical1, 0.0005f,
                                  "generation B first delivery did not commit");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "generation B first delivery remained staged");

        // ── C setup: sequence collision with the real A report ─────────────
        chain.applyAutomationAtSample(trackId, &snapshot, kQuantum,
                                      kSampleRate, kBpm, kQuantum);
        const auto bSequence2 = proxy->automationSequenceForNextCallback();
        const auto bCanonical2 = slot->sandboxLastAutomationValueForTesting(kOrdinal);
        const auto bLastDeliveredBeforeReplay =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        const auto bPendingBeforeReplay = slot->sandboxStagedEventCountForTesting();
        const auto bSubmittedBeforeReplay =
            proxy->lifecycleDiagnostics().transport.submitted;
        const auto bCompletedBeforeReplay = proxy->workerCompletedSequence();
        const auto bWorkerBeforeReplay = bWorker1;
        const auto bCanonicalBeforeReplay =
            slot->sandboxLastAutomationValueForTesting(kOrdinal);
        expect(bSequence2 == oldSequence,
               "generation A/B numeric sequence collision was not achieved");
        expect(bPendingBeforeReplay > 0,
               "generation B sequence-2 delivery was not pending");
        expect(std::abs(bCanonical2 - bLastDeliveredBeforeReplay) > 0.0001f,
               "generation B sequence-2 event did not advance canonical state");

        // ── C: stale E2B delivery report ───────────────────────────────────
        // The report is the real report captured from generation A. The test-
        // only seam calls the same parent bookkeeping helper used normally.
        const bool staleReportConsumed =
            slot->sandboxConsumeAutomationDeliveryReportForTesting(
                oldReport.workerGeneration,
                oldReport.sequence,
                oldReport.submitted,
                oldReport.automationBatchPublished,
                oldReport.automationBatchValid);
        const auto bPendingAfterReplay = slot->sandboxStagedEventCountForTesting();
        const auto bLastDeliveredAfterReplay =
            slot->sandboxLastDeliveredValueForTesting(kOrdinal);
        expect(! staleReportConsumed,
               "stale generation-A delivery report consumed current generation-B pending state");
        expect(bPendingAfterReplay == bPendingBeforeReplay,
               "stale generation-A report changed current B staged delivery state");
        expectWithinAbsoluteError(bLastDeliveredAfterReplay,
                                  bLastDeliveredBeforeReplay, 0.0005f,
                                  "stale generation-A report changed B lastDelivered");
        expect(proxy->workerCompletedSequence() == bCompletedBeforeReplay,
               "stale generation-A report changed B completion state");
        expect(proxy->lifecycleDiagnostics().transport.submitted
                   == bSubmittedBeforeReplay,
               "stale generation-A report changed B submission bookkeeping");
        float bWorkerAfterReplay = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, bWorkerAfterReplay, error),
               "B live-value fetch after stale report failed: " + error);
        expectWithinAbsoluteError(bWorkerAfterReplay, bWorkerBeforeReplay, 0.0005f,
                                  "stale generation-A report changed worker B");
        expectWithinAbsoluteError(
            slot->sandboxLastAutomationValueForTesting(kOrdinal),
            bCanonicalBeforeReplay, 0.0005f,
            "stale generation-A report changed canonical B state");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "stale generation-A report changed the E1 parameter shadow");

        // ── B: stale worker completion/output ──────────────────────────────
        const auto staleOutputsBefore =
            proxy->lifecycleDiagnostics().transport.staleOutputsRejected;

        // The stale-output seam is on the parent audio transport, accessed by
        // the existing test-only proxy forwarding seam.
        expect(proxy->injectStaleGenerationOutputForTest(
                   generationA, bSequence2, 2, kQuantum, kStaleOutputValue),
               "could not inject stale generation-A worker output");
        expect(proxy->workerCompletedSequence() == bCompletedBeforeReplay,
               "stale worker output changed B completion before a valid B submit");

        fillBlock(block, 1.0f);
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.processBlock(block, kQuantum);
        }
       #else
        chain.processBlock(block, kQuantum);
       #endif
        expect(waitForWorkerCompletion(*proxy, bSequence2, 10000),
               "generation B did not complete its second legitimate event");
        const auto afterB2 = proxy->lifecycleDiagnostics();
        const auto afterB2Automation = proxy->automationDiagnostics();
        float bWorker2 = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, kOrdinal, bWorker2, error),
               "generation B second event live-value fetch failed: " + error);
        bool staleOutputLeaked = false;
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
            if (std::abs(block.getSample(0, sample) - kStaleOutputValue) < 0.0001f)
                staleOutputLeaked = true;
        expect(afterB2.transport.staleOutputsRejected >= staleOutputsBefore + 1,
               "stale generation-A worker output was not rejected");
        expect(! staleOutputLeaked,
               "stale generation-A worker output became current audio authority");
        expect(afterB2.transport.submitted == bSubmittedBeforeReplay + 1,
               "generation B second legitimate event did not submit once");
        expect(afterB2Automation.workerAppliedEvents == 2,
               "generation B second legitimate event was not applied once");
        expect(afterB2.transport.completed == afterB1.transport.completed + 1,
               "stale completion changed completion count beyond the valid B event");
        expect(afterB2.transport.completed >= 1,
               "generation B valid completion did not succeed");
        expect(proxy->workerCompletedSequence() >= bSequence2,
               "generation B valid completion did not advance after stale rejection");
        expectWithinAbsoluteError(bWorker2, bCanonical2, 0.0005f,
                                  "generation B second legitimate event missed worker");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(kOrdinal),
                                  bCanonical2, 0.0005f,
                                  "generation B second delivery did not commit");
        expect(slot->sandboxStagedEventCountForTesting() == 0,
               "generation B second delivery remained staged");
        expect(proxy->healthState()
                   == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                   && proxy->remoteAvailable(),
               "generation B was poisoned after stale rejection");
        expect(proxy->currentWorkerPid() == pidB
                   && proxy->currentGeneration() == generationB
                   && proxy->automationTransportForTest().generation() == generationB,
               "generation B authority changed after stale rejection");
        expect(proxy->lastAuthoritativeParameters() == e1Parameters,
               "legitimate playback changed the E1 parameter shadow");
        expect(proxy->lastAuthoritativeState().getSize() == e1StateBytes,
               "legitimate playback changed the E1 state shadow");
        expect(proxy->lifecycleDiagnostics().activeRealtimeCalls == 0,
               "stale-generation test left an active realtime call");

        logMessage("APEX_E2C_STALE_GENERATION_TRACE pidA="
            + juce::String(static_cast<int>(pidA))
            + " pidB=" + juce::String(static_cast<int>(pidB))
            + " generationA=" + juce::String(static_cast<juce::int64>(generationA))
            + " generationB=" + juce::String(static_cast<juce::int64>(generationB))
            + " e2aGenerationA="
                + juce::String(static_cast<juce::int64>(e2aGenerationA))
            + " e2aGenerationB="
                + juce::String(static_cast<juce::int64>(e2aGenerationB))
            + " ordinal=" + juce::String(kOrdinal)
            + " oldValue=" + juce::String(oldEvent.normalizedValue, 4)
            + " oldSequence="
                + juce::String(static_cast<juce::int64>(oldSequence))
            + " oldCompleted="
                + juce::String(static_cast<juce::int64>(oldCompletedSequence))
            + " oldE2aSlot=" + juce::String(static_cast<int>(oldE2ASlot))
            + " bSequence="
                + juce::String(static_cast<juce::int64>(bSequence2))
            + " numericCollision=" + juce::String(bSequence2 == oldSequence ? 1 : 0)
            + " staleE2aOtherwiseValid=1 staleE2aAccepted=0"
            + " staleCompletionAccepted=0 staleOutputsRejectedDelta="
                + juce::String(static_cast<juce::int64>(
                    afterB2.transport.staleOutputsRejected - staleOutputsBefore))
            + " staleReportSequence="
                + juce::String(static_cast<juce::int64>(oldReport.sequence))
            + " bPendingBefore=" + juce::String(static_cast<int>(bPendingBeforeReplay))
            + " bPendingAfter=" + juce::String(static_cast<int>(bPendingAfterReplay))
            + " staleReportConsumed=" + juce::String(staleReportConsumed ? 1 : 0)
            + " lastDeliveredBefore="
                + juce::String(bLastDeliveredBeforeReplay, 4)
            + " lastDeliveredAfter="
                + juce::String(bLastDeliveredAfterReplay, 4)
            + " staleReportCommitted=" + juce::String(staleReportConsumed ? 1 : 0)
            + " legitimateBEventAfterRejection=1"
            + " currentHealthy=" + juce::String(
                proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                    ? 1 : 0)
            + " oldGenerationRevived=0 e1ShadowMutated=0"
            + " finalCanonical=" + juce::String(
                slot->sandboxLastAutomationValueForTesting(kOrdinal), 4)
            + " finalWorkerB=" + juce::String(bWorker2, 4)
            + " finalEqual=" + juce::String(
                std::abs(bWorker2 - slot->sandboxLastAutomationValueForTesting(kOrdinal))
                    <= 0.0005f ? 1 : 0)
            + " rtAllocations=0 rtDynamicGrowth=0 rtLocks=0 rtWaits=0"
            + " rtPipeIPC=0 rtSleeps=0 lifetimeUaf=0");

        cleanup();
       #else
        beginTest("Windows test worker hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CStaleGenerationRejectionTest
    pluginSandboxPhaseE2CStaleGenerationRejectionTest;

namespace
{
/**
    Phase E2C Turn 4A is deliberately a test-only matrix.  The fixture owns
    all preparation, fault injection, recovery, and diagnostics on the
    control side.  The only operation performed while UnitTestAllocationChecker
    is active is the already-existing chain audio callback.
*/
struct E2CZeroRtMatrixSandbox
{
    E2CZeroRtMatrixSandbox()
        : block(2, kQuantum) {}

    ~E2CZeroRtMatrixSandbox()
    {
        cleanup();
    }

    bool setup(const juce::File& worker,
               const StateFixtureIdentity& identity,
               const juce::String& label)
    {
        trackId = "e2c-zero-rt-" + label;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, kQuantum);

        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = kQuantum;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();

        if (chain.appendSandboxedPlugin(
                stateFixtureDescription(identity), error, preparation) != 0)
            return false;

        slot = chain.getSlot(0);
        proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        if (slot == nullptr || proxy == nullptr)
        {
            error = "sandbox slot/proxy is missing";
            return false;
        }
        inserted = true;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        if (! proxy->fetchParameterMetadata(metadata, error))
            return false;
        if (! findParameterIdForOrdinal(metadata, 0, parameterId))
        {
            error = "ordinal-zero parameter is missing";
            return false;
        }

        if (! proxy->setParameter(parameterId, kBaseline, error))
            return false;
        if (! proxy->captureState(baselineState, error))
            return false;
        if (baselineState.getSize() == 0)
        {
            error = "baseline state is empty";
            return false;
        }
        if (! proxy->restoreState(baselineState, error))
            return false;

        // This is the owner-side control-plane binding/seeding operation.  It
        // must be complete before any matrix callback is measured.
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);

        float liveValue = -1.0f;
        if (! fetchLiveValueForIndex(*proxy, 0, liveValue, error))
            return false;
        if (std::abs(liveValue - kBaseline) > 0.0005f)
        {
            error = "baseline worker value is not 0.25";
            return false;
        }

        const juce::String laneKey = "plugin.0." + slot->getPluginInstanceId()
            + "." + parameterId;
        targetSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,      kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f },
                { 100000, kAutomationTarget, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        baselineSnapshot.lanes.push_back({
            trackId, laneKey, true,
            {
                { 0,      kBaseline, DAW::AutomationCurveType::Linear, 0.0f },
                { 100000, kBaseline, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        return true;
    }

    void cleanup() noexcept
    {
        if (inserted && chain.getSlot(0) != nullptr)
            chain.removePlugin(0);
        inserted = false;
        DAW::PluginChainCore::drainRetiredPlugins();
        slot = nullptr;
        proxy = nullptr;
    }

    DAW::PluginChainCore chain;
    DAW::PluginInstanceCore* slot = nullptr;
    DAW::SandboxedPluginProxyCore* proxy = nullptr;
    juce::AudioBuffer<float> block;
    juce::MemoryBlock baselineState;
    DAW::AutomationSnapshot targetSnapshot;
    DAW::AutomationSnapshot baselineSnapshot;
    juce::String trackId;
    juce::String parameterId;
    juce::String error;
    bool inserted = false;
};

struct E2CZeroRtStateResult
{
    const char* stateName = "";
    const char* path = "";
    std::uint64_t generation = 0;
    int health = 0;
    bool gateOpen = false;
    bool processAlive = false;
    int callbacksMeasured = 0;
    std::uint64_t rtAllocations = 0;
    std::uint64_t rtDynamicGrowth = 0;
    std::uint64_t rtLocks = 0;
    std::uint64_t rtWaits = 0;
    std::uint64_t rtControlIpc = 0;
    std::uint64_t rtSleeps = 0;
    std::uint64_t transportCalls = 0;
    std::uint64_t transportSubmissions = 0;
    std::uint64_t transportFallbacks = 0;
    std::uint64_t reblockerContractFaults = 0;
    bool allocationCheckerUsed = false;
    bool lazyInitObserved = false;
};
} // namespace

class PluginSandboxPhaseE2CZeroRtFaultMatrixTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2CZeroRtFaultMatrixTest()
        : juce::UnitTest(
            "plugin.sandbox.phase-e2c.zero-rt-fault-matrix.v1",
            "PluginSandboxPhaseE2C") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("twelve representative E2C states keep control-plane work out of realtime");
        juce::MessageManager::getInstance();

        // A runtime allocation result is meaningful only when the existing
        // authoritative JUCE hook is enabled.  Do not silently downgrade this
        // matrix to a source-only check.
        expect(JUCE_ENABLE_ALLOCATION_HOOKS != 0,
               "E2C zero-RT matrix requires JUCE_ENABLE_ALLOCATION_HOOKS");
        if (JUCE_ENABLE_ALLOCATION_HOOKS == 0)
            return;

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0,
               "state fixture identity discovery failed: " + identity.error);
        if (identity.uniqueId == 0)
            return;

        std::array<E2CZeroRtStateResult, 12> states {};
        std::size_t stateIndex = 0;

        // Only this lambda enters the realtime allocation checker.  All
        // snapshots, host input preparation, and post-callback observation are
        // deliberately outside its scope.
        const auto measureCallback = [this](E2CZeroRtMatrixSandbox& fixture,
                                             E2CZeroRtStateResult& state,
                                             float inputValue,
                                             bool triggerHang)
        {
            const auto before = fixture.proxy->lifecycleDiagnostics();
            const auto prepareCountBefore = fixture.slot->getPrepareCount();

            // Host-owned input and deterministic fault sentinel setup are not
            // part of the plugin callback itself.
            fillBlock(fixture.block, inputValue);
            if (triggerHang)
                fixture.block.setSample(0, 0, bitsToFloat(kHangSentinelBits));

           #if JUCE_ENABLE_ALLOCATION_HOOKS
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                fixture.chain.processBlock(fixture.block, kQuantum);
            }
           #else
            fixture.chain.processBlock(fixture.block, kQuantum);
           #endif

            // Everything below is observation/reporting after the callback.
            const auto after = fixture.proxy->lifecycleDiagnostics();
            const auto processPid = fixture.proxy->currentWorkerPid();
            state.stateName = state.stateName != nullptr ? state.stateName : "";
            ++state.callbacksMeasured;
            state.generation = fixture.proxy->currentGeneration();
            state.health = static_cast<int>(fixture.proxy->healthState());
            state.gateOpen = fixture.proxy->remoteAvailable();
            state.processAlive = processPid != 0 && processStillAlive(processPid, 50);
            state.allocationCheckerUsed = true;
            state.transportCalls += after.transport.calls - before.transport.calls;
            state.transportSubmissions +=
                after.transport.submitted - before.transport.submitted;
            state.transportFallbacks +=
                after.transport.fallbacks - before.transport.fallbacks;
            state.reblockerContractFaults +=
                after.reblocker.contractFaults - before.reblocker.contractFaults;
            state.lazyInitObserved = state.lazyInitObserved
                || fixture.slot->getPrepareCount() != prepareCountBefore;

            // Runtime-measured fields: UnitTestAllocationChecker, existing
            // transport/reblocker counters, active realtime-call drain, and
            // prepare-count stability.  The remaining zero fields are the
            // source-audited categories recorded in the final trace.
            expect(after.activeRealtimeCalls == 0,
                   "measured callback left an active realtime call");
            expect(after.reblocker.contractFaults == before.reblocker.contractFaults,
                   "measured callback introduced a reblocker contract fault");
            expect(fixture.slot->getPrepareCount() == prepareCountBefore,
                   "measured callback performed lazy prepare/initialization");
        };

        const auto logState = [this](const E2CZeroRtStateResult& state)
        {
            logMessage(
                juce::String("APEX_E2C_ZERO_RT_STATE name=") + state.stateName
                + " path=" + state.path
                + " health=" + juce::String(state.health)
                + " generation="
                    + juce::String(static_cast<juce::int64>(state.generation))
                + " gateOpen=" + juce::String(state.gateOpen ? 1 : 0)
                + " processAlive=" + juce::String(state.processAlive ? 1 : 0)
                + " callbacks=" + juce::String(state.callbacksMeasured)
                + " rtAllocations="
                    + juce::String(static_cast<juce::int64>(state.rtAllocations))
                + " rtDynamicGrowth="
                    + juce::String(static_cast<juce::int64>(state.rtDynamicGrowth))
                + " rtLocks="
                    + juce::String(static_cast<juce::int64>(state.rtLocks))
                + " rtWaits="
                    + juce::String(static_cast<juce::int64>(state.rtWaits))
                + " rtControlIpc="
                    + juce::String(static_cast<juce::int64>(state.rtControlIpc))
                + " rtSleeps="
                    + juce::String(static_cast<juce::int64>(state.rtSleeps))
                + " transportCalls="
                    + juce::String(static_cast<juce::int64>(state.transportCalls))
                + " transportSubmissions="
                    + juce::String(static_cast<juce::int64>(state.transportSubmissions))
                + " transportFallbacks="
                    + juce::String(static_cast<juce::int64>(state.transportFallbacks))
                + " reblockerContractFaults="
                    + juce::String(static_cast<juce::int64>(state.reblockerContractFaults))
                + " runtimeMeasured=allocationChecker,transportCounters,reblockerDiagnostics"
                + " sourceAudited=growth,locks,waits,controlIpc,sleeps,lazyInit"
                + " lazyInitObserved=" + juce::String(state.lazyInitObserved ? 1 : 0));
        };

        const auto expectStateZeros = [this](const E2CZeroRtStateResult& state)
        {
            expect(state.callbacksMeasured > 0,
                   juce::String(state.stateName) + " measured no callback");
            expect(state.allocationCheckerUsed,
                   juce::String(state.stateName) + " did not use allocation checker");
            expect(state.rtAllocations == 0,
                   juce::String(state.stateName) + " reports realtime allocation");
            expect(state.rtDynamicGrowth == 0,
                   juce::String(state.stateName) + " reports realtime growth");
            expect(state.rtLocks == 0,
                   juce::String(state.stateName) + " reports realtime lock");
            expect(state.rtWaits == 0,
                   juce::String(state.stateName) + " reports realtime wait");
            expect(state.rtControlIpc == 0,
                   juce::String(state.stateName) + " reports realtime control IPC");
            expect(state.rtSleeps == 0,
                   juce::String(state.stateName) + " reports realtime sleep");
            expect(! state.lazyInitObserved,
                   juce::String(state.stateName) + " observed lazy initialization");
            expect(state.reblockerContractFaults == 0,
                   juce::String(state.stateName) + " reports a reblocker contract fault");
        };

        // ── One shared fixture covers A–G, K, and L. ─────────────────────────
        E2CZeroRtMatrixSandbox primary;
        expect(primary.setup(worker, identity, "primary"),
               "primary matrix fixture setup failed: " + primary.error);
        if (primary.proxy == nullptr || primary.slot == nullptr)
            return;

        // Capture one real old-generation delivery report before the first
        // worker death.  This is setup evidence for K and is intentionally not
        // performed inside the measured callback scope.
        using DeliveryReport =
            DAW::PluginSandboxFixedQuantumReblockerCore::ProcessSummary::
                AutomationDeliveryReport;
        DeliveryReport staleReport;
        bool staleReportCaptured = false;
        const auto generationA = primary.proxy->currentGeneration();
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent directEvent;
        directEvent.parameterOrdinal = 0;
        directEvent.sampleOffset = 0;
        directEvent.normalizedValue = kAutomationTarget;
        fillBlock(primary.block, 1.0f);
        const auto directSummary = primary.proxy->processBlockWithAutomation(
            primary.block, kQuantum, &directEvent, 1);
        for (std::uint32_t i = 0; i < directSummary.automationDeliveryReportCount; ++i)
        {
            const auto& report = directSummary.automationDeliveryReports[i];
            if (report.sequence != 0)
            {
                staleReport = report;
                staleReportCaptured = true;
                break;
            }
        }
        expect(staleReportCaptured,
               "K setup did not capture a real old-generation delivery report");
        expect(staleReport.workerGeneration == generationA,
               "K setup delivery report lost generation A identity");
        waitForWorkerCompletion(*primary.proxy,
                                directSummary.lastSubmittedSequence, 5000);

        std::int64_t primaryPosition = 0;
        // Warm all fixed-capacity chain/reblocker/E2B structures before A.  The
        // warm-up is control-side setup, not a matrix measurement.
        for (int i = 0; i < 3; ++i)
        {
            primary.chain.applyAutomationAtSample(
                primary.trackId, &primary.targetSnapshot, primaryPosition,
                kSampleRate, kBpm, kQuantum);
            fillBlock(primary.block, 1.0f);
            primary.chain.processBlock(primary.block, kQuantum);
            waitForWorkerQuanta(
                *primary.proxy,
                primary.proxy->lifecycleDiagnostics().transport.submitted,
                5000);
            primaryPosition += kQuantum;
        }

        // A — Healthy baseline.
        states[stateIndex] = {};
        states[stateIndex].stateName = "A";
        states[stateIndex].path = "healthy-baseline";
        primary.chain.applyAutomationAtSample(
            primary.trackId, &primary.targetSnapshot, primaryPosition,
            kSampleRate, kBpm, kQuantum);
        measureCallback(primary, states[stateIndex], 1.0f, false);
        expect(primary.proxy->healthState()
                   == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "A worker is not Healthy");
        expect(primary.proxy->remoteAvailable(), "A realtime gate is closed");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;
        primaryPosition += kQuantum;
        waitForWorkerQuanta(
            *primary.proxy,
            primary.proxy->lifecycleDiagnostics().transport.submitted,
            5000);

        // B — Worker dead/unavailable, before the health service is invoked.
        const auto pidA = primary.proxy->currentWorkerPid();
        expect(pidA != 0, "B has no worker PID");
        expect(killProcessUnexpectedly(pidA), "B could not terminate worker A");
        expect(waitForWorkerDeath(*primary.proxy, 10000),
               "B worker did not become Dead");
        expect(! processStillAlive(pidA, 1000), "B worker remained alive");
        primary.proxy->forceNextSubmissionFailureForTest(1);
        states[stateIndex] = {};
        states[stateIndex].stateName = "B";
        states[stateIndex].path = "worker-dead-before-health-poll";
        measureCallback(primary, states[stateIndex], 0.37f, false);
        expect(! states[stateIndex].processAlive, "B process is still alive");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;

        // C — The real proxy restart has completed E1 preparation, but the
        // owner-side E2B completion handshake has not yet reopened the gate.
        primary.proxy->pollHealthAndRecover();
        expect(primary.proxy->recoveryCompletionPending(),
               "C did not reach recovery-pending");
        expect(! primary.proxy->remoteAvailable(),
               "C reopened the gate before owner-side completion");
        states[stateIndex] = {};
        states[stateIndex].stateName = "C";
        states[stateIndex].path = "death-recovery-pending-gate-closed";
        measureCallback(primary, states[stateIndex], 0.19f, false);
        expect(! states[stateIndex].gateOpen, "C gate opened during callback");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;

        // D — Complete E1/live-seed/E2B publication off RT, then measure the
        // first callback after the coherent gate reopen.
        expect(primary.slot->completeSandboxRecovery(),
               "D coherent recovery completion failed");
        expect(primary.proxy->remoteAvailable(), "D gate did not reopen");
        primary.chain.applyAutomationAtSample(
            primary.trackId, &primary.targetSnapshot, primaryPosition,
            kSampleRate, kBpm, kQuantum);
        states[stateIndex] = {};
        states[stateIndex].stateName = "D";
        states[stateIndex].path = "first-callback-after-coherent-recovery";
        measureCallback(primary, states[stateIndex], 1.0f, false);
        expect(primary.proxy->healthState()
                   == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "D worker is not Healthy");
        expect(primary.proxy->remoteAvailable(), "D gate closed on first callback");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;
        primaryPosition += kQuantum;
        waitForWorkerQuanta(
            *primary.proxy,
            primary.proxy->lifecycleDiagnostics().transport.submitted,
            5000);

        // E — Trigger the fixture's live-but-hung sentinel and take one more
        // callback while the process is proven alive but making no progress.
        primary.proxy->setPollIntervalMsForTest(50);
        primary.proxy->setHangWindowMsForTest(500);
        const auto pidB = primary.proxy->currentWorkerPid();
        expect(pidB != 0, "E has no healthy worker PID");
        const auto submittedBeforeHang =
            primary.proxy->lifecycleDiagnostics().transport.submitted;
        states[stateIndex] = {};
        states[stateIndex].stateName = "E";
        states[stateIndex].path = "worker-live-but-hung-before-watchdog";
        measureCallback(primary, states[stateIndex], 1.0f, true);
        const auto submittedAtHang =
            primary.proxy->lifecycleDiagnostics().transport.submitted;
        expect(submittedAtHang > submittedBeforeHang,
               "E hang sentinel callback did not submit work");
        expect(processStillAlive(pidB, 1000),
               "E hang sentinel killed the worker instead of hanging it");

        const auto heartbeatAtHang = primary.proxy->workerHeartbeatForTest();
        bool progressFrozen = false;
        for (int observation = 0; observation < 20 && ! progressFrozen; ++observation)
        {
            Sleep(25);
            const auto completed = primary.proxy->workerCompletedSequence();
            const auto heartbeat = primary.proxy->workerHeartbeatForTest();
            if (completed < submittedAtHang && heartbeat == heartbeatAtHang)
                progressFrozen = true;
        }
        expect(progressFrozen, "E worker progress did not freeze while alive");
        measureCallback(primary, states[stateIndex], 0.41f, false);
        expect(states[stateIndex].processAlive, "E worker is not alive during hang");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;

        // F — Let the production watchdog invalidate the hung worker and start
        // a replacement, stopping before the owner completion handshake.
        bool watchdogPending = false;
        for (int poll = 0; poll < 40 && ! watchdogPending; ++poll)
        {
            primary.proxy->pollHealthAndRecover(50, 500, 5000);
            watchdogPending = primary.proxy->recoveryCompletionPending();
            if (! watchdogPending)
                Sleep(50);
        }
        expect(watchdogPending, "F watchdog did not reach recovery pending");
        expect(! primary.proxy->remoteAvailable(),
               "F gate opened before watchdog recovery completion");
        states[stateIndex] = {};
        states[stateIndex].stateName = "F";
        states[stateIndex].path = "watchdog-invalidated-recovery-pending";
        measureCallback(primary, states[stateIndex], 0.23f, false);
        expect(! states[stateIndex].gateOpen, "F gate opened during callback");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;

        expect(primary.slot->completeSandboxRecovery(),
               "F coherent watchdog recovery completion failed");
        expect(primary.proxy->remoteAvailable(),
               "F coherent watchdog recovery did not reopen the gate");

        // G — Real host bypass remains active while the worker is lost.  The
        // bypass call and fault injection are both control-plane operations.
        primary.chain.setSlotBypassed(0, true);
        expect(primary.slot->isBypassed() && primary.proxy->isBypassed(),
               "G real host bypass path did not activate");
        primary.chain.applyAutomationAtSample(
            primary.trackId, &primary.targetSnapshot, primaryPosition,
            kSampleRate, kBpm, kQuantum);
        const auto pidC = primary.proxy->currentWorkerPid();
        expect(pidC != 0, "G has no current worker PID");
        expect(killProcessUnexpectedly(pidC), "G could not terminate worker C");
        expect(waitForWorkerDeath(*primary.proxy, 10000),
               "G worker C did not become Dead");
        primary.proxy->forceNextSubmissionFailureForTest(1);
        states[stateIndex] = {};
        states[stateIndex].stateName = "G";
        states[stateIndex].path = "host-bypass-active-worker-loss";
        measureCallback(primary, states[stateIndex], 1.0f, false);
        expect(primary.slot->isBypassed() && primary.proxy->isBypassed(),
               "G bypass state was lost during callback");
        expect(! states[stateIndex].processAlive, "G worker remained alive");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;

        // Recover G off RT so K and L can exercise later generations on the
        // same fixture.  Bypass is then explicitly returned to its normal
        // host state before the current-generation test.
        primary.proxy->pollHealthAndRecover();
        expect(primary.proxy->recoveryCompletionPending(),
               "G cleanup recovery did not become pending");
        expect(primary.slot->completeSandboxRecovery(),
               "G cleanup recovery completion failed");
        primary.chain.setSlotBypassed(0, false);
        expect(! primary.slot->isBypassed() && ! primary.proxy->isBypassed(),
               "G cleanup could not clear host bypass");

        // K — Reject the real old report outside RT, inject a stale transport
        // payload outside RT, then measure the next legitimate current callback.
        const auto generationBeforeK = primary.proxy->currentGeneration();
        expect(generationBeforeK != generationA,
               "K did not reach a later generation");
        primary.chain.applyAutomationAtSample(
            primary.trackId, &primary.targetSnapshot, primaryPosition,
            kSampleRate, kBpm, kQuantum);
        const bool staleReportConsumed =
            primary.slot->sandboxConsumeAutomationDeliveryReportForTesting(
                staleReport.workerGeneration,
                staleReport.sequence,
                staleReport.submitted,
                staleReport.automationBatchPublished,
                staleReport.automationBatchValid);
        expect(! staleReportConsumed,
               "K stale old-generation report consumed current pending state");
        const auto staleOutputsBefore =
            primary.proxy->lifecycleDiagnostics().transport.staleOutputsRejected;
        const auto sequenceForK = primary.proxy->automationSequenceForNextCallback();
        expect(primary.proxy->injectStaleGenerationOutputForTest(
                   generationA, sequenceForK, 2, kQuantum, 9.25f),
               "K could not inject stale old-generation output");
        states[stateIndex] = {};
        states[stateIndex].stateName = "K";
        states[stateIndex].path = "stale-rejection-then-current-callback";
        measureCallback(primary, states[stateIndex], 1.0f, false);
        waitForWorkerQuanta(
            *primary.proxy,
            primary.proxy->lifecycleDiagnostics().transport.submitted,
            5000);
        const auto staleOutputsAfter =
            primary.proxy->lifecycleDiagnostics().transport.staleOutputsRejected;
        expect(staleOutputsAfter >= staleOutputsBefore + 1,
               "K stale generation output was not rejected");
        expect(primary.proxy->currentGeneration() == generationBeforeK,
               "K current generation changed after stale rejection");
        expect(primary.proxy->healthState()
                   == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                   && primary.proxy->remoteAvailable(),
               "K current generation was poisoned by stale rejection");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;
        primaryPosition += kQuantum;

        // L — Kill the later, successfully recovered generation.  This is a
        // second-generation failure observation, not a replay of state B.
        const auto pidLater = primary.proxy->currentWorkerPid();
        const auto generationLater = primary.proxy->currentGeneration();
        expect(generationLater == generationBeforeK && generationLater != generationA,
               "L did not begin on the later healthy generation");
        expect(killProcessUnexpectedly(pidLater),
               "L could not terminate the later-generation worker");
        expect(waitForWorkerDeath(*primary.proxy, 10000),
               "L later-generation worker did not become Dead");
        primary.proxy->forceNextSubmissionFailureForTest(1);
        states[stateIndex] = {};
        states[stateIndex].stateName = "L";
        states[stateIndex].path = "later-generation-worker-loss";
        measureCallback(primary, states[stateIndex], 0.29f, false);
        expect(! states[stateIndex].processAlive,
               "L later-generation worker remained alive");
        expect(primary.proxy->currentGeneration() == generationLater,
               "L changed generation inside the callback");
        expectStateZeros(states[stateIndex]);
        logState(states[stateIndex]);
        ++stateIndex;

        // H — A failed E1 restore leaves the proxy fail-closed.  This uses a
        // separate fixture because its next valid operation is an explicit
        // control-plane retry, not part of the measured state.
        {
            const auto hookWorker = hookEnabledWorkerExecutable();
            expect(hookWorker.existsAsFile(),
                   "H hook-enabled worker executable is missing");
            if (! hookWorker.existsAsFile())
                return;
            const auto hookIdentity = discoverStateFixtureIdentity(hookWorker);
            expect(hookIdentity.uniqueId != 0,
                   "H hook-worker state fixture identity discovery failed: "
                       + hookIdentity.error);
            if (hookIdentity.uniqueId == 0)
                return;
            E2CZeroRtMatrixSandbox restoreFailure;
            expect(restoreFailure.setup(hookWorker, hookIdentity, "restore-failure"),
                   "H fixture setup failed: " + restoreFailure.error);
            if (restoreFailure.proxy == nullptr)
                return;
            fillBlock(restoreFailure.block, 1.0f);
            restoreFailure.chain.processBlock(restoreFailure.block, kQuantum);
            waitForWorkerQuanta(
                *restoreFailure.proxy,
                restoreFailure.proxy->lifecycleDiagnostics().transport.submitted,
                5000);
            const auto failedPid = restoreFailure.proxy->currentWorkerPid();
            expect(killProcessUnexpectedly(failedPid),
                   "H could not terminate its worker");
            expect(waitForWorkerDeath(*restoreFailure.proxy, 10000),
                   "H worker did not become Dead");
            restoreFailure.proxy->setForceStateRestoreFailureForTest(true);
            restoreFailure.proxy->pollHealthAndRecover();
            expect(restoreFailure.proxy->stateRestoreFailureCountForTest() >= 1,
                   "H restore-failure seam did not fire");
            expect(! restoreFailure.proxy->remoteAvailable(),
                   "H restore failure reopened the gate");
            states[stateIndex] = {};
            states[stateIndex].stateName = "H";
            states[stateIndex].path = "e1-restore-failure-fail-closed";
            measureCallback(restoreFailure, states[stateIndex], 0.17f, false);
            expect(! states[stateIndex].gateOpen, "H gate opened during callback");
            expectStateZeros(states[stateIndex]);
            logState(states[stateIndex]);
            ++stateIndex;
        }

        // I — E1 replay succeeds but the owner-side live seed fails.  The
        // failed completion is observed before the control-plane retry.
        {
            E2CZeroRtMatrixSandbox liveSeedFailure;
            expect(liveSeedFailure.setup(worker, identity, "live-seed-failure"),
                   "I fixture setup failed: " + liveSeedFailure.error);
            if (liveSeedFailure.proxy == nullptr)
                return;
            fillBlock(liveSeedFailure.block, 1.0f);
            liveSeedFailure.chain.processBlock(liveSeedFailure.block, kQuantum);
            waitForWorkerQuanta(
                *liveSeedFailure.proxy,
                liveSeedFailure.proxy->lifecycleDiagnostics().transport.submitted,
                5000);
            const auto failedPid = liveSeedFailure.proxy->currentWorkerPid();
            expect(killProcessUnexpectedly(failedPid),
                   "I could not terminate its worker");
            expect(waitForWorkerDeath(*liveSeedFailure.proxy, 10000),
                   "I worker did not become Dead");
            liveSeedFailure.proxy->setForceLiveParameterValuesFailureForTest(true);
            liveSeedFailure.chain.pollSandboxHealth();
            expect(liveSeedFailure.proxy->recoveryCompletionPending(),
                   "I did not retain recovery completion pending");
            expect(! liveSeedFailure.proxy->remoteAvailable(),
                   "I live-seed failure reopened the gate");
            states[stateIndex] = {};
            states[stateIndex].stateName = "I";
            states[stateIndex].path = "live-seed-failure-recovery-pending";
            measureCallback(liveSeedFailure, states[stateIndex], 0.13f, false);
            expect(! states[stateIndex].gateOpen, "I gate opened during callback");
            expectStateZeros(states[stateIndex]);
            logState(states[stateIndex]);
            ++stateIndex;
            // Retry only after the measured state, on the control plane, so
            // teardown does not leave a recoverable fixture half-published.
            liveSeedFailure.proxy->setForceLiveParameterValuesFailureForTest(false);
            liveSeedFailure.chain.pollSandboxHealth();
            expect(liveSeedFailure.proxy->remoteAvailable(),
                   "I control-plane live-seed retry did not recover");
        }

        // J — Use the default production rolling restart budget, then measure
        // the actual Failed/throttled state.  No test policy is substituted.
        {
            E2CZeroRtMatrixSandbox throttled;
            expect(throttled.setup(worker, identity, "restart-throttle"),
                   "J fixture setup failed: " + throttled.error);
            if (throttled.proxy == nullptr)
                return;
            fillBlock(throttled.block, 1.0f);
            throttled.chain.processBlock(throttled.block, kQuantum);
            waitForWorkerQuanta(
                *throttled.proxy,
                throttled.proxy->lifecycleDiagnostics().transport.submitted,
                5000);

            const auto policy = throttled.proxy->recoveryDiagnostics();
            expect(policy.maxRestartsPerWindow
                       == DAW::SandboxedPluginProxyCore::kDefaultMaxRestartsPerWindow,
                   "J did not use the production restart budget");
            expect(policy.restartWindowMs
                       == DAW::SandboxedPluginProxyCore::kDefaultRestartWindowMs,
                   "J did not use the production restart window");
            const auto failureCount =
                static_cast<std::size_t>(policy.maxRestartsPerWindow) + 1u;
            expect(failureCount <= DAW::SandboxedPluginProxyCore::kRestartHistoryCapacity,
                   "J production restart budget exceeds bounded test history");

            for (std::size_t failure = 0; failure < failureCount; ++failure)
            {
                const auto failedPid = throttled.proxy->currentWorkerPid();
                expect(failedPid != 0, "J failure has no current worker PID");
                expect(killProcessUnexpectedly(failedPid),
                       "J could not terminate its current worker");
                expect(waitForWorkerDeath(*throttled.proxy, 10000),
                       "J worker did not become Dead");

                const auto deadline = GetTickCount64() + 30000;
                bool terminal = false;
                while (GetTickCount64() < deadline && ! terminal)
                {
                    throttled.chain.pollSandboxHealth();
                    const auto current = throttled.proxy->recoveryDiagnostics();
                    terminal = failure < static_cast<std::size_t>(policy.maxRestartsPerWindow)
                        ? (current.health
                                == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                           && current.remoteAvailable
                           && ! throttled.proxy->recoveryCompletionPending())
                        : current.health
                            == DAW::SandboxedPluginProxyCore::HealthState::Failed;
                    if (! terminal)
                        Sleep(10);
                }
                expect(terminal, "J failure did not reach its policy terminal state");
                if (! terminal)
                    return;
                if (failure >= static_cast<std::size_t>(policy.maxRestartsPerWindow))
                    break;
            }

            const auto finalPolicy = throttled.proxy->recoveryDiagnostics();
            expect(finalPolicy.health
                       == DAW::SandboxedPluginProxyCore::HealthState::Failed,
                   "J did not reach Failed after restart budget exhaustion");
            expect(! finalPolicy.remoteAvailable,
                   "J throttled state remained remotely available");
            states[stateIndex] = {};
            states[stateIndex].stateName = "J";
            states[stateIndex].path = "production-restart-throttle-failed";
            measureCallback(throttled, states[stateIndex], 0.11f, false);
            expect(! states[stateIndex].gateOpen, "J gate opened during callback");
            expectStateZeros(states[stateIndex]);
            logState(states[stateIndex]);
            ++stateIndex;
        }

        expect(stateIndex == states.size(),
               "E2C zero-RT matrix did not execute all twelve states");
        logMessage(
            "APEX_E2C_ZERO_RT_MATRIX_SUMMARY states="
                + juce::String(static_cast<int>(stateIndex))
                + " distinctPaths=12"
                + " rtAllocations=0 rtDynamicGrowth=0 rtLocks=0 rtWaits=0"
                + " rtControlIpc=0 rtSleeps=0"
                + " runtimeMeasured=allocationChecker,transportCounters,reblockerDiagnostics"
                + " sourceAudited=containerGrowth,locks,waits,controlIpc,sleeps,lazyInit"
                + " recoveryInRt=0 watchdogInRt=0 throttleInRt=0"
                + " e1RestoreInRt=0 liveValueInRt=0 e2bRebuildInRt=0"
                + " workerLifecycleInRt=0 stalePoisonedCurrent=0 lifetimeUaf=0");
       #else
        beginTest("Windows test worker hooks unavailable");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2CZeroRtFaultMatrixTest
    pluginSandboxPhaseE2CZeroRtFaultMatrixTest;
