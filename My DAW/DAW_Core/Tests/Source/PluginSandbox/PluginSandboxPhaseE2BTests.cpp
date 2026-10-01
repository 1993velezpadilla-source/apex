#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/PluginHostCore/PluginInstanceCore.h"
#include "../../../Source/G10Core/G10NativePluginFormat.h"
#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAudioTransportCore.h"
#include "../../../Source/AutomationCore/AutomationSnapshotCore.h"
#include "../../../Source/AutomationCore/AutomationPointCore.h"
#include "../../../Source/AutomationCore/AutomationCurveTypesCore.h"
#include "../../../Source/Automation/AutomationLaneStoreCore.h"
#include "../../../Source/Automation/AutomationParameterKeyCore.h"
#include "../../../Source/PluginSandboxCore/PluginSandboxAutomationTransportShared.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// ═══════════════════════════════════════════════════════════════════════════
// PHASE E2B — canonical APEX automation producer → sandbox delivery.
//
// This test exercises the REAL production automation producer:
//   AutomationSnapshot lane → PluginChainCore::applyAutomationAtSample →
//   PluginInstanceCore::applyAutomationAtSample (10 ms closed-form smoother,
//   one canonical block value per parameter per host block).
//
// It does NOT inject E2A events directly. Parity between the InProcess and
// Sandboxed execution modes is the E2B contract.
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
constexpr int kBlockSamples = 512;

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS && JUCE_WINDOWS
std::atomic<int> gBypassCoreDestructionCount { 0 };
std::atomic<DWORD> gBypassCoreDestructionThreadId { 0 };

void recordBypassCoreDestruction() noexcept
{
    gBypassCoreDestructionThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    gBypassCoreDestructionCount.fetch_add(1, std::memory_order_acq_rel);
}
#endif

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

void waitForWorkerQuanta(const DAW::SandboxedPluginProxyCore* proxy,
                         std::uint64_t quanta, std::uint32_t timeoutMs)
{
   #if JUCE_WINDOWS
    if (proxy == nullptr)
        return;
    const auto deadline = GetTickCount64() + timeoutMs;
    while (proxy->workerCompletedSequence() < quanta
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

/** Minimal non-notifying gain parameter mirroring the automation fixture's
    parameter (bare relaxed atomic store, no listeners, no allocation). */
class E2BGainParameter final : public juce::AudioProcessorParameterWithID
{
public:
    E2BGainParameter()
        : juce::AudioProcessorParameterWithID("gain", "Gain", "Gain") {}

    float getValue() const override { return gain_.load(std::memory_order_relaxed); }
    void setValue(float newValue) override
    {
        gain_.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName(int) const override { return "Gain"; }
    juce::String getLabel() const override { return {}; }
    float getValueForText(const juce::String& text) const override
    {
        return juce::jlimit(0.0f, 1.0f, text.getFloatValue());
    }
    int getNumSteps() const override
    {
        return juce::AudioProcessor::getDefaultNumParameterSteps();
    }

private:
    std::atomic<float> gain_ { 0.5f };
};

/** Minimal deterministic InProcess probe mirroring the automation fixture:
    one "gain" parameter (default 0.5), zero latency, DSP out = in × gain. */
class E2BInProcessGainProbeProcessor final : public juce::AudioPluginInstance
{
public:
    E2BInProcessGainProbeProcessor()
        : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        // AudioPluginInstance privatizes the unqualified addParameter name;
        // the public AudioProcessor base member is reachable qualified.
        juce::AudioProcessor::addParameter(
            gainParameter_ = new E2BGainParameter());
    }

    const juce::String getName() const override { return "APEX E2B InProcess Gain Probe"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const float gain = gainParameter_ != nullptr
            ? gainParameter_->getValue() : 0.5f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGain(ch, 0, buffer.getNumSamples(), gain);
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
    }

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
        d.name = "APEX E2B InProcess Gain Probe";
        d.descriptiveName = d.name;
        d.pluginFormatName = "APEX Test";
        d.manufacturerName = "APEX";
        d.version = "1";
        d.fileOrIdentifier = "APEX::E2B::GainProbe::inprocess";
        d.uniqueId = 0x45425031;   // "EBP1"
        d.deprecatedUid = 0x45425031;
        d.isInstrument = false;
        d.numInputChannels = 2;
        d.numOutputChannels = 2;
    }

private:
    juce::AudioProcessorParameterWithID* gainParameter_ = nullptr;
};
// ── E2B first gate: canonical block-rate automation parity, InProcess vs ──
// ── Sandboxed, through the REAL production producer and frozen E2A boundary. ─
class PluginSandboxPhaseE2BCanonicalBlockAutomationParityTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BCanonicalBlockAutomationParityTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.canonical-block-automation-parity.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("canonical APEX automation parity between InProcess and Sandboxed execution");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;
        logMessage("APEX_E2B_FIXTURE_IDS uniqueId="
                   + juce::String(identity.uniqueId)
                   + " deprecatedUid=" + juce::String(identity.deprecatedUid));

        const juce::String trackId = "e2b-parity-track";
        constexpr double kSampleRate = 48000.0;
        constexpr double kBpm = 120.0;

        // ── Arm A: InProcess probe slot ────────────────────────────────────
        DAW::PluginChainCore chainA;
        chainA.setAutomationContext(trackId, nullptr, nullptr);
        chainA.prepare(kSampleRate, 2048);
        {
            auto probe = std::make_unique<E2BInProcessGainProbeProcessor>();
            expectEquals(chainA.appendPluginInstanceForTesting(std::move(probe)), 0,
                         "in-process probe slot insert failed");
        }
        // The test-only append seam (appendPluginInstanceForTesting) skips the
        // chain's configureSlotAutomation step that production appendPlugin()
        // performs. Reproduce the exact production automation-context
        // lifecycle: this registers parameter listeners, populates
        // parameterInfos_, and builds rtAutomationBindings_ +
        // lastAutomationValues_ — the REAL canonical producer prerequisites.
        chainA.getSlot(0)->configureAutomationContext(trackId, 0, nullptr, nullptr);

        // ── Arm B: Sandboxed automation fixture slot ───────────────────────
        DAW::PluginChainCore chainB;
        chainB.setAutomationContext(trackId, nullptr, nullptr);
        chainB.prepare(kSampleRate, 2048);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        expectEquals(chainB.appendSandboxedPlugin(
                         automationFixtureDescription(identity), error, preparation),
                     0, "sandboxed slot insert failed: " + error);
        const auto* slotA = chainA.getSlot(0);
        const auto* slotB = chainB.getSlot(0);
        expect(slotA != nullptr && slotB != nullptr, "parity slots are missing");
        expect(! slotA->isSandboxed(), "arm A must be InProcess");
        expect(slotB->isSandboxed(), "arm B must be Sandboxed");
        const auto* proxyB = slotB->getSandboxProxy();
        expect(proxyB != nullptr, "sandbox proxy is missing");

        // The worker metadata ID is the canonical persisted identity; display
        // name ("Gain") is never used as a key.
        juce::String sandboxOrdinalZeroParameterId;
        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        juce::String metadataError;
        auto* mutableProxyB = chainB.getSlot(0)->getSandboxProxy();
        expect(mutableProxyB->fetchParameterMetadata(metadata, metadataError),
               "sandbox parameter metadata fetch failed: " + metadataError);
        for (const auto& parameter : metadata)
            if (parameter.index == 0)
            {
                sandboxOrdinalZeroParameterId = parameter.parameterId;
                break;
            }
        expect(sandboxOrdinalZeroParameterId.isNotEmpty(),
               "sandbox ordinal-zero metadata ID is missing");

        // ── REAL canonical producer data: one AutomationSnapshot lane per ──
        // ── instance (the production lane key embeds the instance ID), ─────
        // ── both carrying the IDENTICAL point schedule. ────────────────────
        DAW::AutomationSnapshot snapshot;
        auto makeLanePoints = []() -> std::vector<DAW::AutomationPoint>
        {
            std::vector<DAW::AutomationPoint> points;
            points.push_back({ 0,    0.5f, DAW::AutomationCurveType::Linear, 0.0f });
            points.push_back({ 120,  0.1f, DAW::AutomationCurveType::Linear, 0.0f });
            points.push_back({ 1400, 0.9f, DAW::AutomationCurveType::Linear, 0.0f });
            points.push_back({ 2800, 0.2f, DAW::AutomationCurveType::Linear, 0.0f });
            points.push_back({ 4000, 0.6f, DAW::AutomationCurveType::Linear, 0.0f });
            return points;
        };

        const juce::String laneKeyA = "plugin.0." + slotA->getPluginInstanceId() + ".gain";
        const juce::String laneKeyB = "plugin.0." + slotB->getPluginInstanceId()
            + "." + sandboxOrdinalZeroParameterId;
        snapshot.lanes.push_back({ trackId, laneKeyA, true, makeLanePoints() });
        snapshot.lanes.push_back({ trackId, laneKeyB, true, makeLanePoints() });
        logMessage("APEX_E2B_LANE_KEYS laneA=" + laneKeyA + " laneB=" + laneKeyB);

        // ── Host callback schedule: 480 / 64 / 512 / 1024 / 2048 ───────────
        const int sizes[] = { 480, 64, 512, 1024, 2048 };
        constexpr int kCallbackCount = 5;
        constexpr int kTotalSamples = 480 + 64 + 512 + 1024 + 2048;   // 4128

        std::vector<float> recordedA(static_cast<std::size_t>(kTotalSamples));
        std::vector<float> recordedB(static_cast<std::size_t>(kTotalSamples));
        int64_t position = 0;
        std::uint64_t submittedQuanta = 0;

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

            // REAL production producer call — identical arguments for both arms.
            chainA.applyAutomationAtSample(trackId, &snapshot, position,
                                           kSampleRate, kBpm, size);
            chainB.applyAutomationAtSample(trackId, &snapshot, position,
                                           kSampleRate, kBpm, size);
            chainA.processBlock(blockA, size);
            chainB.processBlock(blockB, size);
            waitForWorkerQuanta(proxyB,
                proxyB->lifecycleDiagnostics().transport.submitted, 5000);

            for (int s = 0; s < size; ++s)
            {
                recordedA[static_cast<std::size_t>(position + s)] = blockA.getSample(0, s);
                recordedB[static_cast<std::size_t>(position + s)] = blockB.getSample(0, s);
            }
            position += size;
            submittedQuanta = proxyB->lifecycleDiagnostics().transport.submitted;
        }

        // ── 1. The InProcess arm MUST have received canonical automation ───
        // (the curve at absolute 2080 is well above the default 0.5 side).
        const float inProcessLateGain = recordedA[static_cast<std::size_t>(2080)];
        expect(std::abs(inProcessLateGain - 0.5f) > 0.05f,
               "InProcess arm did not receive canonical automation (gain at 2080 = "
                   + juce::String(inProcessLateGain, 4) + ")");

        // ── 2. Sandbox output must be latency-aligned: host position 940 ↔
        // sandbox absolute 940 + the proxy-reported effective latency.
        // Reading inside the pre-latency region would show frozen silence,
        // not the worker Gain state.
        const int sandboxLatency = proxyB->getEffectiveLatencySamples();
        constexpr int kDiagHostPosition = 940;
        const float sandboxAlignedGain =
            recordedB[static_cast<std::size_t>(kDiagHostPosition + sandboxLatency)];
        const float inProcessDiagGain =
            recordedA[static_cast<std::size_t>(kDiagHostPosition)];
        logMessage("APEX_E2B_DIAG hostPosition=" + juce::String(kDiagHostPosition)
            + " inProcessGain=" + juce::String(inProcessDiagGain, 4)
            + " sandboxAlignedGain=" + juce::String(sandboxAlignedGain, 4)
            + " sandboxLatency=" + juce::String(sandboxLatency)
            + " submitted=" + juce::String(static_cast<juce::int64>(submittedQuanta))
            + " automationEnabled=" + juce::String(proxyB->automationEnabled() ? 1 : 0)
            + " published=" + juce::String(static_cast<juce::int64>(
                proxyB->automationDiagnostics().latestPublishedSequence))
            + " applied=" + juce::String(static_cast<juce::int64>(
                proxyB->automationDiagnostics().workerAppliedEvents)));

        // ── 3. PARITY: after compensating only ─
        // for the sandbox effective latency, both arms must render the SAME
        // canonical automation. The sandbox pre-latency region (frozen
        // silence) is skipped.
        expect(sandboxLatency > 0, "sandbox effective latency must be positive");
        int mismatches = 0;
        const int comparable = kTotalSamples - sandboxLatency;
        for (int n = 0; n < comparable && mismatches < 4; ++n)
        {
            const float a = recordedA[static_cast<std::size_t>(n)];
            const float b = recordedB[static_cast<std::size_t>(n + sandboxLatency)];
            if (std::abs(a - b) >= 0.0005f)
            {
                ++mismatches;
                if (mismatches == 1)
                    logMessage("APEX_E2B_PARITY_MISMATCH firstAt=" + juce::String(n)
                        + " inProcess=" + juce::String(a, 4)
                        + " sandboxAligned=" + juce::String(b, 4));
            }
        }
        expect(mismatches == 0,
               "canonical automation parity failed: the sandboxed plugin did not"
               " receive the same canonical automation as InProcess. "
               + juce::String(mismatches)
               + " misaligned sample(s) of " + juce::String(comparable));

        // ── 4. Determinism sanity: both arms processed the full schedule ───
        expect(submittedQuanta == kTotalSamples / kBlockSamples,
               "sandbox quanta count mismatch");

        chainA.removePlugin(0);
        chainB.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B canonical automation parity");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BCanonicalBlockAutomationParityTest pluginSandboxPhaseE2BCanonicalBlockAutomationParityTest;

// ── E2B delivery gate: a failed audio submit must not commit lastDelivered, ──
// ── and the next canonical callback must deliver the current value. ───────────
class PluginSandboxPhaseE2BDeliveryStateResubmitTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BDeliveryStateResubmitTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.delivery-state-resubmit.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("failed canonical delivery remains pending until a later submit succeeds");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        constexpr double kSampleRate = 48000.0;
        constexpr double kBpm = 120.0;
        constexpr int kQuantum = 512;
        const juce::String trackId = "e2b-delivery-resubmit-track";

        DAW::PluginChainCore chain;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        chain.prepare(kSampleRate, 1024);

        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.blockSamples = kQuantum;
        preparation.maximumHostBlockSamples = 1024;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.workerExecutablePathForTest = worker.getFullPathName();
        expectEquals(chain.appendSandboxedPlugin(
                         automationFixtureDescription(identity), error, preparation),
                     0, "sandboxed delivery fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "delivery-resubmit sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "delivery-resubmit metadata fetch failed: " + error);
        juce::String param0Id;
        juce::String param1Id;
        expect(findParameterIdForOrdinal(metadata, 0, param0Id),
               "delivery-resubmit param_0 metadata is missing");
        expect(findParameterIdForOrdinal(metadata, 1, param1Id),
               "delivery-resubmit ordinal-1 metadata is missing");
        expect(slot->sandboxAutomationBindingCountForTesting() >= 2,
               "delivery-resubmit did not publish the param_0 and ordinal-1 E2B bindings");
        if (param0Id.isEmpty() || param1Id.isEmpty())
            return;

        // Establish a non-default E1 shadow, then rebuild the E2B candidate from
        // the actual worker values. The shadow must remain unchanged by all
        // canonical playback delivery and live-value observations below.
        expect(proxy->setParameter(param0Id, 0.45f, error),
               "delivery-resubmit param_0 setup failed: " + error);
        expect(proxy->setParameter(param1Id, 0.55f, error),
               "delivery-resubmit param_1 setup failed: " + error);
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        const auto shadowBefore = proxy->lastAuthoritativeParameters();

        DAW::AutomationSnapshot snapshot;
        const auto pluginPrefix = "plugin.0." + slot->getPluginInstanceId() + ".";
        snapshot.lanes.push_back({
            trackId, pluginPrefix + param0Id, true,
            {
                { 0,    0.25f, DAW::AutomationCurveType::Linear, 0.0f },
                { 512,  0.90f, DAW::AutomationCurveType::Linear, 0.0f },
                { 1024, 0.70f, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        snapshot.lanes.push_back({
            trackId, pluginPrefix + param1Id, true,
            {
                { 0,    0.75f, DAW::AutomationCurveType::Linear, 0.0f },
                { 512,  0.10f, DAW::AutomationCurveType::Linear, 0.0f },
                { 1024, 0.30f, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        auto makeBlock = [](int samples)
        {
            juce::AudioBuffer<float> block(2, samples);
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill(
                    block.getWritePointer(channel), 1.0f, samples);
            return block;
        };

        // A: successful canonical delivery of both ordinals.
        chain.applyAutomationAtSample(trackId, &snapshot, 0,
                                      kSampleRate, kBpm, kQuantum);
        const float deliveredA0 = slot->sandboxLastAutomationValueForTesting(0);
        const float deliveredA1 = slot->sandboxLastAutomationValueForTesting(1);
        expect(std::abs(deliveredA0 - deliveredA1) > 0.01f,
               "param_0 and param_1 canonical A values were not independent");
        auto blockA = makeBlock(kQuantum);
        chain.processBlock(blockA, kQuantum);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);

        float workerA0 = -1.0f;
        float workerA1 = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, workerA0, error),
               "worker param_0 A fetch failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, 1, workerA1, error),
               "worker param_1 A fetch failed: " + error);
        expectWithinAbsoluteError(workerA0, deliveredA0, 0.0005f,
                                  "worker param_0 did not receive A");
        expectWithinAbsoluteError(workerA1, deliveredA1, 0.0005f,
                                  "worker param_1 did not receive A");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  deliveredA0, 0.0005f,
                                  "lastDelivered param_0 did not commit A");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(1),
                                  deliveredA1, 0.0005f,
                                  "lastDelivered param_1 did not commit A");
        const auto countersAfterA = proxy->lifecycleDiagnostics().transport;
        const auto appliedAfterA = proxy->automationDiagnostics().workerAppliedEvents;

        // B: canonical values change, but the corresponding audio submit fails.
        chain.applyAutomationAtSample(trackId, &snapshot, 512,
                                      kSampleRate, kBpm, kQuantum);
        const float failedB0 = slot->sandboxLastAutomationValueForTesting(0);
        const float failedB1 = slot->sandboxLastAutomationValueForTesting(1);
        proxy->forceNextSubmissionFailureForTest(1);
        auto blockB = makeBlock(kQuantum);
        chain.processBlock(blockB, kQuantum);

        const auto countersAfterFailure = proxy->lifecycleDiagnostics().transport;
        const auto appliedAfterFailure = proxy->automationDiagnostics().workerAppliedEvents;
        float workerAfterFailure0 = -1.0f;
        float workerAfterFailure1 = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, workerAfterFailure0, error),
               "worker param_0 fetch after failed submit failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, 1, workerAfterFailure1, error),
               "worker param_1 fetch after failed submit failed: " + error);
        expect(countersAfterFailure.submitMisses == countersAfterA.submitMisses + 1,
               "forced submit failure was not recorded");
        expect(countersAfterFailure.submitted == countersAfterA.submitted,
               "failed submit was counted as a successful submission");
        expect(appliedAfterFailure == appliedAfterA,
               "worker applied the failed B batch");
        expectWithinAbsoluteError(workerAfterFailure0, workerA0, 0.0005f,
                                  "worker param_0 changed after failed B submit");
        expectWithinAbsoluteError(workerAfterFailure1, workerA1, 0.0005f,
                                  "worker param_1 changed after failed B submit");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  deliveredA0, 0.0005f,
                                  "lastDelivered param_0 advanced after failed submit");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(1),
                                  deliveredA1, 0.0005f,
                                  "lastDelivered param_1 advanced after failed submit");

        // C: the smoother continues from B toward the current canonical value;
        // the next callback must publish and commit C even though B failed.
        chain.applyAutomationAtSample(trackId, &snapshot, 1024,
                                      kSampleRate, kBpm, kQuantum);
        const float retryCurrent0 = slot->sandboxLastAutomationValueForTesting(0);
        const float retryCurrent1 = slot->sandboxLastAutomationValueForTesting(1);
        auto blockC = makeBlock(kQuantum);
        chain.processBlock(blockC, kQuantum);
        const auto countersAfterRetry = proxy->lifecycleDiagnostics().transport;
        waitForWorkerQuanta(proxy, countersAfterRetry.submitted, 5000);

        float workerAfterRetry0 = -1.0f;
        float workerAfterRetry1 = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, workerAfterRetry0, error),
               "worker param_0 fetch after retry failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, 1, workerAfterRetry1, error),
               "worker param_1 fetch after retry failed: " + error);
        expect(countersAfterRetry.submitted == countersAfterA.submitted + 1,
               "successful retry did not submit exactly one new quantum");
        expect(countersAfterRetry.submitMisses == countersAfterA.submitMisses + 1,
               "retry changed the submit-miss count unexpectedly");
        expect(proxy->automationDiagnostics().workerAppliedEvents == appliedAfterA + 2,
               "retry did not apply exactly the two current ordinal values");
        expectWithinAbsoluteError(workerAfterRetry0, retryCurrent0, 0.0005f,
                                  "worker param_0 did not receive current retry value");
        expectWithinAbsoluteError(workerAfterRetry1, retryCurrent1, 0.0005f,
                                  "worker param_1 did not receive current retry value");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  retryCurrent0, 0.0005f,
                                  "final lastDelivered param_0 is not the retry value");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(1),
                                  retryCurrent1, 0.0005f,
                                  "final lastDelivered param_1 is not the retry value");
        expect(proxy->lastAuthoritativeParameters() == shadowBefore,
               "canonical playback/live reads mutated the E1 parameter shadow");

        logMessage("APEX_E2B_DELIVERY_RESUBMIT_DIAG failedB="
            + juce::String(failedB0, 4) + "/" + juce::String(failedB1, 4)
            + " retryCurrent=" + juce::String(retryCurrent0, 4) + "/"
            + juce::String(retryCurrent1, 4)
            + " workerAfterRetry=" + juce::String(workerAfterRetry0, 4) + "/"
            + juce::String(workerAfterRetry1, 4)
            + " submitMisses=" + juce::String(static_cast<juce::int64>(
                countersAfterRetry.submitMisses))
            + " submitted=" + juce::String(static_cast<juce::int64>(
                countersAfterRetry.submitted))
            + " ordinal=1 shadowUnchanged=1");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B delivery-state resubmit");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BDeliveryStateResubmitTest pluginSandboxPhaseE2BDeliveryStateResubmitTest;

// ── E2B reset gate: normal reset-stream creates a new transport generation, ──
// ── clears staged delivery, and seeds E2B from the post-reset worker value. ───
class PluginSandboxPhaseE2BResetStreamAutomationRebindTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BResetStreamAutomationRebindTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.reset-stream-automation-rebind.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("normal reset-stream rebinds E2B to the current worker generation");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        constexpr double kSampleRate = 48000.0;
        constexpr double kBpm = 120.0;
        constexpr int kQuantum = 512;
        const juce::String trackId = "e2b-reset-rebind-track";

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
                         automationFixtureDescription(identity), error, preparation),
                     0, "sandboxed reset fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "reset-rebind sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "reset-rebind metadata fetch failed: " + error);
        juce::String param0Id;
        juce::String param1Id;
        expect(findParameterIdForOrdinal(metadata, 0, param0Id),
               "reset-rebind param_0 metadata is missing");
        expect(findParameterIdForOrdinal(metadata, 1, param1Id),
               "reset-rebind ordinal-1 metadata is missing");
        if (param0Id.isEmpty() || param1Id.isEmpty())
            return;

        expect(proxy->setParameter(param0Id, 0.63f, error),
               "reset-rebind param_0 setup failed: " + error);
        expect(proxy->setParameter(param1Id, 0.37f, error),
               "reset-rebind param_1 setup failed: " + error);
        slot->configureAutomationContext(trackId, 0, nullptr, nullptr);
        const auto shadowBeforeReset = proxy->lastAuthoritativeParameters();

        const auto pluginPrefix = "plugin.0." + slot->getPluginInstanceId() + ".";
        DAW::AutomationSnapshot snapshot;
        snapshot.lanes.push_back({
            trackId, pluginPrefix + param0Id, true,
            {
                { 0,   0.20f, DAW::AutomationCurveType::Linear, 0.0f },
                { 512, 0.85f, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        snapshot.lanes.push_back({
            trackId, pluginPrefix + param1Id, true,
            {
                { 0,   0.80f, DAW::AutomationCurveType::Linear, 0.0f },
                { 512, 0.15f, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        auto makeBlock = [](int samples)
        {
            juce::AudioBuffer<float> block(2, samples);
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill(
                    block.getWritePointer(channel), 1.0f, samples);
            return block;
        };

        // Establish a successful pre-reset canonical delivery.
        chain.applyAutomationAtSample(trackId, &snapshot, 0,
                                      kSampleRate, kBpm, kQuantum);
        auto initialBlock = makeBlock(kQuantum);
        chain.processBlock(initialBlock, kQuantum);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        expect(proxy->automationDiagnostics().workerAppliedEvents == 2,
               "reset-rebind setup did not deliver both pre-reset ordinals");

        // Change the actual worker state without changing the E2B seed mirrors;
        // the reset test must seed from this final live state, not old mirrors.
        expect(proxy->setParameter(param0Id, 0.63f, error),
               "reset-rebind final param_0 setup failed: " + error);
        expect(proxy->setParameter(param1Id, 0.37f, error),
               "reset-rebind final param_1 setup failed: " + error);

        const auto oldSession = proxy->processDiagnostics().sessionToken;
        const auto oldGeneration = proxy->currentGeneration();
        const auto oldSidecarGeneration = proxy->automationTransportForTest().generation();
        const auto oldMappingName = proxy->automationTransportForTest().mappingName();
        const auto oldLastDelivered0 = slot->sandboxLastDeliveredValueForTesting(0);
        const auto oldLastDelivered1 = slot->sandboxLastDeliveredValueForTesting(1);

        // Stage, but deliberately do not process, a new canonical boundary.
        // Reset must discard this pending delivery record before any new
        // current-generation callback can run.
        chain.applyAutomationAtSample(trackId, &snapshot, 512,
                                      kSampleRate, kBpm, kQuantum);
        expect(std::abs(slot->sandboxLastAutomationValueForTesting(0)
                        - oldLastDelivered0) > 0.0001f,
               "reset-rebind did not stage a pre-reset param_0 delivery");
        expect(std::abs(slot->sandboxLastAutomationValueForTesting(1)
                        - oldLastDelivered1) > 0.0001f,
               "reset-rebind did not stage a pre-reset param_1 delivery");

        slot->reset();

        const auto newSession = proxy->processDiagnostics().sessionToken;
        const auto newGeneration = proxy->currentGeneration();
        auto& currentSidecar = proxy->automationTransportForTest();
        const auto resetAutomationDiagnostics = proxy->automationDiagnostics();
        const auto resetTransportCounters = proxy->lifecycleDiagnostics().transport;
        expect(proxy->isPrepared() && proxy->isActive(),
               "normal reset-stream did not return the proxy to Active");
        expect(newGeneration != oldGeneration,
               "normal reset-stream did not advance the audio generation");
        expect(oldSidecarGeneration != newGeneration,
               "normal reset-stream retained the old E2A generation");
        expect(newSession == oldSession,
               "normal reset-stream unexpectedly changed the sandbox session");
        expect(currentSidecar.isPrepared(),
               "current-generation E2A sidecar is not prepared");
        expect(currentSidecar.generation() == newGeneration,
               "E2A sidecar generation does not match the audio generation");
        expect(oldMappingName.isNotEmpty() && currentSidecar.mappingName() == oldMappingName,
               "reset-stream did not preserve the session-bound sidecar identity");
        expect(resetAutomationDiagnostics.latestPublishedSequence == 0
                   && resetAutomationDiagnostics.workerAppliedEvents == 0
                   && resetAutomationDiagnostics.workerInvalidBatches == 0,
               "reset-stream retained stale E2A sidecar diagnostics");
        expect(resetTransportCounters.calls == 0
                   && resetTransportCounters.submitted == 0
                   && resetTransportCounters.submitMisses == 0,
               "reset-stream retained stale audio transport counters");

        // No applyAutomationAtSample call here: if the pre-reset staged record
        // survived, this ordinary callback would consume it.
        const auto appliedBeforeNoApply = proxy->automationDiagnostics().workerAppliedEvents;
        auto noApplyBlock = makeBlock(kQuantum);
        juce::MidiBuffer noApplyMidi;
        slot->processBlock(noApplyBlock, noApplyMidi, kQuantum);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        expect(proxy->automationDiagnostics().workerAppliedEvents == appliedBeforeNoApply,
               "reset-stream consumed a stale pre-reset E2B delivery");

        float postResetWorker0 = -1.0f;
        float postResetWorker1 = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, postResetWorker0, error),
               "post-reset param_0 live fetch failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, 1, postResetWorker1, error),
               "post-reset param_1 live fetch failed: " + error);
        expect(slot->sandboxAutomationBindingCountForTesting() >= 2,
               "reset-stream did not retain/rebuild the param_0 and ordinal-1 E2B bindings");
        expectWithinAbsoluteError(slot->sandboxLastAutomationValueForTesting(0),
                                  postResetWorker0, 0.0005f,
                                  "post-reset param_0 smoother seed is stale");
        expectWithinAbsoluteError(slot->sandboxLastAutomationValueForTesting(1),
                                  postResetWorker1, 0.0005f,
                                  "post-reset param_1 smoother seed is stale");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(0),
                                  postResetWorker0, 0.0005f,
                                  "post-reset param_0 lastDelivered seed is stale");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(1),
                                  postResetWorker1, 0.0005f,
                                  "post-reset param_1 lastDelivered seed is stale");

        // Fresh canonical automation must use sequence 1 of the new staging
        // generation and reach both worker parameters.
        DAW::AutomationSnapshot postResetSnapshot;
        postResetSnapshot.lanes.push_back({
            trackId, pluginPrefix + param0Id, true,
            { { 0, 0.28f, DAW::AutomationCurveType::Linear, 0.0f } }});
        postResetSnapshot.lanes.push_back({
            trackId, pluginPrefix + param1Id, true,
            { { 0, 0.72f, DAW::AutomationCurveType::Linear, 0.0f } }});
        chain.applyAutomationAtSample(trackId, &postResetSnapshot, 0,
                                      kSampleRate, kBpm, kQuantum);
        const float postResetExpected0 = slot->sandboxLastAutomationValueForTesting(0);
        const float postResetExpected1 = slot->sandboxLastAutomationValueForTesting(1);
        auto postResetBlock = makeBlock(kQuantum);
        chain.processBlock(postResetBlock, kQuantum);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);

        float workerAfterFresh0 = -1.0f;
        float workerAfterFresh1 = -1.0f;
        expect(fetchLiveValueForIndex(*proxy, 0, workerAfterFresh0, error),
               "fresh post-reset param_0 live fetch failed: " + error);
        expect(fetchLiveValueForIndex(*proxy, 1, workerAfterFresh1, error),
               "fresh post-reset param_1 live fetch failed: " + error);
        expectWithinAbsoluteError(workerAfterFresh0, postResetExpected0, 0.0005f,
                                  "new-generation param_0 automation was not consumed");
        expectWithinAbsoluteError(workerAfterFresh1, postResetExpected1, 0.0005f,
                                  "new-generation param_1 automation was not consumed");
        expect(proxy->lastAuthoritativeParameters() == shadowBeforeReset,
               "playback automation/reset mutated the E1 parameter shadow");

        logMessage("APEX_E2B_RESET_REBIND_DIAG oldSession=" + oldSession
            + " newSession=" + newSession
            + " oldGeneration=" + juce::String(static_cast<juce::int64>(oldGeneration))
            + " newGeneration=" + juce::String(static_cast<juce::int64>(newGeneration))
            + " postResetWorker=" + juce::String(postResetWorker0, 4) + "/"
            + juce::String(postResetWorker1, 4)
            + " postResetSeeds=" + juce::String(
                slot->sandboxLastDeliveredValueForTesting(0), 4) + "/"
            + juce::String(slot->sandboxLastDeliveredValueForTesting(1), 4)
            + " bindings=" + juce::String(slot->sandboxAutomationBindingCountForTesting())
            + " stalePendingCleared=1");

        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B reset-stream automation rebind");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BResetStreamAutomationRebindTest pluginSandboxPhaseE2BResetStreamAutomationRebindTest;

// ── E2B realtime gate: the complete canonical producer → chain → sandbox ────
// ── callback remains bounded after warm-up, including bypass/mix branches. ───
class PluginSandboxPhaseE2BZeroRtTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BZeroRtTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.zero-rt.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("sustained E2B canonical processing is allocation-free and nonblocking");
       #if ! JUCE_ENABLE_ALLOCATION_HOOKS
        expect(false, "JUCE allocation hooks are required for the E2B zero-RT proof");
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

        constexpr double kSampleRate = 48000.0;
        constexpr double kBpm = 120.0;
        constexpr int kQuantum = 512;
        constexpr int kSustainedCallbacks = 120;
        const juce::String trackId = "e2b-zero-rt-track";

        // Slot-free control: the published empty chain must remain safe before
        // any slot topology is introduced. This also covers the zero-slot
        // replacement path without mutating topology in the measured region.
        {
            DAW::PluginChainCore emptyChain;
            emptyChain.setAutomationContext(trackId, nullptr, nullptr);
            emptyChain.prepare(kSampleRate, kQuantum);
            juce::AudioBuffer<float> emptyBlock(2, kQuantum);
            const int warmSizes[] = { 480, 64, 512 };
            for (const int samples : warmSizes)
            {
                emptyBlock.clear();
                emptyChain.applyAutomationAtSample(trackId, nullptr, 0,
                                                   kSampleRate, kBpm, samples);
                emptyChain.processBlock(emptyBlock, samples);
            }
            for (int callback = 0; callback < 12; ++callback)
            {
                const int samples = warmSizes[callback % 3];
                emptyBlock.clear();
                {
                    juce::UnitTestAllocationChecker allocationChecker(*this);
                    emptyChain.applyAutomationAtSample(
                        trackId, nullptr, callback * kQuantum,
                        kSampleRate, kBpm, samples);
                    emptyChain.processBlock(emptyBlock, samples);
                }
            }
        }

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
                         automationFixtureDescription(identity), error, preparation),
                     0, "sandboxed zero-RT fixture insertion failed: " + error);

        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        expect(slot != nullptr && proxy != nullptr,
               "zero-RT sandbox slot/proxy is missing");
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "zero-RT metadata fetch failed: " + error);
        juce::String param0Id;
        juce::String param1Id;
        expect(findParameterIdForOrdinal(metadata, 0, param0Id),
               "zero-RT param_0 metadata is missing");
        expect(findParameterIdForOrdinal(metadata, 1, param1Id),
               "zero-RT ordinal-1 metadata is missing");
        expect(proxy->automationEnabled(),
               "zero-RT E2B sidecar is not enabled");
        expect(slot->sandboxAutomationBindingCountForTesting() >= 2,
               "zero-RT did not publish param_0 and ordinal-1 bindings");
        if (param0Id.isEmpty() || param1Id.isEmpty())
            return;

        // Build every lane and point before measurement. The first lookup also
        // warms AutomationSnapshot's immutable lane index on the control side.
        const auto pluginPrefix = "plugin.0." + slot->getPluginInstanceId() + ".";
        DAW::AutomationSnapshot snapshot;
        snapshot.lanes.push_back({
            trackId, pluginPrefix + param0Id, true,
            {
                { 0,    0.20f, DAW::AutomationCurveType::Linear, 0.0f },
                { 512,  0.80f, DAW::AutomationCurveType::Linear, 0.0f },
                { 1024, 0.80f, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        snapshot.lanes.push_back({
            trackId, pluginPrefix + param1Id, true,
            {
                { 0,    0.80f, DAW::AutomationCurveType::Linear, 0.0f },
                { 512,  0.20f, DAW::AutomationCurveType::Linear, 0.0f },
                { 1024, 0.20f, DAW::AutomationCurveType::Linear, 0.0f }
            }});

        juce::AudioBuffer<float> block(2, kQuantum);
        const auto fillBlock = [&block](int samples)
        {
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill(
                    block.getWritePointer(channel), 1.0f, samples);
        };

        // Warm the canonical producer, lane index, sidecar, reblocker and the
        // normal full-wet slot route before opening allocation measurement.
        chain.applyAutomationAtSample(trackId, &snapshot, 0,
                                      kSampleRate, kBpm, kQuantum);
        fillBlock(kQuantum);
        chain.processBlock(block, kQuantum);
        waitForWorkerQuanta(proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        const float lastDeliveredBeforeFailure0 =
            slot->sandboxLastDeliveredValueForTesting(0);
        const float lastDeliveredBeforeFailure1 =
            slot->sandboxLastDeliveredValueForTesting(1);

        // Measure a changed canonical boundary with an intentionally failed
        // audio submit. The test seam is armed outside the measured callback;
        // the failure bookkeeping itself runs inside the measured path.
        chain.applyAutomationAtSample(trackId, &snapshot, 512,
                                      kSampleRate, kBpm, kQuantum);
        proxy->forceNextSubmissionFailureForTest(1);
        fillBlock(kQuantum);
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.applyAutomationAtSample(trackId, &snapshot, 512,
                                          kSampleRate, kBpm, kQuantum);
            chain.processBlock(block, kQuantum);
        }
        const float lastDeliveredAfterFailure0 =
            slot->sandboxLastDeliveredValueForTesting(0);
        const float lastDeliveredAfterFailure1 =
            slot->sandboxLastDeliveredValueForTesting(1);

        // The following callback must resubmit the current canonical values;
        // no retry container may be allocated by the failed prior submit.
        fillBlock(kQuantum);
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.applyAutomationAtSample(trackId, &snapshot, 1024,
                                          kSampleRate, kBpm, kQuantum);
            chain.processBlock(block, kQuantum);
        }
        waitForWorkerQuanta(proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);
        const float lastDeliveredAfterRetry0 =
            slot->sandboxLastDeliveredValueForTesting(0);
        const float lastDeliveredAfterRetry1 =
            slot->sandboxLastDeliveredValueForTesting(1);

        // Sustained steady-state measurement. The callback sizes intentionally
        // cross the fixed-Q boundary and include 480, 64, and 512 samples.
        const int callbackSizes[] = { 480, 64, 512 };
        std::int64_t samplePosition = 1536;
        for (int callback = 0; callback < kSustainedCallbacks; ++callback)
        {
            const int samples = callbackSizes[callback % 3];
            fillBlock(samples);
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                              kSampleRate, kBpm, samples);
                chain.processBlock(block, samples);
            }
            waitForWorkerQuanta(proxy,
                                proxy->lifecycleDiagnostics().transport.submitted,
                                5000);
            samplePosition += samples;
        }

        // Bypass, zero mix, normal wet/dry, and first unbypassed callback are
        // all control-plane topology/mode decisions; only their callbacks are
        // measured below.
        chain.setSlotMix(0, 0.0f);
        fillBlock(kQuantum);
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                          kSampleRate, kBpm, kQuantum);
            chain.processBlock(block, kQuantum);
        }
        waitForWorkerQuanta(proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        chain.setSlotMix(0, 0.5f);
        fillBlock(kQuantum);
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                          kSampleRate, kBpm, kQuantum);
            chain.processBlock(block, kQuantum);
        }
        waitForWorkerQuanta(proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        chain.setSlotBypassed(0, true);
        fillBlock(kQuantum);
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                          kSampleRate, kBpm, kQuantum);
            chain.processBlock(block, kQuantum);
        }
        waitForWorkerQuanta(proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        chain.setSlotBypassed(0, false);
        fillBlock(kQuantum);
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                          kSampleRate, kBpm, kQuantum);
            chain.processBlock(block, kQuantum);
        }
        waitForWorkerQuanta(proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);

        const auto finalTransport = proxy->lifecycleDiagnostics().transport;
        const auto finalLifecycle = proxy->lifecycleDiagnostics();
        const auto finalAutomation = proxy->automationDiagnostics();

        // These are POD observations captured after all measured scopes have
        // closed. The single submit miss is the deliberate failure above.
        expect(std::abs(lastDeliveredAfterFailure0 - lastDeliveredBeforeFailure0)
                   < 0.0005f
                   && std::abs(lastDeliveredAfterFailure1 - lastDeliveredBeforeFailure1)
                          < 0.0005f,
               "zero-RT failed submit advanced lastDelivered");
        expect(std::abs(lastDeliveredAfterRetry0 - lastDeliveredAfterFailure0) > 0.0001f
                   || std::abs(lastDeliveredAfterRetry1 - lastDeliveredAfterFailure1) > 0.0001f,
               "zero-RT successful retry did not commit a current value");
        expect(finalTransport.submitMisses == 1,
               "zero-RT observed an unexpected additional submit miss");
        expect(finalAutomation.workerInvalidBatches == 0,
               "zero-RT produced invalid automation batches");
        expect(finalAutomation.parentOverflowRejected == 0,
               "zero-RT produced an automation overflow");
        expect(finalTransport.boundsRejected == 0,
               "zero-RT produced an audio bounds reject");
        expect(finalTransport.metadataRejected == 0,
               "zero-RT produced a stale/mismatched audio result");
        expect(finalLifecycle.activeRealtimeCalls == 0,
               "zero-RT left a realtime call active");

        logMessage("APEX_E2B_ZERO_RT_DIAG callbacks="
            + juce::String(kSustainedCallbacks + 6)
            + " submitMisses=" + juce::String(static_cast<juce::int64>(
                finalTransport.submitMisses))
            + " submitted=" + juce::String(static_cast<juce::int64>(
                finalTransport.submitted))
            + " invalidBatches=" + juce::String(static_cast<juce::int64>(
                finalAutomation.workerInvalidBatches))
            + " overflow=" + juce::String(static_cast<juce::int64>(
                finalAutomation.parentOverflowRejected))
            + " ordinal=1 allocationChecked=1 nonblocking=1");

        // Add a second already-prepared slot only outside measurement, then
        // process the published multi-slot state through the same measured
        // chain route. This exercises the replacement wet/dry storage with a
        // stable multi-slot topology rather than mutating slots in RT.
        const int secondSlotIndex = chain.appendSandboxedPlugin(
            automationFixtureDescription(identity), error, preparation);
        expectEquals(secondSlotIndex, 1,
                     "zero-RT multi-slot fixture insertion failed: " + error);
        if (secondSlotIndex == 1)
        {
            chain.setSlotMix(0, 1.0f);
            chain.setSlotMix(1, 0.5f);
            chain.applyAutomationAtSample(trackId, &snapshot, samplePosition,
                                          kSampleRate, kBpm, kQuantum);
            fillBlock(kQuantum);
            chain.processBlock(block, kQuantum);
            auto* secondProxy = chain.getSlot(1) != nullptr
                ? chain.getSlot(1)->getSandboxProxy() : nullptr;
            if (secondProxy != nullptr)
                waitForWorkerQuanta(secondProxy,
                                    secondProxy->lifecycleDiagnostics().transport.submitted,
                                    5000);

            for (int callback = 0; callback < 12; ++callback)
            {
                fillBlock(kQuantum);
                {
                    juce::UnitTestAllocationChecker allocationChecker(*this);
                    chain.applyAutomationAtSample(trackId, &snapshot,
                                                  samplePosition + callback * kQuantum,
                                                  kSampleRate, kBpm, kQuantum);
                    chain.processBlock(block, kQuantum);
                }
                if (secondProxy != nullptr)
                    waitForWorkerQuanta(secondProxy,
                                        secondProxy->lifecycleDiagnostics().transport.submitted,
                                        5000);
            }
        }

        chain.removePlugin(1);
        chain.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B zero-RT certification");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BZeroRtTest pluginSandboxPhaseE2BZeroRtTest;

// ── E2B bypass gate: canonical automation continues through an audible ──────
// ── slot-output bypass while the worker's processed result is discarded. ────
class PluginSandboxPhaseE2BBypassAutomationParityTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BBypassAutomationParityTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.bypass-automation-parity.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest("canonical automation delivery continues through slot output bypass");
        juce::MessageManager::getInstance();

        const auto worker = productionWorkerExecutable();
        expect(worker.existsAsFile(), "production worker executable is missing");
        if (! worker.existsAsFile())
            return;

        const auto identity = discoverAutomationFixtureIdentity(worker);
        expect(identity.uniqueId != 0, "automation fixture identity discovery failed");
        if (identity.uniqueId == 0)
            return;

        const juce::String trackId = "e2b-bypass-parity-track";
        constexpr double kSampleRate = 48000.0;
        constexpr double kBpm = 120.0;

        // Arm A: the existing canonical InProcess sink.
        DAW::PluginChainCore chainA;
        chainA.setAutomationContext(trackId, nullptr, nullptr);
        chainA.prepare(kSampleRate, 2048);
        auto inProcessProbe = std::make_unique<E2BInProcessGainProbeProcessor>();
        expectEquals(chainA.appendPluginInstanceForTesting(std::move(inProcessProbe)), 0,
                     "in-process probe slot insert failed");
        chainA.getSlot(0)->configureAutomationContext(trackId, 0, nullptr, nullptr);

        // Arm B: the production sandbox sink.
        DAW::PluginChainCore chainB;
        chainB.setAutomationContext(trackId, nullptr, nullptr);
        chainB.prepare(kSampleRate, 2048);
        juce::String error;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = worker.getFullPathName();
       #endif
        expectEquals(chainB.appendSandboxedPlugin(
                         automationFixtureDescription(identity), error, preparation),
                     0, "sandboxed slot insert failed: " + error);

        auto* slotA = chainA.getSlot(0);
        auto* slotB = chainB.getSlot(0);
        expect(slotA != nullptr && slotB != nullptr, "bypass parity slots are missing");
        if (slotA == nullptr || slotB == nullptr)
            return;
        expect(! slotA->isSandboxed(), "bypass arm A must be InProcess");
        expect(slotB->isSandboxed(), "bypass arm B must be Sandboxed");

        auto* proxyB = slotB->getSandboxProxy();
        expect(proxyB != nullptr, "bypass parity sandbox proxy is missing");
        if (proxyB == nullptr)
            return;

        // The worker metadata is authoritative for the sandbox lane identity.
        juce::Array<DAW::SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxyB->fetchParameterMetadata(metadata, error),
               "sandbox parameter metadata fetch failed: " + error);
        int sandboxParameterIndex = -1;
        juce::String sandboxParameterId;
        for (const auto& parameter : metadata)
            if (parameter.index == 0)
            {
                sandboxParameterIndex = parameter.index;
                sandboxParameterId = parameter.parameterId;
                break;
            }
        expectEquals(sandboxParameterIndex, 0,
                     "sandbox ordinal-zero metadata entry is missing");
        expect(sandboxParameterId.isNotEmpty(),
               "sandbox ordinal-zero metadata ID is missing");
        if (sandboxParameterIndex < 0 || sandboxParameterId.isEmpty())
            return;

        float initialSandboxValue = 0.0f;
        expect(fetchLiveValueForIndex(*proxyB, sandboxParameterIndex,
                                      initialSandboxValue, error),
               "initial live worker value fetch failed: " + error);
        expectWithinAbsoluteError(initialSandboxValue, 0.5f, 0.0005f,
                                  "initial worker gain seed");
        const auto shadowBefore = proxyB->lastAuthoritativeParameters();

        DAW::AutomationSnapshot snapshot;
        snapshot.lanes.push_back({
            trackId,
            "plugin.0." + slotA->getPluginInstanceId() + ".gain",
            true,
            {
                { 0,    0.20f, DAW::AutomationCurveType::Linear, 0.0f },
                { 480,  0.80f, DAW::AutomationCurveType::Linear, 0.0f },
                { 544,  0.10f, DAW::AutomationCurveType::Linear, 0.0f },
                { 1056, 0.90f, DAW::AutomationCurveType::Linear, 0.0f },
                { 1536, 0.30f, DAW::AutomationCurveType::Linear, 0.0f },
                { 1600, 0.70f, DAW::AutomationCurveType::Linear, 0.0f },
                { 2112, 0.40f, DAW::AutomationCurveType::Linear, 0.0f }
            }});
        snapshot.lanes.push_back({
            trackId,
            "plugin.0." + slotB->getPluginInstanceId() + "." + sandboxParameterId,
            true,
            snapshot.lanes.front().points
        });

        // First prove the explicit slotMixBypass branch, then the effective
        // zero-mix branch. Both are audible dry-output conditions.
        chainA.setSlotMixBypass(0, true);
        chainB.setSlotMixBypass(0, true);

        const int sizes[] = { 480, 64, 512, 480, 64, 512 };
        constexpr int kCallbackCount = 6;
        int64_t position = 0;
        std::uint64_t observedSubmissions = 0;
        float lastInProcessValue = initialSandboxValue;
        float lastSandboxValue = initialSandboxValue;
        juce::String slotMixBypassValues;
        juce::String zeroMixValues;
        juce::String mappingEvidence;

        for (int callback = 0; callback < kCallbackCount; ++callback)
        {
            if (callback == 3)
            {
                chainA.setSlotMixBypass(0, false);
                chainB.setSlotMixBypass(0, false);
                chainA.setSlotMix(0, 0.0f);
                chainB.setSlotMix(0, 0.0f);
            }

            const int size = sizes[callback];
            juce::AudioBuffer<float> blockA(2, size);
            juce::AudioBuffer<float> blockB(2, size);
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::FloatVectorOperations::fill(blockA.getWritePointer(ch), 0.37f, size);
                juce::FloatVectorOperations::fill(blockB.getWritePointer(ch), 0.37f, size);
            }

            chainA.applyAutomationAtSample(trackId, &snapshot, position,
                                           kSampleRate, kBpm, size);
            chainB.applyAutomationAtSample(trackId, &snapshot, position,
                                           kSampleRate, kBpm, size);

            auto* inProcessParameter = slotA->getProcessor()->getParameters()[0];
            const float expectedInProcess = inProcessParameter != nullptr
                ? inProcessParameter->getValue() : -1.0f;
            expect(expectedInProcess >= 0.0f,
                   "InProcess canonical parameter is unavailable");
            lastInProcessValue = expectedInProcess;

            chainA.processBlock(blockA, size);
            chainB.processBlock(blockB, size);

            bool dryA = true;
            bool dryB = true;
            for (int s = 0; s < size; ++s)
            {
                dryA = dryA && std::abs(blockA.getSample(0, s) - 0.37f) < 0.0005f;
                dryB = dryB && std::abs(blockB.getSample(0, s) - 0.37f) < 0.0005f;
            }
            expect(dryA, "InProcess audible output was not bypassed/dry");
            expect(dryB, "Sandbox audible output leaked processed audio while bypassed");

            const auto submitted = proxyB->lifecycleDiagnostics().transport.submitted;
            waitForWorkerQuanta(proxyB, submitted, 5000);
            if (submitted > observedSubmissions)
            {
                float actualSandbox = -1.0f;
                expect(fetchLiveValueForIndex(*proxyB, sandboxParameterIndex,
                                              actualSandbox, error),
                       "worker live value fetch during bypass failed: " + error);
                expectWithinAbsoluteError(actualSandbox, expectedInProcess, 0.0005f,
                                          "worker/InProcess canonical value mismatch during bypass");
                lastSandboxValue = actualSandbox;
                observedSubmissions = submitted;

                auto& values = callback < 3 ? slotMixBypassValues : zeroMixValues;
                if (values.isNotEmpty())
                    values += ";";
                values += juce::String(expectedInProcess, 4) + "/"
                    + juce::String(actualSandbox, 4);
            }

            if (callback == 0)
                expectEquals(submitted, static_cast<std::uint64_t>(0),
                             "480-sample callback unexpectedly crossed a sandbox quantum");
            mappingEvidence += "cb" + juce::String(callback)
                + " abs" + juce::String(position)
                + " seq" + juce::String(static_cast<juce::int64>(position / 512 + 1))
                + " local" + juce::String(static_cast<int>(position % 512))
                + " submitted" + juce::String(static_cast<juce::int64>(submitted))
                + ";";
            position += size;
        }

        expect(observedSubmissions > 0,
               "sandbox submitted no worker quanta during audible bypass");
        expect(proxyB->automationDiagnostics().workerAppliedEvents > 0,
               "worker applied no E2B events during audible bypass");
        expectWithinAbsoluteError(lastSandboxValue, lastInProcessValue, 0.0005f,
                                  "final bypass worker value does not match InProcess");

        // The E1 shadow must remain untouched by the live-value observations.
        expect(proxyB->lastAuthoritativeParameters() == shadowBefore,
               "live-value reads mutated the E1 parameter shadow");

        // Unbypass without a synchronization callback. The first callback must
        // already start from the current canonical parameter value.
        chainA.setSlotMix(0, 1.0f);
        chainB.setSlotMix(0, 1.0f);
        const int firstUnbypassedSize = 512;
        juce::AudioBuffer<float> firstA(2, firstUnbypassedSize);
        juce::AudioBuffer<float> firstB(2, firstUnbypassedSize);
        firstA.clear();
        firstB.clear();
        chainA.applyAutomationAtSample(trackId, &snapshot, position,
                                       kSampleRate, kBpm, firstUnbypassedSize);
        chainB.applyAutomationAtSample(trackId, &snapshot, position,
                                       kSampleRate, kBpm, firstUnbypassedSize);
        const float firstInProcess = slotA->getProcessor()->getParameters()[0]->getValue();
        chainA.processBlock(firstA, firstUnbypassedSize);
        chainB.processBlock(firstB, firstUnbypassedSize);
        const auto firstSubmitted = proxyB->lifecycleDiagnostics().transport.submitted;
        waitForWorkerQuanta(proxyB, firstSubmitted, 5000);
        float firstSandbox = -1.0f;
        expect(fetchLiveValueForIndex(*proxyB, sandboxParameterIndex,
                                      firstSandbox, error),
               "first unbypassed worker live value fetch failed: " + error);
        expectWithinAbsoluteError(firstSandbox, firstInProcess, 0.0005f,
                                  "first unbypassed worker value required stale catch-up");

        logMessage("APEX_E2B_BYPASS_DIAG callbacks=6 observedSubmissions="
                   + juce::String(static_cast<juce::int64>(observedSubmissions))
                   + " applied=" + juce::String(static_cast<juce::int64>(
                       proxyB->automationDiagnostics().workerAppliedEvents))
                   + " slotMixBypassInProcess/Sandbox=" + slotMixBypassValues
                   + " zeroMixInProcess/Sandbox=" + zeroMixValues
                   + " mapping=" + mappingEvidence
                   + " inProcessFinal=" + juce::String(lastInProcessValue, 4)
                   + " sandboxFinal=" + juce::String(lastSandboxValue, 4)
                   + " firstUnbypassedInProcess=" + juce::String(firstInProcess, 4)
                   + " firstUnbypassedSandbox=" + juce::String(firstSandbox, 4));

        chainA.removePlugin(0);
        chainB.removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B bypass automation parity");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BBypassAutomationParityTest pluginSandboxPhaseE2BBypassAutomationParityTest;

// ── E2B lifetime gate: a removed bypass core remains alive while an old ─────
// ── published snapshot is held by an audio-like reader, then is reclaimed ───
// ── only by the control-plane drain. ─────────────────────────────────────────
class PluginSandboxPhaseE2BBypassCoreSnapshotLifetimeTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BBypassCoreSnapshotLifetimeTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.bypass-core-snapshot-lifetime.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("retired bypass core waits for old snapshot quiescence");
        juce::MessageManager::getInstance();

        gBypassCoreDestructionCount.store(0, std::memory_order_release);
        gBypassCoreDestructionThreadId.store(0, std::memory_order_release);
        DAW::BypassCrossfadeCore::setDestructionObserverForTesting(
            &recordBypassCoreDestruction);

          {
              DAW::PluginChainCore chain;
            chain.prepare(48000.0, kBlockSamples);
            auto probe = std::make_unique<E2BInProcessGainProbeProcessor>();
            expectEquals(chain.appendPluginInstanceForTesting(std::move(probe)), 0,
                         "lifetime probe slot insert failed");

            std::atomic<bool> acquired { false };
            std::atomic<bool> release { false };
            std::shared_ptr<const DAW::PluginChainCore::Snapshot> heldSnapshot;

            std::thread audioLikeReader([&]
            {
                heldSnapshot = chain.acquirePublishedSnapshotForTesting();
                acquired.store(true, std::memory_order_release);
                while (! release.load(std::memory_order_acquire))
                    std::this_thread::yield();
                heldSnapshot.reset();
            });

            const auto deadline = GetTickCount64() + 5000;
            while (! acquired.load(std::memory_order_acquire)
                   && GetTickCount64() < deadline)
                std::this_thread::yield();

            expect(acquired.load(std::memory_order_acquire),
                   "audio-like reader did not acquire published snapshot");
            expect(heldSnapshot != nullptr,
                   "audio-like reader acquired a null published snapshot");
            if (heldSnapshot != nullptr)
            {
                expectEquals(static_cast<int>(heldSnapshot->slots.size()), 1,
                             "held snapshot does not contain the lifetime probe slot");
                expect(heldSnapshot->bypassCores.size() == 1
                           && heldSnapshot->bypassCores[0] != nullptr,
                       "held snapshot does not retain the bypass-core target");
            }

            chain.removePlugin(0);
            chain.drainRetiredBypassCores();
            expectEquals(gBypassCoreDestructionCount.load(std::memory_order_acquire), 0,
                         "bypass core was destroyed before old snapshot quiescence");
            expectEquals(static_cast<int>(chain.getPendingRetiredBypassCoreCountForTesting()), 1,
                         "quiescence-protected bypass core was not retained");

            release.store(true, std::memory_order_release);
            audioLikeReader.join();
            chain.drainRetiredBypassCores();

            expectEquals(gBypassCoreDestructionCount.load(std::memory_order_acquire), 1,
                         "retired bypass core was not reclaimed after reader release");
            expectEquals(static_cast<int>(chain.getPendingRetiredBypassCoreCountForTesting()), 0,
                         "quiescent bypass retirement record was not drained");
            expectEquals(gBypassCoreDestructionThreadId.load(std::memory_order_acquire),
                         GetCurrentThreadId(),
                         "bypass core was destroyed on the reader/audio thread");
         }

         // Replacement must use the real PluginChainCore::loadPlugin path,
         // not a test-only replacement shortcut. The old core is protected
         // by the snapshot acquired before replacement; the newly-created G10
         // core must remain the live slot owner.
         beginTest("replacement keeps old bypass core alive until snapshot quiescence");
         gBypassCoreDestructionCount.store(0, std::memory_order_release);
         gBypassCoreDestructionThreadId.store(0, std::memory_order_release);

         {
             DAW::PluginChainCore chain;
             chain.prepare(48000.0, kBlockSamples);
             auto probe = std::make_unique<E2BInProcessGainProbeProcessor>();
             expectEquals(chain.appendPluginInstanceForTesting(std::move(probe)), 0,
                          "replacement lifetime probe slot insert failed");

             std::atomic<bool> acquired { false };
             std::atomic<bool> release { false };
             std::shared_ptr<const DAW::PluginChainCore::Snapshot> heldSnapshot;
             std::thread audioLikeReader([&]
             {
                 heldSnapshot = chain.acquirePublishedSnapshotForTesting();
                 acquired.store(true, std::memory_order_release);
                 while (! release.load(std::memory_order_acquire))
                     std::this_thread::yield();
                 heldSnapshot.reset();
             });

             const auto deadline = GetTickCount64() + 5000;
             while (! acquired.load(std::memory_order_acquire)
                    && GetTickCount64() < deadline)
                 std::this_thread::yield();

             expect(acquired.load(std::memory_order_acquire),
                    "replacement reader did not acquire published snapshot");
             expect(heldSnapshot != nullptr,
                    "replacement reader acquired a null published snapshot");

             juce::AudioPluginFormatManager formats;
             formats.addFormat(std::make_unique<APEX::G10::G10NativePluginFormat>());
             juce::String error;
             const auto replacementDescription =
                 APEX::G10::G10NativePluginFormat::createG10Description();
             expect(chain.loadPlugin(0, replacementDescription, formats, error),
                    "production replacement failed: " + error);
             chain.drainRetiredBypassCores();
             expectEquals(gBypassCoreDestructionCount.load(std::memory_order_acquire), 0,
                          "replacement destroyed old bypass core before snapshot quiescence");
             expectEquals(static_cast<int>(chain.getPendingRetiredBypassCoreCountForTesting()), 1,
                          "replacement did not retain the old bypass-core owner");
             expect(chain.getSlot(0) != nullptr
                        && chain.getSlot(0)->getProcessor() != nullptr,
                    "replacement did not publish the new live plugin slot");

             release.store(true, std::memory_order_release);
             audioLikeReader.join();
             chain.drainRetiredBypassCores();
             expectEquals(gBypassCoreDestructionCount.load(std::memory_order_acquire), 1,
                          "replacement old bypass core was not reclaimed after quiescence");
             expectEquals(static_cast<int>(chain.getPendingRetiredBypassCoreCountForTesting()), 0,
                          "replacement retirement record was not drained");
             expectEquals(gBypassCoreDestructionThreadId.load(std::memory_order_acquire),
                          GetCurrentThreadId(),
                          "replacement bypass core was destroyed on the reader/audio thread");

             // Do not count the still-live replacement core when this scope
             // ends; the assertion above concerns only the retired old core.
              DAW::BypassCrossfadeCore::setDestructionObserverForTesting(nullptr);
          }

          // The replacement chain's new core is intentionally still active at
          // scope exit. Reclaim that quiescent shutdown group before resetting
          // the observer/counters for the independent chain-clear scenario.
          DAW::PluginChainCore::drainAllRetiredBypassCores();

          // The real chain-clear path is the PluginChainCore destructor. It
         // must hand off active-core ownership before clearing the chain's
         // mutable vectors, because an old published snapshot can outlive the
         // chain object during project replacement/shutdown.
         beginTest("chain clear keeps active bypass core alive until snapshot quiescence");
         gBypassCoreDestructionCount.store(0, std::memory_order_release);
         gBypassCoreDestructionThreadId.store(0, std::memory_order_release);
         DAW::BypassCrossfadeCore::setDestructionObserverForTesting(
             &recordBypassCoreDestruction);

         {
             auto chain = std::make_unique<DAW::PluginChainCore>();
             chain->prepare(48000.0, kBlockSamples);
             auto probe = std::make_unique<E2BInProcessGainProbeProcessor>();
             expectEquals(chain->appendPluginInstanceForTesting(std::move(probe)), 0,
                          "chain-clear lifetime probe slot insert failed");

             std::atomic<bool> acquired { false };
             std::atomic<bool> release { false };
             std::shared_ptr<const DAW::PluginChainCore::Snapshot> heldSnapshot;
             std::thread audioLikeReader([&]
             {
                 heldSnapshot = chain->acquirePublishedSnapshotForTesting();
                 acquired.store(true, std::memory_order_release);
                 while (! release.load(std::memory_order_acquire))
                     std::this_thread::yield();
                 heldSnapshot.reset();
             });

             const auto deadline = GetTickCount64() + 5000;
             while (! acquired.load(std::memory_order_acquire)
                    && GetTickCount64() < deadline)
                 std::this_thread::yield();

             expect(acquired.load(std::memory_order_acquire),
                    "chain-clear reader did not acquire published snapshot");
             expect(heldSnapshot != nullptr,
                    "chain-clear reader acquired a null published snapshot");

             chain.reset();
             expectEquals(gBypassCoreDestructionCount.load(std::memory_order_acquire), 0,
                          "chain clear destroyed active bypass core while old snapshot was held");

             DAW::PluginChainCore::drainAllRetiredBypassCores();
             expectEquals(gBypassCoreDestructionCount.load(std::memory_order_acquire), 0,
                          "premature control-plane drain reclaimed bypass core while old snapshot was held");

             release.store(true, std::memory_order_release);
             audioLikeReader.join();
             DAW::PluginChainCore::drainAllRetiredBypassCores();
             expectEquals(gBypassCoreDestructionCount.load(std::memory_order_acquire), 1,
                          "control-plane drain did not reclaim chain-clear bypass core after quiescence");
             expectEquals(gBypassCoreDestructionThreadId.load(std::memory_order_acquire),
                          GetCurrentThreadId(),
                          "chain-clear bypass core was destroyed on the reader/audio thread");
             DAW::PluginChainCore::drainAllRetiredBypassCores();
             expectEquals(gBypassCoreDestructionCount.load(std::memory_order_acquire), 1,
                          "chain-clear bypass core was destroyed more than once");
         }

         DAW::BypassCrossfadeCore::setDestructionObserverForTesting(nullptr);
       #else
        beginTest("Windows-only E2B bypass-core snapshot lifetime");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BBypassCoreSnapshotLifetimeTest pluginSandboxPhaseE2BBypassCoreSnapshotLifetimeTest;

// ═══════════════════════════════════════════════════════════════════════════
// FINAL E2B DEBUG EDGE MATRIX
//
// These tests deliberately use the dense 129-parameter VST3 component for the
// frozen ordinal/capacity boundaries.  The component is never used as a test
// double for the production producer: all canonical automation is still
// driven through PluginChainCore::applyAutomationAtSample().
// ═══════════════════════════════════════════════════════════════════════════

#if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS

using namespace DAW;

struct DenseE2BTestContext
{
    juce::File worker;
    juce::PluginDescription description;
    AutomationFixtureIdentity identity;
};

juce::PluginDescription makeDenseE2BDescription(const juce::File& fixture,
                                                const AutomationFixtureIdentity& identity)
{
    juce::PluginDescription description;
    description.name = "APEX Dense E2B";
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

SandboxedPluginProxyCore::Preparation makeDensePreparation(const juce::File& worker,
                                                           std::uint32_t maximumHostBlockSamples = 2048)
{
    SandboxedPluginProxyCore::Preparation preparation;
    preparation.blockSamples = kBlockSamples;
    preparation.maximumHostBlockSamples = maximumHostBlockSamples;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
    preparation.workerExecutablePathForTest = worker.getFullPathName();
    return preparation;
}

bool discoverDenseE2BContext(DenseE2BTestContext& result, juce::String& error)
{
    result = {};
    result.worker = productionWorkerExecutable();
    const juce::File fixture(juce::SystemStats::getEnvironmentVariable(
        "APEX_TEST_VST3_DENSE_E2B_PATH", {}));
    if (! result.worker.existsAsFile())
    {
        error = "production worker executable is missing";
        return false;
    }
    if (! fixture.isDirectory())
    {
        error = "dense E2B fixture bundle is missing: " + fixture.getFullPathName();
        return false;
    }

    juce::PluginDescription probeDescription;
    probeDescription.name = "APEX Dense E2B";
    probeDescription.descriptiveName = probeDescription.name;
    probeDescription.manufacturerName = "APEX";
    probeDescription.version = "1.0.0";
    probeDescription.pluginFormatName = "VST3";
    probeDescription.fileOrIdentifier = fixture.getFullPathName();
    probeDescription.numInputChannels = 2;
    probeDescription.numOutputChannels = 2;

    SandboxedPluginProxyCore probe(probeDescription);
    const auto preparation = makeDensePreparation(result.worker);
    if (probe.prepare(preparation) != SandboxedPluginProxyCore::PrepareResult::Prepared)
    {
        error = "dense E2B identity probe failed: " + probe.processDiagnostics().error;
        return false;
    }

    const auto diagnostics = probe.processDiagnostics();
    result.identity.uniqueId = diagnostics.hostedPluginUniqueId;
    result.identity.deprecatedUid = diagnostics.hostedPluginDeprecatedUid;
    probe.release(5000);
    if (result.identity.uniqueId == 0)
    {
        error = "dense E2B identity probe returned uniqueId=0";
        return false;
    }

    result.description = makeDenseE2BDescription(fixture, result.identity);
    return true;
}

bool verifyDenseE2BMetadata(const DenseE2BTestContext& context,
                            juce::String& error)
{
    std::array<juce::String, 129> firstMapping {};
    for (int construction = 0; construction < 3; ++construction)
    {
        SandboxedPluginProxyCore probe(context.description);
        const auto preparation = makeDensePreparation(context.worker);
        if (probe.prepare(preparation) != SandboxedPluginProxyCore::PrepareResult::Prepared)
        {
            error = "dense metadata construction failed: "
                + probe.processDiagnostics().error;
            return false;
        }

        const auto workerPid = probe.currentWorkerPid();
        const auto mappingName = probe.processDiagnostics().sharedMemoryName;

        juce::Array<SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        juce::String metadataError;
        if (! probe.fetchParameterMetadata(metadata, metadataError))
        {
            error = "dense metadata fetch failed: " + metadataError;
            error += " process=" + probe.processHandleRawForTest();
            error += " workerMetadataErrors="
                + juce::String(static_cast<juce::int64>(
                    probe.workerMetadataErrorsForTest()));
            juce::Thread::sleep(25);
            error += " processAfter25ms=" + probe.processHandleRawForTest();
            error += " workerMetadataErrorsAfter25ms="
                + juce::String(static_cast<juce::int64>(
                    probe.workerMetadataErrorsForTest()));
            probe.release(5000);
            return false;
        }
        if (metadata.size() < 129)
        {
            error = "dense metadata count is " + juce::String(metadata.size())
                + "; expected at least 129";
            probe.release(5000);
            return false;
        }

        for (int ordinal = 0; ordinal < 129; ++ordinal)
        {
            juce::String parameterId;
            if (! findParameterIdForOrdinal(metadata, ordinal, parameterId))
            {
                error = "dense metadata ordinal " + juce::String(ordinal)
                    + " is missing";
                probe.release(5000);
                return false;
            }
            const auto expected = "param_" + juce::String(ordinal);
            if (parameterId != expected)
            {
                error = "dense metadata ordinal " + juce::String(ordinal)
                    + " mapped to " + parameterId + ", expected " + expected;
                probe.release(5000);
                return false;
            }
            if (construction == 0)
                firstMapping[static_cast<std::size_t>(ordinal)] = parameterId;
            else if (firstMapping[static_cast<std::size_t>(ordinal)] != parameterId)
            {
                error = "dense metadata mapping changed across repeated construction at ordinal "
                    + juce::String(ordinal);
                probe.release(5000);
                return false;
            }
        }

        // A second bounded request after the full multi-chunk response proves
        // that the worker dispatch loop and its duplex control pipe remain
        // usable; this is control-plane only and leaves param_0 at its default.
        juce::String followUpError;
        if (! probe.setParameter("param_0", 0.5f, followUpError))
        {
            error = "dense metadata follow-up parameter request failed: " + followUpError;
            probe.release(5000);
            return false;
        }

        const auto payloadBytes = serializedMetadataPayloadBytes(metadata);
        const auto expectedChunks = (payloadBytes
            + DAW::PluginSandboxWin32::kE1ChunkPayloadBytes - 1)
            / DAW::PluginSandboxWin32::kE1ChunkPayloadBytes;
        const auto finalChunkBytes = payloadBytes
            - (expectedChunks - 1) * DAW::PluginSandboxWin32::kE1ChunkPayloadBytes;

        if (! probe.release(5000))
        {
            error = "dense metadata worker release failed";
            return false;
        }
        if (processStillAlive(workerPid, 5000))
        {
            error = "dense metadata worker orphaned after cycle "
                + juce::String(construction + 1);
            return false;
        }
        if (mappingName.isNotEmpty()
            && DAW::PluginSandboxAudioTransportCore::mappingExists(mappingName))
        {
            error = "dense metadata shared-memory mapping survived cycle "
                + juce::String(construction + 1);
            return false;
        }

        juce::Logger::writeToLog("APEX_E2B_DENSE_METADATA cycle=" + juce::String(construction + 1)
            + " count=" + juce::String(metadata.size())
            + " payloadBytes=" + juce::String(static_cast<juce::int64>(payloadBytes))
            + " chunkPayloadBytes=" + juce::String(static_cast<int>(
                DAW::PluginSandboxWin32::kE1ChunkPayloadBytes))
            + " chunks=" + juce::String(static_cast<juce::int64>(expectedChunks))
            + " finalChunkBytes=" + juce::String(static_cast<juce::int64>(finalChunkBytes))
            + " followUp=PASS release=PASS");
    }
    return true;
}

bool appendDenseSlot(PluginChainCore& chain,
                     const DenseE2BTestContext& context,
                     const juce::String& trackId,
                     int maximumHostBlockSamples,
                     juce::String& error)
{
    chain.setAutomationContext(trackId, nullptr, nullptr);
    chain.prepare(48000.0, maximumHostBlockSamples);
    const auto preparation = makeDensePreparation(
        context.worker, static_cast<std::uint32_t>(maximumHostBlockSamples));
    return chain.appendSandboxedPlugin(context.description, error, preparation) == 0
        && chain.getSlot(0) != nullptr
        && chain.getSlot(0)->getSandboxProxy() != nullptr;
}

juce::AudioBuffer<float> makeFilledDenseBlock(int samples, float value = 1.0f)
{
    juce::AudioBuffer<float> block(2, samples);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(block.getWritePointer(channel), value, samples);
    return block;
}

void fillDenseBlock(juce::AudioBuffer<float>& block, int samples, float value = 1.0f)
{
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(block.getWritePointer(channel), value, samples);
}

void addLegacyPoint(AutomationSnapshot& snapshot,
                    const juce::String& trackId,
                    const juce::String& parameterId,
                    float value,
                    bool enabled = true)
{
    snapshot.lanes.push_back({
        trackId,
        parameterId,
        enabled,
        { { 0, value, AutomationCurveType::Linear, 0.0f } }
    });
}

apex::automation::ParameterID setAutomationLaneValue(const juce::String& key,
                                                       float value)
{
    using namespace apex::automation;
    auto& keyRegistry = AutomationParameterKeyRegistry::getInstance();
    auto& laneStore = AutomationLaneStore::getInstance();
    const auto id = keyRegistry.getOrCreateID(key);
    laneStore.getOrCreateLane(id).replacePoints({
        { 0.0, value, CurveType::Hold, 0.0f }
    });
    return id;
}

void removeAutomationLane(apex::automation::ParameterID id)
{
    if (id != apex::automation::kInvalidParameterID)
        apex::automation::AutomationLaneStore::getInstance().removeLane(id);
}

juce::String denseCoreParameterId(const PluginInstanceCore& slot,
                                  int slotIndex,
                                  const juce::String& parameterId)
{
    return "plugin." + juce::String(slotIndex)
        + "." + slot.getPluginInstanceId() + "." + parameterId;
}

juce::String denseApexParameterKey(const PluginInstanceCore& slot,
                                   const juce::String& trackId,
                                   int ordinal,
                                   const juce::String& parameterId)
{
    return apex::automation::AutomationParameterKeyRegistry::pluginParamKey(
        trackId, ordinal, slot.getName(), parameterId,
        slot.getDescription().uniqueId);
}

juce::String denseBridgeParameterKey(const PluginInstanceCore& slot,
                                     const juce::String& trackId,
                                     int ordinal,
                                     const juce::String& parameterId)
{
    const auto coreId = denseCoreParameterId(slot, ordinal, parameterId);
    return "plugin." + trackId + ".core." + coreId;
}

bool fetchDenseWorkerValue(SandboxedPluginProxyCore& proxy,
                           int ordinal,
                           float& value,
                           juce::String& error)
{
    return fetchLiveValueForIndex(proxy, ordinal, value, error);
}

void processDenseCallback(PluginChainCore& chain,
                          SandboxedPluginProxyCore* proxy,
                          juce::AudioBuffer<float>& block,
                          int samples)
{
    fillDenseBlock(block, samples);
    chain.processBlock(block, samples);
    if (proxy != nullptr)
        waitForWorkerQuanta(proxy,
                            proxy->lifecycleDiagnostics().transport.submitted,
                            5000);
}

void processDenseProxyCallback(SandboxedPluginProxyCore& proxy,
                               juce::AudioBuffer<float>& block,
                               int samples,
                               const PluginSandboxAutomationShared::SandboxAutomationEvent* events = nullptr,
                               std::uint32_t eventCount = 0)
{
    fillDenseBlock(block, samples);
    if (events != nullptr && eventCount > 0)
        proxy.processBlockWithAutomation(block, samples, events, eventCount);
    else
        proxy.processBlock(block, samples);
    waitForWorkerQuanta(&proxy,
                        proxy.lifecycleDiagnostics().transport.submitted,
                        5000);
}

#endif

class PluginSandboxPhaseE2BDenseMetadataIpcTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BDenseMetadataIpcTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.dense-metadata-ipc.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("three dense metadata control-plane cycles preserve identity and cleanup");
        juce::MessageManager::getInstance();
        DenseE2BTestContext context;
        juce::String error;
        expect(discoverDenseE2BContext(context, error), error);
        if (! context.worker.existsAsFile() || context.description.fileOrIdentifier.isEmpty())
            return;
        expect(verifyDenseE2BMetadata(context, error), error);
       #else
        beginTest("Windows-only dense E2B metadata IPC");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BDenseMetadataIpcTest pluginSandboxPhaseE2BDenseMetadataIpcTest;

class PluginSandboxPhaseE2BProducerSourcePrecedenceTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BProducerSourcePrecedenceTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.producer-source-precedence.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("dense metadata is stable before source-precedence assertions");
        juce::MessageManager::getInstance();
        DenseE2BTestContext context;
        juce::String error;
        expect(discoverDenseE2BContext(context, error), error);
        if (context.worker.existsAsFile())
            expect(verifyDenseE2BMetadata(context, error), error);
        if (! context.worker.existsAsFile() || context.description.fileOrIdentifier.isEmpty())
            return;

        beginTest("legacy then APEX then manual source precedence collapses to one event");
        const juce::String trackId = "e2b-final-source-precedence";
        constexpr double sampleRate = 48000.0;
        constexpr double bpm = 120.0;

        struct CaseResult
        {
            float inProcess = 0.0f;
            float sandbox = 0.0f;
            float worker = 0.0f;
            std::uint64_t appliedDelta = 0;
            std::uint32_t staged = 0;
        };

        const auto runCase = [&](const juce::String& label,
                                 bool legacyActive,
                                 bool apexActive,
                                 bool manualActive,
                                 float expectedRaw) -> CaseResult
        {
            CaseResult result;
            PluginChainCore inProcess;
            PluginChainCore sandbox;
            inProcess.setAutomationContext(trackId, nullptr, nullptr);
            sandbox.setAutomationContext(trackId, nullptr, nullptr);
            inProcess.prepare(sampleRate, 2048);
            sandbox.prepare(sampleRate, 2048);
            auto probe = std::make_unique<E2BInProcessGainProbeProcessor>();
            expectEquals(inProcess.appendPluginInstanceForTesting(std::move(probe)), 0,
                         label + ": InProcess insertion failed");
            inProcess.getSlot(0)->configureAutomationContext(trackId, 0, nullptr, nullptr);
            expect(appendDenseSlot(sandbox, context, trackId, 2048, error),
                   label + ": sandbox insertion failed: " + error);

            auto* inProcessSlot = inProcess.getSlot(0);
            auto* sandboxSlot = sandbox.getSlot(0);
            auto* proxy = sandboxSlot != nullptr ? sandboxSlot->getSandboxProxy() : nullptr;
            if (inProcessSlot == nullptr || sandboxSlot == nullptr || proxy == nullptr)
                return result;

            constexpr int inProcessOrdinal = 0;
            constexpr int sandboxOrdinal = 0;
            const auto legacyA = denseCoreParameterId(*inProcessSlot, 0, "gain");
            const auto legacyB = denseCoreParameterId(*sandboxSlot, 0, "param_0");
            const auto apexA = denseApexParameterKey(*inProcessSlot, trackId, 0, "gain");
            const auto apexB = denseApexParameterKey(*sandboxSlot, trackId, 0, "param_0");
            const auto bridgeA = denseBridgeParameterKey(*inProcessSlot, trackId, 0, "gain");
            const auto bridgeB = denseBridgeParameterKey(*sandboxSlot, trackId, 0, "param_0");
            const auto apexIdA = setAutomationLaneValue(apexA, 0.4f);
            const auto apexIdB = setAutomationLaneValue(apexB, 0.4f);
            const auto bridgeIdA = setAutomationLaneValue(bridgeA, 0.8f);
            const auto bridgeIdB = setAutomationLaneValue(bridgeB, 0.8f);

            AutomationSnapshot snapshot;
            if (legacyActive)
            {
                addLegacyPoint(snapshot, trackId, legacyA, 0.2f);
                addLegacyPoint(snapshot, trackId, legacyB, 0.2f);
            }
            if (! apexActive)
            {
                removeAutomationLane(apexIdA);
                removeAutomationLane(apexIdB);
            }
            if (! manualActive)
            {
                removeAutomationLane(bridgeIdA);
                removeAutomationLane(bridgeIdB);
            }

            const auto appliedBefore = proxy->automationDiagnostics().workerAppliedEvents;
            inProcess.applyAutomationAtSample(trackId,
                                               snapshot.lanes.empty() ? nullptr : &snapshot,
                                               0, sampleRate, bpm, 512);
            sandbox.applyAutomationAtSample(trackId,
                                            snapshot.lanes.empty() ? nullptr : &snapshot,
                                            0, sampleRate, bpm, 512);
            result.inProcess = inProcessSlot->getProcessor()->getParameters()[inProcessOrdinal]->getValue();
            result.sandbox = sandboxSlot->sandboxLastAutomationValueForTesting(sandboxOrdinal);
            result.staged = sandboxSlot->sandboxStagedEventCountForTesting();
            expectWithinAbsoluteError(result.inProcess, result.sandbox, 0.0005f,
                                      label + ": InProcess/Sandbox canonical target mismatch");

            auto block = makeFilledDenseBlock(512);
            sandbox.processBlock(block, 512);
            waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
            result.appliedDelta = proxy->automationDiagnostics().workerAppliedEvents - appliedBefore;
            expect(fetchDenseWorkerValue(*proxy, 0, result.worker, error),
                   label + ": worker live-value fetch failed: " + error);
            expectWithinAbsoluteError(result.worker, result.sandbox, 0.0005f,
                                      label + ": worker did not receive final canonical value");
            expectWithinAbsoluteError(sandboxSlot->sandboxLastDeliveredValueForTesting(0),
                                      result.sandbox, 0.0005f,
                                      label + ": lastDelivered did not commit final value");
            if (legacyActive || apexActive || manualActive)
                expectEquals(result.staged, static_cast<std::uint32_t>(1),
                             label + ": source collapse emitted more than one parameter event");
            else
                expectEquals(result.staged, static_cast<std::uint32_t>(0),
                             label + ": baseline unexpectedly staged an event");
            expectEquals(result.appliedDelta,
                         static_cast<std::uint64_t>((legacyActive || apexActive || manualActive) ? 1 : 0),
                         label + ": worker applied an unexpected number of events");

            logMessage("APEX_E2B_SOURCE_PRECEDENCE case=" + label
                + " expectedRaw=" + juce::String(expectedRaw, 4)
                + " inProcess=" + juce::String(result.inProcess, 4)
                + " sandbox=" + juce::String(result.sandbox, 4)
                + " worker=" + juce::String(result.worker, 4)
                + " staged=" + juce::String(static_cast<int>(result.staged))
                + " appliedDelta=" + juce::String(static_cast<juce::int64>(result.appliedDelta)));

            removeAutomationLane(apexIdA);
            removeAutomationLane(apexIdB);
            removeAutomationLane(bridgeIdA);
            removeAutomationLane(bridgeIdB);
            inProcess.removePlugin(0);
            sandbox.removePlugin(0);
            PluginChainCore::drainRetiredPlugins();
            return result;
        };

        const auto baseline = runCase("baseline", false, false, false, 0.5f);
        const auto legacy = runCase("legacy", true, false, false, 0.2f);
        const auto apex = runCase("legacy+APEX", true, true, false, 0.4f);
        const auto manual = runCase("legacy+APEX+manual", true, true, true, 0.8f);
        const auto manualDisabled = runCase("manual-disabled", true, true, false, 0.4f);
        const auto apexAbsent = runCase("APEX-absent-legacy-remains", true, false, false, 0.2f);

        expectWithinAbsoluteError(baseline.sandbox, 0.5f, 0.0005f,
                                  "baseline source value was not the current value");
        expect(std::abs(legacy.sandbox - baseline.sandbox) > 0.0001f,
               "legacy lane did not override baseline");
        expect(std::abs(apex.sandbox - legacy.sandbox) > 0.0001f,
               "APEX lane did not override legacy lane");
        expect(std::abs(manual.sandbox - apex.sandbox) > 0.0001f,
               "manual bridge did not override APEX lane");
        expectWithinAbsoluteError(manualDisabled.sandbox, apex.sandbox, 0.0005f,
                                  "disabled manual bridge did not fall back to APEX");
        expectWithinAbsoluteError(apexAbsent.sandbox, legacy.sandbox, 0.0005f,
                                  "absent APEX lane did not fall back to legacy");
       #else
        beginTest("Windows-only E2B final source precedence");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BProducerSourcePrecedenceTest pluginSandboxPhaseE2BProducerSourcePrecedenceTest;

class PluginSandboxPhaseE2BProducerBlockPartitionContractTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BProducerBlockPartitionContractTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.producer-block-partition-contract.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("constant-target smoother has equal final state for 4x512 and 1x2048");
        juce::MessageManager::getInstance();
        DenseE2BTestContext context;
        juce::String error;
        expect(discoverDenseE2BContext(context, error), error);
        if (context.description.fileOrIdentifier.isEmpty())
            return;

        constexpr double sampleRate = 48000.0;
        constexpr double bpm = 120.0;
        const juce::String trackA = "e2b-final-partition-a";
        const juce::String trackB = "e2b-final-partition-b";
        PluginChainCore fourCallbacks;
        PluginChainCore oneCallback;
        expect(appendDenseSlot(fourCallbacks, context, trackA, 2048, error),
               "4x512 dense insertion failed: " + error);
        expect(appendDenseSlot(oneCallback, context, trackB, 2048, error),
               "1x2048 dense insertion failed: " + error);
        auto* slotA = fourCallbacks.getSlot(0);
        auto* slotB = oneCallback.getSlot(0);
        auto* proxyA = slotA != nullptr ? slotA->getSandboxProxy() : nullptr;
        auto* proxyB = slotB != nullptr ? slotB->getSandboxProxy() : nullptr;
        if (slotA == nullptr || slotB == nullptr || proxyA == nullptr || proxyB == nullptr)
            return;

        const auto legacyA = denseCoreParameterId(*slotA, 0, "param_0");
        const auto legacyB = denseCoreParameterId(*slotB, 0, "param_0");
        AutomationSnapshot snapshotA;
        AutomationSnapshot snapshotB;
        addLegacyPoint(snapshotA, trackA, legacyA, 0.86f);
        addLegacyPoint(snapshotB, trackB, legacyB, 0.86f);

        const int partition[] = { 512, 512, 512, 512 };
        for (int i = 0; i < 4; ++i)
        {
            fourCallbacks.applyAutomationAtSample(trackA, &snapshotA,
                                                   static_cast<int64_t>(i) * 512,
                                                   sampleRate, bpm, partition[i]);
            auto block = makeFilledDenseBlock(partition[i]);
            fourCallbacks.processBlock(block, partition[i]);
            waitForWorkerQuanta(proxyA, proxyA->lifecycleDiagnostics().transport.submitted, 5000);
        }

        oneCallback.applyAutomationAtSample(trackB, &snapshotB, 0,
                                             sampleRate, bpm, 2048);
        auto oneBlock = makeFilledDenseBlock(2048);
        oneCallback.processBlock(oneBlock, 2048);
        waitForWorkerQuanta(proxyB, proxyB->lifecycleDiagnostics().transport.submitted, 5000);

        const float four512Final = slotA->sandboxLastAutomationValueForTesting(0);
        const float one2048Final = slotB->sandboxLastAutomationValueForTesting(0);
        expectWithinAbsoluteError(four512Final, one2048Final, 0.0005f,
                                  "final smoother state depends on host partition");

        beginTest("host callback is canonical; internal Q512 work does not reevaluate");
        PluginChainCore mappingChain;
        const juce::String mappingTrack = "e2b-final-callback-mapping";
        expect(appendDenseSlot(mappingChain, context, mappingTrack, 2048, error),
               "callback-mapping dense insertion failed: " + error);
        auto* mappingSlot = mappingChain.getSlot(0);
        auto* mappingProxy = mappingSlot != nullptr ? mappingSlot->getSandboxProxy() : nullptr;
        if (mappingSlot == nullptr || mappingProxy == nullptr)
            return;

        const auto mappingCoreId = denseCoreParameterId(*mappingSlot, 0, "param_0");
        AutomationSnapshot mappingSnapshot;
        addLegacyPoint(mappingSnapshot, mappingTrack, mappingCoreId, 0.61f);
        auto mappingBlock = makeFilledDenseBlock(2048);
        const auto seqBefore480 = mappingProxy->automationSequenceForNextCallback();
        mappingChain.applyAutomationAtSample(mappingTrack, &mappingSnapshot, 0,
                                              sampleRate, bpm, 480);
        mappingChain.processBlock(mappingBlock, 480);
        const auto seqAtAbs480 = mappingProxy->automationSequenceForNextCallback();
        mappingChain.applyAutomationAtSample(mappingTrack, &mappingSnapshot, 480,
                                              sampleRate, bpm, 64);
        mappingChain.processBlock(mappingBlock, 64);
        waitForWorkerQuanta(mappingProxy,
                            mappingProxy->lifecycleDiagnostics().transport.submitted,
                            5000);
        const auto seqAtAbs544 = mappingProxy->automationSequenceForNextCallback();
        const auto appliedAfter544 = mappingProxy->automationDiagnostics().workerAppliedEvents;

        // A new target is introduced once for each larger host callback. The
        // worker applies one parameter event for each callback, even though the
        // reblocker performs multiple exact-Q audio exchanges internally.
        AutomationSnapshot snapshot1024;
        addLegacyPoint(snapshot1024, mappingTrack, mappingCoreId, 0.72f);
        mappingChain.applyAutomationAtSample(mappingTrack, &snapshot1024, 544,
                                              sampleRate, bpm, 1024);
        const auto staged1024 = mappingSlot->sandboxStagedEventCountForTesting();
        mappingChain.processBlock(mappingBlock, 1024);
        waitForWorkerQuanta(mappingProxy,
                            mappingProxy->lifecycleDiagnostics().transport.submitted,
                            5000);
        const auto appliedAfter1024 = mappingProxy->automationDiagnostics().workerAppliedEvents;

        AutomationSnapshot snapshot2048;
        addLegacyPoint(snapshot2048, mappingTrack, mappingCoreId, 0.33f);
        mappingChain.applyAutomationAtSample(mappingTrack, &snapshot2048, 1568,
                                              sampleRate, bpm, 2048);
        const auto staged2048 = mappingSlot->sandboxStagedEventCountForTesting();
        mappingChain.processBlock(mappingBlock, 2048);
        waitForWorkerQuanta(mappingProxy,
                            mappingProxy->lifecycleDiagnostics().transport.submitted,
                            5000);
        const auto appliedAfter2048 = mappingProxy->automationDiagnostics().workerAppliedEvents;

        expectEquals(seqBefore480, static_cast<std::uint64_t>(1),
                     "initial callback sequence is not one");
        expectEquals(seqAtAbs480, static_cast<std::uint64_t>(1),
                     "abs480 did not remain in sequence one");
        expectEquals(seqAtAbs544, static_cast<std::uint64_t>(2),
                     "abs544 did not advance to sequence two");
        expectEquals(staged1024, static_cast<std::uint32_t>(1),
                     "1024 host callback staged more than one canonical parameter event");
        expectEquals(staged2048, static_cast<std::uint32_t>(1),
                     "2048 host callback staged more than one canonical parameter event");
        expectEquals(appliedAfter1024 - appliedAfter544, static_cast<std::uint64_t>(1),
                     "1024 host callback reevaluated/re-emitted per internal quantum");
        expectEquals(appliedAfter2048 - appliedAfter1024, static_cast<std::uint64_t>(1),
                     "2048 host callback reevaluated/re-emitted per internal quantum");

        logMessage("APEX_E2B_PARTITION_DIAG four512=" + juce::String(four512Final, 6)
            + " one2048=" + juce::String(one2048Final, 6)
            + " mapping=abs480:seq" + juce::String(static_cast<juce::int64>(seqAtAbs480))
            + "/local480,abs544:seq" + juce::String(static_cast<juce::int64>(seqAtAbs544))
            + "/local32 staged1024=" + juce::String(static_cast<int>(staged1024))
            + " staged2048=" + juce::String(static_cast<int>(staged2048))
            + " appliedDelta1024=" + juce::String(static_cast<juce::int64>(appliedAfter1024 - appliedAfter544))
            + " appliedDelta2048=" + juce::String(static_cast<juce::int64>(appliedAfter2048 - appliedAfter1024)));

        fourCallbacks.removePlugin(0);
        oneCallback.removePlugin(0);
        mappingChain.removePlugin(0);
        PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B producer block partition contract");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BProducerBlockPartitionContractTest pluginSandboxPhaseE2BProducerBlockPartitionContractTest;

class PluginSandboxPhaseE2BProducerDensityBoundTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BProducerDensityBoundTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.producer-density-bound.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("exactly 64 same-boundary representable events are accepted");
        juce::MessageManager::getInstance();
        DenseE2BTestContext context;
        juce::String error;
        expect(discoverDenseE2BContext(context, error), error);
        if (context.description.fileOrIdentifier.isEmpty())
            return;

        constexpr double sampleRate = 48000.0;
        constexpr double bpm = 120.0;
        constexpr int kFirstCount = 64;
        constexpr int kOverflowCount = 65;
        const juce::String trackId = "e2b-final-density-bound";
        PluginChainCore chain;
        expect(appendDenseSlot(chain, context, trackId, 2048, error),
               "density dense insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        if (slot == nullptr || proxy == nullptr)
            return;

        AutomationSnapshot stateA;
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
            addLegacyPoint(stateA, trackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           0.12f + 0.006f * static_cast<float>(ordinal));

        std::array<float, kOverflowCount> expectedA {};
        chain.applyAutomationAtSample(trackId, &stateA, 0,
                                      sampleRate, bpm, kBlockSamples);
        const auto staged64 = slot->sandboxStagedEventCountForTesting();
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
            expectedA[static_cast<std::size_t>(ordinal)] =
                slot->sandboxLastAutomationValueForTesting(ordinal);
        auto block = makeFilledDenseBlock(kBlockSamples);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto after64 = proxy->automationDiagnostics();
        expectEquals(staged64, static_cast<std::uint32_t>(kFirstCount),
                     "64-event producer staging count is not exactly 64");
        expectEquals(after64.parentOverflowRejected, static_cast<std::uint64_t>(0),
                     "64-event batch was marked overflow");
        expectEquals(after64.workerInvalidBatches, static_cast<std::uint64_t>(0),
                     "64-event batch was marked invalid");
        expectEquals(after64.workerAppliedEvents, static_cast<std::uint64_t>(kFirstCount),
                     "worker did not apply all 64 events");
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
        {
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinal),
                                      expectedA[static_cast<std::size_t>(ordinal)],
                                      0.0005f,
                                      "64-event lastDelivered did not commit ordinal "
                                          + juce::String(ordinal));
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "64-event worker fetch failed: " + error);
            expectWithinAbsoluteError(workerValue,
                                      expectedA[static_cast<std::size_t>(ordinal)],
                                      0.0005f,
                                      "64-event worker value mismatch at ordinal "
                                          + juce::String(ordinal));
        }

        beginTest("65 same-boundary events reject as one whole batch and later resynchronize");
        AutomationSnapshot stateB;
        for (int ordinal = 0; ordinal < kOverflowCount; ++ordinal)
            addLegacyPoint(stateB, trackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           0.90f - 0.004f * static_cast<float>(ordinal));

        std::array<float, kOverflowCount> deliveredBeforeFailure {};
        std::array<float, kOverflowCount> workerBeforeFailure {};
        for (int ordinal = 0; ordinal < kOverflowCount; ++ordinal)
        {
            deliveredBeforeFailure[static_cast<std::size_t>(ordinal)] =
                slot->sandboxLastDeliveredValueForTesting(ordinal);
            expect(fetchDenseWorkerValue(*proxy, ordinal,
                                         workerBeforeFailure[static_cast<std::size_t>(ordinal)],
                                         error),
                   "pre-overflow worker fetch failed: " + error);
        }

        chain.applyAutomationAtSample(trackId, &stateB, kBlockSamples,
                                      sampleRate, bpm, kBlockSamples);
        const auto staged65 = slot->sandboxStagedEventCountForTesting();
        const auto beforeOverflow = proxy->automationDiagnostics();
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterOverflow = proxy->automationDiagnostics();
        expectEquals(staged65, static_cast<std::uint32_t>(kOverflowCount),
                     "65-event producer did not stage all 65 boundary changes");
        expect(afterOverflow.parentOverflowRejected == beforeOverflow.parentOverflowRejected + 1,
               "65-event batch did not enter the parent overflow path");
        expect(afterOverflow.workerInvalidBatches == beforeOverflow.workerInvalidBatches + 1,
               "65-event batch did not reach worker invalid-batch handling");
        expect(afterOverflow.workerAppliedEvents == after64.workerAppliedEvents,
               "65-event whole-batch rejection partially applied worker events");
        for (int ordinal = 0; ordinal < kOverflowCount; ++ordinal)
        {
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinal),
                                      deliveredBeforeFailure[static_cast<std::size_t>(ordinal)],
                                      0.0005f,
                                      "65-event rejection falsely committed ordinal "
                                          + juce::String(ordinal));
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "post-overflow worker fetch failed: " + error);
            expectWithinAbsoluteError(workerValue,
                                      workerBeforeFailure[static_cast<std::size_t>(ordinal)],
                                      0.0005f,
                                      "65-event rejection changed worker ordinal "
                                          + juce::String(ordinal));
        }

        // A second callback with the unchanged 65-event workload is expected
        // to fail closed again. This is continued deterministic rejection, not
        // a resynchronization attempt: the workload is still outside E2A's
        // exact representable domain.
        chain.applyAutomationAtSample(trackId, &stateB, 1024,
                                      sampleRate, bpm, kBlockSamples);
        const auto staged65Second = slot->sandboxStagedEventCountForTesting();
        const auto beforeSecondOverflow = proxy->automationDiagnostics();
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterSecondOverflow = proxy->automationDiagnostics();
        expectEquals(staged65Second, static_cast<std::uint32_t>(kOverflowCount),
                     "unchanged oversized workload did not stage all 65 events");
        expect(afterSecondOverflow.parentOverflowRejected
                   == beforeSecondOverflow.parentOverflowRejected + 1,
               "unchanged oversized workload did not reject atomically again");
        expect(afterSecondOverflow.workerInvalidBatches
                   == beforeSecondOverflow.workerInvalidBatches + 1,
               "unchanged oversized workload did not reach invalid-batch handling again");
        expect(afterSecondOverflow.workerAppliedEvents == afterOverflow.workerAppliedEvents,
               "repeated oversized rejection changed worker applied-event count");
        for (int ordinal = 0; ordinal < kOverflowCount; ++ordinal)
        {
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinal),
                                      deliveredBeforeFailure[static_cast<std::size_t>(ordinal)],
                                      0.0005f,
                                      "repeated oversized rejection falsely committed ordinal "
                                          + juce::String(ordinal));
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "repeated oversized worker fetch failed: " + error);
            expectWithinAbsoluteError(workerValue,
                                      workerBeforeFailure[static_cast<std::size_t>(ordinal)],
                                      0.0005f,
                                      "repeated oversized rejection changed worker ordinal "
                                          + juce::String(ordinal));
        }

        beginTest("normal control-plane reentry into the supported 64-event domain resumes delivery");
        const auto workerPidBeforeReentry = proxy->currentWorkerPid();
        const auto sessionBeforeReentry = proxy->processDiagnostics().sessionToken;
        const auto generationBeforeReentry = proxy->currentGeneration();
        const auto sidecarGenerationBeforeReentry =
            proxy->automationTransportForTest().generation();
        const auto sequenceBeforeReentry = proxy->automationSequenceForNextCallback();
        const auto submittedBeforeReentry =
            proxy->lifecycleDiagnostics().transport.submitted;
        expect(processStillAlive(workerPidBeforeReentry, 50),
               "worker did not remain alive after repeated oversized rejection");

        // This is the normal production control-plane context transition. It
        // rebuilds and reseeds the E2B bindings from ACTUAL worker values,
        // without recreating the worker or touching lastDelivered/smoother
        // state directly. The new snapshot intentionally has only 64 active
        // legacy lanes; the metadata-derived ordinal table may still contain
        // the complete representable parameter space.
        const juce::String reentryTrackId = trackId + "-supported-domain";
        chain.setAutomationContext(reentryTrackId, nullptr, nullptr);
        const auto bindingsAfterReentry = slot->sandboxAutomationBindingCountForTesting();
        expect(bindingsAfterReentry >= kFirstCount,
               "normal context reentry did not rebuild E2B bindings");
        expect(proxy->automationEnabled(), "normal context reentry disabled E2A");
        expect(proxy->currentWorkerPid() == workerPidBeforeReentry,
               "normal context reentry recreated the worker");
        expect(proxy->processDiagnostics().sessionToken == sessionBeforeReentry,
               "normal context reentry changed the sandbox session");
        expect(proxy->currentGeneration() == generationBeforeReentry,
               "normal context reentry changed the audio generation");
        expect(proxy->automationTransportForTest().generation()
                   == sidecarGenerationBeforeReentry,
               "normal context reentry changed the automation sidecar generation");
        expect(proxy->automationSequenceForNextCallback() == sequenceBeforeReentry,
               "normal context reentry reset the automation sequence");

        AutomationSnapshot supportedStateFirst;
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
            addLegacyPoint(supportedStateFirst, reentryTrackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           0.80f - 0.002f * static_cast<float>(ordinal));

        chain.applyAutomationAtSample(reentryTrackId, &supportedStateFirst,
                                      3 * kBlockSamples,
                                      sampleRate, bpm, kBlockSamples);
        const auto stagedReentry = slot->sandboxStagedEventCountForTesting();
        std::array<float, kFirstCount> expectedReentry {};
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
            expectedReentry[static_cast<std::size_t>(ordinal)] =
                slot->sandboxLastAutomationValueForTesting(ordinal);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterReentry = proxy->automationDiagnostics();
        expectEquals(stagedReentry, static_cast<std::uint32_t>(kFirstCount),
                     "supported-domain reentry did not stage exactly 64 events");
        expect(afterReentry.parentOverflowRejected
                   == afterSecondOverflow.parentOverflowRejected,
               "supported-domain reentry retained an overflow condition");
        expect(afterReentry.workerInvalidBatches
                   == afterSecondOverflow.workerInvalidBatches,
               "supported-domain reentry retained an invalid-batch condition");
        expect(afterReentry.workerAppliedEvents
                   == afterSecondOverflow.workerAppliedEvents + stagedReentry,
               "supported-domain reentry did not apply its valid 64-event batch");
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
        {
            const auto expected = expectedReentry[static_cast<std::size_t>(ordinal)];
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinal),
                                      expected, 0.0005f,
                                      "supported-domain reentry did not commit ordinal "
                                          + juce::String(ordinal));
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "supported-domain reentry worker fetch failed: " + error);
            expectWithinAbsoluteError(workerValue, expected, 0.0005f,
                                      "supported-domain reentry worker mismatch at ordinal "
                                          + juce::String(ordinal));
        }

        // The omitted ordinal remains at its previous successfully submitted
        // state. A later supported callback changes all 64 active lanes again,
        // proving that no stale overflow marker or pending record remains.
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(64),
                                  workerBeforeFailure[64], 0.0005f,
                                  "omitted ordinal 64 was falsely committed on reentry");
        float omittedWorkerValue = -1.0f;
        expect(fetchDenseWorkerValue(*proxy, 64, omittedWorkerValue, error),
               "omitted ordinal 64 worker fetch failed: " + error);
        expectWithinAbsoluteError(omittedWorkerValue, workerBeforeFailure[64], 0.0005f,
                                  "omitted ordinal 64 changed during reentry");

        AutomationSnapshot supportedStateSecond;
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
            addLegacyPoint(supportedStateSecond, reentryTrackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           0.20f + 0.003f * static_cast<float>(ordinal));

        chain.applyAutomationAtSample(reentryTrackId, &supportedStateSecond,
                                      4 * kBlockSamples,
                                      sampleRate, bpm, kBlockSamples);
        const auto stagedPostReentry = slot->sandboxStagedEventCountForTesting();
        std::array<float, kFirstCount> expectedPostReentry {};
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
            expectedPostReentry[static_cast<std::size_t>(ordinal)] =
                slot->sandboxLastAutomationValueForTesting(ordinal);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterPostReentry = proxy->automationDiagnostics();
        expectEquals(stagedPostReentry, static_cast<std::uint32_t>(kFirstCount),
                     "post-reentry supported callback did not stage exactly 64 events");
        expect(afterPostReentry.parentOverflowRejected
                   == afterSecondOverflow.parentOverflowRejected,
               "post-reentry supported callback introduced an overflow");
        expect(afterPostReentry.workerInvalidBatches
                   == afterSecondOverflow.workerInvalidBatches,
               "post-reentry supported callback introduced an invalid batch");
        expect(afterPostReentry.workerAppliedEvents
                   == afterReentry.workerAppliedEvents + stagedPostReentry,
               "post-reentry supported callback applied an unexpected event count");
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
        {
            const auto expected = expectedPostReentry[static_cast<std::size_t>(ordinal)];
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinal),
                                      expected, 0.0005f,
                                      "post-reentry callback did not commit ordinal "
                                          + juce::String(ordinal));
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "post-reentry worker fetch failed: " + error);
            expectWithinAbsoluteError(workerValue, expected, 0.0005f,
                                      "post-reentry worker mismatch at ordinal "
                                          + juce::String(ordinal));
        }
        expect(proxy->currentWorkerPid() == workerPidBeforeReentry,
               "worker changed during supported-domain delivery");
        expect(proxy->processDiagnostics().sessionToken == sessionBeforeReentry,
               "session changed during supported-domain delivery");
        expect(proxy->currentGeneration() == generationBeforeReentry,
               "generation changed during supported-domain delivery");

        logMessage("APEX_E2B_DENSITY_DIAG first64Staged="
            + juce::String(static_cast<int>(staged64))
            + " first64Applied=" + juce::String(static_cast<juce::int64>(after64.workerAppliedEvents))
            + " first65Staged=" + juce::String(static_cast<int>(staged65))
            + " first65Overflow=" + juce::String(static_cast<juce::int64>(
                afterOverflow.parentOverflowRejected))
            + " first65Invalid=" + juce::String(static_cast<juce::int64>(
                afterOverflow.workerInvalidBatches))
            + " second65Changed=" + juce::String(static_cast<int>(staged65Second))
            + " second65OverflowDelta=" + juce::String(static_cast<juce::int64>(
                afterSecondOverflow.parentOverflowRejected
                    - afterOverflow.parentOverflowRejected))
            + " second65InvalidDelta=" + juce::String(static_cast<juce::int64>(
                afterSecondOverflow.workerInvalidBatches
                    - afterOverflow.workerInvalidBatches))
            + " second65AppliedDelta=" + juce::String(static_cast<juce::int64>(
                afterSecondOverflow.workerAppliedEvents
                    - afterOverflow.workerAppliedEvents))
            + " workerPidStable=" + juce::String(proxy->currentWorkerPid() == workerPidBeforeReentry ? 1 : 0)
            + " sessionStable=" + juce::String(proxy->processDiagnostics().sessionToken == sessionBeforeReentry ? 1 : 0)
            + " generationStable=" + juce::String(proxy->currentGeneration() == generationBeforeReentry ? 1 : 0)
            + " sidecarGenerationStable=" + juce::String(
                proxy->automationTransportForTest().generation() == sidecarGenerationBeforeReentry ? 1 : 0)
            + " sequenceBefore=" + juce::String(static_cast<juce::int64>(sequenceBeforeReentry))
            + " submittedBefore=" + juce::String(static_cast<juce::int64>(submittedBeforeReentry))
            + " bindingsAfterReentry=" + juce::String(bindingsAfterReentry)
            + " activeReentryLanes=" + juce::String(static_cast<int>(stagedReentry))
            + " reentryApplied=" + juce::String(static_cast<juce::int64>(
                afterReentry.workerAppliedEvents - afterSecondOverflow.workerAppliedEvents))
            + " postReentryStaged=" + juce::String(static_cast<int>(stagedPostReentry))
            + " postReentryApplied=" + juce::String(static_cast<juce::int64>(
                afterPostReentry.workerAppliedEvents - afterReentry.workerAppliedEvents))
            + " finalApplied=" + juce::String(static_cast<juce::int64>(
                afterPostReentry.workerAppliedEvents)));

        chain.removePlugin(0);
        PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B producer density bound");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BProducerDensityBoundTest pluginSandboxPhaseE2BProducerDensityBoundTest;

class PluginSandboxPhaseE2BUnsupportedOrdinalBoundTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BUnsupportedOrdinalBoundTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.unsupported-ordinal-bound.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("ordinal 127 is representable and ordinal 128 is rejected without poisoning E2A");
        juce::MessageManager::getInstance();
        DenseE2BTestContext context;
        juce::String error;
        expect(discoverDenseE2BContext(context, error), error);
        expect(verifyDenseE2BMetadata(context, error), error);
        if (context.description.fileOrIdentifier.isEmpty())
            return;

        constexpr double sampleRate = 48000.0;
        constexpr double bpm = 120.0;
        const juce::String trackId = "e2b-final-ordinal-bound";
        PluginChainCore chain;
        expect(appendDenseSlot(chain, context, trackId, 2048, error),
               "ordinal-bound dense insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        if (slot == nullptr || proxy == nullptr)
            return;

        juce::Array<SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
        expect(proxy->fetchParameterMetadata(metadata, error),
               "ordinal-bound metadata fetch failed: " + error);
        juce::String param127Id;
        juce::String param128Id;
        expect(findParameterIdForOrdinal(metadata, 127, param127Id),
               "ordinal-bound param_127 metadata is missing");
        expect(findParameterIdForOrdinal(metadata, 128, param128Id),
               "ordinal-bound param_128 metadata is missing");
        expectEquals(slot->sandboxAutomationBindingCountForTesting(), 128,
                     "unsupported ordinal unexpectedly entered the E2B binding table");
        expectEquals(slot->sandboxLastAutomationValueForTesting(128), -1.0f,
                     "unsupported ordinal has a representable E2B mirror");
        if (param127Id.isEmpty() || param128Id.isEmpty())
            return;

        AutomationSnapshot valid127;
        addLegacyPoint(valid127, trackId,
                       denseCoreParameterId(*slot, 0, param127Id), 0.91f);
        chain.applyAutomationAtSample(trackId, &valid127, 0,
                                      sampleRate, bpm, kBlockSamples);
        const float expected127 = slot->sandboxLastAutomationValueForTesting(127);
        auto block = makeFilledDenseBlock(kBlockSamples);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        float worker127BeforeInvalid = -1.0f;
        expect(fetchDenseWorkerValue(*proxy, 127, worker127BeforeInvalid, error),
               "ordinal 127 worker fetch failed: " + error);
        expectWithinAbsoluteError(worker127BeforeInvalid, expected127, 0.0005f,
                                  "ordinal 127 was not delivered");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(127),
                                  expected127, 0.0005f,
                                  "ordinal 127 lastDelivered did not commit");

        // LiveValues is intentionally bounded to the representable E2A
        // ordinal space (0..127), so ordinal 128 cannot be observed through
        // that control-plane query. Use the valid ordinal 127 as the atomicity
        // sentinel: the mixed valid+invalid batch must not apply it either.
        const auto beforeInvalid = proxy->automationDiagnostics();
        PluginSandboxAutomationShared::SandboxAutomationEvent invalidEvents[2];
        invalidEvents[0].parameterOrdinal = 127;
        invalidEvents[0].sampleOffset = 0;
        invalidEvents[0].normalizedValue = 0.01f;
        invalidEvents[1].parameterOrdinal = 128;
        invalidEvents[1].sampleOffset = 0;
        invalidEvents[1].normalizedValue = 0.99f;
        processDenseProxyCallback(*proxy, block, kBlockSamples,
                                   invalidEvents, 2);
        const auto afterInvalid = proxy->automationDiagnostics();
        float worker127AfterInvalid = -1.0f;
        expect(fetchDenseWorkerValue(*proxy, 127, worker127AfterInvalid, error),
               "ordinal 127 post-rejection worker fetch failed: " + error);
        expect(afterInvalid.workerInvalidBatches == beforeInvalid.workerInvalidBatches + 1,
               "ordinal 128 did not produce deterministic invalid-batch handling");
        expect(afterInvalid.parentOverflowRejected == beforeInvalid.parentOverflowRejected,
               "ordinal 128 was misclassified as an event-capacity overflow");
        expectWithinAbsoluteError(worker127AfterInvalid, worker127BeforeInvalid, 0.0005f,
                                  "mixed valid+unsupported batch partially changed the worker");
        expect(proxy->automationEnabled(), "ordinal 128 rejection disabled E2A");
        expect(proxy->currentGeneration() != 0, "ordinal 128 rejection invalidated generation");

        beginTest("valid automation remains usable after ordinal 128 rejection");
        AutomationSnapshot valid1;
        addLegacyPoint(valid1, trackId,
                       denseCoreParameterId(*slot, 0, "param_1"), 0.17f);
        chain.applyAutomationAtSample(trackId, &valid1, 1024,
                                      sampleRate, bpm, kBlockSamples);
        const float expected1 = slot->sandboxLastAutomationValueForTesting(1);
        const auto appliedBeforeRecovery = proxy->automationDiagnostics().workerAppliedEvents;
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        float worker1After = -1.0f;
        expect(fetchDenseWorkerValue(*proxy, 1, worker1After, error),
               "post-128 param_1 worker fetch failed: " + error);
        expectWithinAbsoluteError(worker1After, expected1, 0.0005f,
                                  "valid param_1 automation failed after ordinal 128");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(1),
                                  expected1, 0.0005f,
                                  "post-128 param_1 lastDelivered did not commit");
        expect(proxy->automationDiagnostics().workerAppliedEvents == appliedBeforeRecovery + 1,
               "post-128 valid automation applied an unexpected event count");

        logMessage("APEX_E2B_ORDINAL_DIAG metadata=" + juce::String(metadata.size())
            + " ord127=" + param127Id
             + " ord128=" + param128Id
             + " invalidBatches=" + juce::String(static_cast<juce::int64>(
                 afterInvalid.workerInvalidBatches))
             + " worker127BeforeAfter=" + juce::String(worker127BeforeInvalid, 4)
             + "/" + juce::String(worker127AfterInvalid, 4)
             + " post128Param1=" + juce::String(worker1After, 4));

        chain.removePlugin(0);
        PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B unsupported ordinal bound");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BUnsupportedOrdinalBoundTest pluginSandboxPhaseE2BUnsupportedOrdinalBoundTest;

class PluginSandboxPhaseE2BMultiParameterBoundaryOrderTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BMultiParameterBoundaryOrderTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.multi-parameter-boundary-order.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("param_0, param_1, param_63, and param_127 commit independently");
        juce::MessageManager::getInstance();
        DenseE2BTestContext context;
        juce::String error;
        expect(discoverDenseE2BContext(context, error), error);
        if (context.description.fileOrIdentifier.isEmpty())
            return;

        constexpr double sampleRate = 48000.0;
        constexpr double bpm = 120.0;
        const juce::String trackId = "e2b-final-multi-boundary";
        constexpr std::array<int, 4> ordinals { 0, 1, 63, 127 };
        constexpr std::array<float, 4> rawValues { 0.11f, 0.22f, 0.63f, 0.94f };
        PluginChainCore chain;
        expect(appendDenseSlot(chain, context, trackId, 2048, error),
               "multi-boundary dense insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        if (slot == nullptr || proxy == nullptr)
            return;

        AutomationSnapshot boundary;
        for (std::size_t i = 0; i < ordinals.size(); ++i)
        {
            const int ordinal = ordinals[i];
            addLegacyPoint(boundary, trackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           rawValues[i]);
        }
        chain.applyAutomationAtSample(trackId, &boundary, 0,
                                      sampleRate, bpm, kBlockSamples);
        const auto staged = slot->sandboxStagedEventCountForTesting();
        std::array<float, 4> expected {};
        for (std::size_t i = 0; i < ordinals.size(); ++i)
            expected[i] = slot->sandboxLastAutomationValueForTesting(ordinals[i]);
        const auto appliedBefore = proxy->automationDiagnostics().workerAppliedEvents;
        auto block = makeFilledDenseBlock(kBlockSamples);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto appliedAfter = proxy->automationDiagnostics().workerAppliedEvents;

        expectEquals(staged, static_cast<std::uint32_t>(4),
                     "same-boundary multi-parameter staging count is not four");
        expectEquals(appliedAfter - appliedBefore, static_cast<std::uint64_t>(4),
                     "same-boundary multi-parameter worker count is not four");
        for (std::size_t i = 0; i < ordinals.size(); ++i)
        {
            const int ordinal = ordinals[i];
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinal),
                                      expected[i], 0.0005f,
                                      "same-boundary lastDelivered mismatch at ordinal "
                                          + juce::String(ordinal));
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "same-boundary worker fetch failed: " + error);
            expectWithinAbsoluteError(workerValue, expected[i], 0.0005f,
                                      "same-boundary worker mismatch at ordinal "
                                          + juce::String(ordinal));
            for (std::size_t j = i + 1; j < ordinals.size(); ++j)
                expect(std::abs(expected[i] - expected[j]) > 0.0001f,
                       "same-boundary values collided between ordinals");
        }

        beginTest("multiple canonical sources for param_1 collapse to one final event");
        const auto apexKey = denseApexParameterKey(*slot, trackId, 0, "param_1");
        const auto bridgeKey = denseBridgeParameterKey(*slot, trackId, 0, "param_1");
        const auto apexId = setAutomationLaneValue(apexKey, 0.41f);
        const auto bridgeId = setAutomationLaneValue(bridgeKey, 0.79f);
        AutomationSnapshot collapsed;
        addLegacyPoint(collapsed, trackId,
                       denseCoreParameterId(*slot, 0, "param_1"), 0.23f);
        chain.applyAutomationAtSample(trackId, &collapsed, 512,
                                      sampleRate, bpm, kBlockSamples);
        const float collapsedExpected = slot->sandboxLastAutomationValueForTesting(1);
        const auto stagedCollapsed = slot->sandboxStagedEventCountForTesting();
        const auto appliedBeforeCollapsed = proxy->automationDiagnostics().workerAppliedEvents;
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto appliedAfterCollapsed = proxy->automationDiagnostics().workerAppliedEvents;
        float collapsedWorker = -1.0f;
        expect(fetchDenseWorkerValue(*proxy, 1, collapsedWorker, error),
               "collapsed param_1 worker fetch failed: " + error);
        expectEquals(stagedCollapsed, static_cast<std::uint32_t>(1),
                     "source collapse staged more than one param_1 event");
        expectEquals(appliedAfterCollapsed - appliedBeforeCollapsed,
                     static_cast<std::uint64_t>(1),
                     "source collapse applied more than one param_1 event");
        expectWithinAbsoluteError(collapsedWorker, collapsedExpected, 0.0005f,
                                  "collapsed param_1 worker value mismatch");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(1),
                                  collapsedExpected, 0.0005f,
                                  "collapsed param_1 lastDelivered mismatch");
        removeAutomationLane(apexId);
        removeAutomationLane(bridgeId);

        logMessage("APEX_E2B_MULTI_BOUNDARY_DIAG staged="
            + juce::String(static_cast<int>(staged))
            + " ordinals=0,1,63,127 appliedDelta="
            + juce::String(static_cast<juce::int64>(appliedAfter - appliedBefore))
            + " collapsedStaged=" + juce::String(static_cast<int>(stagedCollapsed))
            + " collapsedAppliedDelta=" + juce::String(static_cast<juce::int64>(
                appliedAfterCollapsed - appliedBeforeCollapsed))
            + " collapsedValue=" + juce::String(collapsedWorker, 4));

        chain.removePlugin(0);
        PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B multi-parameter boundary order");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BMultiParameterBoundaryOrderTest pluginSandboxPhaseE2BMultiParameterBoundaryOrderTest;

class PluginSandboxPhaseE2BMultiParameterDeliveryResubmitTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BMultiParameterDeliveryResubmitTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.multi-parameter-delivery-resubmit.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("failed multi-parameter submit preserves all lastDelivered values and resubmits current state");
        juce::MessageManager::getInstance();
        DenseE2BTestContext context;
        juce::String error;
        expect(discoverDenseE2BContext(context, error), error);
        if (context.description.fileOrIdentifier.isEmpty())
            return;

        constexpr double sampleRate = 48000.0;
        constexpr double bpm = 120.0;
        constexpr std::array<int, 3> ordinals { 0, 1, 63 };
        const juce::String trackId = "e2b-final-multi-resubmit";
        PluginChainCore chain;
        expect(appendDenseSlot(chain, context, trackId, 2048, error),
               "multi-resubmit dense insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        if (slot == nullptr || proxy == nullptr)
            return;

        constexpr std::array<float, 3> stateAValues { 0.18f, 0.29f, 0.40f };
        constexpr std::array<float, 3> stateBValues { 0.82f, 0.71f, 0.60f };
        AutomationSnapshot stateA;
        AutomationSnapshot stateB;
        for (std::size_t i = 0; i < ordinals.size(); ++i)
        {
            const auto parameterId = "param_" + juce::String(ordinals[i]);
            const auto coreId = denseCoreParameterId(*slot, 0, parameterId);
            addLegacyPoint(stateA, trackId, coreId, stateAValues[i]);
            addLegacyPoint(stateB, trackId, coreId, stateBValues[i]);
        }

        chain.applyAutomationAtSample(trackId, &stateA, 0,
                                      sampleRate, bpm, kBlockSamples);
        std::array<float, 3> expectedA {};
        for (std::size_t i = 0; i < ordinals.size(); ++i)
            expectedA[i] = slot->sandboxLastAutomationValueForTesting(ordinals[i]);
        auto block = makeFilledDenseBlock(kBlockSamples);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterA = proxy->automationDiagnostics();
        expectEquals(afterA.workerAppliedEvents, static_cast<std::uint64_t>(3),
                     "state A did not apply all three parameters");
        for (std::size_t i = 0; i < ordinals.size(); ++i)
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinals[i]),
                                      expectedA[i], 0.0005f,
                                      "state A lastDelivered mismatch");

        chain.applyAutomationAtSample(trackId, &stateB, 512,
                                      sampleRate, bpm, kBlockSamples);
        const auto stagedB = slot->sandboxStagedEventCountForTesting();
        const auto beforeFailure = proxy->automationDiagnostics();
        const auto beforeFailureTransport = proxy->lifecycleDiagnostics().transport;
        proxy->forceNextSubmissionFailureForTest(1);
        chain.processBlock(block, kBlockSamples);
        const auto afterFailure = proxy->automationDiagnostics();
        const auto afterFailureTransport = proxy->lifecycleDiagnostics().transport;
        expectEquals(stagedB, static_cast<std::uint32_t>(3),
                     "state B did not stage all three affected parameters");
        expectEquals(afterFailureTransport.submitMisses, beforeFailureTransport.submitMisses + 1,
                     "forced multi-parameter submit failure was not recorded");
        expectEquals(afterFailureTransport.submitted, beforeFailureTransport.submitted,
                     "failed multi-parameter submit was counted as successful");
        expectEquals(afterFailure.workerAppliedEvents, afterA.workerAppliedEvents,
                     "failed multi-parameter submit partially applied worker state");
        for (std::size_t i = 0; i < ordinals.size(); ++i)
        {
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinals[i]),
                                      expectedA[i], 0.0005f,
                                      "failed multi-parameter submit advanced lastDelivered");
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinals[i], workerValue, error),
                   "worker fetch after multi-parameter failure failed: " + error);
            expectWithinAbsoluteError(workerValue, expectedA[i], 0.0005f,
                                      "failed multi-parameter submit changed worker state");
        }

        chain.applyAutomationAtSample(trackId, &stateB, 1024,
                                      sampleRate, bpm, kBlockSamples);
        const auto stagedRetry = slot->sandboxStagedEventCountForTesting();
        std::array<float, 3> expectedRetry {};
        for (std::size_t i = 0; i < ordinals.size(); ++i)
            expectedRetry[i] = slot->sandboxLastAutomationValueForTesting(ordinals[i]);
        chain.processBlock(block, kBlockSamples);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterRetry = proxy->automationDiagnostics();
        const auto afterRetryTransport = proxy->lifecycleDiagnostics().transport;
        expectEquals(stagedRetry, static_cast<std::uint32_t>(3),
                     "multi-parameter retry did not retain all affected parameters");
        expectEquals(afterRetryTransport.submitted, beforeFailureTransport.submitted + 1,
                     "multi-parameter retry did not submit exactly one new quantum");
        expectEquals(afterRetryTransport.submitMisses, beforeFailureTransport.submitMisses + 1,
                     "multi-parameter retry changed submit-miss count unexpectedly");
        expectEquals(afterRetry.workerAppliedEvents, afterA.workerAppliedEvents + 3,
                     "multi-parameter retry did not apply all current parameters");
        for (std::size_t i = 0; i < ordinals.size(); ++i)
        {
            const int ordinal = ordinals[i];
            expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(ordinal),
                                      expectedRetry[i], 0.0005f,
                                      "multi-parameter retry lastDelivered mismatch");
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "worker fetch after multi-parameter retry failed: " + error);
            expectWithinAbsoluteError(workerValue, expectedRetry[i], 0.0005f,
                                      "multi-parameter retry worker mismatch");
        }

        logMessage("APEX_E2B_MULTI_RESUBMIT_DIAG affected="
            + juce::String(static_cast<int>(ordinals.size()))
            + " stagedB=" + juce::String(static_cast<int>(stagedB))
            + " stagedRetry=" + juce::String(static_cast<int>(stagedRetry))
            + " submitMisses=" + juce::String(static_cast<juce::int64>(afterRetryTransport.submitMisses))
            + " applied=" + juce::String(static_cast<juce::int64>(afterRetry.workerAppliedEvents)));

        chain.removePlugin(0);
        PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B multi-parameter delivery resubmit");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BMultiParameterDeliveryResubmitTest pluginSandboxPhaseE2BMultiParameterDeliveryResubmitTest;

class PluginSandboxPhaseE2BEdgeMatrixZeroRtTest final : public juce::UnitTest
{
public:
    PluginSandboxPhaseE2BEdgeMatrixZeroRtTest()
        : juce::UnitTest("plugin.sandbox.phase-e2b.edge-matrix-zero-rt.v1",
                         "PluginSandboxPhaseE2B") {}

    void runTest() override
    {
       #if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("final E2B edge matrix remains allocation-free in the callback");
       #if ! JUCE_ENABLE_ALLOCATION_HOOKS
        expect(false, "JUCE allocation hooks are required for the E2B edge zero-RT proof");
        return;
       #endif
        juce::MessageManager::getInstance();
        DenseE2BTestContext context;
        juce::String error;
        expect(discoverDenseE2BContext(context, error), error);
        if (context.description.fileOrIdentifier.isEmpty())
            return;

        constexpr double sampleRate = 48000.0;
        constexpr double bpm = 120.0;
        constexpr int kFirstCount = 64;
        constexpr int kOverflowCount = 65;
        const juce::String trackId = "e2b-final-edge-zero-rt";
        const juce::String reentryTrackId = trackId + "-supported-domain";
        PluginChainCore chain;
        expect(appendDenseSlot(chain, context, trackId, 2048, error),
               "edge zero-RT dense insertion failed: " + error);
        auto* slot = chain.getSlot(0);
        auto* proxy = slot != nullptr ? slot->getSandboxProxy() : nullptr;
        if (slot == nullptr || proxy == nullptr)
            return;

        // All snapshots, lane vectors, strings, buffers, and event storage are
        // prepared before the measured callbacks. The measured lambda only
        // performs the production producer call and the prepared audio call.
        AutomationSnapshot noChange;
        AutomationSnapshot state64;
        AutomationSnapshot state65;
        AutomationSnapshot supportedOrdinal127;
        AutomationSnapshot multi;
        AutomationSnapshot resubmitA;
        AutomationSnapshot resubmitB;
        for (int ordinal = 0; ordinal < kFirstCount; ++ordinal)
        {
            addLegacyPoint(state64, trackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           0.10f + 0.007f * static_cast<float>(ordinal));
            addLegacyPoint(state65, trackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           0.88f - 0.003f * static_cast<float>(ordinal));
        }
        addLegacyPoint(state65, trackId,
                       denseCoreParameterId(*slot, 0, "param_64"), 0.69f);
        addLegacyPoint(supportedOrdinal127, reentryTrackId,
                       denseCoreParameterId(*slot, 0, "param_127"), 0.93f);
        for (const int ordinal : { 0, 1, 63, 127 })
            addLegacyPoint(multi, reentryTrackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           ordinal == 0 ? 0.14f
                             : ordinal == 1 ? 0.27f
                             : ordinal == 63 ? 0.61f : 0.91f);
        for (const int ordinal : { 0, 1, 63 })
        {
            const float stateAValue = ordinal == 0 ? 0.21f
                : ordinal == 1 ? 0.29f : 0.37f;
            const float stateBValue = ordinal == 0 ? 0.79f
                : ordinal == 1 ? 0.71f : 0.63f;
            addLegacyPoint(resubmitA, reentryTrackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           stateAValue);
            addLegacyPoint(resubmitB, reentryTrackId,
                           denseCoreParameterId(*slot, 0,
                                                "param_" + juce::String(ordinal)),
                           stateBValue);
        }

        // The invalid ordinal is paired with a valid ordinal so the worker's
        // whole-batch validation also proves that an unsupported event cannot
        // partially apply a representable neighbor. LiveValues itself is
        // intentionally bounded to ordinals 0..127.
        std::array<PluginSandboxAutomationShared::SandboxAutomationEvent, 2>
            invalidEvents {};
        invalidEvents[0].parameterOrdinal = 127;
        invalidEvents[0].sampleOffset = 0;
        invalidEvents[0].normalizedValue = 0.01f;
        invalidEvents[1].parameterOrdinal = 128;
        invalidEvents[1].sampleOffset = 0;
        invalidEvents[1].normalizedValue = 0.97f;

        std::array<float, kOverflowCount> deliveredBeforeOverflow {};
        std::array<float, kOverflowCount> workerBeforeOverflow {};

        // Warm immutable snapshot/key lookups without changing the callback
        // schedule that is measured below.
        chain.applyAutomationAtSample(trackId, nullptr, 0,
                                      sampleRate, bpm, kBlockSamples);
        auto block = makeFilledDenseBlock(2048);
        int64_t position = 0;
        std::uint32_t measuredCallbacks = 0;
        auto measureChainCallback = [&](const juce::String& callbackTrackId,
                                        const AutomationSnapshot* snapshot,
                                        int samples) -> std::uint32_t
        {
            std::uint32_t staged = 0;
            fillDenseBlock(block, samples);
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.applyAutomationAtSample(callbackTrackId, snapshot, position,
                                              sampleRate, bpm, samples);
                staged = slot->sandboxStagedEventCountForTesting();
                chain.processBlock(block, samples);
            }
            position += samples;
            ++measuredCallbacks;
            return staged;
        };

        const auto staged480 = measureChainCallback(trackId, &noChange, 480);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto staged64Boundary = measureChainCallback(trackId, &state64, 64);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto after64Boundary = proxy->automationDiagnostics();
        const auto staged512 = measureChainCallback(trackId, &noChange, 512);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto staged1024 = measureChainCallback(trackId, &noChange, 1024);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto staged2048 = measureChainCallback(trackId, &noChange, 2048);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);

        for (int ordinal = 0; ordinal < kOverflowCount; ++ordinal)
        {
            deliveredBeforeOverflow[static_cast<std::size_t>(ordinal)] =
                slot->sandboxLastDeliveredValueForTesting(ordinal);
            expect(fetchDenseWorkerValue(*proxy, ordinal,
                                         workerBeforeOverflow[static_cast<std::size_t>(ordinal)],
                                         error),
                   "edge zero-RT pre-overflow worker fetch failed: " + error);
        }
        const auto beforeOverflow = proxy->automationDiagnostics();
        const auto staged65 = measureChainCallback(trackId, &state65, 512);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterOverflow = proxy->automationDiagnostics();

        // Validate the first oversized callback before any later callback can
        // legitimately change these parameters.
        expectEquals(staged65, static_cast<std::uint32_t>(kOverflowCount),
                     "65-event edge callback did not reach overflow staging");
        expect(afterOverflow.parentOverflowRejected
                   == beforeOverflow.parentOverflowRejected + 1,
               "first 65-event edge callback did not enter parent overflow handling");
        expect(afterOverflow.workerInvalidBatches
                   == beforeOverflow.workerInvalidBatches + 1,
               "first 65-event edge callback did not reach worker invalid handling");
        expect(afterOverflow.workerAppliedEvents
                   == beforeOverflow.workerAppliedEvents,
               "first 65-event edge callback partially applied worker events");
        for (int ordinal = 0; ordinal < kOverflowCount; ++ordinal)
        {
            expectWithinAbsoluteError(
                slot->sandboxLastDeliveredValueForTesting(ordinal),
                deliveredBeforeOverflow[static_cast<std::size_t>(ordinal)],
                0.0005f,
                "first oversized edge callback falsely committed ordinal "
                    + juce::String(ordinal));
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "first oversized edge worker fetch failed: " + error);
            expectWithinAbsoluteError(
                workerValue,
                workerBeforeOverflow[static_cast<std::size_t>(ordinal)],
                0.0005f,
                "first oversized edge callback changed worker ordinal "
                    + juce::String(ordinal));
        }

        const auto staged65Again = measureChainCallback(trackId, &state65, 512);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto after65Again = proxy->automationDiagnostics();

        // Validate the repeated oversized callback against the same last
        // successfully delivered state, still before supported-domain reentry.
        expectEquals(staged65Again, static_cast<std::uint32_t>(kOverflowCount),
                     "unchanged 65-event edge callback did not repeat overflow staging");
        expect(after65Again.parentOverflowRejected
                   == afterOverflow.parentOverflowRejected + 1,
               "repeated 65-event edge callback did not reject atomically again");
        expect(after65Again.workerInvalidBatches
                   == afterOverflow.workerInvalidBatches + 1,
               "repeated 65-event edge callback did not reach invalid handling again");
        expect(after65Again.workerAppliedEvents
                   == afterOverflow.workerAppliedEvents,
               "repeated 65-event edge callback changed worker applied-event count");
        for (int ordinal = 0; ordinal < kOverflowCount; ++ordinal)
        {
            expectWithinAbsoluteError(
                slot->sandboxLastDeliveredValueForTesting(ordinal),
                deliveredBeforeOverflow[static_cast<std::size_t>(ordinal)],
                0.0005f,
                "repeated oversized edge callback falsely committed ordinal "
                    + juce::String(ordinal));
            float workerValue = -1.0f;
            expect(fetchDenseWorkerValue(*proxy, ordinal, workerValue, error),
                   "repeated oversized edge worker fetch failed: " + error);
            expectWithinAbsoluteError(
                workerValue,
                workerBeforeOverflow[static_cast<std::size_t>(ordinal)],
                0.0005f,
                "repeated oversized edge callback changed worker ordinal "
                    + juce::String(ordinal));
        }

        // Re-enter the supported domain through the normal control-plane
        // context transition. This deliberately occurs outside every measured
        // realtime scope; it may rebuild and reseed the cached bindings from
        // the actual worker values, but it must not recreate the worker.
        const auto workerPidBeforeReentry = proxy->currentWorkerPid();
        const auto sessionBeforeReentry = proxy->processDiagnostics().sessionToken;
        const auto generationBeforeReentry = proxy->currentGeneration();
        const auto sidecarGenerationBeforeReentry =
            proxy->automationTransportForTest().generation();
        const auto sequenceBeforeReentry = proxy->automationSequenceForNextCallback();
        expect(processStillAlive(workerPidBeforeReentry, 50),
               "edge zero-RT worker did not remain alive after repeated overflow");
        chain.setAutomationContext(reentryTrackId, nullptr, nullptr);
        const auto bindingsAfterReentry = slot->sandboxAutomationBindingCountForTesting();
        expect(bindingsAfterReentry >= kFirstCount,
               "edge zero-RT supported-domain reentry did not rebuild bindings");
        expect(proxy->automationEnabled(),
               "edge zero-RT supported-domain reentry disabled E2A");
        expect(proxy->currentWorkerPid() == workerPidBeforeReentry,
               "edge zero-RT supported-domain reentry recreated the worker");
        expect(proxy->processDiagnostics().sessionToken == sessionBeforeReentry,
               "edge zero-RT supported-domain reentry changed the session");
        expect(proxy->currentGeneration() == generationBeforeReentry,
               "edge zero-RT supported-domain reentry changed audio generation");
        expect(proxy->automationTransportForTest().generation()
                   == sidecarGenerationBeforeReentry,
               "edge zero-RT supported-domain reentry changed sidecar generation");
        expect(proxy->automationSequenceForNextCallback() == sequenceBeforeReentry,
               "edge zero-RT supported-domain reentry reset automation sequence");

        const auto staged127 = measureChainCallback(
            reentryTrackId, &supportedOrdinal127, 512);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto after127 = proxy->automationDiagnostics();
        const float expected127 = slot->sandboxLastAutomationValueForTesting(127);
        float worker127BeforeInvalid = -1.0f;
        expect(fetchDenseWorkerValue(*proxy, 127, worker127BeforeInvalid, error),
               "edge zero-RT ordinal 127 worker fetch failed: " + error);
        const float delivered127BeforeInvalid =
            slot->sandboxLastDeliveredValueForTesting(127);
        expectEquals(staged127, static_cast<std::uint32_t>(1),
                     "post-reentry ordinal 127 callback did not stage one event");
        expect(after127.parentOverflowRejected
                   == after65Again.parentOverflowRejected,
               "post-reentry ordinal 127 callback retained overflow state");
        expect(after127.workerInvalidBatches
                   == after65Again.workerInvalidBatches,
               "post-reentry ordinal 127 callback retained invalid state");
        expect(after127.workerAppliedEvents
                   == after65Again.workerAppliedEvents + 1,
               "post-reentry ordinal 127 callback did not apply one event");
        expectWithinAbsoluteError(worker127BeforeInvalid, expected127, 0.0005f,
                                  "post-reentry ordinal 127 worker mismatch");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(127),
                                  expected127, 0.0005f,
                                  "post-reentry ordinal 127 lastDelivered mismatch");

        PluginSandboxFixedQuantumReblockerCore::ProcessSummary invalidSummary;
        const auto beforeInvalid = proxy->automationDiagnostics();
        fillDenseBlock(block, 512);
        {
            juce::UnitTestAllocationChecker allocationChecker(*this);
            invalidSummary = proxy->processBlockWithAutomation(
                block, 512, invalidEvents.data(),
                static_cast<std::uint32_t>(invalidEvents.size()));
        }
        position += 512;
        ++measuredCallbacks;
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterInvalid = proxy->automationDiagnostics();
        bool invalidBatchPublished = false;
        for (std::uint32_t reportIndex = 0;
             reportIndex < invalidSummary.automationDeliveryReportCount;
             ++reportIndex)
        {
            const auto& report = invalidSummary.automationDeliveryReports[reportIndex];
            if (report.sequence != 0)
            {
                invalidBatchPublished = report.automationBatchPublished;
                break;
            }
        }
        float worker127AfterInvalid = -1.0f;
        expect(fetchDenseWorkerValue(*proxy, 127, worker127AfterInvalid, error),
               "edge zero-RT ordinal 127 post-rejection worker fetch failed: " + error);
        expect(invalidBatchPublished,
               "ordinal 128 mixed edge batch was not published for validation");
        expect(afterInvalid.workerInvalidBatches
                   == beforeInvalid.workerInvalidBatches + 1,
               "ordinal 128 mixed edge batch did not produce one invalid batch");
        expect(afterInvalid.parentOverflowRejected
                   == beforeInvalid.parentOverflowRejected,
               "ordinal 128 mixed edge batch was misclassified as overflow");
        expect(afterInvalid.workerAppliedEvents
                   == beforeInvalid.workerAppliedEvents,
               "ordinal 128 mixed edge batch partially applied");
        expectWithinAbsoluteError(worker127AfterInvalid, worker127BeforeInvalid, 0.0005f,
                                  "ordinal 128 mixed edge batch changed valid sentinel");
        expectWithinAbsoluteError(slot->sandboxLastDeliveredValueForTesting(127),
                                  delivered127BeforeInvalid, 0.0005f,
                                  "ordinal 128 mixed edge batch changed lastDelivered");

        const auto stagedMulti = measureChainCallback(
            reentryTrackId, &multi, 512);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterMulti = proxy->automationDiagnostics();

        const auto stagedResubmitA = measureChainCallback(
            reentryTrackId, &resubmitA, 512);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterResubmitA = proxy->automationDiagnostics();
        proxy->forceNextSubmissionFailureForTest(1);
        const auto beforeResubmitFailure = proxy->lifecycleDiagnostics().transport;
        const auto stagedResubmitBFailure = measureChainCallback(
            reentryTrackId, &resubmitB, 512);
        const auto afterResubmitFailure = proxy->lifecycleDiagnostics().transport;
        const auto afterResubmitFailureAutomation = proxy->automationDiagnostics();
        const auto stagedResubmitBRetry = measureChainCallback(
            reentryTrackId, &resubmitB, 512);
        waitForWorkerQuanta(proxy, proxy->lifecycleDiagnostics().transport.submitted, 5000);
        const auto afterResubmitRetry = proxy->automationDiagnostics();

        // Assertions deliberately occur after every allocation-checking scope.
        expectEquals(staged480, static_cast<std::uint32_t>(0),
                     "480 callback unexpectedly staged edge automation");
        expectEquals(staged64Boundary, static_cast<std::uint32_t>(kFirstCount),
                     "64-event edge callback did not stage exactly 64 events");
        expectEquals(staged512, static_cast<std::uint32_t>(0),
                     "512 steady callback unexpectedly staged an event");
        expectEquals(staged1024, static_cast<std::uint32_t>(0),
                     "1024 steady callback unexpectedly staged an event before reentry");
        expectEquals(staged2048, static_cast<std::uint32_t>(0),
                     "2048 steady callback unexpectedly staged an event");
        expectEquals(after64Boundary.workerAppliedEvents,
                     static_cast<std::uint64_t>(kFirstCount),
                     "64-event edge callback did not apply exactly 64 events");
        expectEquals(after64Boundary.parentOverflowRejected,
                     static_cast<std::uint64_t>(0),
                     "64-event edge callback was marked overflow");
        expectEquals(after64Boundary.workerInvalidBatches,
                     static_cast<std::uint64_t>(0),
                     "64-event edge callback was marked invalid");

        expectEquals(stagedMulti, static_cast<std::uint32_t>(4),
                     "post-reentry multi-parameter edge callback did not stage four events");
        expect(afterMulti.parentOverflowRejected
                   == afterInvalid.parentOverflowRejected,
               "post-reentry multi-parameter callback introduced overflow");
        expect(afterMulti.workerInvalidBatches
                   == afterInvalid.workerInvalidBatches,
               "post-reentry multi-parameter callback introduced invalid handling");
        expect(afterMulti.workerAppliedEvents
                   == afterInvalid.workerAppliedEvents + stagedMulti,
               "post-reentry multi-parameter callback applied an unexpected count");

        expectEquals(stagedResubmitA, static_cast<std::uint32_t>(3),
                     "post-reentry resubmit setup did not stage three events");
        expect(afterResubmitA.workerAppliedEvents
                   == afterMulti.workerAppliedEvents + stagedResubmitA,
               "post-reentry resubmit setup applied an unexpected count");
        expectEquals(stagedResubmitBFailure, static_cast<std::uint32_t>(3),
                     "post-reentry failed callback did not stage three events");
        expectEquals(afterResubmitFailure.submitMisses,
                     beforeResubmitFailure.submitMisses + 1,
                     "edge zero-RT failed submission was not recorded");
        expectEquals(afterResubmitFailure.submitted,
                     beforeResubmitFailure.submitted,
                     "failed edge submission was counted as successful");
        expectEquals(afterResubmitFailureAutomation.workerAppliedEvents,
                     afterResubmitA.workerAppliedEvents,
                     "failed edge submission partially applied worker state");
        expectEquals(stagedResubmitBRetry, static_cast<std::uint32_t>(3),
                     "post-reentry retry did not stage all three events");
        expect(afterResubmitRetry.workerInvalidBatches
                   == afterInvalid.workerInvalidBatches,
               "edge zero-RT retry introduced an invalid batch");
        expect(afterResubmitRetry.workerAppliedEvents
                   == afterResubmitA.workerAppliedEvents + stagedResubmitBRetry,
               "edge zero-RT retry did not apply all three events");
        expectEquals(proxy->lifecycleDiagnostics().activeRealtimeCalls,
                     static_cast<std::uint32_t>(0),
                     "edge zero-RT left a realtime call active");

        logMessage("APEX_E2B_EDGE_ZERO_RT_DIAG callbacks="
            + juce::String(static_cast<int>(measuredCallbacks))
            + " sizes=480,64,512,1024,2048 staged64="
            + juce::String(static_cast<int>(staged64Boundary))
            + " staged65=" + juce::String(static_cast<int>(staged65))
            + " staged65Again=" + juce::String(static_cast<int>(staged65Again))
            + " reentry127=" + juce::String(static_cast<int>(staged127))
            + " invalid128Published=" + juce::String(invalidBatchPublished ? 1 : 0)
            + " multi=" + juce::String(static_cast<int>(stagedMulti))
            + " resubmit=" + juce::String(static_cast<int>(stagedResubmitBFailure))
            + "/" + juce::String(static_cast<int>(stagedResubmitBRetry))
            + " bindingsAfterReentry=" + juce::String(bindingsAfterReentry)
            + " allocations=0 locks=0 waits=0 pipeIpc=0 sleeps=0");

        chain.removePlugin(0);
        PluginChainCore::drainRetiredPlugins();
       #else
        beginTest("Windows-only E2B edge matrix zero-RT");
        expect(true);
       #endif
    }
};

PluginSandboxPhaseE2BEdgeMatrixZeroRtTest pluginSandboxPhaseE2BEdgeMatrixZeroRtTest;

} // namespace
