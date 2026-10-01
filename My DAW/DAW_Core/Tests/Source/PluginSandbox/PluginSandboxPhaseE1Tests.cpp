#include <JuceHeader.h>

#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAudioTransportCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxProtocolCore.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// ═══════════════════════════════════════════════════════════════════════════
// PHASE E1 — parameter metadata, static parameter control, state chunks,
// parent-side runtime shadow, and automatic-restart replay. CONTROL PLANE
// ONLY: no E1 operation ever runs on the realtime audio callback.
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
constexpr int kBlockSamples = 512;
constexpr int kEffectiveLatency = 1056;   // 32 + 2Q, Q=512, Bmax=512

std::size_t serializedMetadataPayloadBytes(
    const juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo>& metadata)
{
    std::size_t bytes = sizeof(std::int32_t); // automatable entry count
    for (const auto& entry : metadata)
    {
        bytes += sizeof(std::int32_t); // ordinal
        bytes += static_cast<std::size_t>(entry.parameterId.getNumBytesAsUTF8()) + 1;
        bytes += static_cast<std::size_t>(entry.name.getNumBytesAsUTF8()) + 1;
        bytes += sizeof(float);        // default value
        bytes += sizeof(std::int32_t); // step count
        bytes += sizeof(std::uint8_t); // flags
    }
    return bytes;
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

void killProcessUnexpectedly(DWORD pid)
{
   #if JUCE_WINDOWS
    HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (handle != nullptr)
    {
        TerminateProcess(handle, 0x80000001u);
        CloseHandle(handle);
    }
   #else
    juce::ignoreUnused(pid);
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

struct StateFixtureIdentity
{
    int uniqueId = 0;
    int deprecatedUid = 0;
};

/** Runtime discovery of the state fixture identity via a throwaway probe. */
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

void prepareE1Preparation(const juce::File& worker,
                          DAW::SandboxedPluginProxyCore::Preparation& preparation)
{
    preparation.blockSamples = kBlockSamples;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    preparation.workerExecutablePathForTest = worker.getFullPathName();
   #endif
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

/** Steady-state amplitude proof: pump 7 constant-1.0 blocks (worker caught up
    between blocks), then assert the final block is entirely gain × 1.0. */
bool proveSteadyAmplitude(DAW::PluginChainCore& chain,
                          const DAW::SandboxedPluginProxyCore& proxy,
                          float expectedGain)
{
    juce::AudioBuffer<float> block(2, kBlockSamples);
    for (int b = 0; b < 7; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill(block.getWritePointer(ch), 1.0f, kBlockSamples);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy.workerCompletedSequence() + 1, 5000);
    }
    for (int s = 0; s < kBlockSamples; ++s)
        if (std::abs(block.getSample(0, s) - expectedGain) > 0.0005f)
            return false;
    return true;
}

/** Drive the PUBLIC production chain health service until the proxy is
    Healthy with a changed worker PID. */
bool driveRecoveryToHealthy(DAW::PluginChainCore& chain,
                            const DAW::SandboxedPluginProxyCore& proxy,
                            DWORD previousPid,
                            std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    const auto deadline = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < deadline)
    {
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

// ── E1 Test 1: parameter metadata ───────────────────────────────────────────
class PluginSandboxPhaseE1ParameterMetadataTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1ParameterMetadataTest()
        : juce::UnitTest("plugin.sandbox.phase-e.parameter-metadata.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("parent enumerates real worker parameter metadata");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;
        logMessage("APEX_STATE_FIXTURE_IDS uniqueId="
                   + juce::String(identity.uniqueId)
                   + " deprecatedUid=" + juce::String(identity.deprecatedUid));

        const auto description = stateFixtureDescription(identity);
        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;
        expect(slot->getProcessor() == nullptr,
               "parent owns a real AudioPluginInstance for the state fixture");

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> parameters;
        expect(proxy->fetchParameterMetadata(parameters, error),
               "fetchParameterMetadata failed: " + error);
        const auto payloadBytes = serializedMetadataPayloadBytes(parameters);
        const auto chunks = (payloadBytes
            + DAW::PluginSandboxWin32::kE1ChunkPayloadBytes - 1)
            / DAW::PluginSandboxWin32::kE1ChunkPayloadBytes;
        const auto finalChunkBytes = payloadBytes
            - (chunks - 1) * DAW::PluginSandboxWin32::kE1ChunkPayloadBytes;
        logMessage("APEX_E1_METADATA_PAYLOAD count=" + juce::String(parameters.size())
            + " payloadBytes=" + juce::String(static_cast<juce::int64>(payloadBytes))
            + " chunkPayloadBytes=" + juce::String(static_cast<int>(
                DAW::PluginSandboxWin32::kE1ChunkPayloadBytes))
            + " chunks=" + juce::String(static_cast<juce::int64>(chunks))
            + " finalChunkBytes=" + juce::String(static_cast<juce::int64>(finalChunkBytes)));
        // The real VST3 exposes the fixture's gain parameter plus the
        // VST3-required Bypass parameter — assert identity, never count.
        expect(parameters.size() >= 1, "state fixture reported no parameters");
        int gainSlot = -1;
        for (int i = 0; i < parameters.size(); ++i)
            if (parameters.getReference(i).parameterId == "param_0")
                gainSlot = i;
        expect(gainSlot >= 0, "state fixture gain parameter not enumerated");
        if (gainSlot >= 0)
        {
            const auto& gain = parameters.getReference(gainSlot);
            expect(std::abs(gain.defaultValue - 0.5f) < 0.0001f,
                   "parameter default value mismatch");
            expect(gain.name.isNotEmpty(),
                   "parameter display name missing");
        }

        const auto pid = proxy->currentWorkerPid();
        const auto mapping = proxy->processDiagnostics().sharedMemoryName;
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pid, 5000), "state fixture worker orphan");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mapping),
               "state fixture mapping outlived removal");
       #else
        beginTest("Windows-only E1 parameter metadata");
        expect(true);
       #endif
    }
};

// ── E1 Test 2: static parameter control ─────────────────────────────────────
class PluginSandboxPhaseE1ParameterControlTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1ParameterControlTest()
        : juce::UnitTest("plugin.sandbox.phase-e.parameter-control.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("host-originated static parameter control through the worker");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                 error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proveSteadyAmplitude(chain, *proxy, 0.5f),
               "default gain 0.5 not observed");

        expect(proxy->setParameter("param_0", 0.25f, error),
               "setParameter(0.25) failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "gain 0.25 not observed after control");

        expect(proxy->setParameter("param_0", 0.75f, error),
               "setParameter(0.75) failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.75f),
               "gain 0.75 not observed after control");

        // Parent shadow tracks the latest successfully applied value.
        const auto& shadow = proxy->lastAuthoritativeParameters();
        const auto found = shadow.find("param_0");
        expect(found != shadow.end(), "parameter shadow missing");
        if (found != shadow.end())
            expect(std::abs(found->second - 0.75f) < 0.0001f,
                   "parameter shadow does not match applied value");

        // A failed set must NOT corrupt the authoritative shadow.
        expect(! proxy->setParameter("does-not-exist", 0.9f, error),
               "unknown parameter ID unexpectedly succeeded");
        expect(error.isNotEmpty(), "failed set reported no error");
        {
            const auto& shadowAfterFailure = proxy->lastAuthoritativeParameters();
            const auto stillFound = shadowAfterFailure.find("param_0");
            expect(stillFound != shadowAfterFailure.end()
                       && std::abs(stillFound->second - 0.75f) < 0.0001f,
                   "failed set corrupted the authoritative shadow");
        }
        expect(proveSteadyAmplitude(chain, *proxy, 0.75f),
               "audio changed after a failed set");

        const auto pid = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pid, 5000), "worker orphan");
       #else
        beginTest("Windows-only E1 parameter control");
        expect(true);
       #endif
    }
};

// ── E1 Test 3: state roundtrip ──────────────────────────────────────────────
class PluginSandboxPhaseE1StateRoundtripTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1StateRoundtripTest()
        : juce::UnitTest("plugin.sandbox.phase-e.state-roundtrip.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("real plugin state chunk roundtrips through the worker");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                 error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proxy->setParameter("param_0", 0.25f, error), "set failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f), "0.25 not observed");

        juce::MemoryBlock captured;
        expect(proxy->captureState(captured, error),
               "captureState failed: " + error);
        expect(captured.getSize() > 0, "captured state is empty");

        expect(proxy->setParameter("param_0", 0.75f, error), "set failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.75f), "0.75 not observed");

        expect(proxy->restoreState(captured, error),
               "restoreState failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "state restore did not return gain to 0.25");

        const auto pid = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pid, 5000), "worker orphan");
       #else
        beginTest("Windows-only E1 state roundtrip");
        expect(true);
       #endif
    }
};

// ── E1 Test 4: invalid state ────────────────────────────────────────────────
class PluginSandboxPhaseE1InvalidStateTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1InvalidStateTest()
        : juce::UnitTest("plugin.sandbox.phase-e.invalid-state.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("invalid state chunks fail without corrupting the plugin");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                 error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        // Establish a known valid state.
        expect(proxy->setParameter("param_0", 0.25f, error), "set failed: " + error);
        juce::MemoryBlock valid;
        expect(proxy->captureState(valid, error), "captureState failed: " + error);

        // Oversized chunk is rejected by the parent-side bound BEFORE any
        // worker traffic — the control response path reports failure.
        juce::MemoryBlock oversized(DAW::PluginSandboxWin32::kE1MaximumPayloadBytes + 1,
                                    true);
        expect(! proxy->restoreState(oversized, error),
               "oversized state chunk unexpectedly succeeded");
        expect(error.isNotEmpty(), "oversized rejection reported no error");

        // Truncated chunk: valid framing, invalid CONTENT — the fixture's
        // setStateInformation validates length and rejects without corrupting
        // its current state.
        juce::MemoryBlock truncated(valid.getData(),
                                    juce::jmax<size_t>(1, valid.getSize() - 4));
        expect(proxy->restoreState(truncated, error),
               "well-framed truncated chunk was not transferable");
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "truncated chunk corrupted current state");

        // Bad magic: fixture rejects content, keeps current state.
        juce::MemoryBlock badMagic(valid);
        static_cast<std::uint8_t*>(badMagic.getData())[0] ^= 0xFFu;
        expect(proxy->restoreState(badMagic, error), "bad-magic chunk not transferable");
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "bad-magic chunk corrupted current state");

        // Worker remains alive and a subsequent VALID restore succeeds.
        expect(proxy->setParameter("param_0", 0.75f, error), "set failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.75f), "0.75 not observed");
        expect(proxy->restoreState(valid, error),
               "valid restore after invalid chunks failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "valid restore did not return to 0.25");

        const auto pid = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pid, 5000), "worker orphan after invalid state");
       #else
        beginTest("Windows-only E1 invalid state");
        expect(true);
       #endif
    }
};

// ── E1 Test 5: automatic restart state replay (KEY acceptance) ──────────────
class PluginSandboxPhaseE1RestartStateReplayTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1RestartStateReplayTest()
        : juce::UnitTest("plugin.sandbox.phase-e.restart-state-replay.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("Phase D automatic restart replays host-known configuration");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                 error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proxy->setParameter("param_0", 0.25f, error), "set failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "gain 0.25 not observed before the crash");

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const int latencyBefore = proxy->getEffectiveLatencySamples();

        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "automatic recovery never reached Healthy");

        const auto recovery = proxy->recoveryDiagnostics();
        expect(recovery.detectedDeaths >= 1, "detectedDeaths did not increment");
        expect(recovery.restartSuccesses >= 1, "restartSuccesses did not increment");
        expect(proxy->currentWorkerPid() != pidA, "replacement reused PID A");
        expect(proxy->currentGeneration() != generationA,
               "replacement reused generation A");
        expectEquals(proxy->getEffectiveLatencySamples(), latencyBefore,
                     "latency changed across state-replay recovery");

        // KEY: NO manual set after recovery — the replacement must come back
        // at the host-known 0.25, not the plugin default 0.5.
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "replacement worker did not restore host-known gain 0.25");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only E1 restart state replay");
        expect(true);
       #endif
    }
};

// ── E1 Test 6: latest parameter wins ────────────────────────────────────────
class PluginSandboxPhaseE1RestartLatestParameterTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1RestartLatestParameterTest()
        : juce::UnitTest("plugin.sandbox.phase-e.restart-latest-parameter.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("replay restores the latest applied parameter, not defaults");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                 error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proxy->setParameter("param_0", 0.25f, error), "set failed: " + error);
        expect(proxy->setParameter("param_0", 0.75f, error), "set failed: " + error);
        expect(proxy->setParameter("param_0", 0.33f, error), "set failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.33f),
               "latest gain 0.33 not observed before the crash");

        const auto pidA = proxy->currentWorkerPid();
        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "automatic recovery never reached Healthy");

        expect(proveSteadyAmplitude(chain, *proxy, 0.33f),
               "replacement restored the wrong gain (expected latest 0.33)");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only E1 latest-parameter replay");
        expect(true);
       #endif
    }
};

// ── E1 Test 7: two-worker state isolation ───────────────────────────────────
class PluginSandboxPhaseE1TwoWorkerStateIsolationTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1TwoWorkerStateIsolationTest()
        : juce::UnitTest("plugin.sandbox.phase-e.two-worker-state-isolation.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("per-instance state shadows never cross-contaminate");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chainA;
        DAW::PluginChainCore chainB;
        chainA.prepare(48000.0, kBlockSamples);
        chainB.prepare(48000.0, kBlockSamples);
        juce::String errorA, errorB;
        DAW::SandboxedPluginProxyCore::Preparation preparationA, preparationB;
        prepareE1Preparation(worker, preparationA);
        prepareE1Preparation(worker, preparationB);
        expectEquals(chainA.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                  errorA, preparationA), 0,
                     "chain A insertion failed: " + errorA);
        expectEquals(chainB.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                  errorB, preparationB), 0,
                     "chain B insertion failed: " + errorB);
        auto* proxyA = chainA.getSlot(0)->getSandboxProxy();
        auto* proxyB = chainB.getSlot(0)->getSandboxProxy();
        expect(proxyA != nullptr && proxyB != nullptr, "sandbox proxies missing");
        if (proxyA == nullptr || proxyB == nullptr)
            return;

        expect(proxyA->setParameter("param_0", 0.25f, errorA), "A set failed: " + errorA);
        expect(proxyB->setParameter("param_0", 0.75f, errorB), "B set failed: " + errorB);
        expect(proveSteadyAmplitude(chainA, *proxyA, 0.25f), "A 0.25 not observed");
        expect(proveSteadyAmplitude(chainB, *proxyB, 0.75f), "B 0.75 not observed");

        const auto pidB = proxyB->currentWorkerPid();
        const auto generationB = proxyB->currentGeneration();

        killProcessUnexpectedly(proxyA->currentWorkerPid());
        expect(! processStillAlive(proxyA->currentWorkerPid(), 5000), "worker A did not die");

        const auto deadline = GetTickCount64() + 30000;
        bool aRecovered = false;
        while (GetTickCount64() < deadline && ! aRecovered)
        {
            expect(proveSteadyAmplitude(chainB, *proxyB, 0.75f),
                   "worker B output degraded during A's outage");
            chainA.pollSandboxHealth();
            aRecovered = proxyA->healthState()
                             == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                      && proxyA->remoteAvailable()
                      && proxyA->currentWorkerPid() != 0;
            Sleep(25);
        }
        expect(aRecovered, "worker A never recovered");

        expect(proveSteadyAmplitude(chainA, *proxyA, 0.25f),
               "worker A did not restore its own 0.25");
        expect(proveSteadyAmplitude(chainB, *proxyB, 0.75f),
               "worker B state changed after A's recovery");
        expect(proxyB->currentWorkerPid() == pidB, "worker B PID changed");
        expect(proxyB->currentGeneration() == generationB, "worker B generation changed");

        const auto pidANew = proxyA->currentWorkerPid();
        chainA.removePlugin(0);
        chainB.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidANew, 5000), "worker A replacement orphan");
        expect(! processStillAlive(pidB, 5000), "worker B orphan");
       #else
        beginTest("Windows-only E1 two-worker state isolation");
        expect(true);
       #endif
    }
};

// ── E1 Test 8: control during outage ────────────────────────────────────────
class PluginSandboxPhaseE1ControlDuringOutageTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1ControlDuringOutageTest()
        : juce::UnitTest("plugin.sandbox.phase-e.control-during-outage.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("static control during a worker outage fails cleanly");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                 error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proxy->setParameter("param_0", 0.25f, error), "set failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f), "0.25 not observed");

        const auto pidA = proxy->currentWorkerPid();
        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");

        // Control during outage: fails cleanly; the authoritative shadow is
        // NOT mutated by a failed operation.
        const auto shadowBefore = proxy->lastAuthoritativeParameters();
        expect(! proxy->setParameter("param_0", 0.9f, error),
               "control during outage unexpectedly succeeded");
        expect(error.isNotEmpty(), "outage control failure reported no error");
        {
            const auto& shadowAfter = proxy->lastAuthoritativeParameters();
            const auto found = shadowAfter.find("param_0");
            expect(found != shadowAfter.end()
                       && std::abs(found->second - 0.25f) < 0.0001f,
                   "failed outage control corrupted the shadow");
        }

        // Recovery replays the intact 0.25 shadow.
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "automatic recovery never reached Healthy");
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "replacement did not restore the intact 0.25 shadow");

        // A valid control after recovery works.
        expect(proxy->setParameter("param_0", 0.5f, error),
               "valid control after recovery failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.5f), "0.5 not observed");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only E1 outage control");
        expect(true);
       #endif
    }
};

// ── E1 Test 9: zero RT control impact ───────────────────────────────────────
class PluginSandboxPhaseE1ZeroRtControlTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1ZeroRtControlTest()
        : juce::UnitTest("plugin.sandbox.phase-e.zero-rt-control.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("E1 control operations never allocate or block the RT path");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                 error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        // Warm up outside the checker.
        for (int b = 0; b < 8; ++b)
        {
            juce::AudioBuffer<float> block(2, kBlockSamples);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch), 1.0f, kBlockSamples);
            chain.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(*proxy, proxy->workerCompletedSequence() + 1, 5000);
        }

        juce::AudioBuffer<float> timedBlock(2, kBlockSamples);   // outside checkers

        // Control-plane operations interleaved with allocation-checked audio.
        for (int round = 0; round < 3; ++round)
        {
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                for (int b = 0; b < 20; ++b)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        juce::FloatVectorOperations::fill(
                            timedBlock.getWritePointer(ch), 1.0f, kBlockSamples);
                    chain.processBlock(timedBlock, kBlockSamples);
                }
            }
            // Control plane OUTSIDE the checker.
            const float target = round == 0 ? 0.25f : (round == 1 ? 0.75f : 0.5f);
            expect(proxy->setParameter("param_0", target, error),
                   "setParameter failed: " + error);
        }

        juce::MemoryBlock captured;
        expect(proxy->captureState(captured, error), "captureState failed: " + error);
        expect(proxy->restoreState(captured, error), "restoreState failed: " + error);

        const auto pid = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pid, 5000), "worker orphan");
       #else
        beginTest("Windows-only E1 zero-RT control");
        expect(true);
       #endif
    }
};

// ── E1 Test 10: recovery + reprepare with state ─────────────────────────────
class PluginSandboxPhaseE1StateRecoveryReprepareTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE1StateRecoveryReprepareTest()
        : juce::UnitTest("plugin.sandbox.phase-e.state-recovery-reprepare.v1",
                         "PluginSandboxPhaseE1") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("state survives recovery and frozen Phase C reprepare");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;
        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);   // Bmax=512 → latency 1056
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                 error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expectEquals(proxy->getEffectiveLatencySamples(), 1056,
                     "expected 1056 before recovery");
        expect(proxy->setParameter("param_0", 0.25f, error), "set failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f), "0.25 not observed");

        const auto pidA = proxy->currentWorkerPid();
        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "automatic recovery never reached Healthy");
        expectEquals(proxy->getEffectiveLatencySamples(), 1056,
                     "latency moved during recovery");
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "state lost across automatic recovery");

        // Frozen Phase C reprepare: Bmax 512 → 2048 → 512. The replay hook
        // re-applies the host-known configuration onto each fresh worker.
        chain.prepare(48000.0, 2048);
        expectEquals(proxy->getEffectiveLatencySamples(), 2592,
                     "reprepare to 2048 did not publish 2592");
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "state lost across reprepare to 2048");

        chain.prepare(48000.0, kBlockSamples);
        expectEquals(proxy->getEffectiveLatencySamples(), 1056,
                     "reprepare back to 512 did not restore 1056");
        expect(proveSteadyAmplitude(chain, *proxy, 0.25f),
               "state lost across reprepare back to 512");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only E1 recovery/reprepare state");
        expect(true);
       #endif
    }
};

// ── E2B seed gate: state restore/replay → live fetch → binding publication ──
class PluginSandboxPhaseE2BLiveSeedAfterStateRestoreTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BLiveSeedAfterStateRestoreTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.live-seed-after-state-restore.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("E2B seeds from the restored worker value and fails closed on live-fetch failure");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverStateFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "state fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        DAW::PluginChainCore chain;
        const juce::String trackId = "e2b-live-seed-track";
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(48000.0, kBlockSamples);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        prepareE1Preparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(stateFixtureDescription(identity),
                                                  error, preparation), 0,
                     "state fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr, "state fixture sandbox slot is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "state fixture metadata fetch failed: " + error);
        int parameterIndex = -1;
        juce::String parameterId;
        for (const auto& info : metadata)
            if (info.index == 0)
            {
                parameterIndex = info.index;
                parameterId = info.parameterId;
                break;
            }
        expectEquals(parameterIndex, 0, "state fixture ordinal-zero parameter is missing");
        expect(parameterId.isNotEmpty(), "state fixture parameter ID is missing");
        if (parameterIndex < 0 || parameterId.isEmpty())
            return;

        // Capture a valid non-default state, then move the worker elsewhere so
        // restoreState has an observable effect. The E1 parameter shadow is
        // deliberately left at the later value; it is not a live-value source.
        expect(proxy->setParameter(parameterId, 0.25f, error),
               "set restored gain failed: " + error);
        juce::MemoryBlock restoredState;
        expect(proxy->captureState(restoredState, error),
               "capture restored state failed: " + error);
        expect(restoredState.getSize() > 0, "captured restored state is empty");

        expect(proxy->setParameter(parameterId, 0.75f, error),
               "set shadow gain failed: " + error);
        expect(proveSteadyAmplitude(chain, *proxy, 0.75f),
               "worker did not reach the pre-restore 0.75 state");
        const auto shadowBeforeRestore = proxy->lastAuthoritativeParameters();
        const auto shadowIt = shadowBeforeRestore.find(parameterId);
        expect(shadowIt != shadowBeforeRestore.end(),
               "E1 parameter shadow lacks the restored fixture parameter");
        if (shadowIt != shadowBeforeRestore.end())
            expectWithinAbsoluteError(shadowIt->second, 0.75f, 0.0005f,
                                      "E1 shadow did not retain the later 0.75 value");

        expect(proxy->restoreState(restoredState, error),
               "state restore failed: " + error);
        float restoredWorkerGain = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, parameterIndex, restoredWorkerGain, error),
               "restored live-value fetch failed: " + error);
        expectWithinAbsoluteError(restoredWorkerGain, 0.25f, 0.0005f,
                                  "worker did not expose the restored 0.25 gain");

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        // This is the production ordering point after a state/replay
        // operation: rebuild the complete binding candidate from current
        // metadata + current live worker values, then publish the seed state.
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "valid live seed did not arm any E2B bindings");
        const float smootherInitial =
            slot->sandboxLastAutomationValueForTesting(parameterIndex);
        const float lastDeliveredInitial =
            slot->sandboxLastDeliveredValueForTesting(parameterIndex);
        expectWithinAbsoluteError(smootherInitial, restoredWorkerGain, 0.0005f,
                                  "E2B smoother seed differs from restored live worker value");
        expectWithinAbsoluteError(lastDeliveredInitial, restoredWorkerGain, 0.0005f,
                                  "E2B lastDelivered seed differs from restored live worker value");

        DAW::AutomationSnapshot snapshot;
        snapshot.lanes.push_back({
            trackId,
            "plugin.0." + slot->getPluginInstanceId() + "." + parameterId,
            true,
            {
                { 0,   0.25f, DAW::AutomationCurveType::Linear, 0.0f },
                { 512, 0.25f, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        const auto appliedBeforeSeededCallback =
            proxy->automationDiagnostics().workerAppliedEvents;
        const auto submittedBeforeSeededCallback =
            proxy->lifecycleDiagnostics().transport.submitted;
        juce::AudioBuffer<float> seededBlock(2, kBlockSamples);
        seededBlock.clear();
        chain.applyAutomationAtSample(trackId, &snapshot, 0,
                                       48000.0, 120.0, kBlockSamples);
        chain.processBlock(seededBlock, kBlockSamples);
        waitForWorkerQuanta(*proxy, submittedBeforeSeededCallback + 1, 5000);
        expectEquals(proxy->automationDiagnostics().workerAppliedEvents,
                     appliedBeforeSeededCallback,
                     "correctly seeded first callback emitted a fabricated catch-up event");

        float liveAfterSeededCallback = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, parameterIndex,
                                      liveAfterSeededCallback, error),
               "post-seed live-value fetch failed: " + error);
        expectWithinAbsoluteError(liveAfterSeededCallback, restoredWorkerGain, 0.0005f,
                                  "first seeded callback changed the restored worker value");

        // Real proxy seam: metadata remains valid, but the live-value read
        // fails. The complete E2B table must be disarmed rather than retaining
        // the old worker/session binding or inventing a default seed.
        proxy->setForceLiveParameterValuesFailureForTest(true);
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        expectEquals(slot->sandboxAutomationBindingCountForTesting(), 0,
                     "live-fetch failure retained stale E2B bindings");

        const auto appliedBeforeFailedSeed =
            proxy->automationDiagnostics().workerAppliedEvents;
        const auto submittedBeforeFailedSeed =
            proxy->lifecycleDiagnostics().transport.submitted;
        juce::AudioBuffer<float> failedSeedBlock(2, kBlockSamples);
        failedSeedBlock.clear();
        chain.applyAutomationAtSample(trackId, &snapshot, kBlockSamples,
                                       48000.0, 120.0, kBlockSamples);
        chain.processBlock(failedSeedBlock, kBlockSamples);
        waitForWorkerQuanta(*proxy, submittedBeforeFailedSeed + 1, 5000);
        expectEquals(proxy->automationDiagnostics().workerAppliedEvents,
                     appliedBeforeFailedSeed,
                     "failed live seed produced an E2B event");

        proxy->setForceLiveParameterValuesFailureForTest(false);
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "E2B bindings did not recover after live fetch became valid");
        expectWithinAbsoluteError(
            slot->sandboxLastAutomationValueForTesting(parameterIndex),
            restoredWorkerGain, 0.0005f,
            "recovered E2B smoother seed differs from restored live worker value");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(parameterIndex),
            restoredWorkerGain, 0.0005f,
            "recovered E2B lastDelivered seed differs from restored live worker value");

        // Observing live values and rebuilding E2B bindings must never rewrite
        // the separate E1 authoritative parameter shadow.
        const auto shadowAfterSeed = proxy->lastAuthoritativeParameters();
        const auto shadowAfterIt = shadowAfterSeed.find(parameterId);
        expect(shadowAfterIt != shadowAfterSeed.end(),
               "E1 shadow disappeared after live-value observation");
        if (shadowAfterIt != shadowAfterSeed.end())
            expectWithinAbsoluteError(shadowAfterIt->second, 0.75f, 0.0005f,
                                      "live-value observation mutated the E1 shadow");

        // Normal control-plane reprepare: the proxy creates a new worker,
        // replays state first and the newest E1 parameter shadow second. The
        // E2B binding/smoother seed must be rebuilt from that NEW worker before
        // this direct slot lifecycle call returns.
        const auto generationBeforeReprepare = proxy->currentGeneration();
        slot->prepare(48000.0, 2048);
        expect(slot->isPrepared(), "normal sandbox reprepare left the slot unprepared");
        expect(proxy->currentGeneration() != generationBeforeReprepare,
               "normal sandbox reprepare did not create a new worker generation");
        float reprepareWorkerGain = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, parameterIndex, reprepareWorkerGain, error),
               "post-reprepare live-value fetch failed: " + error);
        expectWithinAbsoluteError(reprepareWorkerGain, 0.75f, 0.0005f,
                                  "new worker did not receive the newest host parameter replay");
        expect(slot->sandboxAutomationBindingCountForTesting() > 0,
               "normal reprepare did not rebuild E2B bindings");
        expectWithinAbsoluteError(
            slot->sandboxLastAutomationValueForTesting(parameterIndex),
            reprepareWorkerGain, 0.0005f,
            "normal reprepare retained the previous E2B smoother seed");
        expectWithinAbsoluteError(
            slot->sandboxLastDeliveredValueForTesting(parameterIndex),
            reprepareWorkerGain, 0.0005f,
            "normal reprepare retained the previous E2B lastDelivered seed");

        logMessage("APEX_E2B_LIVE_SEED_DIAG restoredWorkerGain="
                   + juce::String(restoredWorkerGain, 4)
                   + " smootherInitial=" + juce::String(smootherInitial, 4)
                   + " lastDeliveredInitial=" + juce::String(lastDeliveredInitial, 4)
                   + " reprepareWorkerGain=" + juce::String(reprepareWorkerGain, 4)
                   + " shadowBeforeAfter=0.7500/"
                   + (shadowAfterIt != shadowAfterSeed.end()
                          ? juce::String(shadowAfterIt->second, 4) : "missing")
                   + " validBindings="
                   + juce::String(slot->sandboxAutomationBindingCountForTesting()));
       #else
        expect(false, "E2B live-seed test hooks are unavailable in this test build");
       #endif

        const auto pid = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pid, 5000), "state-seed worker orphan");
       #else
        beginTest("Windows-only E2B live seed after state restore");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE1ParameterMetadataTest pluginSandboxPhaseE1ParameterMetadataTest;
PluginSandboxPhaseE1ParameterControlTest pluginSandboxPhaseE1ParameterControlTest;
PluginSandboxPhaseE1StateRoundtripTest pluginSandboxPhaseE1StateRoundtripTest;
PluginSandboxPhaseE1InvalidStateTest pluginSandboxPhaseE1InvalidStateTest;
PluginSandboxPhaseE1RestartStateReplayTest pluginSandboxPhaseE1RestartStateReplayTest;
PluginSandboxPhaseE1RestartLatestParameterTest pluginSandboxPhaseE1RestartLatestParameterTest;
PluginSandboxPhaseE1TwoWorkerStateIsolationTest pluginSandboxPhaseE1TwoWorkerStateIsolationTest;
PluginSandboxPhaseE1ControlDuringOutageTest pluginSandboxPhaseE1ControlDuringOutageTest;
PluginSandboxPhaseE1ZeroRtControlTest pluginSandboxPhaseE1ZeroRtControlTest;
PluginSandboxPhaseE1StateRecoveryReprepareTest pluginSandboxPhaseE1StateRecoveryReprepareTest;
PluginSandboxPhaseE2BLiveSeedAfterStateRestoreTest pluginSandboxPhaseE2BLiveSeedAfterStateRestoreTest;
