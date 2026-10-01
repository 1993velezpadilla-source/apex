#include <JuceHeader.h>

#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAudioTransportCore.h"
#include "../../../Source/SoundEngineCore/ApexPluginPdcContractCore.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
// JUCE derives these PluginDescription IDs from the VST3 component CID
// ABCDEF019182FAEB4150585441545633. They are cached metadata; the parent does
// not load the module to obtain them.
constexpr int kFixtureUniqueId       = 2021071337;
constexpr int kFixtureDeprecatedUid  = 1612553221;
constexpr int kFixtureLatencySamples = 32;
constexpr int kBlockSamples          = 512;
constexpr int kReblockTransportLatency = 1024;   // 2Q with Q = 512, Bmax = 512
constexpr int kEffectiveLatency      = 1056;     // 32 + 1024
constexpr int kReblockTransportLatency2048 = 2560;   // 5Q with Q = 512, Bmax = 2048
constexpr int kEffectiveLatency2048  = 2592;     // 32 + 2560

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

/** Test-side bounded worker-completion polling. Runs OUTSIDE any production
    realtime processing path; the chain/proxy/reblocker never waits. */
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

/** Bounded test-side child-process exit check. Returns true while the worker
    is still alive after timeoutMs. */
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

/** Minimal known-safe deterministic in-process AudioPluginInstance.
    Exists only to prove the ordinary PluginInstanceCore InProcess path is
    unchanged by sandbox support. No worker, no shared memory, no delay. */
class ChainInProcessProbeProcessor final : public juce::AudioPluginInstance
{
public:
    ChainInProcessProbeProcessor()
        : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "APEX Chain InProcess Probe"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = juce::jmin(buffer.getNumSamples(), kBlockSamples);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
                d[i] *= 0.5f;
        }
        ++processCount;
    }

    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name = "APEX Chain InProcess Probe";
        d.descriptiveName = d.name;
        d.pluginFormatName = "APEX Test";
        d.manufacturerName = "APEX";
        d.version = "1";
        d.fileOrIdentifier = "APEX::ChainProbe::inprocess";
        d.uniqueId = 0x43485000;   // "CHP\0"
        d.deprecatedUid = 0x43485000;
        d.isInstrument = false;
        d.numInputChannels = 2;
        d.numOutputChannels = 2;
    }

    int processCount = 0;
};

// ── Test 1: real production PluginChain hosts a sandboxed real VST3 slot ──
class PluginSandboxRealVst3ChainIntegrationTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3ChainIntegrationTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-chain-integration.v1",
                         "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("real VST3 executes through the production PluginChain sandbox slot");

        // The sandbox deleter uses the message-thread retire queue contract.
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(worker.existsAsFile(),
               "production worker executable is missing: " + worker.getFullPathName());
        expect(juce::File(description.fileOrIdentifier).isDirectory(),
               "fixture bundle is missing: " + description.fileOrIdentifier);
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module was loaded in the parent before the chain test");
        if (! worker.existsAsFile()
            || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        const int slotIndex = chain.appendSandboxedPlugin(description, error, preparation);
        expectEquals(slotIndex, 0, error);
        if (slotIndex < 0)
            return;

        auto* slot = chain.getSlot(0);
        expect(slot != nullptr, "chain did not publish the sandboxed slot");
        if (slot == nullptr)
            return;

        // ── Structural placement ──
        expect(slot->isSandboxed(), "chain slot did not report sandboxed execution mode");
        expectEquals(static_cast<int>(slot->getExecutionMode()),
                     static_cast<int>(DAW::PluginExecutionMode::Sandboxed));
        expect(slot->getProcessor() == nullptr,
               "sandboxed slot exposed a parent AudioProcessor");
        expect(slot->getSandboxProxy() != nullptr,
               "sandboxed slot does not own its parent proxy");
        expectEquals(slot->getLatencySamples(), kEffectiveLatency);
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency);
        expectEquals(slot->getDescription().uniqueId, kFixtureUniqueId);
        expectEquals(slot->getDescription().deprecatedUid, kFixtureDeprecatedUid);
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "chain insertion loaded the fixture module in the parent");

        const auto* proxy = slot->getSandboxProxy();
        const auto processDiag = proxy->processDiagnostics();
        expect(processDiag.hostedPluginCreated,
               "worker did not create and prepare the fixture");
        expect(processDiag.workerProcessId != processDiag.parentProcessId,
               "sandbox worker PID equals the parent PID");
        expect(processDiag.workerProcessId != 0, "sandbox worker PID is zero");
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;
        expect(mappingName.isNotEmpty(), "sandbox shared mapping has no name");
        expect(DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox shared mapping missing while the slot is active");

        // ── Impulse through the real production chain path ──
        // Input impulse at global sample 0 -> fixture 0.5 gain + 32-sample
        // plugin delay -> global output sample 1024 + 32 = 1056 (block 2, offset 32).
        juce::AudioBuffer<float> block(2, kBlockSamples);
        block.clear();
        block.setSample(0, 0, 1.0f);
        block.setSample(1, 0, -1.0f);

        for (int blockIndex = 1; blockIndex <= 8; ++blockIndex)
        {
            if (blockIndex > 1)
                block.clear();

            if (blockIndex == 6)
            {
               #if JUCE_ENABLE_ALLOCATION_HOOKS
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kBlockSamples);
               #else
                expect(false, "JUCE allocation hooks are required for chain RT allocation proof");
                chain.processBlock(block, kBlockSamples);
               #endif
            }
            else
            {
                chain.processBlock(block, kBlockSamples);
            }

            // Test-side worker polling only; the chain/reblock path never waits.
            waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(blockIndex), 5000);

            if (blockIndex == 3)
            {
                expectWithinAbsoluteError(block.getSample(0, kFixtureLatencySamples),
                                          0.5f, 0.0f,
                                          "left chain impulse mismatch at effective latency");
                expectWithinAbsoluteError(block.getSample(1, kFixtureLatencySamples),
                                          -0.5f, 0.0f,
                                          "right chain impulse mismatch at effective latency");
                expectWithinAbsoluteError(block.getSample(0, kFixtureLatencySamples - 1),
                                          0.0f, 0.0f,
                                          "chain impulse arrived one sample early");
                expectWithinAbsoluteError(block.getSample(0, kFixtureLatencySamples + 1),
                                          0.0f, 0.0f,
                                          "chain impulse is wider than one sample");
            }
        }

        expect(proxy->workerCompletedSequence() >= 8,
               "worker did not complete the chain integration stream");

        const auto realtimeDiag = proxy->lifecycleDiagnostics();
        expectEquals(static_cast<juce::int64>(realtimeDiag.transport.boundsRejected),
                     juce::int64(0));
        expectEquals(static_cast<juce::int64>(realtimeDiag.transport.calls),
                     juce::int64(8));
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into the parent during chain processing");

        // ── Control-plane removal: retire, drain, reap ──
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived chain slot removal (orphan worker)");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox shared mapping outlived chain slot removal");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into the parent after chain removal");
       #else
        beginTest("Windows-only chain integration contract");
        expect(true);
       #endif
    }
};

// ── Test 2: variable host callbacks through the real production chain ──
class PluginSandboxRealVst3ChainVariableCallbackTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3ChainVariableCallbackTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-chain-variable-callback.v1",
                         "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("variable host callbacks never produce partial wet/dry provenance");

        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(), "fixture bundle is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, 2048);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        const int slotIndex = chain.appendSandboxedPlugin(description, error, preparation);
        expectEquals(slotIndex, 0, error);
        if (slotIndex < 0)
            return;

        auto* slot = chain.getSlot(0);
        expect(slot != nullptr && slot->getSandboxProxy() != nullptr,
               "sandboxed chain slot is missing");
        if (slot == nullptr || slot->getSandboxProxy() == nullptr)
            return;

        const auto* proxy = slot->getSandboxProxy();
        const auto processDiag = proxy->processDiagnostics();
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;

        // Prepared with maximumHostBlockSamples = 2048 and Q = 512:
        // depth d = 4, transport = 5Q = 2560, effective = 2592. These are
        // PREPARED-SESSION properties and must not change with actual
        // callback partition sizes.
        expectEquals(proxy->getPreparedBlockSamples(), kBlockSamples);
        expectEquals(proxy->getPreparedOutstandingDepth(), 4);

        // Partition schedule including the historical defect shape (256 then
        // 512) and 2048/1024 callbacks. Total fed = 6272 samples.
        const int partitions[] = { 256, 512, 2048, 128, 1024, 480, 2048, 64, 512 };
        int totalFed = 0;

        for (const int partition : partitions)
        {
            juce::AudioBuffer<float> block(2, partition);
            for (int ch = 0; ch < block.getNumChannels(); ++ch)
            {
                auto* d = block.getWritePointer(ch);
                for (int i = 0; i < partition; ++i)
                    d[i] = 1.0f;   // constant deterministic stream
            }

            if (totalFed >= 2048)
            {
               #if JUCE_ENABLE_ALLOCATION_HOOKS
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, partition);
               #else
                expect(false, "JUCE allocation hooks are required for variable-callback RT proof");
                chain.processBlock(block, partition);
               #endif
            }
            else
            {
                chain.processBlock(block, partition);
            }

            totalFed += partition;

            // Test-side worker polling; production processing never waits.
            waitForWorkerQuanta(*proxy,
                                static_cast<std::uint64_t>(totalFed / kBlockSamples),
                                5000);

            // Provenance check across the WHOLE callback: with constant input,
            // pre-roll (s < 2560) and the fixture's 32-sample delay fill
            // (2560 <= s < 2592) must be zero; from 2592 the processed gain is
            // exactly 0.5. A partial remote + accidental dry/stale mix would
            // surface as 1.0 (misaligned dry) or a nonzero early sample.
            for (int i = 0; i < partition; ++i)
            {
                const int globalSample = totalFed - partition + i;
                const float expected = (globalSample < kEffectiveLatency2048) ? 0.0f : 0.5f;
                expectWithinAbsoluteError(block.getSample(0, i), expected, 0.0f,
                                          "left stream provenance violation at global sample "
                                          + juce::String(globalSample));
                expectWithinAbsoluteError(block.getSample(1, i), expected, 0.0f,
                                          "right stream provenance violation at global sample "
                                          + juce::String(globalSample));
            }
        }

        // Worker quantum exactness: 6272 samples = 12 exact-Q exchanges; the
        // worker never receives a 2048/1024/480/256/128/64 partial plugin block.
        const auto realtimeDiag = proxy->lifecycleDiagnostics();
        expectEquals(static_cast<juce::int64>(realtimeDiag.transport.calls),
                     juce::int64(totalFed / kBlockSamples));
        expectEquals(static_cast<juce::int64>(realtimeDiag.transport.boundsRejected),
                     juce::int64(0));
        expectEquals(static_cast<juce::int64>(realtimeDiag.reblocker.backendCalls),
                     juce::int64(totalFed / kBlockSamples));
        // Depth-4: committed = exchanges - depth.
        expectEquals(static_cast<juce::int64>(realtimeDiag.reblocker.committedQuanta),
                     juce::int64(totalFed / kBlockSamples - 4));
        expectEquals(static_cast<juce::int64>(realtimeDiag.reblocker.fallbackQuanta),
                     juce::int64(0));
        expect(proxy->workerCompletedSequence()
                   >= static_cast<std::uint64_t>(totalFed / kBlockSamples),
               "worker did not complete all exact-Q quanta");
        expectEquals(proxy->getEffectiveLatencySamples(), kEffectiveLatency2048);
        expectEquals(proxy->getReblockTransportLatencySamples(), kReblockTransportLatency2048);
        expectEquals(proxy->getWorkerPluginLatencySamples(), kFixtureLatencySamples);
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency2048);
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into the parent during variable-callback processing");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived variable-callback chain removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived variable-callback chain removal");
       #else
        beginTest("Windows-only variable-callback chain contract");
        expect(true);
       #endif
    }
};

// ── Test 3: in-process + sandboxed slots coexist in one production chain ──
class PluginSandboxRealVst3InprocessCoexistenceTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3InprocessCoexistenceTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-inprocess-coexistence.v1",
                         "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("in-process and sandboxed slots coexist in one production chain");

        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(), "fixture bundle is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        auto probe = std::make_unique<ChainInProcessProbeProcessor>();
        ChainInProcessProbeProcessor* probePtr = probe.get();
        const int inProcessIndex = chain.appendPluginInstanceForTesting(std::move(probe));
        expectEquals(inProcessIndex, 0, "in-process probe did not insert first");
        if (inProcessIndex < 0)
            return;
       #else
        return;
       #endif

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        const int sandboxIndex = chain.appendSandboxedPlugin(description, error, preparation);
        expectEquals(sandboxIndex, 1, error);
        if (sandboxIndex < 0)
            return;

        auto* inProcessSlot = chain.getSlot(0);
        auto* sandboxSlot = chain.getSlot(1);
        expect(inProcessSlot != nullptr && sandboxSlot != nullptr,
               "coexistence chain did not publish both slots");
        if (inProcessSlot == nullptr || sandboxSlot == nullptr)
            return;

        // ── Structural coexistence ──
        expectEquals(static_cast<int>(inProcessSlot->getExecutionMode()),
                     static_cast<int>(DAW::PluginExecutionMode::InProcess));
        expect(! inProcessSlot->isSandboxed(), "in-process probe reported sandboxed mode");
        expect(inProcessSlot->getProcessor() != nullptr,
               "in-process probe lost its parent processor");
        expect(inProcessSlot->getSandboxProxy() == nullptr,
               "in-process probe owns a sandbox proxy");
        expectEquals(inProcessSlot->getLatencySamples(), 0);

        expectEquals(static_cast<int>(sandboxSlot->getExecutionMode()),
                     static_cast<int>(DAW::PluginExecutionMode::Sandboxed));
        expect(sandboxSlot->isSandboxed(), "fixture slot did not report sandboxed mode");
        expect(sandboxSlot->getProcessor() == nullptr,
               "sandboxed fixture slot exposed a parent processor");
        expect(sandboxSlot->getSandboxProxy() != nullptr,
               "sandboxed fixture slot owns no proxy");
        expectEquals(sandboxSlot->getLatencySamples(), kEffectiveLatency);
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency);
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "coexistence chain loaded the fixture module in the parent");

        const auto* proxy = sandboxSlot->getSandboxProxy();
        const auto processDiag = proxy->processDiagnostics();
        expect(processDiag.workerProcessId != 0, "sandbox worker PID is zero");
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;
        expect(DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping missing while coexistence chain is active");

        // ── Combined stream: probe gain 0.5 in-process, then sandbox fixture
        //     gain 0.5 with 1056 samples effective delay. Pre-roll/delay fill
        //     outputs are zero until global sample 1056, then 0.25. ──
        juce::AudioBuffer<float> block(2, kBlockSamples);
        for (int blockIndex = 1; blockIndex <= 8; ++blockIndex)
        {
            block.clear();
            for (int ch = 0; ch < block.getNumChannels(); ++ch)
            {
                auto* d = block.getWritePointer(ch);
                for (int i = 0; i < kBlockSamples; ++i)
                    d[i] = 1.0f;
            }

            chain.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(blockIndex), 5000);

            const int blockStart = (blockIndex - 1) * kBlockSamples;
            for (int i = 0; i < kBlockSamples; ++i)
            {
                const int globalSample = blockStart + i;
                const float expected = (globalSample < kEffectiveLatency) ? 0.0f : 0.25f;
                expectWithinAbsoluteError(block.getSample(0, i), expected, 0.0f,
                                          "coexistence left stream mismatch at global sample "
                                          + juce::String(globalSample));
                expectWithinAbsoluteError(block.getSample(1, i), expected, 0.0f,
                                          "coexistence right stream mismatch at global sample "
                                          + juce::String(globalSample));
            }
        }

        expect(probePtr->processCount >= 8,
               "in-process probe was not processed by the chain");

        // ── Removing the sandboxed slot must not affect the in-process slot ──
        chain.removePlugin(1);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived coexistence removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived coexistence removal");

        const int probeCountBefore = probePtr->processCount;
        block.clear();
        for (int ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* d = block.getWritePointer(ch);
            for (int i = 0; i < kBlockSamples; ++i)
                d[i] = 1.0f;
        }
        chain.processBlock(block, kBlockSamples);
        expect(probePtr->processCount == probeCountBefore + 1,
               "in-process probe stopped processing after sandbox removal");
        for (int i = 0; i < kBlockSamples; ++i)
        {
            expectWithinAbsoluteError(block.getSample(0, i), 0.5f, 0.0f,
                                      "in-process-only left output mismatch at sample "
                                      + juce::String(i));
            expectWithinAbsoluteError(block.getSample(1, i), 0.5f, 0.0f,
                                      "in-process-only right output mismatch at sample "
                                      + juce::String(i));
        }
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into the parent after coexistence removal");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only in-process coexistence contract");
        expect(true);
       #endif
    }
};

PluginSandboxRealVst3ChainIntegrationTest pluginSandboxRealVst3ChainIntegrationTest;
PluginSandboxRealVst3ChainVariableCallbackTest pluginSandboxRealVst3ChainVariableCallbackTest;
PluginSandboxRealVst3InprocessCoexistenceTest pluginSandboxRealVst3InprocessCoexistenceTest;

// ── Test 4: real production PluginChain with a 2048-sample host callback ──
class PluginSandboxRealVst3Chain2048Test final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3Chain2048Test()
        : juce::UnitTest("plugin.sandbox.real-vst3-chain-2048.v1", "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("2048 host callbacks reblock into four exact 512-sample worker quanta");

        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(), "fixture bundle is missing");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module was loaded in the parent before the 2048 chain test");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        constexpr int kHostBlock = 2048;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kHostBlock);   // maximum HOST callback size

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kBlockSamples;   // Q stays 512
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        const int slotIndex = chain.appendSandboxedPlugin(description, error, preparation);
        expectEquals(slotIndex, 0, error);
        if (slotIndex < 0)
            return;

        auto* slot = chain.getSlot(0);
        expect(slot != nullptr && slot->getSandboxProxy() != nullptr,
               "sandboxed 2048 chain slot is missing");
        if (slot == nullptr || slot->getSandboxProxy() == nullptr)
            return;

        const auto* proxy = slot->getSandboxProxy();
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "2048 chain loaded the fixture module in the parent");
        expectEquals(proxy->getPreparedBlockSamples(), kBlockSamples);
        expectEquals(proxy->getPreparedMaximumHostBlockSamples(), kHostBlock);
        expectEquals(proxy->getPreparedOutstandingDepth(), 4);
        expectEquals(proxy->getReblockTransportLatencySamples(), kReblockTransportLatency2048);
        expectEquals(proxy->getEffectiveLatencySamples(), kEffectiveLatency2048);
        expectEquals(slot->getLatencySamples(), kEffectiveLatency2048);
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency2048);

        const auto processDiag = proxy->processDiagnostics();
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;
        expect(DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "2048 chain sandbox mapping missing while active");

        // Impulse at sample 0 -> 2048 host block -> 4 exact 512 quanta ->
        // fixture gain 0.5 + 32-sample delay -> global sample 2560+32 = 2592
        // (block 1, offset 544).
        juce::AudioBuffer<float> block(2, kHostBlock);
        for (int blockIndex = 1; blockIndex <= 4; ++blockIndex)
        {
            block.clear();
            if (blockIndex == 1)
            {
                block.setSample(0, 0, 1.0f);
                block.setSample(1, 0, -1.0f);
            }

            if (blockIndex == 3)
            {
               #if JUCE_ENABLE_ALLOCATION_HOOKS
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.processBlock(block, kHostBlock);
               #else
                expect(false, "JUCE allocation hooks are required for the 2048 chain RT proof");
                chain.processBlock(block, kHostBlock);
               #endif
            }
            else
            {
                chain.processBlock(block, kHostBlock);
            }

            waitForWorkerQuanta(*proxy,
                                static_cast<std::uint64_t>(blockIndex)
                                    * static_cast<std::uint64_t>(kHostBlock / kBlockSamples),
                                5000);

            if (blockIndex == 2)
            {
                const int offset = kEffectiveLatency2048 - kHostBlock;
                expectWithinAbsoluteError(block.getSample(0, offset), 0.5f, 0.0f,
                                          "left 2048 chain impulse mismatch at effective latency");
                expectWithinAbsoluteError(block.getSample(1, offset), -0.5f, 0.0f,
                                          "right 2048 chain impulse mismatch at effective latency");
                expectWithinAbsoluteError(block.getSample(0, offset - 1), 0.0f, 0.0f,
                                          "2048 chain impulse arrived one sample early");
                expectWithinAbsoluteError(block.getSample(0, offset + 1), 0.0f, 0.0f,
                                          "2048 chain impulse is wider than one sample");
            }
        }

        // 4 blocks x 2048 = 8192 samples = 16 exact 512-sample exchanges.
        const auto realtimeDiag = proxy->lifecycleDiagnostics();
        expectEquals(static_cast<juce::int64>(realtimeDiag.transport.calls), juce::int64(16));
        expectEquals(static_cast<juce::int64>(realtimeDiag.transport.boundsRejected), juce::int64(0));
        expectEquals(static_cast<juce::int64>(realtimeDiag.reblocker.backendCalls), juce::int64(16));
        expectEquals(static_cast<juce::int64>(realtimeDiag.reblocker.committedQuanta), juce::int64(12));
        expectEquals(static_cast<juce::int64>(realtimeDiag.reblocker.fallbackQuanta), juce::int64(0));
        expectEquals(static_cast<juce::int64>(realtimeDiag.reblocker.remoteQuanta), juce::int64(12));
        expect(proxy->workerCompletedSequence() >= 16,
               "worker did not complete all 2048-chain quanta");
        expectEquals(proxy->getEffectiveLatencySamples(), kEffectiveLatency2048);
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency2048);
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into the parent during 2048 processing");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived 2048 chain removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived 2048 chain removal");
       #else
        beginTest("Windows-only 2048 chain contract");
        expect(true);
       #endif
    }
};

// ── Test 5: prepared depth reprepare 512 -> 2048 -> 512 through the chain ──
class PluginSandboxRealVst3ChainReprepareDepthTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3ChainReprepareDepthTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-chain-reprepare-depth.v1",
                         "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("chain reprepare transitions prepared depth and latency without stale state");

        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(), "fixture bundle is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);   // Bmax = 512 -> d = 1

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kBlockSamples;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        const int slotIndex = chain.appendSandboxedPlugin(description, error, preparation);
        expectEquals(slotIndex, 0, error);
        if (slotIndex < 0)
            return;

        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "sandboxed reprepare slot is missing");
        if (proxy == nullptr)
            return;

        expectEquals(proxy->getPreparedOutstandingDepth(), 1);
        expectEquals(proxy->getEffectiveLatencySamples(), kEffectiveLatency);
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency);

        // Feed the 512-prepared generation.
        juce::AudioBuffer<float> block(2, kBlockSamples);
        for (int b = 1; b <= 6; ++b)
        {
            block.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSamples; ++i)
                    block.setSample(ch, i, 1.0f);
            chain.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(b), 5000);
            const int blockStart = (b - 1) * kBlockSamples;
            for (int i = 0; i < kBlockSamples; ++i)
            {
                const int globalSample = blockStart + i;
                const float expected = (globalSample < kEffectiveLatency) ? 0.0f : 0.5f;
                expectWithinAbsoluteError(block.getSample(0, i), expected, 0.0f,
                                          "512-prepared stream mismatch at global sample "
                                          + juce::String(globalSample));
            }
        }

        // Reprepare the chain for a 2048 maximum host block: depth 2->4,
        // prepared latency 1056 -> 2592. Old queued state must not leak.
        chain.prepare(48000.0, 2048);
        expectEquals(proxy->getPreparedOutstandingDepth(), 4,
                     proxy->processDiagnostics().error);
        expectEquals(proxy->getEffectiveLatencySamples(), kEffectiveLatency2048);
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency2048);

        juce::AudioBuffer<float> block2048(2, 2048);
        for (int b = 1; b <= 4; ++b)
        {
            block2048.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 2048; ++i)
                    block2048.setSample(ch, i, 1.0f);
            chain.processBlock(block2048, 2048);
            waitForWorkerQuanta(*proxy,
                                static_cast<std::uint64_t>(b) * 4, 5000);
            const int blockStart = (b - 1) * 2048;
            for (int i = 0; i < 2048; ++i)
            {
                const int globalSample = blockStart + i;
                const float expected = (globalSample < kEffectiveLatency2048) ? 0.0f : 0.5f;
                expectWithinAbsoluteError(block2048.getSample(0, i), expected, 0.0f,
                                          "2048-prepared stream mismatch at global sample "
                                          + juce::String(globalSample));
            }
        }

        // Reverse: 2048 -> 512. Depth returns to 1; latency to 1056; no stale
        // outstanding slots or dry history survive.
        chain.prepare(48000.0, kBlockSamples);
        expectEquals(proxy->getPreparedOutstandingDepth(), 1);
        expectEquals(proxy->getEffectiveLatencySamples(), kEffectiveLatency);
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency);

        for (int b = 1; b <= 4; ++b)
        {
            block.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSamples; ++i)
                    block.setSample(ch, i, 1.0f);
            chain.processBlock(block, kBlockSamples);
            waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(b), 5000);
            const int blockStart = (b - 1) * kBlockSamples;
            for (int i = 0; i < kBlockSamples; ++i)
            {
                const int globalSample = blockStart + i;
                const float expected = (globalSample < kEffectiveLatency) ? 0.0f : 0.5f;
                expectWithinAbsoluteError(block.getSample(0, i), expected, 0.0f,
                                          "512-reprepared stream mismatch at global sample "
                                          + juce::String(globalSample));
            }
        }

        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module leaked into the parent during reprepare coverage");

        const auto processDiag = proxy->processDiagnostics();
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived reprepare-depth chain removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived reprepare-depth chain removal");
       #else
        beginTest("Windows-only reprepare depth contract");
        expect(true);
       #endif
    }
};

PluginSandboxRealVst3Chain2048Test pluginSandboxRealVst3Chain2048Test;
PluginSandboxRealVst3ChainReprepareDepthTest pluginSandboxRealVst3ChainReprepareDepthTest;

// ── Test 6: prepared sandbox latency flows through the existing PDC path ──
class PluginSandboxRealVst3LatencyTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3LatencyTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-latency.v1", "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("prepared sandbox latency reaches the existing PDC contract (512/1024/2048)");

        juce::MessageManager::getInstance();
        DAW::HostedPluginIsolationCore::ScopedOverride hostedOverride(false);

        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(), "fixture bundle is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        runLatencyCase(worker, description, 512, 1, 1024, 1056);
        runLatencyCase(worker, description, 1024, 2, 1536, 1568);
        runLatencyCase(worker, description, 2048, 4, 2560, 2592, true);

        beginTest("control-plane reprepare republishes prepared latency 1056 -> 2592 -> 1056");
        runReprepareLatencyCase(worker, description);

        beginTest("bypass preserves latency; removal recalculates; SUM rule intact");
        runBypassAndRemovalCase(worker, description);
       #else
        beginTest("Windows-only latency publication contract");
        expect(true);
       #endif
    }

private:
    using ChainMap = DAW::SoundEngine::ApexPluginPdcContractCore::PluginChainMap;
    static constexpr const char* kTrackId = "latency-contract-track";

    struct LatencyCase
    {
        ChainMap chains;
        DAW::PluginChainCore* chain = nullptr;
    };

    void expectLatencyContract(DAW::PluginChainCore& chain,
                               const ChainMap& chains,
                               int expectedTransport,
                               int expectedEffective)
    {
        const auto* slot = chain.getSlot(0);
        expect(slot != nullptr, "latency-case sandbox slot missing");
        if (slot == nullptr)
            return;

        const auto* proxy = slot->getSandboxProxy();
        expect(proxy != nullptr, "latency-case sandbox proxy missing");
        if (proxy == nullptr)
            return;

        expectEquals(proxy->getWorkerPluginLatencySamples(), kFixtureLatencySamples);
        expectEquals(proxy->getReblockTransportLatencySamples(), expectedTransport);
        expectEquals(proxy->getEffectiveLatencySamples(), expectedEffective);
        expectEquals(slot->getLatencySamples(), expectedEffective);
        expectEquals(chain.totalLatencySamples(), expectedEffective);
        expectEquals(DAW::SoundEngine::ApexPluginPdcContractCore::getTrackLatencySamples(
                         &chains, kTrackId),
                     expectedEffective);
        // The exact value ApplicationCore::publishPluginChainLatencies()
        // publishes (HostedPluginIsolationCore is identity when active).
        expectEquals(DAW::HostedPluginIsolationCore::effectiveHostedLatencySamples(
                         chain.totalLatencySamples()),
                     expectedEffective);
    }

    void runLatencyCase(const juce::File& worker,
                        const juce::PluginDescription& description,
                        int maximumHostBlock,
                        int expectedDepth,
                        int expectedTransport,
                        int expectedEffective,
                        bool variableCallbacks = false)
    {
        ChainMap chains;
        chains[static_cast<const juce::String&>(juce::String(kTrackId))]
            = std::make_unique<DAW::PluginChainCore>();
        auto* chain = chains[static_cast<const juce::String&>(juce::String(kTrackId))].get();

        chain->prepare(48000.0, maximumHostBlock);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kBlockSamples;   // Q = 512 fixed
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        const int slotIndex = chain->appendSandboxedPlugin(description, error, preparation);
        expectEquals(slotIndex, 0, error);
        if (slotIndex < 0)
            return;

        const auto* proxy = chain->getSlot(0) != nullptr
            ? chain->getSlot(0)->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "latency-case proxy missing after insert");
        if (proxy == nullptr)
            return;
        expectEquals(proxy->getPreparedOutstandingDepth(), expectedDepth);

        expectLatencyContract(*chain, chains, expectedTransport, expectedEffective);

        if (variableCallbacks)
        {
            static constexpr int partitions[] = { 64, 128, 256, 480, 512, 1024, 2048 };
            int totalFed = 0;
            for (const int partition : partitions)
            {
                juce::AudioBuffer<float> block(2, partition);
                block.clear();
                chain->processBlock(block, partition);
                totalFed += partition;
                waitForWorkerQuanta(*proxy,
                                    static_cast<std::uint64_t>(totalFed / kBlockSamples),
                                    5000);
                // Prepared latency must never change with the actual callback size.
                expectLatencyContract(*chain, chains, expectedTransport, expectedEffective);
            }
        }

        const auto processDiag = proxy->processDiagnostics();
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;
        chain->removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived latency-case removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived latency-case removal");
        expectEquals(chain->totalLatencySamples(), 0);
        expectEquals(DAW::SoundEngine::ApexPluginPdcContractCore::getTrackLatencySamples(
                         &chains, kTrackId),
                     0);
    }

    void runReprepareLatencyCase(const juce::File& worker,
                                 const juce::PluginDescription& description)
    {
        ChainMap chains;
        chains[static_cast<const juce::String&>(juce::String(kTrackId))]
            = std::make_unique<DAW::PluginChainCore>();
        auto* chain = chains[static_cast<const juce::String&>(juce::String(kTrackId))].get();

        chain->prepare(48000.0, 512);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kBlockSamples;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        expectEquals(chain->appendSandboxedPlugin(description, error, preparation), 0, error);

        expectLatencyContract(*chain, chains, 1024, 1056);

        chain->prepare(48000.0, 2048);
        expectLatencyContract(*chain, chains, 2560, 2592);

        chain->prepare(48000.0, 512);
        expectLatencyContract(*chain, chains, 1024, 1056);

        const auto* proxy = chain->getSlot(0) != nullptr
            ? chain->getSlot(0)->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "reprepare latency proxy missing");
        if (proxy == nullptr)
            return;
        const auto processDiag = proxy->processDiagnostics();
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;
        chain->removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived reprepare-latency removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived reprepare-latency removal");
    }

    void runBypassAndRemovalCase(const juce::File& worker,
                                 const juce::PluginDescription& description)
    {
        ChainMap chains;
        chains[static_cast<const juce::String&>(juce::String(kTrackId))]
            = std::make_unique<DAW::PluginChainCore>();
        auto* chain = chains[static_cast<const juce::String&>(juce::String(kTrackId))].get();
        chain->prepare(48000.0, 512);

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        auto probe = std::make_unique<ChainInProcessProbeProcessor>();
        expectEquals(chain->appendPluginInstanceForTesting(std::move(probe)), 0);
       #endif

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kBlockSamples;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        expectEquals(chain->appendSandboxedPlugin(description, error, preparation), 1, error);

        // Existing chain aggregation rule is SUM over slots
        // (PluginChainCore::totalLatencySamples). Probe 0 + sandbox 1056.
        expectEquals(chain->totalLatencySamples(), kEffectiveLatency);

        // Existing bypass semantics: latency is preserved while bypassed.
        chain->setSlotBypassed(1, true);
        expectEquals(chain->totalLatencySamples(), kEffectiveLatency);
        expectEquals(DAW::SoundEngine::ApexPluginPdcContractCore::getTrackLatencySamples(
                         &chains, kTrackId),
                     kEffectiveLatency);
        chain->setSlotBypassed(1, false);
        expectEquals(chain->totalLatencySamples(), kEffectiveLatency);

        const auto* proxy = chain->getSlot(1) != nullptr
            ? chain->getSlot(1)->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "bypass-case sandbox proxy missing");
        if (proxy == nullptr)
            return;
        const auto processDiag = proxy->processDiagnostics();
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;

        // Removing the sandboxed slot must recalculate the aggregate (0 left).
        chain->removePlugin(1);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived bypass-case removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived bypass-case removal");
        expectEquals(chain->totalLatencySamples(), 0);
        expectEquals(DAW::SoundEngine::ApexPluginPdcContractCore::getTrackLatencySamples(
                         &chains, kTrackId),
                     0);

        chain->removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
    }
};

PluginSandboxRealVst3LatencyTest pluginSandboxRealVst3LatencyTest;

// ── Test 7: stereo + mono layout evidence through the production chain ──
class PluginSandboxRealVst3LayoutTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3LayoutTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-layout.v1", "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("stereo 2->2 and mono 1->1 both negotiate and process correctly");

        juce::MessageManager::getInstance();
        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");

        // ── Stereo: existing validated fixture, quick re-proof of layout ──
        {
            const auto description = fixtureDescription();
            expect(juce::File(description.fileOrIdentifier).isDirectory(),
                   "stereo fixture bundle is missing");
            DAW::PluginChainCore chain;
            chain.prepare(48000.0, kBlockSamples);
            juce::String error;
            DAW::SandboxedPluginProxyCore::Preparation preparation;
           #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
            preparation.workerExecutablePathForTest = worker.getFullPathName();
           #endif
            expectEquals(chain.appendSandboxedPlugin(description, error, preparation), 0, error);
            const auto* slot = chain.getSlot(0);
            expect(slot != nullptr, "stereo layout slot missing");
            if (slot != nullptr)
            {
                expectEquals(slot->getSandboxProxy()->getMainInputChannels(), 2);
                expectEquals(slot->getSandboxProxy()->getMainOutputChannels(), 2);
                expect(slot->getProcessor() == nullptr,
                       "stereo sandbox slot exposed a parent processor");
                expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
                       "stereo fixture module leaked into the parent");
            }
            const auto pid = slot != nullptr && slot->getSandboxProxy() != nullptr
                ? slot->getSandboxProxy()->processDiagnostics().workerProcessId : 0;
            chain.removePlugin(0);
            DAW::PluginChainCore::drainRetiredPlugins();
            if (pid != 0)
                expect(! processStillAlive(pid, 5000), "stereo layout worker orphan");
        }

        // ── Mono: second deterministic fixture with distinct identity ──
        {
            const juce::File monoBundle(juce::SystemStats::getEnvironmentVariable(
                "APEX_TEST_VST3_MONO_PATH", {}));
            expect(monoBundle.isDirectory(), "mono fixture bundle is missing: "
                       + monoBundle.getFullPathName());
            if (! monoBundle.isDirectory())
                return;

            juce::PluginDescription monoDescription;
            monoDescription.name = "APEX Test VST3 Mono";
            monoDescription.descriptiveName = monoDescription.name;
            monoDescription.manufacturerName = "APEX";
            monoDescription.version = "1.0.0";
            monoDescription.pluginFormatName = "VST3";
            monoDescription.fileOrIdentifier = monoBundle.getFullPathName();
            monoDescription.numInputChannels = 1;
            monoDescription.numOutputChannels = 1;

            // The worker create-response reports the discovered component
            // identity even when the requested UID does not match. Probe once
            // with UID 0 to discover the mono fixture's exact JUCE IDs.
            juce::PluginDescription probeDescription = monoDescription;
            probeDescription.uniqueId = 0;
            probeDescription.deprecatedUid = 0;
            int discoveredUniqueId = 0;
            int discoveredDeprecatedUid = 0;
            {
                DAW::SandboxedPluginProxyCore probe(probeDescription);
                DAW::SandboxedPluginProxyCore::Preparation probePreparation;
                probePreparation.mainInputChannels = 1;
                probePreparation.mainOutputChannels = 1;
               #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                probePreparation.workerExecutablePathForTest = worker.getFullPathName();
               #endif
                probe.prepare(probePreparation);
                const auto& probeDiagnostics = probe.processDiagnostics();
                discoveredUniqueId = probeDiagnostics.hostedPluginUniqueId;
                discoveredDeprecatedUid = probeDiagnostics.hostedPluginDeprecatedUid;
                logMessage("APEX_MONO_FIXTURE_IDS uniqueId="
                           + juce::String(discoveredUniqueId)
                           + " deprecatedUid="
                           + juce::String(discoveredDeprecatedUid));
                probe.release(5000);
            }
            expect(discoveredUniqueId != 0,
                   "worker did not report the mono fixture discovered identity");

            monoDescription.uniqueId = discoveredUniqueId;
            monoDescription.deprecatedUid = discoveredDeprecatedUid;

            DAW::PluginChainCore chain;
            chain.prepare(48000.0, kBlockSamples);
            juce::String error;
            DAW::SandboxedPluginProxyCore::Preparation preparation;
            preparation.blockSamples = kBlockSamples;
            preparation.mainInputChannels = 1;
            preparation.mainOutputChannels = 1;
           #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
            preparation.workerExecutablePathForTest = worker.getFullPathName();
           #endif
            const int monoIndex = chain.appendSandboxedPlugin(monoDescription, error, preparation);
            expectEquals(monoIndex, 0, error);
            if (monoIndex < 0)
                return;

            auto* slot = chain.getSlot(0);
            expect(slot != nullptr && slot->getSandboxProxy() != nullptr,
                   "mono layout slot missing");
            if (slot == nullptr || slot->getSandboxProxy() == nullptr)
                return;
            const auto* proxy = slot->getSandboxProxy();
            expectEquals(proxy->getMainInputChannels(), 1);
            expectEquals(proxy->getMainOutputChannels(), 1);
            expect(slot->getProcessor() == nullptr,
                   "mono sandbox slot exposed a parent processor");
            expectEquals(proxy->getEffectiveLatencySamples(), kEffectiveLatency);
            expect(GetModuleHandleW(L"APEXTestVST3Mono.vst3") == nullptr,
                   "mono fixture module leaked into the parent");

            // Impulse at sample 0 -> mono gain 0.5 + 32-sample delay ->
            // channel 0 sample 1056; channel 1 must be silent.
            juce::AudioBuffer<float> block(2, kBlockSamples);
            for (int b = 1; b <= 4; ++b)
            {
                block.clear();
                if (b == 1)
                    block.setSample(0, 0, 1.0f);
                chain.processBlock(block, kBlockSamples);
                waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(b), 5000);
                if (b == 3)
                {
                    expectWithinAbsoluteError(block.getSample(0, kFixtureLatencySamples),
                                              0.5f, 0.0f,
                                              "mono chain impulse mismatch at effective latency");
                    expectWithinAbsoluteError(block.getSample(1, kFixtureLatencySamples),
                                              0.0f, 0.0f,
                                              "mono chain leaked content into channel 1");
                }
            }

            const auto processDiag = proxy->processDiagnostics();
            const DWORD workerPid = processDiag.workerProcessId;
            const juce::String mappingName = processDiag.sharedMemoryName;
            chain.removePlugin(0);
            DAW::PluginChainCore::drainRetiredPlugins();
            expect(! processStillAlive(workerPid, 5000),
                   "mono layout worker orphan");
            expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
                   "mono layout mapping outlived removal");
        }
       #else
        beginTest("Windows-only layout contract");
        expect(true);
       #endif
    }
};

// ── Test 8: focused bypass/remove lifecycle through the production chain ──
class PluginSandboxRealVst3BypassRemoveTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3BypassRemoveTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-bypass-remove.v1", "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("bypass is quantum-safe and removal is deferred and clean");

        juce::MessageManager::getInstance();
        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(), "fixture bundle is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, kBlockSamples);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        const int insertResult = chain.appendSandboxedPlugin(description, error, preparation);
        expectEquals(insertResult, 0, "bypass/remove insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "bypass sandbox proxy missing");
        if (proxy == nullptr)
            return;

        const auto processDiag = proxy->processDiagnostics();
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;

        juce::AudioBuffer<float> block(2, kBlockSamples);
        const auto feedConstant = [&](float value)
        {
            block.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSamples; ++i)
                    block.setSample(ch, i, value);
            chain.processBlock(block, kBlockSamples);
        };

        // Warm remote processing: constant 1.0 -> 0.5 after 1056.
        for (int b = 1; b <= 4; ++b)
        {
            feedConstant(1.0f);
            waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(b), 5000);
        }
        expectWithinAbsoluteError(block.getSample(0, kBlockSamples - 1), 0.5f, 0.0f,
                                  "remote stream not established before bypass");

        // Engage bypass on a quantum boundary (block boundary here).
        chain.setSlotBypassed(0, true);
        expect(proxy->isBypassed(), "proxy bypass flag not forwarded");
        for (int b = 1; b <= 3; ++b)
        {
            feedConstant(1.0f);
            waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(4 + b), 5000);
        }
        // Bypassed quanta commit latency-aligned delayed dry = 1.0, never 0.5.
        expectWithinAbsoluteError(block.getSample(0, kBlockSamples - 1), 1.0f, 0.0f,
                                  "bypassed stream did not produce aligned dry");
        expectEquals(chain.totalLatencySamples(), kEffectiveLatency);

        // Unbypass resumes remote processing coherently.
        chain.setSlotBypassed(0, false);
        for (int b = 1; b <= 3; ++b)
        {
            feedConstant(1.0f);
            waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(7 + b), 5000);
        }
        expectWithinAbsoluteError(block.getSample(0, kBlockSamples - 1), 0.5f, 0.0f,
                                  "remote stream did not resume after unbypass");

        // Warmed zero-allocation proof on the production path.
       #if JUCE_ENABLE_ALLOCATION_HOOKS
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            feedConstant(1.0f);
        }
       #endif
        waitForWorkerQuanta(*proxy, 12, 5000);

        // Removal lifecycle: deferred retirement -> drain -> reap -> mapping.
        chain.removePlugin(0);
        expectEquals(chain.totalLatencySamples(), 0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived bypass/remove chain removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived bypass/remove chain removal");
       #else
        beginTest("Windows-only bypass/remove contract");
        expect(true);
       #endif
    }
};

// ── Test 9: transactional create-failure coverage ──
class PluginSandboxRealVst3CreateFailureTest final : public juce::UnitTest
{
public:
    PluginSandboxRealVst3CreateFailureTest()
        : juce::UnitTest("plugin.sandbox.real-vst3-create-failure.v1", "PluginSandbox") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("invalid preparations fail transactionally without poisoning the host");

        juce::MessageManager::getInstance();
        const auto worker = productionWorkerExecutable();
        const auto description = fixtureDescription();
        expect(worker.existsAsFile(), "production worker executable is missing");
        expect(juce::File(description.fileOrIdentifier).isDirectory(), "fixture bundle is missing");
        if (! worker.existsAsFile() || ! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, 2048);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif

        const auto expectCleanFailure = [&](const juce::String& label)
        {
            expectEquals(chain.getNumSlots(), 0,
                         label + " left a half-created slot in the chain");
            expectEquals(chain.totalLatencySamples(), 0,
                         label + " published latency for a failed slot");
            expect(error.isNotEmpty(), label + " reported no error");
            const auto orphanMappings = juce::StringArray::fromTokens(
                "APEX.PluginSandbox.Audio", ".", {});
            juce::ignoreUnused(orphanMappings);
        };

        // 1. Nonexistent plugin path.
        juce::PluginDescription badPath = description;
        badPath.fileOrIdentifier = "C:\\nonexistent\\NoSuchPlugin.vst3";
        error.clear();
        expectEquals(chain.appendSandboxedPlugin(badPath, error, preparation), -1);
        expectCleanFailure("nonexistent path");

        // 2. Wrong component UID for the real bundle.
        juce::PluginDescription wrongUid = description;
        wrongUid.uniqueId = 1;
        wrongUid.deprecatedUid = 1;
        error.clear();
        expectEquals(chain.appendSandboxedPlugin(wrongUid, error, preparation), -1);
        expectCleanFailure("wrong UID");

        // 3. Invalid quantum (0).
        juce::PluginDescription zeroQ = description;
        error.clear();
        DAW::SandboxedPluginProxyCore::Preparation zeroQPreparation = preparation;
        zeroQPreparation.blockSamples = 0;
        expectEquals(chain.appendSandboxedPlugin(zeroQ, error, zeroQPreparation), -1);
        expectCleanFailure("zero quantum");

        // 4. maximumHostBlockSamples requiring depth > kMaxOutstandingDepth.
        juce::PluginDescription tooDeep = description;
        error.clear();
        DAW::SandboxedPluginProxyCore::Preparation tooDeepPreparation = preparation;
        tooDeepPreparation.blockSamples = kBlockSamples;
        tooDeepPreparation.maximumHostBlockSamples = 4096;   // d = 8 > 4
        expectEquals(chain.appendSandboxedPlugin(tooDeep, error, tooDeepPreparation), -1);
        expectCleanFailure("invalid depth");

        // The host must remain healthy: a valid insertion still succeeds.
        error.clear();
        const int validIndex = chain.appendSandboxedPlugin(description, error, preparation);
        expectEquals(validIndex, 0, error);
        if (validIndex < 0)
            return;
        auto* slot = chain.getSlot(0);
        const auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(proxy != nullptr, "valid insertion after failures failed");
        if (proxy == nullptr)
            return;

        juce::AudioBuffer<float> block(2, 2048);
        block.clear();
        block.setSample(0, 0, 1.0f);
        for (int b = 1; b <= 4; ++b)
        {
            chain.processBlock(block, 2048);
            waitForWorkerQuanta(*proxy, static_cast<std::uint64_t>(b) * 4, 5000);
            block.clear();
        }

        const auto processDiag = proxy->processDiagnostics();
        const DWORD workerPid = processDiag.workerProcessId;
        const juce::String mappingName = processDiag.sharedMemoryName;
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        expect(! processStillAlive(workerPid, 5000),
               "sandbox worker outlived create-failure chain removal");
        expect(! DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName),
               "sandbox mapping outlived create-failure chain removal");
       #else
        beginTest("Windows-only create-failure contract");
        expect(true);
       #endif
    }
};

PluginSandboxRealVst3LayoutTest pluginSandboxRealVst3LayoutTest;
PluginSandboxRealVst3BypassRemoveTest pluginSandboxRealVst3BypassRemoveTest;
PluginSandboxRealVst3CreateFailureTest pluginSandboxRealVst3CreateFailureTest;
} // namespace
