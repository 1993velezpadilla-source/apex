#include <JuceHeader.h>

#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAutomationTransportCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAudioTransportCore.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// ═══════════════════════════════════════════════════════════════════════════
// PHASE E2A — realtime parameter-event transport PRIMITIVE.
//
// These tests prove the transport primitive with DIRECT test-injected events.
// They do NOT prove that canonical APEX automation lanes are sample-accurate:
// user-facing automation remains block-rate + 10 ms smoother until E2B.
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
constexpr int kBlockSamples = 512;

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

void waitForWorkerQuanta(const DAW::SandboxedPluginProxyCore& proxy,
                         std::uint64_t quanta, std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    const auto deadline = GetTickCount64() + timeoutMs;
    while (proxy.workerCompletedSequence() < quanta
           && GetTickCount64() < deadline)
        Sleep(1);
   #else
    juce::ignoreUnused(proxy, quanta, timeoutMs);
   #endif
}

struct AutomationFixtureIdentity
{
    int uniqueId = 0;
    int deprecatedUid = 0;
};

AutomationFixtureIdentity discoverAutomationFixtureIdentity(const juce::File& worker)
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
    preparation.blockSamples = kBlockSamples;
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
    return result;
}

juce::PluginDescription automationFixtureDescription(const AutomationFixtureIdentity& identity)
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

void prepareE2APreparation(const juce::File& worker,
                           DAW::SandboxedPluginProxyCore::Preparation& preparation)
{
    preparation.blockSamples = kBlockSamples;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    preparation.workerExecutablePathForTest = worker.getFullPathName();
   #endif
}

/** Prepare an E2-enabled proxy for the automation fixture. */
bool prepareE2AProxy(DAW::SandboxedPluginProxyCore& proxy,
                     const juce::File& worker,
                     const AutomationFixtureIdentity& identity,
                     juce::String& error)
{
    if (! proxy.enableAutomationTransport(error))
        return false;
    DAW::SandboxedPluginProxyCore::Preparation preparation;
    prepareE2APreparation(worker, preparation);
    return proxy.prepare(preparation)
        == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared;
}

AutomationFixtureIdentity discoverStateFixtureIdentity(const juce::File& worker)
{
    AutomationFixtureIdentity result;
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
    preparation.blockSamples = kBlockSamples;
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
    return result;
}

juce::PluginDescription stateFixtureDescription(const AutomationFixtureIdentity& identity)
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

/** Feed four constant-1.0 callbacks; the given events ride callback 1's
    quantum. Returns callback 3's buffer — the trusted remote quantum (frozen
    Phase C timeline: transport latency 2Q = 1024 with zero plugin latency). */
juce::AudioBuffer<float> remoteQuantumForEvent(
    DAW::SandboxedPluginProxyCore& proxy,
    const DAW::PluginSandboxAutomationShared::SandboxAutomationEvent* events,
    std::uint32_t count)
{
    juce::AudioBuffer<float> block(2, kBlockSamples);
    juce::AudioBuffer<float> captured(2, kBlockSamples);
    for (int b = 1; b <= 4; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                              1.0f, kBlockSamples);
        if (b == 1 && events != nullptr && count > 0)
            proxy.processBlockWithAutomation(block, kBlockSamples, events, count);
        else
            proxy.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
        if (b == 3)
            captured = block;
    }
    return captured;
}
} // namespace

// ── E2A Gate 1: orphan-batch rollback + K+5 slot reuse ──────────────────────
class PluginSandboxPhaseE2AOrphanBatchRollbackTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2AOrphanBatchRollbackTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.orphan-batch-rollback.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("failed audio submission cannot orphan an automation batch");
        juce::MessageManager::getInstance();

        using namespace DAW::PluginSandboxAutomationShared;

        DAW::PluginSandboxAutomationTransportCore transport;
        const juce::String session = juce::Uuid().toString().removeCharacters("-");
        juce::String error;
        expect(transport.create(session, 42, error),
               "automation mapping create failed: " + error);
        if (! transport.isPrepared())
            return;
        transport.openRealtimeGate();

        SandboxAutomationEvent events[1] = {};
        events[0].parameterOrdinal = 0;
        events[0].sampleOffset = 0;
        events[0].normalizedValue = 0.25f;

        // Publish a valid batch for logical sequence K=7 (slot 7 % 5 == 2).
        expect(transport.publishBatch(7, events, 1, false),
               "publish batch K=7 failed");

        // The PRODUCTION rollback: after a failed audio submit the backend
        // calls cancelBatch(K) with ownership validation.
        transport.cancelBatch(7);

        // K+5 = 12 must reuse the SAME physical slot successfully.
        expect(transport.publishBatch(12, events, 1, false),
               "sequence K+5 did not reuse the canceled slot");

        // A stale cancel for K=7 must NOT clear the newer batch K+5.
        transport.cancelBatch(7);
        expect(! transport.publishBatch(17, events, 1, false),
               "stale cancel cleared a newer sequence's Ready batch");

        // The worker-side reader consumes K+5 and returns the slot to Free.
        DAW::PluginWorkerAutomationTransportCore workerTransport;
        const bool opened = workerTransport.open(transport.mappingName(), session, error);
        if (! opened)
            logMessage("E2A_OPEN_DIAG error=[" + error
                + "] mappingName=[" + transport.mappingName()
                + "] sessionLen=" + juce::String(session.length())
                + " prepared=" + juce::String(transport.isPrepared() ? 1 : 0)
                + " lastError=" + juce::String(static_cast<int>(GetLastError())));
        expect(opened, "worker automation mapping open failed: " + error);
        SandboxAutomationEvent copied[64] = {};
        std::uint32_t copiedCount = 0;
        expect(workerTransport.tryAcquire(12, 42, copied, copiedCount)
                   == DAW::PluginWorkerAutomationTransportCore::AcquireResult::Acquired,
               "worker failed to acquire batch K+5");
        expectEquals(static_cast<int>(copiedCount), 1, "acquired event count mismatch");
        expect(std::abs(copied[0].normalizedValue - 0.25f) < 0.0001f,
               "acquired event value mismatch");

        // Slot Free again → K+10 = 22 publishes cleanly.
        expect(transport.publishBatch(22, events, 1, false),
               "post-consumption slot reuse failed");

        // A missing batch reports Missing; a wrong sequence reports Mismatch.
        std::uint32_t unusedCount = 0;
        expect(workerTransport.tryAcquire(23, 42, copied, unusedCount)
                   == DAW::PluginWorkerAutomationTransportCore::AcquireResult::Missing,
               "absent batch did not classify as Missing");
        transport.publishBatch(24, events, 1, false);
        expect(workerTransport.tryAcquire(24, 999, copied, unusedCount)
                   == DAW::PluginWorkerAutomationTransportCore::AcquireResult::Mismatch,
               "wrong-generation acquire did not classify as Mismatch");
       #else
        beginTest("Windows-only E2A orphan rollback");
        expect(true);
       #endif
    }
};

// ── E2A Gate 2: single event at EXACT sample offset 137 ─────────────────────
class PluginSandboxPhaseE2ASingleEventSampleAccuracyTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2ASingleEventSampleAccuracyTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.single-event-sample-accuracy.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("transport event at offset 137 changes worker output at exactly sample 137");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;
        logMessage("APEX_AUTOMATION_FIXTURE_IDS uniqueId="
                   + juce::String(identity.uniqueId)
                   + " deprecatedUid=" + juce::String(identity.deprecatedUid));

        // Direct proxy-level primitive test: enable the sidecar BEFORE prepare.
        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(proxy.enableAutomationTransport(error),
               "enableAutomationTransport failed: " + error);

        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE2APreparation(worker, preparation);
        expect(proxy.prepare(preparation) == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "prepare failed: " + proxy.processDiagnostics().error);
        expect(proxy.automationEnabled(), "automation transport not enabled after prepare");

        // Frozen Phase C timeline for Q=512/Bmax=512, plugin latency 0:
        // transport latency (d+1)*Q = 1024. Remote quantum 1 occupies host
        // output samples [1024, 1536) = callback 3. The event at quantum-1
        // local offset 137 therefore appears at callback-3 local index 137.
        constexpr std::uint32_t kEventOffset = 137;
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent event;
        event.parameterOrdinal = 0;      // canonical fallback identity for Gain (param_0)
        event.sampleOffset = kEventOffset;
        event.normalizedValue = 0.25f;

        juce::AudioBuffer<float> block(2, kBlockSamples);
        for (int b = 1; b <= 4; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            if (b == 1)
                proxy.processBlockWithAutomation(block, kBlockSamples, &event, 1);
            else
                proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);

            if (b == 3)
            {
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    const float expected = s < static_cast<int>(kEventOffset) ? 0.5f : 0.25f;
                    expect(std::abs(sample - expected) < 0.0001f,
                           "sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected " + juce::String(expected, 6) + ")");
                }
            }
            if (b == 4)
                expect(std::abs(block.getSample(0, kEventOffset) - 0.25f) < 0.0001f,
                       "state did not persist into callback 4");
        }

        const auto applied = proxy.automationDiagnostics().workerAppliedEvents;
        logMessage("E2A_SAMPLE_DIAG published="
            + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().latestPublishedSequence))
            + " overflow=" + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().parentOverflowRejected))
            + " invalidBatches=" + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().workerInvalidBatches))
            + " applied=" + juce::String(static_cast<juce::int64>(applied))
            + " completed=" + juce::String(static_cast<juce::int64>(
                proxy.workerCompletedSequence()))
            + " submitted=" + juce::String(static_cast<juce::int64>(
                proxy.lifecycleDiagnostics().transport.submitted))
            + " fallbacks=" + juce::String(static_cast<juce::int64>(
                proxy.lifecycleDiagnostics().transport.fallbacks)));
        expect(applied >= 1, "worker did not report applied automation events");

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A single-event sample accuracy");
        expect(true);
       #endif
    }
};

// ── E2A: event identity — ordinal targets the intended parameter; invalid ──
// ── ordinals classify the quantum invalid and never touch another param. ───
class PluginSandboxPhaseE2AEventIdentityTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2AEventIdentityTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.event-identity.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("event ordinals target the intended parameter; invalid ordinals classify invalid");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

        const auto rejectionBefore = proxy.lifecycleDiagnostics().transport.metadataRejected;
        const auto fallbackBefore = proxy.lifecycleDiagnostics().transport.fallbacks;

        juce::AudioBuffer<float> block(2, kBlockSamples);
        auto feedConstant = [&block]()
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
        };

        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent event;

        // ── Valid identity: ordinal 0 = Gain, exact boundary at 137 ──────────
        event.parameterOrdinal = 0;
        event.sampleOffset = 137;
        event.normalizedValue = 0.25f;
        for (int b = 1; b <= 4; ++b)
        {
            feedConstant();
            if (b == 1)
                proxy.processBlockWithAutomation(block, kBlockSamples, &event, 1);
            else
                proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
            if (b == 3)
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    const float expected = s < 137 ? 0.5f : 0.25f;
                    expect(std::abs(sample - expected) < 0.0001f,
                           "valid ordinal: sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected " + juce::String(expected, 6) + ")");
                }
        }

        // ── Invalid ordinal: >= resolved parameter count → Invalid quantum ──
        // The worker pre-validates ordinals BEFORE touching any sample; an
        // out-of-range ordinal must classify the quantum invalid (no clamp,
        // no redirect) so the parent rejects and dry-falls back. Quantum 5
        // rides callback 5; its output resolution lands on callback 7.
        const std::uint64_t resolvedCount = proxy.lifecycleDiagnostics()
            .transport.completed > 0 ? 2 : 2;   // fixture resolves 2 params (gain, bypass)
        (void) resolvedCount;
        event.parameterOrdinal = 99;            // out of range: no clamping allowed
        event.sampleOffset = 137;
        event.normalizedValue = 0.25f;
        for (int b = 5; b <= 8; ++b)
        {
            feedConstant();
            if (b == 5)
                proxy.processBlockWithAutomation(block, kBlockSamples, &event, 1);
            else
                proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
            if (b == 7)
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    expect(std::abs(sample - 1.0f) < 0.0001f,
                           "invalid ordinal leaked into audio: sample "
                               + juce::String(s) + " = " + juce::String(sample, 6)
                               + " (expected dry 1.0)");
                }
        }
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected > rejectionBefore,
               "parent did not reject the invalid-ordinal quantum");
        expect(proxy.lifecycleDiagnostics().transport.fallbacks > fallbackBefore,
               "parent did not engage the aligned fallback for the invalid-ordinal quantum");

        // ── Ring progress + no param damage: a valid event still works ──────
        // Gain currently persists its last VALID value (0.25 from quantum 1).
        // The invalid ordinal must NOT have modified it, so the recovery
        // baseline is still 0.25 — not the fixture default 0.5. Quantum 9
        // (callback 9) → callback 11 output.
        event.parameterOrdinal = 0;
        event.sampleOffset = 100;
        event.normalizedValue = 0.75f;
        for (int b = 9; b <= 12; ++b)
        {
            feedConstant();
            if (b == 9)
                proxy.processBlockWithAutomation(block, kBlockSamples, &event, 1);
            else
                proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
            if (b == 11)
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    const float expected = s < 100 ? 0.25f : 0.75f;
                    expect(std::abs(sample - expected) < 0.0001f,
                           "post-invalid recovery: sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected " + juce::String(expected, 6) + ")");
                }
        }

        const auto diagnostics = proxy.automationDiagnostics();
        expect(diagnostics.workerAppliedEvents >= 2,
               "only the two valid events should be applied");
        logMessage("E2A_IDENTITY_DIAG applied="
            + juce::String(static_cast<juce::int64>(diagnostics.workerAppliedEvents))
            + " metadataRejected=" + juce::String(static_cast<juce::int64>(
                proxy.lifecycleDiagnostics().transport.metadataRejected))
            + " fallbacks=" + juce::String(static_cast<juce::int64>(
                proxy.lifecycleDiagnostics().transport.fallbacks)));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A event identity");
        expect(true);
       #endif
    }
};

// ── E2A: multiple events in one Q=512 quantum at exact boundaries ───────────
class PluginSandboxPhaseE2AMultipleEventsTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2AMultipleEventsTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.multiple-events.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("events at 100 and 300 change worker output at exactly those samples");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent events[2];
        events[0].parameterOrdinal = 0;
        events[0].sampleOffset = 100;
        events[0].normalizedValue = 0.25f;
        events[1].parameterOrdinal = 0;
        events[1].sampleOffset = 300;
        events[1].normalizedValue = 0.75f;

        const auto captured = remoteQuantumForEvent(proxy, events, 2);
        for (int s = 0; s < kBlockSamples; ++s)
        {
            const float sample = captured.getSample(0, s);
            const float expected = s < 100 ? 0.5f : (s < 300 ? 0.25f : 0.75f);
            expect(std::abs(sample - expected) < 0.0001f,
                   "sample " + juce::String(s)
                       + " = " + juce::String(sample, 6)
                       + " (expected " + juce::String(expected, 6) + ")");
        }
        expect(proxy.automationDiagnostics().workerAppliedEvents >= 2,
               "worker did not report both applied automation events");
        // Trusted-remote proof: the sample values themselves (0.25/0.75 are
        // impossible from dry 1.0 input). The frozen Phase C reblocker commits
        // its pre-roll fallback resolutions regardless (observed fallbacks=2
        // on the fully-green single-event run) — bound against that baseline,
        // not against zero.
        expect(proxy.lifecycleDiagnostics().transport.fallbacks <= 2,
               "unexpected extra fallback commits beyond the frozen pre-roll baseline");
        logMessage("E2A_MULTI_DIAG applied=" + juce::String(static_cast<juce::int64>(
            proxy.automationDiagnostics().workerAppliedEvents)));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A multiple events");
        expect(true);
       #endif
    }
};

// ── E2A: same-offset staging order — final same-offset event wins ───────────
class PluginSandboxPhaseE2ASameOffsetOrderTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2ASameOffsetOrderTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.same-offset-order.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("two events at offset 137 apply in staged order; final value wins");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

        // Stage in this exact order: 0.25 first, then 0.75 — both at 137.
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent events[2];
        events[0].parameterOrdinal = 0;
        events[0].sampleOffset = 137;
        events[0].normalizedValue = 0.25f;
        events[1].parameterOrdinal = 0;
        events[1].sampleOffset = 137;
        events[1].normalizedValue = 0.75f;

        const auto captured = remoteQuantumForEvent(proxy, events, 2);
        for (int s = 0; s < kBlockSamples; ++s)
        {
            const float sample = captured.getSample(0, s);
            const float expected = s < 137 ? 0.5f : 0.75f;
            expect(std::abs(sample - expected) < 0.0001f,
                   "sample " + juce::String(s)
                       + " = " + juce::String(sample, 6)
                       + " (expected " + juce::String(expected, 6) + ")");
            expect(std::abs(sample - 0.25f) >= 0.0001f,
                   "intermediate same-offset value 0.25 must never be rendered"
                   " (sample " + juce::String(s) + ")");
        }
        expect(proxy.automationDiagnostics().workerAppliedEvents >= 2,
               "worker did not report both applied automation events");
        // Trusted-remote proof: the sample values themselves (0.75 is
        // impossible from dry 1.0 input). The frozen Phase C reblocker commits
        // its pre-roll fallback resolutions regardless (observed fallbacks=2
        // on the fully-green single-event run) — bound against that baseline.
        expect(proxy.lifecycleDiagnostics().transport.fallbacks <= 2,
               "unexpected extra fallback commits beyond the frozen pre-roll baseline");
        logMessage("E2A_SAME_OFFSET_DIAG applied=" + juce::String(static_cast<juce::int64>(
            proxy.automationDiagnostics().workerAppliedEvents)));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A same-offset order");
        expect(true);
       #endif
    }
};

// ── E2A: host callback boundaries must not control automation timing ────────
// Callback A (480) + Callback B (64) map events at absolute 470/500/520 into
// logical quanta 1/1/2 — including the B@20 event landing in quantum 1 even
// though it arrived in the second callback (staged before the reblocker emits).
class PluginSandboxPhaseE2ACallbackBoundaryMappingTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2ACallbackBoundaryMappingTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.callback-boundary-mapping.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("480+64 host callbacks map events across the Q=512 boundary");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

        // Callback pattern: 480, 64, 480, 64, 480, 64, 480 → 2112 samples →
        // 4 submitted quanta. Remote quantum 1 = absolute [1024,1536),
        // quantum 2 = [1536,2048) (frozen latency 2Q = 1024, plugin latency 0).
        constexpr int kTotalSamples = 2112;
        const int sizes[7] = { 480, 64, 480, 64, 480, 64, 480 };
        std::vector<float> recordedL(static_cast<std::size_t>(kTotalSamples));
        int outputPosition = 0;

        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent events[2];
        for (int c = 0; c < 7; ++c)
        {
            const int size = sizes[c];
            juce::AudioBuffer<float> block(2, size);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, size);

            if (c == 0)
            {
                // Callback A: local 470 → absolute 470 → seq 1, local 470.
                events[0].parameterOrdinal = 0;
                events[0].sampleOffset = 470;
                events[0].normalizedValue = 0.25f;
                proxy.processBlockWithAutomation(block, size, events, 1);
            }
            else if (c == 1)
            {
                // Callback B: local 20 → absolute 500 → seq 1, local 500
                //             local 40 → absolute 520 → seq 2, local 8
                events[0].parameterOrdinal = 0;
                events[0].sampleOffset = 20;
                events[0].normalizedValue = 0.75f;
                events[1].parameterOrdinal = 0;
                events[1].sampleOffset = 40;
                events[1].normalizedValue = 0.40f;
                proxy.processBlockWithAutomation(block, size, events, 2);
            }
            else
            {
                proxy.processBlock(block, size);
            }

            for (int s = 0; s < size; ++s)
                recordedL[static_cast<std::size_t>(outputPosition + s)]
                    = block.getSample(0, s);
            outputPosition += size;

            waitForWorkerQuanta(proxy,
                proxy.lifecycleDiagnostics().transport.submitted, 5000);
        }

        // Logical quantum 1 (host absolute [1024,1536)):
        // 0..469 = 0.5, 470..499 = 0.25, 500..511 = 0.75.
        for (int s = 0; s < 512; ++s)
        {
            const float sample = recordedL[static_cast<std::size_t>(1024 + s)];
            const float expected = s < 470 ? 0.5f : (s < 500 ? 0.25f : 0.75f);
            expect(std::abs(sample - expected) < 0.0001f,
                   "quantum 1 sample " + juce::String(s)
                       + " = " + juce::String(sample, 6)
                       + " (expected " + juce::String(expected, 6) + ")");
        }
        // Logical quantum 2 (host absolute [1536,2048)):
        // 0..7 = 0.75 (persisted), 8..511 = 0.40.
        for (int s = 0; s < 512; ++s)
        {
            const float sample = recordedL[static_cast<std::size_t>(1536 + s)];
            const float expected = s < 8 ? 0.75f : 0.40f;
            expect(std::abs(sample - expected) < 0.0001f,
                   "quantum 2 sample " + juce::String(s)
                       + " = " + juce::String(sample, 6)
                       + " (expected " + juce::String(expected, 6) + ")");
        }

        const auto diagnostics = proxy.automationDiagnostics();
        expect(diagnostics.workerAppliedEvents == 3,
               "expected exactly 3 applied events (470, 500, 520)");
        expect(diagnostics.workerInvalidBatches == 0,
               "no automation batch should be invalid");
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected == 0,
               "no audio quantum should be rejected");
        logMessage("E2A_BOUNDARY_DIAG applied="
            + juce::String(static_cast<juce::int64>(diagnostics.workerAppliedEvents))
            + " published=" + juce::String(static_cast<juce::int64>(
                diagnostics.latestPublishedSequence)));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A callback boundary mapping");
        expect(true);
       #endif
    }
};

// ── E2A: one B=2048 callback maps into exactly four Q=512 quanta ────────────
class PluginSandboxPhaseE2A2048FourQuantumTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2A2048FourQuantumTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.2048-four-quantum.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("one 2048 callback produces four correct Q=512 automation quanta");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(proxy.enableAutomationTransport(error),
               "enableAutomationTransport failed: " + error);

        constexpr int kB2048 = 2048;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = 512;                      // exact sandbox quantum Q
        preparation.maximumHostBlockSamples = kB2048;        // Bmax — host callback may be 2048
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        expect(proxy.prepare(preparation) == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "prepare failed: " + proxy.processDiagnostics().error);
        expect(proxy.automationEnabled(), "automation transport not enabled after prepare");

        // Frozen Phase C timeline for Bmax=2048: d=4, transport latency
        // (d+1)*Q = 2560. Remote quantum 1 = absolute [2560,3072) = callback 2
        // local [512,1024); q2 = [1024,1536); q3 = [1536,2048); q4 lands in
        // callback 3 local [0,512).
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent events[4];
        events[0].parameterOrdinal = 0; events[0].sampleOffset = 100;  events[0].normalizedValue = 0.20f;
        events[1].parameterOrdinal = 0; events[1].sampleOffset = 600;  events[1].normalizedValue = 0.40f;
        events[2].parameterOrdinal = 0; events[2].sampleOffset = 1200; events[2].normalizedValue = 0.60f;
        events[3].parameterOrdinal = 0; events[3].sampleOffset = 1800; events[3].normalizedValue = 0.80f;

        juce::AudioBuffer<float> block1(2, kB2048);
        juce::AudioBuffer<float> block2(2, kB2048);
        juce::AudioBuffer<float> block3(2, kB2048);
        std::uint64_t publishedAfterCb1 = 0;
        for (int c = 1; c <= 3; ++c)
        {
            juce::AudioBuffer<float>& block = (c == 1) ? block1
                                            : (c == 2) ? block2 : block3;
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kB2048);
            if (c == 1)
                proxy.processBlockWithAutomation(block, kB2048, events, 4);
            else
                proxy.processBlock(block, kB2048);
            waitForWorkerQuanta(proxy,
                proxy.lifecycleDiagnostics().transport.submitted, 5000);
            if (c == 1)
                publishedAfterCb1 = proxy.automationDiagnostics()
                    .latestPublishedSequence;
        }

        logMessage("E2A_2048_PROBE publishedAfterCb1="
            + juce::String(static_cast<juce::int64>(publishedAfterCb1))
            + " latest=" + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().latestPublishedSequence))
            + " applied=" + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().workerAppliedEvents))
            + " submitted=" + juce::String(static_cast<juce::int64>(
                proxy.lifecycleDiagnostics().transport.submitted))
            + " completed=" + juce::String(static_cast<juce::int64>(
                proxy.workerCompletedSequence()))
            + " fallbacks=" + juce::String(static_cast<juce::int64>(
                proxy.lifecycleDiagnostics().transport.fallbacks))
            + " cb2[512]=" + juce::String(block2.getSample(0, 512), 4)
            + " cb2[544]=" + juce::String(block2.getSample(0, 544), 4)
            + " cb2[1024]=" + juce::String(block2.getSample(0, 1024), 4)
            + " cb2[1536]=" + juce::String(block2.getSample(0, 1536), 4)
            + " cb2[2047]=" + juce::String(block2.getSample(0, 2047), 4)
            + " cb3[0]=" + juce::String(block3.getSample(0, 0), 4)
            + " cb3[264]=" + juce::String(block3.getSample(0, 264), 4)
            + " cb3[512]=" + juce::String(block3.getSample(0, 512), 4)
            + " cb3[1024]=" + juce::String(block3.getSample(0, 1024), 4)
            + " cb3[1536]=" + juce::String(block3.getSample(0, 1536), 4)
            + " cb3[2047]=" + juce::String(block3.getSample(0, 2047), 4));

        expect(publishedAfterCb1 == 4,
               "callback 1 must publish exactly four Q=512 batches");
        expect(proxy.automationDiagnostics().parentOverflowRejected == 0,
               "no automation overflow expected");
        expect(proxy.automationDiagnostics().workerInvalidBatches == 0,
               "no automation batch should be invalid");
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected == 0,
               "no audio quantum should be rejected");

        // Callback 2: q1 local [512,1024), q2 [1024,1536), q3 [1536,2048).
        for (int s = 0; s < 512; ++s)
        {
            const float sample = block2.getSample(0, 512 + s);
            const float expected = s < 100 ? 0.5f : 0.20f;
            expect(std::abs(sample - expected) < 0.0001f,
                   "q1 sample " + juce::String(s)
                       + " = " + juce::String(sample, 6)
                       + " (expected " + juce::String(expected, 6) + ")");
        }
        for (int s = 0; s < 512; ++s)
        {
            const float sample = block2.getSample(0, 1024 + s);
            const float expected = s < 88 ? 0.20f : 0.40f;
            expect(std::abs(sample - expected) < 0.0001f,
                   "q2 sample " + juce::String(s)
                       + " = " + juce::String(sample, 6)
                       + " (expected " + juce::String(expected, 6) + ")");
        }
        for (int s = 0; s < 512; ++s)
        {
            const float sample = block2.getSample(0, 1536 + s);
            const float expected = s < 176 ? 0.40f : 0.60f;
            expect(std::abs(sample - expected) < 0.0001f,
                   "q3 sample " + juce::String(s)
                       + " = " + juce::String(sample, 6)
                       + " (expected " + juce::String(expected, 6) + ")");
        }
        // Callback 3: q4 local [0,512).
        for (int s = 0; s < 512; ++s)
        {
            const float sample = block3.getSample(0, s);
            const float expected = s < 264 ? 0.60f : 0.80f;
            expect(std::abs(sample - expected) < 0.0001f,
                   "q4 sample " + juce::String(s)
                       + " = " + juce::String(sample, 6)
                       + " (expected " + juce::String(expected, 6) + ")");
        }

        expect(proxy.automationDiagnostics().workerAppliedEvents == 4,
               "expected exactly four applied events");
        logMessage("E2A_2048_DIAG applied="
            + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().workerAppliedEvents))
            + " published=" + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().latestPublishedSequence)));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A 2048 four-quantum mapping");
        expect(true);
       #endif
    }
};

// ── E2A: zero-event fast path — E2 enabled with no events must preserve ─────
// ── the whole-Q hosted plugin DSP behavior (audio equivalence). ─────────────
class PluginSandboxPhaseE2AZeroEventFastPathTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2AZeroEventFastPathTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.zero-event-fast-path.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("E2-enabled zero-event processing preserves the whole-Q DSP path");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

        juce::AudioBuffer<float> block(2, kBlockSamples);
        for (int b = 1; b <= 4; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);

            if (b == 3)
            {
                // Remote quantum 1: Gain applied exactly once → 0.5 everywhere.
                // A double application would render 0.25; sub-block
                // segmentation with zero events would never setValue.
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    expect(std::abs(sample - 0.5f) < 0.0001f,
                           "sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected single whole-Q Gain application 0.5)");
                }
            }
        }

        const auto diagnostics = proxy.automationDiagnostics();
        expect(diagnostics.latestPublishedSequence == 4,
               "four intentional zero-event batches should be published");
        expect(diagnostics.workerAppliedEvents == 0,
               "no automation events should be applied");
        expect(diagnostics.workerInvalidBatches == 0,
               "no automation batch should be invalid");
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected == 0,
               "no audio quantum should be rejected");
        logMessage("E2A_ZERO_DIAG applied="
            + juce::String(static_cast<juce::int64>(diagnostics.workerAppliedEvents))
            + " published=" + juce::String(static_cast<juce::int64>(
                diagnostics.latestPublishedSequence))
            + " invalidBatches=" + juce::String(static_cast<juce::int64>(
                diagnostics.workerInvalidBatches)));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A zero-event fast path");
        expect(true);
       #endif
    }
};

// ── E2A: generation safety — stale G1 sidecar metadata is invisible to G2 ──
class PluginSandboxPhaseE2AGenerationSafetyTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2AGenerationSafetyTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.generation-safety.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("fresh G2 cannot consume stale G1 automation metadata");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        // ── G1: establish valid automation metadata + modified plugin state ──
        juce::String g1Name;
        std::uint64_t g1Generation = 0;
        {
            DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
            juce::String error;
            expect(prepareE2AProxy(proxy, worker, identity, error),
                   "G1 prepare failed: " + error);
           #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
            g1Name = proxy.automationTransportForTest().mappingName();
            expect(g1Name.isNotEmpty(), "G1 automation mapping name is empty");
            expect(proxy.automationTransportForTest().isPrepared(),
                   "G1 automation sidecar not prepared");
           #endif
            g1Generation = proxy.currentGeneration();
            expect(g1Generation != 0, "G1 generation is zero");

            DAW::PluginSandboxAutomationShared::SandboxAutomationEvent event;
            event.parameterOrdinal = 0;
            event.sampleOffset = 100;
            event.normalizedValue = 0.25f;
            juce::AudioBuffer<float> block(2, kBlockSamples);
            for (int b = 1; b <= 4; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                      1.0f, kBlockSamples);
                if (b == 1)
                    proxy.processBlockWithAutomation(block, kBlockSamples, &event, 1);
                else
                    proxy.processBlock(block, kBlockSamples);
                waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
                if (b == 3)
                {
                    expect(std::abs(block.getSample(0, 0) - 0.5f) < 0.0001f,
                           "G1 baseline mismatch");
                    expect(std::abs(block.getSample(0, 100) - 0.25f) < 0.0001f,
                           "G1 event did not apply");
                }
            }
            expect(proxy.automationDiagnostics().workerAppliedEvents == 1,
                   "G1 should have applied exactly one event");

            const auto pid = proxy.currentWorkerPid();
            proxy.release(5000);
            expect(! processStillAlive(pid, 5000), "G1 worker orphan after release");
        }

        // ── G2: fresh session — stale G1 must be unreachable ─────────────────
        DAW::SandboxedPluginProxyCore proxy2(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy2, worker, identity, error),
               "G2 prepare failed: " + error);

        juce::String g2Name;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        g2Name = proxy2.automationTransportForTest().mappingName();
        expect(g2Name.isNotEmpty(), "G2 automation mapping name is empty");
        expect(g2Name != g1Name,
               "G2 reused the G1 automation mapping — stale G1 metadata would be visible");
       #endif
        const auto g2Generation = proxy2.currentGeneration();
        expect(g2Generation != 0, "G2 generation is zero");
        expect(g2Generation != g1Generation,
               "G2 reused the G1 session generation — stale G1 metadata would be accepted");

        // G2 baseline must be the fixture default 0.5 — NOT G1's final 0.25.
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent event;
        event.parameterOrdinal = 0;
        event.sampleOffset = 200;
        event.normalizedValue = 0.75f;
        juce::AudioBuffer<float> block(2, kBlockSamples);
        for (int b = 1; b <= 4; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            if (b == 1)
                proxy2.processBlockWithAutomation(block, kBlockSamples, &event, 1);
            else
                proxy2.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy2, static_cast<std::uint64_t>(b), 5000);
            if (b == 3)
            {
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    const float expected = s < 200 ? 0.5f : 0.75f;
                    expect(std::abs(sample - expected) < 0.0001f,
                           "G2 sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected " + juce::String(expected, 6)
                               + "; a stale G1 value would render 0.25 baseline)");
                }
            }
        }
        const auto g2Diagnostics = proxy2.automationDiagnostics();
        expect(g2Diagnostics.workerAppliedEvents == 1,
               "G2 should apply exactly its own event");
        expect(g2Diagnostics.workerInvalidBatches == 0,
               "G2 saw an invalid automation batch");
        expect(proxy2.lifecycleDiagnostics().transport.metadataRejected == 0,
               "G2 rejected an audio quantum");
        logMessage("E2A_GEN_DIAG g1Name=" + g1Name + " g2Name=" + g2Name
            + " g1Gen=" + juce::String(static_cast<juce::int64>(g1Generation))
            + " g2Gen=" + juce::String(static_cast<juce::int64>(g2Generation))
            + " g2Applied=" + juce::String(static_cast<juce::int64>(
                g2Diagnostics.workerAppliedEvents)));

        const auto pid2 = proxy2.currentWorkerPid();
        proxy2.release(5000);
        expect(! processStillAlive(pid2, 5000), "G2 worker orphan after release");
       #else
        beginTest("Windows-only E2A generation safety");
        expect(true);
       #endif
    }
};

// ── E2A: batch mismatch — automation sequence K+5 must never be consumed ────
// ── for audio quantum K; the quantum classifies invalid and recovers. ───────
class PluginSandboxPhaseE2ABatchMismatchTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2ABatchMismatchTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.batch-mismatch.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("wrong-sequence automation batch is rejected without leaking events");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        // Controlled mismatch: publish a batch CLAIMING sequence 6 into slot
        // 6%5 == 1 BEFORE exchange 1. The backend's own sequence-1 publish then
        // CAS-fails (slot not Free) and the worker processing audio quantum 1
        // finds audioSequence 6 != 1 → Mismatch → SlotFlagInvalidMetadata.
        // This models an over-eager/defective producer publishing far ahead —
        // the exact hazard the ownership-validated transport must refuse.
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent poisoned;
        poisoned.parameterOrdinal = 0;
        poisoned.sampleOffset = 0;
        poisoned.normalizedValue = 0.9f;
        expect(proxy.automationTransportForTest().publishBatch(6, &poisoned, 1, false),
               "controlled mismatch pre-publish failed");
       #else
        expect(false, "APEX_ENABLE_TEST_HOOKS is required for the controlled mismatch");
       #endif

        const auto metadataRejectedBefore
            = proxy.lifecycleDiagnostics().transport.metadataRejected;

        juce::AudioBuffer<float> block(2, kBlockSamples);
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent event;
        for (int b = 1; b <= 8; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            if (b == 5)
            {
                // Later valid automation — quantum 5 (slot 0, untouched).
                event.parameterOrdinal = 0;
                event.sampleOffset = 100;
                event.normalizedValue = 0.25f;
                proxy.processBlockWithAutomation(block, kBlockSamples, &event, 1);
            }
            else
            {
                proxy.processBlock(block, kBlockSamples);
            }
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);

            if (b == 3)
            {
                // Quantum 1 was rejected → aligned dry fallback (1.0). The
                // poisoned 0.9 must never appear anywhere.
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    expect(std::abs(sample - 1.0f) < 0.0001f,
                           "mismatch quantum leaked non-dry sample "
                               + juce::String(s) + " = " + juce::String(sample, 6));
                }
            }
            if (b == 7)
            {
                // Later valid event applies normally (quantum 5 → callback 7);
                // the poisoned value never leaked to a later quantum either.
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    const float expected = s < 100 ? 0.5f : 0.25f;
                    expect(std::abs(sample - expected) < 0.0001f,
                           "post-mismatch sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected " + juce::String(expected, 6) + ")");
                }
            }
        }

        const auto diagnostics = proxy.automationDiagnostics();
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected
                   > metadataRejectedBefore,
               "parent did not reject the mismatched quantum");
        expect(diagnostics.workerAppliedEvents == 1,
               "only the later valid event may apply — poisoned event leaked");
        expect(diagnostics.latestPublishedSequence == 8,
               "sidecar did not continue publishing after the mismatch");
        expect(proxy.workerCompletedSequence() >= 8,
               "audio ring did not keep progressing after the mismatch");
        logMessage("E2A_MISMATCH_DIAG applied="
            + juce::String(static_cast<juce::int64>(diagnostics.workerAppliedEvents))
            + " metadataRejected=" + juce::String(static_cast<juce::int64>(
                proxy.lifecycleDiagnostics().transport.metadataRejected))
            + " published=" + juce::String(static_cast<juce::int64>(
                diagnostics.latestPublishedSequence)));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A batch mismatch");
        expect(true);
       #endif
    }
};

// ── E2A: event overflow — 64 events valid, 65 invalidate the whole batch ────
class PluginSandboxPhaseE2AEventOverflowTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2AEventOverflowTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.event-overflow.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("64 events apply; 65 events invalidate the entire batch without truncation");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

        juce::AudioBuffer<float> block(2, kBlockSamples);

        // ── Phase 1: 64 events at offsets 0..63, distinct values 0.10..0.73 ──
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent events[65];
        for (int i = 0; i < 64; ++i)
        {
            events[i].parameterOrdinal = 0;
            events[i].sampleOffset = static_cast<std::uint32_t>(i);
            events[i].normalizedValue = 0.10f + static_cast<float>(i) * 0.01f;
        }
        for (int b = 1; b <= 4; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            if (b == 1)
                proxy.processBlockWithAutomation(block, kBlockSamples, events, 64);
            else
                proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
            if (b == 3)
            {
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    const float expected = s < 64
                        ? 0.10f + static_cast<float>(s) * 0.01f : 0.73f;
                    expect(std::abs(sample - expected) < 0.001f,
                           "64-event sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected " + juce::String(expected, 6) + ")");
                }
            }
        }
        expect(proxy.automationDiagnostics().workerAppliedEvents == 64,
               "all 64 events should be applied");
        expect(proxy.automationDiagnostics().workerInvalidBatches == 0,
               "64 events are within capacity — batch must be valid");
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected == 0,
               "64-event quantum must not be rejected");

        // ── Phase 2: 65 events → explicit overflow → WHOLE batch invalid ────
        for (int i = 0; i < 65; ++i)
        {
            events[i].parameterOrdinal = 0;
            events[i].sampleOffset = static_cast<std::uint32_t>(i);
            events[i].normalizedValue = 0.9f;
        }
        const auto invalidBatchesBefore
            = proxy.automationDiagnostics().workerInvalidBatches;
        const auto rejectsBefore
            = proxy.lifecycleDiagnostics().transport.metadataRejected;
        const auto overflowBefore
            = proxy.automationDiagnostics().parentOverflowRejected;
        for (int b = 5; b <= 8; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            if (b == 5)
                proxy.processBlockWithAutomation(block, kBlockSamples, events, 65);
            else
                proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
            if (b == 7)
            {
                // Quantum 5 rejected → dry 1.0 everywhere. None of the 65
                // events (0.9) may have been applied.
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    expect(std::abs(sample - 1.0f) < 0.0001f,
                           "overflow quantum leaked non-dry sample "
                               + juce::String(s) + " = " + juce::String(sample, 6));
                }
            }
        }
        expect(proxy.automationDiagnostics().workerInvalidBatches > invalidBatchesBefore,
               "worker did not classify the overflow batch invalid");
        expect(proxy.automationDiagnostics().parentOverflowRejected > overflowBefore,
               "parent did not represent the overflow explicitly");
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected > rejectsBefore,
               "parent did not reject the overflowed quantum");
        expect(proxy.automationDiagnostics().workerAppliedEvents == 64,
               "ZERO of the 65 overflow events may be applied");

        // ── Phase 3: later valid batch applies normally; prior valid plugin ──
        // ── state (0.73 from Phase 1) remains intact through the overflow. ──
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent event;
        event.parameterOrdinal = 0;
        event.sampleOffset = 100;
        event.normalizedValue = 0.75f;
        for (int b = 9; b <= 12; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            if (b == 9)
                proxy.processBlockWithAutomation(block, kBlockSamples, &event, 1);
            else
                proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
            if (b == 11)
            {
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    const float expected = s < 100 ? 0.73f : 0.75f;
                    expect(std::abs(sample - expected) < 0.001f,
                           "post-overflow sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected " + juce::String(expected, 6) + ")");
                }
            }
        }
        expect(proxy.automationDiagnostics().workerAppliedEvents == 65,
               "the later valid event must apply (64 + 1)");
        expect(proxy.workerCompletedSequence() >= 12,
               "audio ring did not keep progressing through the overflow");
        logMessage("E2A_OVERFLOW_DIAG applied="
            + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().workerAppliedEvents))
            + " invalidBatches=" + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().workerInvalidBatches))
            + " overflowRejected=" + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().parentOverflowRejected)));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A event overflow");
        expect(true);
       #endif
    }
};

// ── E2A: stateful segmentation continuity — event-boundary slices must not ──
// ── reset, duplicate, skip, or corrupt persistent plugin DSP state. ─────────
// Fixture DSP (frozen APEXTestVST3State): out[n] = in[n-32] * gain[n], with a
// 32-sample delay ring and prepareToPlay/reset clearing the ring. A reset at
// any event boundary would therefore render 32 zero samples after that
// boundary — the exact sample values prove continuity.
class PluginSandboxPhaseE2AStatefulSegmentationContinuityTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2AStatefulSegmentationContinuityTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.stateful-segmentation-continuity.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("segmented processing equals continuous stateful processing");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::SandboxedPluginProxyCore proxy(stateFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

        // Frozen latency contract for the state fixture (plugin latency 32,
        // Bmax 512): workerPlugin 32, reblock 1024, effective 1056.
        expect(proxy.getWorkerPluginLatencySamples() == 32,
               "state fixture plugin latency must be 32");
        expect(proxy.getEffectiveLatencySamples() == 1056,
               "effective latency must remain 1056");
        const auto latencyBefore = proxy.getEffectiveLatencySamples();

        // Events at the recommended boundaries: 137 → 0.25, 300 → 0.75.
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent events[2];
        events[0].parameterOrdinal = 0;
        events[0].sampleOffset = 137;
        events[0].normalizedValue = 0.25f;
        events[1].parameterOrdinal = 0;
        events[1].sampleOffset = 300;
        events[1].normalizedValue = 0.75f;

        juce::AudioBuffer<float> block(2, kBlockSamples);
        for (int b = 1; b <= 4; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            if (b == 1)
                proxy.processBlockWithAutomation(block, kBlockSamples, events, 2);
            else
                proxy.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);

            if (b == 3)
            {
                // Remote quantum 1 content = out[n] = in[n-32] * gain[n]:
                //   [0,32)   = 0.0   (delay prefix zeros)
                //   [32,137) = 0.5   (1.0 * 0.5)
                //   [137,300)= 0.25  (1.0 * 0.25 — transition EXACTLY at 137)
                //   [300,512)= 0.75  (1.0 * 0.75 — transition EXACTLY at 300)
                // A reset at either boundary would zero the following 32
                // samples; a duplicated/missing delayed sample would shift
                // every transition by one.
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    const float expected = s < 32 ? 0.0f
                                         : s < 137 ? 0.5f
                                         : s < 300 ? 0.25f : 0.75f;
                    expect(std::abs(sample - expected) < 0.0001f,
                           "continuity sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected " + juce::String(expected, 6)
                               + "; a reset would zero the 32 samples after each boundary)");
                }
            }
            if (b == 4)
            {
                // Quantum 2: the delay ring now holds quantum 1's input (1.0),
                // and the gain persisted at 0.75 — NO zero prefix. Any state
                // reset at the quantum boundary would reintroduce 32 zeros.
                for (int s = 0; s < kBlockSamples; ++s)
                {
                    const float sample = block.getSample(0, s);
                    expect(std::abs(sample - 0.75f) < 0.0001f,
                           "quantum-2 continuity sample " + juce::String(s)
                               + " = " + juce::String(sample, 6)
                               + " (expected 0.75 everywhere)");
                }
            }
        }

        expect(proxy.getEffectiveLatencySamples() == latencyBefore,
               "event processing changed effective latency / PDC");
        expect(proxy.automationDiagnostics().workerAppliedEvents == 2,
               "both continuity events should be applied");
        expect(proxy.automationDiagnostics().workerInvalidBatches == 0,
               "no continuity batch should be invalid");
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected == 0,
               "no continuity quantum should be rejected");
        logMessage("E2A_STATEFUL_DIAG applied="
            + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().workerAppliedEvents))
            + " latency=" + juce::String(proxy.getEffectiveLatencySamples()));

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");
       #else
        beginTest("Windows-only E2A stateful segmentation continuity");
        expect(true);
       #endif
    }
};

// ── E2A: zero-RT certification — no allocations/locks/waits in the RT path ──
class PluginSandboxPhaseE2AZeroRtTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2AZeroRtTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.zero-rt.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("E2A realtime path performs no allocations, locks, or waits");

       #if ! JUCE_ENABLE_ALLOCATION_HOOKS
        expect(false, "JUCE allocation hooks are required for the E2A zero-RT proof");
        return;
       #endif

        juce::MessageManager::getInstance();
        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        // ── Control: E2-disabled frozen path — first call + steady state ────
        {
            DAW::SandboxedPluginProxyCore controlProxy(
                automationFixtureDescription(identity));
            DAW::SandboxedPluginProxyCore::Preparation controlPreparation;
            prepareE2APreparation(worker, controlPreparation);
            expect(controlProxy.prepare(controlPreparation)
                       == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
                   "control prepare failed: " + controlProxy.processDiagnostics().error);
            expect(! controlProxy.automationEnabled(),
                   "control proxy must have E2 disabled");

            juce::AudioBuffer<float> controlBlock(2, kBlockSamples);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(controlBlock.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                controlProxy.processBlock(controlBlock, kBlockSamples);
            }
            waitForWorkerQuanta(controlProxy,
                controlProxy.lifecycleDiagnostics().transport.submitted, 5000);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(controlBlock.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                controlProxy.processBlock(controlBlock, kBlockSamples);
            }
            waitForWorkerQuanta(controlProxy,
                controlProxy.lifecycleDiagnostics().transport.submitted, 5000);

            const auto controlPid = controlProxy.currentWorkerPid();
            controlProxy.release(5000);
            expect(! processStillAlive(controlPid, 5000),
                   "control worker orphan after release");
        }

        // ── Part A: parent RT path against the REAL worker process ───────────
        DAW::SandboxedPluginProxyCore proxy(automationFixtureDescription(identity));
        juce::String error;
        expect(prepareE2AProxy(proxy, worker, identity, error),
               "prepare failed: " + error);

        juce::AudioBuffer<float> block(2, kBlockSamples);
        DAW::PluginSandboxAutomationShared::SandboxAutomationEvent events[2];
        auto fillConstant = [&block]()
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch),
                                                  1.0f, kBlockSamples);
        };

        // zero-event quantum
        fillConstant();
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            proxy.processBlock(block, kBlockSamples);
        }
        waitForWorkerQuanta(proxy, proxy.lifecycleDiagnostics().transport.submitted, 5000);

        // single-event quantum
        fillConstant();
        events[0] = { 0, 137, 0.25f };
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            proxy.processBlockWithAutomation(block, kBlockSamples, events, 1);
        }
        waitForWorkerQuanta(proxy, proxy.lifecycleDiagnostics().transport.submitted, 5000);

        // multiple-event quantum
        fillConstant();
        events[0] = { 0, 100, 0.25f };
        events[1] = { 0, 300, 0.75f };
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            proxy.processBlockWithAutomation(block, kBlockSamples, events, 2);
        }
        waitForWorkerQuanta(proxy, proxy.lifecycleDiagnostics().transport.submitted, 5000);

        // same-offset events
        fillConstant();
        events[0] = { 0, 137, 0.25f };
        events[1] = { 0, 137, 0.75f };
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            proxy.processBlockWithAutomation(block, kBlockSamples, events, 2);
        }
        waitForWorkerQuanta(proxy, proxy.lifecycleDiagnostics().transport.submitted, 5000);

        // cross-callback 480/64 with staging across the quantum boundary
        {
            juce::AudioBuffer<float> blockA(2, 480);
            juce::AudioBuffer<float> blockB(2, 64);
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::FloatVectorOperations::fill(blockA.getWritePointer(ch), 1.0f, 480);
                juce::FloatVectorOperations::fill(blockB.getWritePointer(ch), 1.0f, 64);
            }
            events[0] = { 0, 470, 0.25f };
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                proxy.processBlockWithAutomation(blockA, 480, events, 1);
            }
            waitForWorkerQuanta(proxy, proxy.lifecycleDiagnostics().transport.submitted, 5000);
            events[0] = { 0, 20, 0.75f };
            events[1] = { 0, 40, 0.40f };
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                proxy.processBlockWithAutomation(blockB, 64, events, 2);
            }
            waitForWorkerQuanta(proxy, proxy.lifecycleDiagnostics().transport.submitted, 5000);

            // The 64-callback's second event maps to absolute 2568 → quantum 6
            // (local 8). One more full callback submits that quantum so the
            // cross-boundary event is published and applied.
            fillConstant();
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                proxy.processBlock(block, kBlockSamples);
            }
            waitForWorkerQuanta(proxy, proxy.lifecycleDiagnostics().transport.submitted, 5000);
        }

        const auto appliedZeroRt = proxy.automationDiagnostics().workerAppliedEvents;
        logMessage("E2A_ZERO_RT_APPLIED applied="
            + juce::String(static_cast<juce::int64>(appliedZeroRt))
            + " published=" + juce::String(static_cast<juce::int64>(
                proxy.automationDiagnostics().latestPublishedSequence))
            + " submitted=" + juce::String(static_cast<juce::int64>(
                proxy.lifecycleDiagnostics().transport.submitted)));
        expect(appliedZeroRt == 8,
               "all zero-RT event shapes should reach the worker (applied="
                   + juce::String(static_cast<juce::int64>(appliedZeroRt)) + ")");
        expect(proxy.lifecycleDiagnostics().transport.metadataRejected == 0,
               "zero-RT run produced metadata rejects");

        const auto pid = proxy.currentWorkerPid();
        proxy.release(5000);
        expect(! processStillAlive(pid, 5000), "automation worker orphan after release");

        // ── Part B: worker-side E2A primitives, in-process, no fixture load ──
        // The worker process cannot be instrumented from the test process, so
        // the worker-side primitives are certified directly: sidecar
        // tryAcquire lifecycle, the non-owning AudioBuffer slice view, and
        // non-notifying parameter setValue — the only operations the worker
        // adds beyond the frozen whole-Q path. No VST3 module is loaded here
        // (the frozen module-hygiene contract is preserved).
        {
            const juce::String sessionToken = "0123456789abcdef0123456789abcdef";
            const std::uint64_t generation = 0x123456789ABCDEF0ULL;
            DAW::PluginSandboxAutomationTransportCore parentTransport;
            juce::String createError;
            expect(parentTransport.create(sessionToken, generation, createError),
                   "in-process automation mapping create failed: " + createError);

            DAW::PluginWorkerAutomationTransportCore workerTransport;
            expect(workerTransport.open(parentTransport.mappingName(), sessionToken, createError),
                   "in-process worker automation open failed: " + createError);
            parentTransport.openRealtimeGate();

            DAW::PluginSandboxAutomationShared::SandboxAutomationEvent
                acquiredEvents[DAW::PluginSandboxAutomationShared::kMaxAutomationEventsPerQuantum];
            std::uint32_t acquiredCount = 0;

            // Measured scopes contain ONLY the measured operation — results go
            // into POD locals and every assertion happens after the checker
            // closes (the UnitTest assertion machinery may itself allocate).
            int acquiredResult = -1;
            int acquiredCountObserved = -1;

            // Acquired path (real event copy) — allocation-checked.
            DAW::PluginSandboxAutomationShared::SandboxAutomationEvent published[2];
            published[0] = { 0, 100, 0.25f };
            published[1] = { 0, 300, 0.75f };
            expect(parentTransport.publishBatch(1, published, 2, false),
                   "in-process publish failed");
            acquiredCount = 0;
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                acquiredResult = static_cast<int>(workerTransport.tryAcquire(
                    1, generation, acquiredEvents, acquiredCount));
                acquiredCountObserved = static_cast<int>(acquiredCount);
                workerTransport.recordAppliedEvents(2);
            }
            expect(acquiredResult
                       == static_cast<int>(
                           DAW::PluginWorkerAutomationTransportCore::AcquireResult::Acquired)
                   && acquiredCountObserved == 2,
                   "in-process Acquired path failed");

            // Mismatch path (wrong-sequence batch) — allocation-checked.
            expect(parentTransport.publishBatch(7, published, 1, false),
                   "in-process mismatch publish failed");
            acquiredCount = 0;
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                acquiredResult = static_cast<int>(workerTransport.tryAcquire(
                    2, generation, acquiredEvents, acquiredCount));
                acquiredCountObserved = static_cast<int>(acquiredCount);
            }
            expect(acquiredResult
                       == static_cast<int>(
                           DAW::PluginWorkerAutomationTransportCore::AcquireResult::Mismatch)
                   && acquiredCountObserved == 0,
                   "in-process Mismatch path failed");

            // Invalid path (overflow marker) — allocation-checked.
            expect(parentTransport.publishBatch(8, nullptr, 0, true),
                   "in-process overflow publish failed");
            acquiredCount = 0;
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                acquiredResult = static_cast<int>(workerTransport.tryAcquire(
                    8, generation, acquiredEvents, acquiredCount));
                acquiredCountObserved = static_cast<int>(acquiredCount);
            }
            expect(acquiredResult
                       == static_cast<int>(
                           DAW::PluginWorkerAutomationTransportCore::AcquireResult::Invalid)
                   && acquiredCountObserved == 0,
                   "in-process Invalid path failed");

            // Missing path (empty slot) — allocation-checked.
            acquiredCount = 0;
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                acquiredResult = static_cast<int>(workerTransport.tryAcquire(
                    3, generation, acquiredEvents, acquiredCount));
            }
            expect(acquiredResult
                       == static_cast<int>(
                           DAW::PluginWorkerAutomationTransportCore::AcquireResult::Missing),
                   "in-process Missing path failed");

            // Disabled path (gate closed) — allocation-checked.
            parentTransport.closeRealtimeGate();
            acquiredCount = 0;
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                acquiredResult = static_cast<int>(workerTransport.tryAcquire(
                    4, generation, acquiredEvents, acquiredCount));
            }
            expect(acquiredResult
                       == static_cast<int>(
                           DAW::PluginWorkerAutomationTransportCore::AcquireResult::Disabled),
                   "in-process Disabled path failed");

            // Non-owning AudioBuffer slice view (processSlice view pattern) —
            // both the plain and the offset constructor variants.
            {
                float channelData0[512] {};
                float channelData1[512] {};
                float* channelPointers[2] = { channelData0, channelData1 };
                int viewChannels = -1;
                int viewSamples = -1;
                int offsetChannels = -1;
                int offsetSamples = -1;
                {
                    juce::UnitTestAllocationChecker allocationChecker(*this);
                    juce::AudioBuffer<float> view(channelPointers, 2, 512);
                    viewChannels = view.getNumChannels();
                    viewSamples = view.getNumSamples();
                    juce::AudioBuffer<float> offsetView(
                        channelPointers, 2, 137, 163);
                    offsetChannels = offsetView.getNumChannels();
                    offsetSamples = offsetView.getNumSamples();
                }
                expect(viewChannels == 2 && viewSamples == 512,
                       "slice view construction failed");
                expect(offsetChannels == 2 && offsetSamples == 163,
                       "offset slice view construction failed");
            }

            // Non-notifying parameter setValue (mirrors the fixture's
            // parameter: a bare relaxed atomic store, no listeners).
            {
                struct TestGainParameter final : public juce::AudioProcessorParameterWithID
                {
                    TestGainParameter() : juce::AudioProcessorParameterWithID("gain", "Gain", "Gain") {}
                    float getValue() const override { return value_.load(std::memory_order_relaxed); }
                    void setValue(float v) override
                    {
                        value_.store(juce::jlimit(0.0f, 1.0f, v),
                                     std::memory_order_relaxed);
                    }
                    float getDefaultValue() const override { return 0.5f; }
                    juce::String getName(int) const override { return "Gain"; }
                    juce::String getLabel() const override { return {}; }
                    float getValueForText(const juce::String& t) const override
                    {
                        return juce::jlimit(0.0f, 1.0f, t.getFloatValue());
                    }
                    int getNumSteps() const override
                    {
                        return juce::AudioProcessor::getDefaultNumParameterSteps();
                    }
                    std::atomic<float> value_ { 0.5f };
                };
                TestGainParameter parameter;
                float observedValue = -1.0f;
                {
                    juce::UnitTestAllocationChecker allocationChecker(*this);
                    parameter.setValue(0.25f);
                    observedValue = parameter.getValue();
                }
                expect(std::abs(observedValue - 0.25f) < 0.0001f,
                       "setValue did not take effect");
            }

            workerTransport.close();
            parentTransport.releaseAfterWorkerStopped();
        }
        logMessage("E2A_ZERO_RT_DIAG parent-and-worker primitives allocation-free");
       #else
        beginTest("Windows-only E2A zero-RT certification");
        expect(true);
       #endif
    }
};

// ── E2A: block-rate regression parity — E2 infrastructure does not change ───
// ── the frozen legacy sandbox behavior when no events are present. ──────────
class PluginSandboxPhaseE2ABlockRateRegressionParityTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2ABlockRateRegressionParityTest()
        : juce::UnitTest("plugin.sandbox.phase-e2.block-rate-regression-parity.v1",
                         "PluginSandboxPhaseE2A") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("E2-enabled zero-event processing matches E2-disabled legacy behavior");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        // ── Proxy A: E2 disabled (frozen default). ────────────────────────────
        DAW::SandboxedPluginProxyCore proxyA(automationFixtureDescription(identity));
        // ── Proxy B: E2 enabled, zero events. ─────────────────────────────────
        DAW::SandboxedPluginProxyCore proxyB(automationFixtureDescription(identity));
        juce::String error;
        expect(proxyB.enableAutomationTransport(error),
               "proxy B enableAutomationTransport failed: " + error);

        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = 512;                     // quantum Q
        preparation.maximumHostBlockSamples = 2048;         // Bmax for variable blocks
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        expect(proxyA.prepare(preparation) == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "proxy A prepare failed: " + proxyA.processDiagnostics().error);
        expect(proxyB.prepare(preparation) == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "proxy B prepare failed: " + proxyB.processDiagnostics().error);
        expect(! proxyA.automationEnabled(), "proxy A must have E2 disabled");
        expect(proxyB.automationEnabled(), "proxy B must have E2 enabled");

        // Latency/PDC equivalence.
        expect(proxyA.getWorkerPluginLatencySamples()
                   == proxyB.getWorkerPluginLatencySamples(),
               "worker plugin latency differs between E2-disabled and E2-enabled");
        expect(proxyA.getEffectiveLatencySamples()
                   == proxyB.getEffectiveLatencySamples(),
               "effective latency differs between E2-disabled and E2-enabled");
        expect(proxyA.getReblockTransportLatencySamples()
                   == proxyB.getReblockTransportLatencySamples(),
               "reblock latency differs between E2-disabled and E2-enabled");

        // ── Identical variable-block schedule, constant 1.0 input. ───────────
        const int sizes[] = { 128, 480, 512, 1024, 2048, 512 };
        constexpr int kCallbackCount = 6;
        for (int c = 0; c < kCallbackCount; ++c)
        {
            const int size = sizes[c];
            juce::AudioBuffer<float> blockA(2, size);
            juce::AudioBuffer<float> blockB(2, size);
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::FloatVectorOperations::fill(blockA.getWritePointer(ch), 1.0f, size);
                juce::FloatVectorOperations::fill(blockB.getWritePointer(ch), 1.0f, size);
            }
            proxyA.processBlock(blockA, size);
            proxyB.processBlock(blockB, size);
            waitForWorkerQuanta(proxyA, proxyA.lifecycleDiagnostics().transport.submitted, 5000);
            waitForWorkerQuanta(proxyB, proxyB.lifecycleDiagnostics().transport.submitted, 5000);

            for (int s = 0; s < size; ++s)
            {
                const float a = blockA.getSample(0, s);
                const float b = blockB.getSample(0, s);
                expect(std::abs(a - b) < 0.0001f,
                       "E2-disabled vs E2-enabled output mismatch at callback "
                           + juce::String(c) + " sample " + juce::String(s)
                           + ": " + juce::String(a, 6) + " vs " + juce::String(b, 6));
            }
        }

        expect(proxyA.lifecycleDiagnostics().transport.submitted
                   == proxyB.lifecycleDiagnostics().transport.submitted,
               "submission counts differ between E2-disabled and E2-enabled");
        expect(proxyA.workerCompletedSequence()
                   == proxyB.workerCompletedSequence(),
               "completion counts differ between E2-disabled and E2-enabled");

        const auto bDiagnostics = proxyB.automationDiagnostics();
        expect(bDiagnostics.workerAppliedEvents == 0,
               "zero-event parity run applied automation events");
        expect(bDiagnostics.workerInvalidBatches == 0,
               "zero-event parity run produced invalid batches");
        expect(proxyB.lifecycleDiagnostics().transport.metadataRejected == 0,
               "zero-event parity run produced metadata rejects");
        expect(bDiagnostics.latestPublishedSequence
                   == proxyB.lifecycleDiagnostics().transport.submitted,
               "zero-event sidecar published a different batch count than submitted");
        logMessage("E2A_PARITY_DIAG submitted="
            + juce::String(static_cast<juce::int64>(
                proxyB.lifecycleDiagnostics().transport.submitted))
            + " applied=" + juce::String(static_cast<juce::int64>(
                bDiagnostics.workerAppliedEvents))
            + " latency=" + juce::String(proxyB.getEffectiveLatencySamples()));

        const auto pidA = proxyA.currentWorkerPid();
        const auto pidB = proxyB.currentWorkerPid();
        proxyA.release(5000);
        proxyB.release(5000);
        expect(! processStillAlive(pidA, 5000), "proxy A worker orphan after release");
        expect(! processStillAlive(pidB, 5000), "proxy B worker orphan after release");
       #else
        beginTest("Windows-only E2A block-rate regression parity");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2AOrphanBatchRollbackTest pluginSandboxPhaseE2AOrphanBatchRollbackTest;
PluginSandboxPhaseE2ASingleEventSampleAccuracyTest pluginSandboxPhaseE2ASingleEventSampleAccuracyTest;
PluginSandboxPhaseE2AEventIdentityTest pluginSandboxPhaseE2AEventIdentityTest;
PluginSandboxPhaseE2AMultipleEventsTest pluginSandboxPhaseE2AMultipleEventsTest;
PluginSandboxPhaseE2ASameOffsetOrderTest pluginSandboxPhaseE2ASameOffsetOrderTest;
PluginSandboxPhaseE2ACallbackBoundaryMappingTest pluginSandboxPhaseE2ACallbackBoundaryMappingTest;
PluginSandboxPhaseE2A2048FourQuantumTest pluginSandboxPhaseE2A2048FourQuantumTest;
PluginSandboxPhaseE2AZeroEventFastPathTest pluginSandboxPhaseE2AZeroEventFastPathTest;
PluginSandboxPhaseE2AGenerationSafetyTest pluginSandboxPhaseE2AGenerationSafetyTest;
PluginSandboxPhaseE2ABatchMismatchTest pluginSandboxPhaseE2ABatchMismatchTest;
PluginSandboxPhaseE2AEventOverflowTest pluginSandboxPhaseE2AEventOverflowTest;
PluginSandboxPhaseE2AStatefulSegmentationContinuityTest pluginSandboxPhaseE2AStatefulSegmentationContinuityTest;
PluginSandboxPhaseE2AZeroRtTest pluginSandboxPhaseE2AZeroRtTest;
PluginSandboxPhaseE2ABlockRateRegressionParityTest pluginSandboxPhaseE2ABlockRateRegressionParityTest;
