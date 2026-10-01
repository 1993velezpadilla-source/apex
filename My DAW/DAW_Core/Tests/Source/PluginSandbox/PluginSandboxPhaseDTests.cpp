#include <JuceHeader.h>

#include "../../../Source/AppCore/ApplicationCore.h"
#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAudioTransportCore.h"

#include <chrono>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <tlhelp32.h>
#endif

// ═══════════════════════════════════════════════════════════════════════════
// PHASE D — worker crash/hang containment + automatic detection/restart.
//
// These tests drive the PUBLIC production control-plane path:
//     PluginChainCore::pollSandboxHealth()
//         → SandboxedPluginProxyCore::pollHealthAndRecover()
// The realtime path (chain.processBlock) runs concurrently exactly as in
// production. No internal "restart now" helper is invoked.
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
constexpr int kBlockSamples = 512;
constexpr int kFixtureLatencySamples = 32;
constexpr int kEffectiveLatency = 1056;   // 32 + 2Q, Q=512, Bmax=512

// Phase D fault sentinels (exact bit patterns used by the fault fixture).
constexpr std::uint32_t kCrashSentinelBits = 0x7FC0CAFEu;
constexpr std::uint32_t kHangSentinelBits  = 0x7FC0BEEFu;

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

juce::PluginDescription stereoFixtureDescription()
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
    description.uniqueId = 2021071337;
    description.deprecatedUid = 1612553221;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
    return description;
}

bool processStillAlive(DWORD pid, std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle == nullptr)
        return false;   // cannot open -> already reaped

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

/** Single-shot OS liveness query — performs the check exactly once even for
    a zero timeout (unlike processStillAlive, whose polling loop never runs
    when timeoutMs == 0). */
bool isProcessAliveNow(DWORD pid)
{
   #if JUCE_WINDOWS
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle == nullptr)
        return false;   // cannot open -> already reaped
    DWORD exitCode = STILL_ACTIVE;
    const bool alive = GetExitCodeProcess(handle, &exitCode) != FALSE
                    && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
   #else
    juce::ignoreUnused(pid);
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

/** Submit one 512-sample constant block and return the value at a sample. */
float processConstantAndRead(const DAW::PluginChainCore& chain, float value, int sample)
{
    juce::AudioBuffer<float> block(2, kBlockSamples);
    block.clear();
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill(block.getWritePointer(ch), value, kBlockSamples);
    const_cast<DAW::PluginChainCore&>(chain).processBlock(block, kBlockSamples);
    return block.getSample(0, juce::jlimit(0, kBlockSamples - 1, sample));
}

/** Steady-state remote proof for a WARM chain: pump several constant-1.0
    blocks (flushing the ~1056-sample pipeline), keeping the worker caught up
    between blocks, then assert the final block is entirely wet (0.5). Avoids
    the fresh-impulse timeline race that proveRemoteGainHalf has on an
    already-running chain. */
bool proveWarmRemoteWet(DAW::PluginChainCore& chain,
                        const DAW::SandboxedPluginProxyCore& proxy)
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
        if (std::abs(block.getSample(0, s) - 0.5f) > 0.0005f)
            return false;
    return true;
}

/** Drive the PUBLIC production control-plane health service until the proxy
    is Healthy with a changed worker PID, or the deadline expires. Returns
    true on success. */
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

struct FaultFixtureIdentity
{
    int uniqueId = 0;
    int deprecatedUid = 0;
};

/** Runtime discovery of the fault fixture identity via a throwaway proxy.
    The worker create response reports the exact TUID-derived IDs; nothing is
    guessed. */
FaultFixtureIdentity discoverFaultFixtureIdentity(const juce::File& worker)
{
    FaultFixtureIdentity result;
    const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_VST3_FAULT_PATH", {}));
    if (! worker.existsAsFile() || ! fixture.isDirectory())
        return result;

    juce::PluginDescription description;
    description.name = "APEX Test VST3 Fault";
    description.descriptiveName = description.name;
    description.manufacturerName = "APEX";
    description.version = "1.0.0";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = fixture.getFullPathName();
    description.uniqueId = 0;      // unknown — the worker reports it
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

juce::PluginDescription faultFixtureDescription(const FaultFixtureIdentity& identity)
{
    const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_VST3_FAULT_PATH", {}));
    juce::PluginDescription description;
    description.name = "APEX Test VST3 Fault";
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

void preparePhaseDPreparation(const juce::File& worker,
                              DAW::SandboxedPluginProxyCore::Preparation& preparation)
{
    preparation.blockSamples = kBlockSamples;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    preparation.workerExecutablePathForTest = worker.getFullPathName();
   #endif
}

/** Healthy-path proof: impulse in block 1 → gain 0.5 at the effective
    latency sample in block 3 (identical timeline to the frozen Phase C
    stereo fixture). Returns true when verified. */
bool proveRemoteGainHalf(DAW::PluginChainCore& chain,
                         const DAW::SandboxedPluginProxyCore& proxy)
{
    for (int b = 1; b <= 4; ++b)
    {
        juce::AudioBuffer<float> block(2, kBlockSamples);
        block.clear();
        if (b == 1)
            block.setSample(0, 0, 1.0f);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, static_cast<std::uint64_t>(b), 5000);
        if (b == 3)
        {
            if (std::abs(block.getSample(0, kFixtureLatencySamples) - 0.5f) > 0.0001f)
                return false;
        }
    }
    return true;
}
} // namespace

// ── Phase D Test 1: unexpected external worker death auto-recovery ─────────
class PluginSandboxPhaseDWorkerDeathRestartTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDWorkerDeathRestartTest()
        : juce::UnitTest("plugin.sandbox.phase-d.worker-death-restart.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("external worker death is auto-detected and restarted");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(),
               "stereo fixture bundle is missing");
        if (! worker.existsAsFile()
            || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        // Healthy remote proof.
        expect(proveRemoteGainHalf(chain, *proxy), "healthy remote gain 0.5 proof failed");

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const auto mappingA = proxy->processDiagnostics().sharedMemoryName;
        const int latencyBefore = proxy->getEffectiveLatencySamples();
        expectEquals(latencyBefore, kEffectiveLatency, "unexpected pre-failure latency");
        expect(pidA != 0 && generationA != 0, "healthy worker identity missing");

        // Unexpected death from the TEST control plane — no graceful path.
        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");

        // RT path stays alive and falls back to deterministic dry output.
        bool dryFallbackObserved = false;
        for (int b = 0; b < 64 && ! dryFallbackObserved; ++b)
        {
            const float sample = processConstantAndRead(chain, 1.0f, 0);
            dryFallbackObserved = std::abs(sample - 1.0f) <= 0.0001f;
            Sleep(5);
        }
        expect(dryFallbackObserved, "deterministic dry fallback never observed after death");

        // PUBLIC production control-plane recovery path.
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "automatic recovery never reached Healthy with a new PID");

        const auto recovery = proxy->recoveryDiagnostics();
        expect(recovery.detectedDeaths >= 1, "detectedDeaths did not increment");
        expect(recovery.restartAttempts >= 1, "restartAttempts did not increment");
        expect(recovery.restartSuccesses >= 1, "restartSuccesses did not increment");
        const auto pidB = proxy->currentWorkerPid();
        const auto generationB = proxy->currentGeneration();
        const auto mappingB = proxy->processDiagnostics().sharedMemoryName;
        expect(pidB != pidA, "replacement worker reused PID A");
        expect(generationB != generationA, "replacement worker reused generation A");
        expect(mappingB != mappingA, "replacement worker reused mapping A");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingA),
               "old generation-A mapping survived retirement");
        expectEquals(proxy->getEffectiveLatencySamples(), kEffectiveLatency,
                     "latency changed during recovery");

        // Remote processing genuinely resumes through the replacement worker.
        expect(proveRemoteGainHalf(chain, *proxy),
               "replacement worker remote gain 0.5 proof failed");

        // Clean final removal; no orphans from either generation.
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidA, 5000), "PID A orphan after removal");
        expect(! processStillAlive(pidB, 5000), "PID B orphan after removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingB),
               "replacement mapping outlived removal");
       #else
        beginTest("Windows-only Phase D death recovery");
        expect(true);
       #endif
    }
};

// ── Phase D Test 2: real plugin-induced worker crash containment ───────────
class PluginSandboxPhaseDPluginCrashContainmentTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDPluginCrashContainmentTest()
        : juce::UnitTest("plugin.sandbox.phase-d.plugin-crash-containment.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("real VST3 crash inside the worker is contained and recovered");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverFaultFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "fault fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;
        logMessage("APEX_FAULT_FIXTURE_IDS uniqueId="
                   + juce::String(identity.uniqueId)
                   + " deprecatedUid=" + juce::String(identity.deprecatedUid));

        const auto description = faultFixtureDescription(identity);
        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "fault fixture insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "fault fixture sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proveRemoteGainHalf(chain, *proxy),
               "fault fixture normal remote gain 0.5 proof failed");

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        const int latencyBefore = proxy->getEffectiveLatencySamples();
        expect(pidA != 0, "fault worker PID missing");

        // Send the exact crash sentinel through the REAL audio path.
        {
            juce::AudioBuffer<float> block(2, kBlockSamples);
            block.clear();
            block.setSample(0, 0, bitsToFloat(kCrashSentinelBits));
            chain.processBlock(block, kBlockSamples);
        }

        // Keep the RT path alive until the worker actually dies; bounded.
        const auto deathDeadline = GetTickCount64() + 15000;
        bool workerDied = false;
        while (GetTickCount64() < deathDeadline && ! workerDied)
        {
            processConstantAndRead(chain, 1.0f, 0);
            workerDied = ! isProcessAliveNow(pidA);
            Sleep(5);
        }
        expect(workerDied, "fault sentinel did not crash the worker process");
        if (! workerDied)
            return;
        expect(true, "parent test process survived the worker crash");

        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "crash recovery never reached Healthy with a new PID");

        const auto recovery = proxy->recoveryDiagnostics();
        expect(recovery.detectedDeaths >= 1, "crash did not increment detectedDeaths");
        expect(recovery.restartSuccesses >= 1, "crash recovery did not succeed");
        expect(proxy->currentWorkerPid() != pidA, "crashed PID reused after recovery");
        expect(proxy->currentGeneration() != generationA, "crashed generation reused");
        expectEquals(proxy->getEffectiveLatencySamples(), latencyBefore,
                     "latency changed across crash recovery");

        expect(proveRemoteGainHalf(chain, *proxy),
               "post-crash replacement worker gain 0.5 proof failed");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidA, 5000), "crashed worker orphan");
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only Phase D crash containment");
        expect(true);
       #endif
    }
};

// ── Phase D Test 3: real plugin-induced hang watchdog ──────────────────────
class PluginSandboxPhaseDPluginHangRestartTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDPluginHangRestartTest()
        : juce::UnitTest("plugin.sandbox.phase-d.plugin-hang-restart.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("real VST3 hang inside the worker is watchdog-terminated and recovered");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverFaultFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "fault fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        const auto description = faultFixtureDescription(identity);
        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "fault fixture insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "fault fixture sandbox proxy missing");
        if (proxy == nullptr)
            return;

        // Short TEST-ONLY hang window through the guarded hook; the
        // production default (6000 ms) is untouched.
        proxy->setPollIntervalMsForTest(50);
        proxy->setHangWindowMsForTest(500);

        expect(proveRemoteGainHalf(chain, *proxy),
               "fault fixture normal remote gain 0.5 proof failed");

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();

        // Send the exact hang sentinel through the REAL audio path.
        {
            juce::AudioBuffer<float> block(2, kBlockSamples);
            block.clear();
            block.setSample(0, 0, bitsToFloat(kHangSentinelBits));
            chain.processBlock(block, kBlockSamples);
        }

        // The worker process must remain ALIVE (this is a hang, not a crash).
        expect(processStillAlive(pidA, 1000),
               "hang sentinel unexpectedly killed the worker (wanted a hang)");

        // Continue parent callbacks + drive the PUBLIC production service.
        const auto deadline = GetTickCount64() + 45000;
        bool recovered = false;
        while (GetTickCount64() < deadline && ! recovered)
        {
            processConstantAndRead(chain, 1.0f, 0);
            chain.pollSandboxHealth();
            recovered = proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                     && proxy->remoteAvailable()
                     && proxy->currentWorkerPid() != pidA;
            Sleep(25);
        }
        expect(recovered, "hang watchdog recovery never completed");

        const auto recovery = proxy->recoveryDiagnostics();
        expect(recovery.detectedStalls >= 1, "hang did not increment detectedStalls");
        expect(recovery.forcedTerminations >= 1, "hang recovery did not force-terminate");
        expect(recovery.restartSuccesses >= 1, "hang recovery did not succeed");
        expect(proxy->currentGeneration() != generationA, "hung generation reused");
        expect(! processStillAlive(pidA, 5000), "hung worker PID A orphan after recovery");

        expect(proveRemoteGainHalf(chain, *proxy),
               "post-hang replacement worker gain 0.5 proof failed");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only Phase D hang recovery");
        expect(true);
       #endif
    }
};

// ── Phase D Test 4: idle worker is never classified hung ───────────────────
class PluginSandboxPhaseDIdleNotHangTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDIdleNotHangTest()
        : juce::UnitTest("plugin.sandbox.phase-d.idle-not-hang.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("idle worker survives the health service without a false hang");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(),
               "stereo fixture bundle is missing");
        if (! worker.existsAsFile()
            || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        // Aggressive test-only hang window — any false positive would fire.
        proxy->setPollIntervalMsForTest(25);
        proxy->setHangWindowMsForTest(300);

        expect(proveRemoteGainHalf(chain, *proxy), "healthy remote proof failed");
        waitForWorkerQuanta(*proxy, 4, 5000);

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();

        // STOP submitting audio entirely, then keep servicing health for
        // several multiples of the hang window.
        const auto deadline = GetTickCount64() + 3000;
        while (GetTickCount64() < deadline)
        {
            chain.pollSandboxHealth();
            Sleep(50);
        }

        const auto recovery = proxy->recoveryDiagnostics();
        expect(proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "idle worker left Healthy state");
        expectEquals(static_cast<juce::int64>(recovery.detectedStalls),
                     static_cast<juce::int64>(0),
                     "idle worker falsely classified hung");
        expectEquals(static_cast<juce::int64>(recovery.restartAttempts),
                     static_cast<juce::int64>(0),
                     "idle worker triggered a restart");
        expect(proxy->currentWorkerPid() == pidA, "idle worker PID changed");
        expect(proxy->currentGeneration() == generationA, "idle worker generation changed");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidA, 5000), "idle worker orphan after removal");
       #else
        beginTest("Windows-only Phase D idle test");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseDWorkerDeathRestartTest pluginSandboxPhaseDWorkerDeathRestartTest;
PluginSandboxPhaseDPluginCrashContainmentTest pluginSandboxPhaseDPluginCrashContainmentTest;
PluginSandboxPhaseDPluginHangRestartTest pluginSandboxPhaseDPluginHangRestartTest;
PluginSandboxPhaseDIdleNotHangTest pluginSandboxPhaseDIdleNotHangTest;

// ── Phase D Test 5: worker failure during variable callbacks / 2048 ─────────
class PluginSandboxPhaseDVariableBlockRecoveryTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDVariableBlockRecoveryTest()
        : juce::UnitTest("plugin.sandbox.phase-d.variable-block-recovery.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("variable host callbacks survive worker failure at Bmax=2048");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, 2048);   // Bmax = 2048, Q stays 512 → d=4
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);   // Q=512
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expectEquals(proxy->getPreparedBlockSamples(), 512, "Q changed");
        expectEquals(proxy->getPreparedMaximumHostBlockSamples(), 2048, "Bmax changed");
        expectEquals(proxy->getPreparedOutstandingDepth(), 4, "depth changed");
        expectEquals(proxy->getEffectiveLatencySamples(), 2592, "prepared 2592 latency");
        const int latencyPrepared = proxy->getEffectiveLatencySamples();

        const int sizes[] = { 64, 128, 256, 480, 512, 1024, 2048 };

        // Wait until every submitted quantum is complete (idle drain). In
        // steady state the worker finishes all pending work when the host
        // stops submitting; only then is the next block's whole output
        // guaranteed remote (otherwise a later exchange inside a large block
        // deterministically falls back to dry 1.0 — correct behavior, but not
        // what the steady-state check asserts).
        auto waitFullyDrained = [&]
        {
            for (int i = 0; i < 5000; ++i)
            {
                if (proxy->lifecycleDiagnostics().transport.submitted
                    <= proxy->workerCompletedSequence())
                    return true;
                Sleep(1);
            }
            return false;
        };

        // Feed constant 1.0 in 512-sample blocks until at least targetSamples
        // have been ingested (beyond the (d+1)*Q = 2560-sample pre-roll plus
        // the 32-sample plugin delay the output must be constant 0.5).
        auto pumpWarm = [&](int targetSamples)
        {
            int fed = 0;
            for (int b = 0; b < 64 && fed < targetSamples; ++b)
            {
                juce::AudioBuffer<float> block(2, 512);
                for (int ch = 0; ch < 2; ++ch)
                    juce::FloatVectorOperations::fill(block.getWritePointer(ch), 1.0f, 512);
                chain.processBlock(block, 512);
                fed += 512;
                waitForWorkerQuanta(*proxy, proxy->workerCompletedSequence() + 1, 5000);
            }
            return waitFullyDrained();
        };

        // After a full drain every sample of a full-size block must be 0.5.
        auto checkSteadyWet = [&](int blockSize)
        {
            if (! waitFullyDrained())
                return false;
            juce::AudioBuffer<float> block(2, blockSize);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill(block.getWritePointer(ch), 1.0f, blockSize);
            chain.processBlock(block, blockSize);
            for (int s = 0; s < blockSize; ++s)
                if (std::abs(block.getSample(0, s) - 0.5f) > 0.0005f)
                    return false;
            return true;
        };

        pumpWarm(6144);   // well past 2592-sample priming timeline
        bool steady = true;
        for (const int size : sizes)
            steady = steady && checkSteadyWet(size);
        expect(steady, "variable-size steady-state wet 0.5 not reached before failure");

        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");

        // Outage provenance: every sample must be WHOLE — exactly the remote
        // value 0.5 (already-produced quanta draining from the ring) or the
        // dry value 1.0. Any in-between value would be mixed provenance.
        bool wholeProvenance = true;
        for (const int size : sizes)
        {
            for (int b = 0; b < 8; ++b)
            {
                juce::AudioBuffer<float> block(2, size);
                for (int ch = 0; ch < 2; ++ch)
                    juce::FloatVectorOperations::fill(block.getWritePointer(ch), 1.0f, size);
                chain.processBlock(block, size);
                for (int s = 0; s < size; ++s)
                {
                    const float v = block.getSample(0, s);
                    const bool remoteValue = std::abs(v - 0.5f) <= 0.0005f;
                    const bool dryValue    = std::abs(v - 1.0f) <= 0.0005f;
                    if (! remoteValue && ! dryValue)
                        wholeProvenance = false;
                }
            }
        }
        expect(wholeProvenance, "mixed provenance observed during variable-size outage");

        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "variable-block recovery never reached Healthy");

        expectEquals(proxy->getPreparedBlockSamples(), 512, "Q changed after recovery");
        expectEquals(proxy->getPreparedMaximumHostBlockSamples(), 2048, "Bmax changed after recovery");
        expectEquals(proxy->getPreparedOutstandingDepth(), 4, "depth changed after recovery");
        expectEquals(proxy->getEffectiveLatencySamples(), latencyPrepared,
                     "latency moved during variable-block recovery");
        expect(proxy->currentGeneration() != generationA, "generation reused after recovery");

        // The replacement worker re-primes its full pre-roll; re-warm and
        // re-verify every callback size returns to steady remote 0.5.
        pumpWarm(6144);
        steady = true;
        for (const int size : sizes)
            steady = steady && checkSteadyWet(size);
        expect(steady, "variable-size remote 0.5 did not resume after recovery");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only Phase D variable-block recovery");
        expect(true);
       #endif
    }
};

// ── Phase D Test 6: two workers / failure isolation ─────────────────────────
class PluginSandboxPhaseDTwoWorkerIsolationTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDTwoWorkerIsolationTest()
        : juce::UnitTest("plugin.sandbox.phase-d.two-worker-isolation.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("killing worker A never disturbs worker B");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chainA;
        DAW::PluginChainCore chainB;
        chainA.prepare(48000.0, kBlockSamples);
        chainB.prepare(48000.0, kBlockSamples);
        juce::String errorA, errorB;
        DAW::SandboxedPluginProxyCore::Preparation preparationA, preparationB;
        preparePhaseDPreparation(worker, preparationA);
        preparePhaseDPreparation(worker, preparationB);
        expectEquals(chainA.appendSandboxedPlugin(description, errorA, preparationA), 0,
                     "chain A insertion failed: " + errorA);
        expectEquals(chainB.appendSandboxedPlugin(description, errorB, preparationB), 0,
                     "chain B insertion failed: " + errorB);
        const auto* proxyA = chainA.getSlot(0)->getSandboxProxy();
        const auto* proxyB = chainB.getSlot(0)->getSandboxProxy();
        expect(proxyA != nullptr && proxyB != nullptr, "sandbox proxies missing");
        if (proxyA == nullptr || proxyB == nullptr)
            return;

        expect(proveRemoteGainHalf(chainA, *proxyA), "chain A healthy proof failed");
        expect(proveRemoteGainHalf(chainB, *proxyB), "chain B healthy proof failed");

        const auto pidB = proxyB->currentWorkerPid();
        const auto generationB = proxyB->currentGeneration();
        const auto mappingB = proxyB->processDiagnostics().sharedMemoryName;
        const auto fallbacksBefore = proxyB->lifecycleDiagnostics().transport.fallbacks;

        killProcessUnexpectedly(proxyA->currentWorkerPid());
        expect(! processStillAlive(proxyA->currentWorkerPid(), 5000), "worker A did not die");

        // Drive ONLY chain A's health service to recovery while B keeps
        // processing remote audio uninterrupted.
        const auto deadline = GetTickCount64() + 30000;
        bool aRecovered = false;
        while (GetTickCount64() < deadline && ! aRecovered)
        {
            expect(proveWarmRemoteWet(chainB, *proxyB), "worker B remote processing interrupted");
            chainA.pollSandboxHealth();
            aRecovered = proxyA->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                      && proxyA->remoteAvailable()
                      && proxyA->currentWorkerPid() != 0;
            Sleep(25);
        }
        expect(aRecovered, "worker A never recovered");

        expect(proxyB->currentWorkerPid() == pidB, "worker B PID changed");
        expect(proxyB->currentGeneration() == generationB, "worker B generation changed");
        expect(proxyB->processDiagnostics().sharedMemoryName == mappingB,
               "worker B mapping/session changed");
        expect(proxyB->recoveryDiagnostics().detectedDeaths == 0,
               "worker B observed an unrelated death");
        expect(proxyB->recoveryDiagnostics().restartAttempts == 0,
               "worker B restarted unexpectedly");
        expect(proxyB->lifecycleDiagnostics().transport.fallbacks == fallbacksBefore,
               "worker B fell back during worker A's outage");
        expect(proveWarmRemoteWet(chainB, *proxyB), "worker B output degraded");

        const auto pidANew = proxyA->currentWorkerPid();
        chainA.removePlugin(0);
        chainB.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidANew, 5000), "worker A replacement orphan");
        expect(! processStillAlive(pidB, 5000), "worker B orphan");
       #else
        beginTest("Windows-only Phase D two-worker isolation");
        expect(true);
       #endif
    }
};

// ── Phase D Test 7: restart failure transaction ─────────────────────────────
class PluginSandboxPhaseDRestartFailureTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDRestartFailureTest()
        : juce::UnitTest("plugin.sandbox.phase-d.restart-failure.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("failed automatic recreation is transactional and recoverable");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proveRemoteGainHalf(chain, *proxy), "healthy remote proof failed");
        const auto pidA = proxy->currentWorkerPid();
        const int latencyBefore = proxy->getEffectiveLatencySamples();

        // Single automatic restart budget → one failure, then stable Failed.
        proxy->setRestartPolicyForTest(1, 60000);
        proxy->setNextRestartWorkerPathForTest("C:\\definitely-missing\\DAW_Core.exe");

        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");

        // Drive the PUBLIC production service to Failed.
        const auto deadline = GetTickCount64() + 30000;
        bool failed = false;
        while (GetTickCount64() < deadline && ! failed)
        {
            processConstantAndRead(chain, 1.0f, 0);
            chain.pollSandboxHealth();
            failed = proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Failed;
            Sleep(25);
        }
        expect(failed, "health never reached stable Failed after restart failure");

        const auto recovery = proxy->recoveryDiagnostics();
        expect(recovery.restartFailures >= 1, "restartFailures did not increment");
        expect(recovery.restartAttempts == 1, "restart attempts exceeded the test budget");
        expect(recovery.restartSuccesses == 0, "a failed restart reported success");
        expect(proxy->currentWorkerPid() == 0,
               "half-created replacement worker PID remains");
        expect(! proxy->remoteAvailable(), "remote reported available after failed restart");
        expectEquals(proxy->getEffectiveLatencySamples(), latencyBefore,
                     "latency moved during failed restart");

        // Fallback stays deterministic during Failed.
        const float dry = processConstantAndRead(chain, 1.0f, 0);
        expect(std::abs(dry - 1.0f) <= 0.0005f || std::abs(dry - 0.0f) <= 0.0005f,
               "fallback output is not deterministic dry during Failed");

        // Explicit valid control-plane reprepare recovers.
        proxy->clearRecoveryIncident();
        DAW::SandboxedPluginProxyCore::Preparation validPreparation;
        preparePhaseDPreparation(worker, validPreparation);
        expect(proxy->reprepare(validPreparation)
                   == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "explicit reprepare after failed restart did not succeed");
        expect(proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "explicit reprepare did not restore Healthy");
        expect(proveRemoteGainHalf(chain, *proxy),
               "remote gain 0.5 did not resume after explicit recovery");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only Phase D restart-failure");
        expect(true);
       #endif
    }
};

// ── Phase D Test 8: crash loop / circuit breaker ────────────────────────────
class PluginSandboxPhaseDRestartThrottleTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDRestartThrottleTest()
        : juce::UnitTest("plugin.sandbox.phase-d.restart-throttle.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("automatic restarts are bounded by the rolling-window throttle");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proveRemoteGainHalf(chain, *proxy), "healthy remote proof failed");
        const auto pidA = proxy->currentWorkerPid();

        // Shortened TEST policy: max 2 automatic restarts per window.
        proxy->setRestartPolicyForTest(2, 60000);

        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");

        // Repeatedly fail every automatic recreation through the real
        // process-start path; count actual attempts and assert boundedness.
        juce::int64 attemptsWhenFailed = -1;
        bool failed = false;
        for (int poll = 0; poll < 30 && ! failed; ++poll)
        {
            proxy->setNextRestartWorkerPathForTest("C:\\definitely-missing\\DAW_Core.exe");
            chain.pollSandboxHealth();
            if (proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Failed)
            {
                attemptsWhenFailed = static_cast<juce::int64>(
                    proxy->recoveryDiagnostics().restartAttempts);
                failed = true;
            }
            Sleep(25);
        }
        expect(failed, "throttle never promoted the incident to stable Failed");
        expectEquals(attemptsWhenFailed, static_cast<juce::int64>(2),
                     "automatic restart attempts were not bounded by the test policy");

        const auto recovery = proxy->recoveryDiagnostics();
        expect(recovery.restartFailures == recovery.restartAttempts,
               "every bounded attempt failed but counters disagree");
        expect(recovery.restartSuccesses == 0, "throttled recovery reported success");
        expect(! proxy->remoteAvailable(), "remote available while Failed");

        // No spawn storm: after Failed, further polls change nothing.
        const auto attemptsAtFail = recovery.restartAttempts;
        for (int i = 0; i < 10; ++i)
        {
            chain.pollSandboxHealth();
            Sleep(10);
        }
        expectEquals(static_cast<juce::int64>(proxy->recoveryDiagnostics().restartAttempts),
                     static_cast<juce::int64>(attemptsAtFail),
                     "restart attempts continued after throttle exhaustion");

        // Explicit control-plane reset + reprepare clears the breaker.
        proxy->clearRecoveryIncident();
        DAW::SandboxedPluginProxyCore::Preparation validPreparation;
        preparePhaseDPreparation(worker, validPreparation);
        expect(proxy->reprepare(validPreparation)
                   == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "explicit reprepare after throttle did not succeed");
        expect(proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy,
               "explicit reprepare did not restore Healthy after throttle");
        expect(proveRemoteGainHalf(chain, *proxy),
               "remote gain 0.5 did not resume after explicit throttle reset");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only Phase D restart-throttle");
        expect(true);
       #endif
    }
};

// ── Phase D Test 9: zero RT allocation / bounded callback through recovery ──
class PluginSandboxPhaseDZeroRtRecoveryTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDZeroRtRecoveryTest()
        : juce::UnitTest("plugin.sandbox.phase-d.zero-rt-recovery.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("parent RT callback allocates zero and stays bounded through recovery");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        // Warm the full path before any checker is constructed.
        for (int b = 0; b < 8; ++b)
            processConstantAndRead(chain, 1.0f, 0);
        expect(proveWarmRemoteWet(chain, *proxy), "healthy remote proof failed");

        std::chrono::steady_clock::duration maximumCallbackDuration { 0 };

        // Pre-allocate the RT block OUTSIDE every allocation checker: the
        // checker's malloc hooks throw on the first allocation, and buffer
        // construction must never run inside a checked scope (the frozen
        // zero-alloc contract covers the processBlock path itself).
        juce::AudioBuffer<float> timedBlock(2, kBlockSamples);

        auto processTimed = [&](int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    juce::FloatVectorOperations::fill(timedBlock.getWritePointer(ch),
                                                      1.0f, kBlockSamples);
                const auto before = std::chrono::steady_clock::now();
                chain.processBlock(timedBlock, kBlockSamples);
                const auto elapsed = std::chrono::steady_clock::now() - before;
                if (elapsed > maximumCallbackDuration)
                    maximumCallbackDuration = elapsed;
            }
        };

        // Phase 1 — Healthy: allocation-checked audio only.
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            processTimed(50);
        }

        // Phase 2 — outage + fallback: allocation-checked audio only.
        const auto pidA = proxy->currentWorkerPid();
        const auto generationA = proxy->currentGeneration();
        killProcessUnexpectedly(pidA);

        // Confirm death on the REAL process handle. TerminateProcess lands
        // asynchronously (hundreds of ms on this machine), and reopening by
        // PID can misreport; the owned handle is the authoritative source.
        {
            const auto deathDeadline = GetTickCount64() + 30000;
            bool dead = false;
            while (GetTickCount64() < deathDeadline && ! dead)
            {
                dead = proxy->pollLivenessForTest() != 1;   // not Alive
                if (! dead)
                    Sleep(5);
            }
            expect(dead, "worker process never died on the owned handle after kill");
        }

        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            processTimed(50);
        }

        // Phase 3 — failure → recovery: audio runs inside the checker while
        // control-plane polling stays OUTSIDE it. The Recovering state may be
        // short-lived (detection + retirement + restart can complete in one
        // poll), so the transition is proven by DURABLE diagnostics below,
        // never by externally catching the transient enum.
        const auto deadline = GetTickCount64() + 30000;
        bool healthy = false;
        while (GetTickCount64() < deadline && ! healthy)
        {
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                processTimed(10);
            }
            chain.pollSandboxHealth();
            // Durable transition predicate: Healthy AND remote AND a genuinely
            // NEW worker PID. Never exit on the pre-detection Healthy state.
            healthy = proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                   && proxy->remoteAvailable()
                   && proxy->currentWorkerPid() != pidA;
            Sleep(10);
        }
        expect(healthy, "recovery never returned to Healthy");

        // Durable evidence of a REAL completed recovery transition — no
        // dependence on observing a transient intermediate health state.
        const auto recovery = proxy->recoveryDiagnostics();
        expect(recovery.detectedDeaths >= 1, "no death was detected during zero-RT recovery");
        expect(recovery.restartAttempts >= 1, "no restart attempt occurred");
        expect(recovery.restartSuccesses >= 1, "no successful restart occurred");
        expect(proxy->currentWorkerPid() != pidA, "replacement worker reused PID A");
        expect(proxy->currentGeneration() != generationA,
               "replacement worker reused generation A");

        // Phase 4 — replacement Healthy: allocation-checked audio only.
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            processTimed(50);
        }

        const auto maximumMilliseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(maximumCallbackDuration).count();
        logMessage("APEX_PHASE_D_MAX_CALLBACK_US max=" + juce::String(maximumMilliseconds));
        expect(maximumMilliseconds < 100000,   // 100 ms — generous; real work is memcpy-scale
               "parent callback duration suggests a wait during recovery");

        expect(proveWarmRemoteWet(chain, *proxy),
               "replacement worker remote proof failed");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only Phase D zero-RT recovery");
        expect(true);
       #endif
    }
};

// ── Phase D Test 10: recovery does not poison ordinary reprepare ────────────
class PluginSandboxPhaseDRecoveryThenReprepareTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDRecoveryThenReprepareTest()
        : juce::UnitTest("plugin.sandbox.phase-d.recovery-then-reprepare.v1",
                         "PluginSandboxPhaseD") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("automatic recovery then ordinary Phase C reprepare");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);   // Bmax=512 → latency 1056
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expectEquals(proxy->getEffectiveLatencySamples(), 1056, "expected 1056 before recovery");
        expect(proveRemoteGainHalf(chain, *proxy), "healthy remote proof failed");

        const auto pidA = proxy->currentWorkerPid();
        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");
        expect(driveRecoveryToHealthy(chain, *proxy, pidA, 30000),
               "automatic recovery never reached Healthy");
        expectEquals(proxy->getEffectiveLatencySamples(), 1056,
                     "latency moved during recovery (expected 1056)");
        expect(proveRemoteGainHalf(chain, *proxy),
               "remote 0.5 did not resume after recovery");

        // Ordinary Phase C reprepare: Bmax 512 → 2048 → 512. After the
        // 2048 reprepare the priming timeline is 2592 samples, so the proof
        // uses the steady-state warm-wet helper (constant 1.0 → whole 0.5).
        chain.prepare(48000.0, 2048);
        expectEquals(proxy->getEffectiveLatencySamples(), 2592,
                     "reprepare to 2048 did not publish 2592");
        expectEquals(proxy->getPreparedOutstandingDepth(), 4,
                     "reprepare to 2048 did not derive depth 4");
        expect(proveWarmRemoteWet(chain, *proxy),
               "remote 0.5 failed after reprepare to 2048");

        chain.prepare(48000.0, kBlockSamples);
        expectEquals(proxy->getEffectiveLatencySamples(), 1056,
                     "reprepare back to 512 did not restore 1056");
        expect(proveRemoteGainHalf(chain, *proxy),
               "remote 0.5 failed after reprepare back to 512");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only Phase D recovery-then-reprepare");
        expect(true);
       #endif
    }
};

// ── Phase D Test 11: production wiring proof ────────────────────────────────
class PluginSandboxPhaseDProductionWiringTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseDProductionWiringTest()
        : juce::UnitTest("plugin.sandbox.phase-d.production-wiring.v1",
                         "PluginSandboxPhaseD") {}

    // The full production traversal is:
    //
    //   MainComponent::inputWatchdogTick()            (2 s message-thread tick)
    //     → appCore_.serviceSandboxWorkers()          (ApplicationCore.h — inline)
    //       → chain->pollSandboxHealth()              (PluginChainCore.h/.cpp)
    //         → proxy->pollHealthAndRecover()         (SandboxedPluginProxyCore.h)
    //
    // MainComponent.cpp wiring (production, verified in tree):
    //   inputWatchdogTick() calls DAW::PluginChainCore::drainRetiredPlugins()
    //   and immediately after calls appCore_.serviceSandboxWorkers().
    //
    // ApplicationCore cannot be instantiated inside the APEXTests link set
    // (several of its value members have out-of-line ctors in app-only
    // translation units), so this test proves the DEEPEST linkable production
    // seams instead — the real PluginChainCore created through the exact
    // production creation sequence (mirrors ApplicationCore::getPluginChain),
    // and recovery driven exclusively through the real production chain
    // health service PluginChainCore::pollSandboxHealth(), which is the exact
    // method ApplicationCore::serviceSandboxWorkers() invokes in its body.
    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("production chain health service auto-recovers a sandboxed slot");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = stereoFixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        // Production chain creation sequence (ApplicationCore::getPluginChain):
        //   chain = std::make_shared<PluginChainCore>();
        //   chain->setPlayheadInfoCore(&pluginPlayheadInfoCore_);
        //   chain->setAutomationContext(trackId, ...);
        //   chain->prepare(currentSampleRate_, currentBlockSize_);
        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparePhaseDPreparation(worker, preparation);
        expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0,
                     "insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expect(proveRemoteGainHalf(chain, *proxy), "healthy remote proof failed");
        const auto pidA = proxy->currentWorkerPid();

        killProcessUnexpectedly(pidA);
        expect(! processStillAlive(pidA, 5000), "killed worker PID A is still alive");

        // Recovery is driven ONLY through the production chain health service
        // (the exact method ApplicationCore::serviceSandboxWorkers() calls).
        const auto deadline = GetTickCount64() + 30000;
        bool recovered = false;
        while (GetTickCount64() < deadline && ! recovered)
        {
            processConstantAndRead(chain, 1.0f, 0);
            chain.pollSandboxHealth();
            recovered = proxy->healthState() == DAW::SandboxedPluginProxyCore::HealthState::Healthy
                     && proxy->remoteAvailable()
                     && proxy->currentWorkerPid() != pidA;
            Sleep(25);
        }
        expect(recovered, "production chain health service never recovered the worker");
        const auto recovery = proxy->recoveryDiagnostics();
        expect(recovery.detectedDeaths >= 1, "detectedDeaths did not increment");
        expect(recovery.restartSuccesses >= 1, "restartSuccesses did not increment");
        expect(proveWarmRemoteWet(chain, *proxy),
               "remote 0.5 did not resume through the production health service");

        const auto pidB = proxy->currentWorkerPid();
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(pidB, 5000), "replacement worker orphan");
       #else
        beginTest("Windows-only Phase D production wiring");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseDVariableBlockRecoveryTest pluginSandboxPhaseDVariableBlockRecoveryTest;
PluginSandboxPhaseDTwoWorkerIsolationTest pluginSandboxPhaseDTwoWorkerIsolationTest;
PluginSandboxPhaseDRestartFailureTest pluginSandboxPhaseDRestartFailureTest;
PluginSandboxPhaseDRestartThrottleTest pluginSandboxPhaseDRestartThrottleTest;
PluginSandboxPhaseDZeroRtRecoveryTest pluginSandboxPhaseDZeroRtRecoveryTest;
PluginSandboxPhaseDRecoveryThenReprepareTest pluginSandboxPhaseDRecoveryThenReprepareTest;
PluginSandboxPhaseDProductionWiringTest pluginSandboxPhaseDProductionWiringTest;
