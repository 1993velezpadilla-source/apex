// PluginBusLayoutMatrixTests.cpp
//
// APEX #7 host-compatibility RED regressions (Waves Clarity Vx canary):
//
//   plugin.bus-layout.matrix.v1
//     The host must negotiate a valid main I/O layout from the plugin's
//     capabilities instead of forcing stereo, must present exactly
//     max(totalInputs, totalOutputs) process channels, must never allocate
//     in processBlock, and a rejected sidechain proposal must leave the slot
//     prepared under its previous valid layout.
//
//   plugin.project-restore.lifecycle-layout.v1
//     Save/restore must persist the negotiated bus layout, restore opaque
//     state under that layout, preserve slot identity/host fields, and a
//     detectable state-restore failure must be contained (slot preserved in
//     a safe unprepared state) instead of escaping as an APEX crash.
//
// All processors are deterministic mocks; no third-party plugin is required.
// Waves Clarity Vx remains the external acceptance example only.

#include <JuceHeader.h>
#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/PluginHostCore/ClipRegionPluginCore.h"
#include "../../../Source/Automation/AutomationSystemCore.h"
#include "../../../Source/CommandCore/GeneralCommands.h"

#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr double kRate  = 48000.0;
constexpr int    kBlock = 512;

constexpr const char* kFormatName        = "APEX Matrix Test";
constexpr const char* kIdentifierPrefix  = "APEX::MatrixTest::";

// Test component UIDs (deterministic, non-Waves).
constexpr int kUidStereo          = 0x4D543001;
constexpr int kUidMono            = 0x4D543002;
constexpr int kUidMonoToStereo    = 0x4D543003;
constexpr int kUidStereoToMono    = 0x4D543004;
constexpr int kUidInstrumentMono  = 0x4D543005;
constexpr int kUidInstrumentStereo= 0x4D543006;
constexpr int kUidDual            = 0x4D543007;
constexpr int kUidStateThrow      = 0x4D543008;
constexpr int kUidSidechainReject = 0x4D543009;
constexpr int kUidTwinA           = 0x4D54300A;   // same display name, distinct UID
constexpr int kUidTwinB           = 0x4D54300B;
constexpr int kUidFamilyBase      = 0x4D54300C;   // same vendor, similar name
constexpr int kUidFamilyPro       = 0x4D54300D;
constexpr int kUidModulePro       = 0x4D54300E;   // module A ("ShellA")
constexpr int kUidModuleBase      = 0x4D54300F;   // module B ("ShellB")
constexpr int kUidModuleSibling   = 0x4D543010;   // module B, second component

constexpr const char* kShellAPath = "APEX::ShellA.vst3";
constexpr const char* kShellBPath = "APEX::ShellB.vst3";

struct MockModule;   // defined with the module-lifetime suite below

using LifecycleLog = std::vector<juce::String>;

struct MatrixSpec
{
    int  uid                      = 0;
    juce::String name             = "APEX Matrix Probe";
    juce::Array<int> allowedInputChannels;    // empty => no main input bus
    juce::Array<int> allowedOutputChannels;   // empty => no main output bus
    int  defaultInputChannels     = 2;
    int  defaultOutputChannels    = 2;
    bool hasSidechainBus          = false;
    int  sidechainDefaultChannels = 1;
    bool rejectEnabledAuxBus      = false;    // veto any enabled auxiliary input
    bool stateThrows              = false;
    float gain                    = 1.0f;
    bool parameterControlsGain    = false;
    bool parameterIsBoolean      = false;   // true: discrete VST3 choice/switch probe
    juce::uint32 stateMarker      = 0;        // written to state / recorded from state
    juce::String modulePath;                  // non-empty => instance retains a mock module
};

juce::AudioChannelSet makeSet (int channels)
{
    switch (channels)
    {
        case 0:  return juce::AudioChannelSet::disabled();
        case 1:  return juce::AudioChannelSet::mono();
        default: return juce::AudioChannelSet::stereo();
    }
}

/** Deterministic hosted-processor mock. Records every host lifecycle call in
    a shared log so tests can prove call ORDER and negotiated LAYOUT without
    any third-party dependency. */
class MatrixProbeProcessor final : public juce::AudioPluginInstance
{
public:
    explicit MatrixProbeProcessor (MatrixSpec spec)
        : juce::AudioPluginInstance (buildBuses (spec)),
          spec_ (spec),
          log_ (std::make_shared<LifecycleLog>())
    {
        log_->reserve (64);
        log_->push_back ("ctor");
        // One automatable parameter so the production automation-registration
        // path (configureSlotAutomation) is exercised by identity tests.
        // (AudioPluginInstance hides addParameter as private in JUCE 8; the
        // qualified AudioProcessor path is the repo's proven G10 pattern.)
        if (spec_.parameterIsBoolean)
            juce::AudioProcessor::addParameter (new juce::AudioParameterBool ("param0", "Param 0", false));
        else
            juce::AudioProcessor::addParameter (new juce::AudioParameterFloat ("param0", "Param 0", 0.0f, 1.0f, 0.5f));
    }

    static juce::AudioProcessor::BusesProperties buildBuses (const MatrixSpec& s)
    {
        juce::AudioProcessor::BusesProperties props;
        if (! s.allowedInputChannels.isEmpty())
            props = props.withInput ("Input", makeSet (s.defaultInputChannels), true);
        props = props.withOutput ("Output", makeSet (s.defaultOutputChannels), true);
        if (s.hasSidechainBus)
            props = props.withInput ("Sidechain", makeSet (s.sidechainDefaultChannels), false);
        return props;
    }

    const juce::String getName() const override { return spec_.name; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        // Main input (when the plugin declares one).
        if (! spec_.allowedInputChannels.isEmpty())
        {
            const int mainIn = layouts.inputBuses.isEmpty()
                ? 0 : layouts.getChannelSet (true, 0).size();
            if (! spec_.allowedInputChannels.contains (mainIn))
                return false;
        }
        else if (! layouts.inputBuses.isEmpty() && ! layouts.getChannelSet (true, 0).isDisabled())
        {
            return false;   // instrument: no main input bus may be enabled
        }

        // Main output.
        if (spec_.allowedOutputChannels.isEmpty())
            return false;
        const int mainOut = layouts.outputBuses.isEmpty()
            ? 0 : layouts.getChannelSet (false, 0).size();
        if (! spec_.allowedOutputChannels.contains (mainOut))
            return false;

        // Auxiliary inputs beyond the main bus.
        for (int bus = 1; bus < layouts.inputBuses.size(); ++bus)
        {
            const auto& set = layouts.getChannelSet (true, bus);
            if (set.isDisabled())
                continue;
            if (! spec_.hasSidechainBus)
                return false;
            if (spec_.rejectEnabledAuxBus)
                return false;
            if (set.size() < 1 || set.size() > 2)
                return false;
        }
        // Auxiliary outputs must stay disabled.
        for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
            if (! layouts.getChannelSet (false, bus).isDisabled())
                return false;
        return true;
    }

    void prepareToPlay (double, int blockSize) override
    {
        ++prepareCount;
        maxBlock_ = juce::jmax(maxBlock_, blockSize);
        log_->push_back (juce::String::formatted ("prepare in=%d out=%d",
            getTotalNumInputChannels(), getTotalNumOutputChannels()));
    }
    void releaseResources() override { log_->push_back ("release"); }

    void setStateInformation (const void* data, int size) override
    {
        if (spec_.stateThrows)
        {
            log_->push_back ("state-throw");
            throw std::runtime_error ("controlled-state-failure");
        }
        lastStateSize = size;
        if (data != nullptr && size >= 4)
            std::memcpy (&lastStateMarker, data, 4);
        log_->push_back (juce::String::formatted ("state len=%d in=%d out=%d",
            size, getTotalNumInputChannels(), getTotalNumOutputChannels()));
    }

    void getStateInformation (juce::MemoryBlock& block) override
    {
        block.append (&spec_.stateMarker, 4);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int channels = buffer.getNumChannels();
        const int n = juce::jmin (buffer.getNumSamples(), maxBlock_);
        const int observation = processObservationCount++;
        if (observation < (int) observedParameterValues.size())
        {
            observedParameterValues[(size_t) observation] = getParameters()[0]->getValue();
            observedMidiEventCounts[(size_t) observation] = (int) midi.getNumEvents();
            observedFirstMidiSamplePositions[(size_t) observation] = -1;
            for (const auto metadata : midi)
            {
                observedFirstMidiSamplePositions[(size_t) observation] = metadata.samplePosition;
                break;
            }
        }
        lastChannels.store (channels, std::memory_order_relaxed);
        lastSamples.store (n, std::memory_order_relaxed);
        if (! quietLogging.load (std::memory_order_relaxed))
            log_->push_back (juce::String::formatted ("process ch=%d n=%d", channels, n));

        firstInput0 = n > 0 && channels > 0 ? buffer.getSample (0, 0) : -1000.0f;
        firstInput1 = n > 0 && channels > 1 ? buffer.getSample (1, 0) : -1000.0f;

        // Plugin-side DSP model (the HOST adapter owns the engine<->main
        // boundary mapping; the probe models what a real plugin does with
        // its own main input/output buses):
        //   instrument        -> deterministic tones
        //   stereo -> mono    -> 0.5 * (L + R)
        //   mono   -> stereo  -> mono input duplicated to both outputs
        //   matched           -> 1:1 pass
        const bool hasInput = ! spec_.allowedInputChannels.isEmpty();
        const int  inCount  = hasInput ? juce::jmin (spec_.defaultInputChannels, channels) : 0;
        const int  outCount = juce::jmin (spec_.defaultOutputChannels, channels);
        static constexpr float kTone[2] = { 0.5f, 0.75f };
        for (int ch = 0; ch < outCount; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float src = 0.0f;
                if (! hasInput)
                {
                    src = ch < 2 ? kTone[ch] : 0.0f;
                }
                else if (spec_.defaultOutputChannels == 1)
                {
                    src = 0.5f * (buffer.getSample (0, i)
                                  + (inCount >= 2 ? buffer.getSample (1, i) : buffer.getSample (0, i)));
                }
                else if (spec_.defaultInputChannels == 1)
                {
                    src = buffer.getSample (0, i);      // mono source duplicated by plugin DSP
                }
                else
                {
                    src = buffer.getSample (ch, i);
                }
                const float parameterGain = spec_.parameterControlsGain
                    ? getParameters()[0]->getValue() : 1.0f;
                d[i] = spec_.gain * parameterGain * src;
            }
        }
    }

    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name            = spec_.name;
        d.descriptiveName = spec_.name;
        d.pluginFormatName = kFormatName;
        d.manufacturerName = "APEX";
        d.version          = "1";
        d.fileOrIdentifier = kIdentifierPrefix + juce::String (spec_.uid);
        d.uniqueId         = spec_.uid;
        d.deprecatedUid    = spec_.uid;
        d.isInstrument     = spec_.allowedInputChannels.isEmpty();
        d.numInputChannels = spec_.defaultInputChannels;
        d.numOutputChannels = spec_.defaultOutputChannels;
    }

    std::shared_ptr<LifecycleLog> log_;
    MatrixSpec spec_;
    std::atomic<bool> quietLogging { false };   // allocation-checker scopes: no log growth
    int prepareCount = 0;
    int lastStateSize = 0;
    juce::uint32 lastStateMarker = 0;
    std::atomic<int> lastChannels { 0 };
    std::atomic<int> lastSamples  { 0 };
    std::atomic<float> firstInput0 { -1000.0f };
    std::atomic<float> firstInput1 { -1000.0f };
    std::array<float, 256> observedParameterValues {};
    std::array<int, 256> observedMidiEventCounts {};
    std::array<int, 256> observedFirstMidiSamplePositions {};
    int processObservationCount = 0;

    // Module-lifetime instrumentation (defined after MockModule).
    ~MatrixProbeProcessor() override;
    void retainModule (std::shared_ptr<MockModule> module);

private:
    std::shared_ptr<MockModule> retainedModule_;
    int maxBlock_ = kBlock;
};

// ── Spec registry ────────────────────────────────────────────────────────────

std::optional<MatrixSpec> makeSpecForUid (int uid)
{
    MatrixSpec s;
    s.uid = uid;
    switch (uid)
    {
        case kUidStereo:
            s.name = "APEX Matrix Stereo Probe";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            break;
        case kUidMono:
            s.name = "APEX Matrix Mono Probe";
            s.allowedInputChannels  = { 1 };
            s.allowedOutputChannels = { 1 };
            s.defaultInputChannels  = 1;
            s.defaultOutputChannels = 1;
            break;
        case kUidMonoToStereo:
            s.name = "APEX Matrix MonoToStereo Probe";
            s.allowedInputChannels  = { 1 };
            s.allowedOutputChannels = { 2 };
            s.defaultInputChannels  = 1;
            break;
        case kUidStereoToMono:
            s.name = "APEX Matrix StereoToMono Probe";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 1 };
            s.defaultOutputChannels = 1;
            break;
        case kUidInstrumentMono:
            s.name = "APEX Matrix InstrumentMono Probe";
            s.allowedOutputChannels = { 1 };
            s.defaultInputChannels  = 0;
            s.defaultOutputChannels = 1;
            break;
        case kUidInstrumentStereo:
            s.name = "APEX Matrix InstrumentStereo Probe";
            s.allowedOutputChannels = { 2 };
            s.defaultInputChannels  = 0;
            break;
        case kUidDual:
            s.name = "APEX Matrix Dual Probe";
            s.allowedInputChannels  = { 1, 2 };
            s.allowedOutputChannels = { 1, 2 };
            s.defaultInputChannels  = 1;
            s.defaultOutputChannels = 1;
            break;
        case kUidStateThrow:
            s.name = "APEX Matrix StateThrow Probe";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            s.stateThrows = true;
            break;
        case kUidSidechainReject:
            s.name = "APEX Matrix SidechainReject Probe";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            s.hasSidechainBus = true;
            s.rejectEnabledAuxBus = true;
            break;
        case kUidTwinA:
            s.name = "APEX Twin Probe";          // identical display names,
            s.allowedInputChannels  = { 2 };     // distinct component UIDs
            s.allowedOutputChannels = { 2 };
            s.stateMarker = 0xA1A1A1A1;
            break;
        case kUidTwinB:
            s.name = "APEX Twin Probe";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            s.stateMarker = 0xB2B2B2B2;
            break;
        case kUidFamilyBase:
            s.name = "APEX Family Probe";        // same vendor, similar names
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            s.stateMarker = 0xBA5EBA5E;
            break;
        case kUidFamilyPro:
            s.name = "APEX Family Probe Pro";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            s.stateMarker = 0x1DEA1DEA;
            break;
        case kUidModulePro:
            s.name = "Mock Foo Pro";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            s.stateMarker = 0x3A03A0;
            s.modulePath = kShellAPath;
            break;
        case kUidModuleBase:
            s.name = "Mock Foo";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            s.stateMarker = 0xBA5EBA5E;
            s.modulePath = kShellBPath;
            break;
        case kUidModuleSibling:
            s.name = "Mock Foo Duo";
            s.allowedInputChannels  = { 2 };
            s.allowedOutputChannels = { 2 };
            s.stateMarker = 0xD00D00;
            s.modulePath = kShellBPath;
            break;
        default:
            return std::nullopt;
    }
    return s;
}

juce::PluginDescription makeDescription (const MatrixSpec& s,
                                         const juce::String& formatName = kFormatName,
                                         const juce::String& identifierPrefix = kIdentifierPrefix)
{
    juce::PluginDescription d;
    d.name             = s.name;
    d.descriptiveName  = s.name;
    d.pluginFormatName = formatName;
    d.manufacturerName = "APEX";
    d.version          = "1";
    d.fileOrIdentifier = identifierPrefix + juce::String (s.uid);
    d.uniqueId         = s.uid;
    d.deprecatedUid    = s.uid;
    d.isInstrument     = s.allowedInputChannels.isEmpty();
    d.numInputChannels = s.defaultInputChannels;
    d.numOutputChannels = s.defaultOutputChannels;
    return d;
}

/** Deterministic AudioPluginFormat that instantiates the mocks through the
    production AudioPluginFormatManager creation path (same path the real
    Clarity Vx VST3 uses). */
class MatrixTestPluginFormat final : public juce::AudioPluginFormat
{
public:
    juce::String getName() const override { return kFormatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                              const juce::String&) override
    {
        for (int uid : { kUidStereo, kUidMono, kUidMonoToStereo, kUidStereoToMono,
                         kUidInstrumentMono, kUidInstrumentStereo, kUidDual,
                         kUidStateThrow, kUidSidechainReject,
                         kUidTwinA, kUidTwinB, kUidFamilyBase, kUidFamilyPro })
            if (auto spec = makeSpecForUid (uid))
                results.add (new juce::PluginDescription (makeDescription (*spec)));
    }

    bool fileMightContainThisPluginType (const juce::String& identifier) override
    {
        return identifier.startsWith (kIdentifierPrefix);
    }

    juce::String getNameOfPluginFromIdentifier (const juce::String& identifier) override
    {
        const int uid = identifier.substring (juce::String (kIdentifierPrefix).length()).getIntValue();
        if (auto spec = makeSpecForUid (uid))
            return spec->name;
        return {};
    }

    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }

    bool doesPluginStillExist (const juce::PluginDescription& desc) override
    {
        return desc.pluginFormatName == kFormatName && makeSpecForUid (desc.uniqueId).has_value();
    }

    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }

    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }

    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override
    {
        return false;
    }

    /** Log of the most recently created instance (captured synchronously). */
    static std::shared_ptr<LifecycleLog> lastCreatedLog;

protected:
    void createPluginInstance (const juce::PluginDescription& description,
                               double initialSampleRate,
                               int initialBufferSize,
                               PluginCreationCallback callback) override
    {
        if (callback == nullptr)
            return;

        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;

        if (auto spec = makeSpecForUid (description.uniqueId))
        {
            try
            {
                instance = std::make_unique<MatrixProbeProcessor> (*spec);
                instance->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
                lastCreatedLog = dynamic_cast<MatrixProbeProcessor*> (instance.get())->log_;
            }
            catch (...)
            {
                instance.reset();
                error = "APEX Matrix Test plugin creation failed.";
            }
        }
        else
        {
            error = "Unknown APEX Matrix Test uid " + juce::String (description.uniqueId);
        }

        callback (std::move (instance), error);
    }
};

std::shared_ptr<LifecycleLog> MatrixTestPluginFormat::lastCreatedLog;

// ── Helpers ───────────────────────────────────────────────────────────────────

bool eventExists (const std::shared_ptr<LifecycleLog>& log, const juce::String& prefix)
{
    if (! log) return false;
    for (const auto& e : *log)
        if (e.startsWith (prefix)) return true;
    return false;
}

int firstEventIndex (const std::shared_ptr<LifecycleLog>& log, const juce::String& prefix)
{
    if (! log) return -1;
    for (int i = 0; i < (int) log->size(); ++i)
        if ((*log)[i].startsWith (prefix)) return i;
    return -1;
}

void fillTonePair (juce::AudioBuffer<float>& buffer, int numSamples, float l, float r)
{
    if (buffer.getNumChannels() < 1) return;
    auto* L = buffer.getWritePointer (0);
    for (int i = 0; i < numSamples; ++i) L[i] = l;
    if (buffer.getNumChannels() > 1)
    {
        auto* R = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i) R[i] = r;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// plugin.bus-layout.matrix.v1
// ══════════════════════════════════════════════════════════════════════════════

class PluginBusLayoutMatrixTests final : public juce::UnitTest
{
public:
    PluginBusLayoutMatrixTests() : UnitTest ("plugin.bus-layout.matrix.v1", "PluginHost") {}

    void runTest() override
    {
        testStereoControl();
        testMonoInsertion();
        testMonoToStereoInsertion();
        testStereoToMonoInsertion();
        testInstrumentMonoOutput();
        testInstrumentStereoOutput();
        testDualLayoutPreference();
        testUnroutedAuxDisabled();
        testRejectedSidechainKeepsSlotPrepared();
        testNoRealtimeAllocation();
    }

    // Row A — control.
    void testStereoControl()
    {
        beginTest ("stereo track + stereo plugin = PASS");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidStereo);
        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (spec));
        expectEquals (idx, 0, "stereo plugin must insert");
        auto* slot = chain.getSlot (0);
        expect (slot != nullptr && slot->isPrepared(), "slot prepared");
        if (slot == nullptr) return;

        juce::AudioBuffer<float> buf (2, kBlock);
        fillTonePair (buf, kBlock, 0.25f, 0.75f);
        chain.processBlock (buf, kBlock);
        expectEquals ((int) slot->getProcessor()->getTotalNumInputChannels(), 2, "active input = 2");
        expectEquals ((int) slot->getProcessor()->getTotalNumOutputChannels(), 2, "active output = 2");
        expectEquals (slot->getLastProcessSamples(), kBlock, "active frame count");
        expectWithinAbsoluteError (buf.getSample (0, 0), 0.25f, 1.0e-5f, "L passthrough");
        expectWithinAbsoluteError (buf.getSample (1, 0), 0.75f, 1.0e-5f, "R passthrough");
    }

    // Row B — mono-only must be hostable (RED today: forced stereo rejects).
    void testMonoInsertion()
    {
        beginTest ("mono track + mono plugin = PASS");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidMono);
        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (spec));
        expectEquals (idx, 0, "mono plugin must insert (host must negotiate mono)");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        expect (slot->isPrepared(), "mono slot prepared");
        expectEquals ((int) slot->getProcessor()->getTotalNumInputChannels(), 1, "active input = 1");
        expectEquals ((int) slot->getProcessor()->getTotalNumOutputChannels(), 1, "active output = 1");

        juce::AudioBuffer<float> buf (2, kBlock);
        fillTonePair (buf, kBlock, 0.25f, 0.75f);
        chain.processBlock (buf, kBlock);
        expectWithinAbsoluteError (buf.getSample (0, 0), 0.5f, 1.0e-4f, "mono input = 0.5*(L+R)");
        expectWithinAbsoluteError (buf.getSample (1, 0), 0.5f, 1.0e-4f, "mono output duplicated to R");
    }

    // Row C — mono-to-stereo (RED today).
    void testMonoToStereoInsertion()
    {
        beginTest ("mono-to-stereo plugin = negotiated 1->2");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidMonoToStereo);
        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (spec));
        expectEquals (idx, 0, "mono-to-stereo plugin must insert");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        expect (slot->isPrepared(), "mono-to-stereo slot prepared");
        expectEquals ((int) slot->getProcessor()->getTotalNumInputChannels(), 1, "active input = 1");
        expectEquals ((int) slot->getProcessor()->getTotalNumOutputChannels(), 2, "active output = 2");

        juce::AudioBuffer<float> buf (2, kBlock);
        fillTonePair (buf, kBlock, 0.25f, 0.75f);
        chain.processBlock (buf, kBlock);
        expectWithinAbsoluteError (buf.getSample (0, 0), 0.5f, 1.0e-4f, "stereo output L from mono input");
        expectWithinAbsoluteError (buf.getSample (1, 0), 0.5f, 1.0e-4f, "stereo output R from mono input");
    }

    // Row D — stereo-to-mono (RED today).
    void testStereoToMonoInsertion()
    {
        beginTest ("stereo-to-mono plugin = negotiated 2->1");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidStereoToMono);
        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (spec));
        expectEquals (idx, 0, "stereo-to-mono plugin must insert");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        expect (slot->isPrepared(), "stereo-to-mono slot prepared");
        expectEquals ((int) slot->getProcessor()->getTotalNumInputChannels(), 2, "active input = 2");
        expectEquals ((int) slot->getProcessor()->getTotalNumOutputChannels(), 1, "active output = 1");

        juce::AudioBuffer<float> buf (2, kBlock);
        fillTonePair (buf, kBlock, 0.25f, 0.75f);
        chain.processBlock (buf, kBlock);
        expectWithinAbsoluteError (buf.getSample (0, 0), 0.5f, 1.0e-4f, "mono output duplicated to L");
        expectWithinAbsoluteError (buf.getSample (1, 0), 0.5f, 1.0e-4f, "mono output duplicated to R");
    }

    // Row E — mono-output instrument (RED today: output forced stereo).
    void testInstrumentMonoOutput()
    {
        beginTest ("0->1 instrument = negotiated mono output");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidInstrumentMono);
        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (spec));
        expectEquals (idx, 0, "mono-output instrument must insert");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        expect (slot->isPrepared(), "instrument slot prepared");
        expectEquals ((int) slot->getProcessor()->getTotalNumInputChannels(), 0, "active input = 0");
        expectEquals ((int) slot->getProcessor()->getTotalNumOutputChannels(), 1, "active output = 1");

        juce::AudioBuffer<float> buf (2, kBlock);
        buf.clear();
        chain.processBlock (buf, kBlock);
        expectWithinAbsoluteError (buf.getSample (0, 0), 0.5f, 1.0e-4f, "generated tone duplicated to L");
        expectWithinAbsoluteError (buf.getSample (1, 0), 0.5f, 1.0e-4f, "generated tone duplicated to R");
    }

    // Row F — stereo-output instrument control.
    void testInstrumentStereoOutput()
    {
        beginTest ("0->2 instrument = PASS");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidInstrumentStereo);
        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (spec));
        expectEquals (idx, 0, "stereo instrument must insert");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        expect (slot->isPrepared(), "stereo instrument slot prepared");

        juce::AudioBuffer<float> buf (2, kBlock);
        buf.clear();
        chain.processBlock (buf, kBlock);
        expectWithinAbsoluteError (buf.getSample (0, 0), 0.5f, 1.0e-4f, "tone L");
        expectWithinAbsoluteError (buf.getSample (1, 0), 0.75f, 1.0e-4f, "tone R");
    }

    // Row G — dual-layout plugin: host must deterministically negotiate one
    // valid layout (stereo for the stereo engine).
    void testDualLayoutPreference()
    {
        beginTest ("dual-layout plugin negotiates deterministically (stereo preferred)");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidDual);
        auto probe = std::make_unique<MatrixProbeProcessor> (spec);
        const auto log = probe->log_;
        const int idx = chain.appendPluginInstanceForTesting (std::move (probe));
        expectEquals (idx, 0, "dual-layout plugin must insert");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        expect (slot->isPrepared(), "dual-layout slot prepared");
        expectEquals ((int) slot->getProcessor()->getTotalNumInputChannels(), 2,
                      "negotiated stereo input for stereo engine");
        expectEquals ((int) slot->getProcessor()->getTotalNumOutputChannels(), 2,
                      "negotiated stereo output for stereo engine");
        expect (eventExists (log, "prepare in=2 out=2"), "prepare observed the negotiated stereo layout");
    }

    // Unrouted auxiliary bus must be disabled (host must not leak storage
    // channels into the plugin's active layout).
    void testUnroutedAuxDisabled()
    {
        beginTest ("unrouted auxiliary bus stays disabled");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        MatrixSpec spec = *makeSpecForUid (kUidStereo);
        spec.hasSidechainBus = true;
        spec.sidechainDefaultChannels = 2;
        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (spec));
        expectEquals (idx, 0, "sidechain-capable plugin must insert");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        expectEquals ((int) slot->getProcessor()->getTotalNumInputChannels(), 2,
                      "unrouted aux must not contribute active channels");
    }

    // RED today: a rejected sidechain proposal destroys the previous valid
    // prepared state (PluginInstanceCore::prepare leaves prepared_ == false).
    void testRejectedSidechainKeepsSlotPrepared()
    {
        beginTest ("rejected sidechain proposal keeps the previous valid layout prepared");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidSidechainReject);
        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (spec));
        expectEquals (idx, 0, "sidechain-rejecting plugin must insert main-only");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        expect (slot->isPrepared(), "main-only slot prepared initially");

        std::unordered_map<int, juce::Array<int>> proposal;
        proposal[0] = { 1 };
        chain.setActiveSidechainBusConfig (proposal);

        expect (! chain.isAuxInputBusActive (0, 1), "rejected aux bus must not be committed");
        expect (slot->isPrepared(),
                "slot must remain prepared under its previous valid layout after a rejected proposal");

        juce::AudioBuffer<float> buf (2, kBlock);
        fillTonePair (buf, kBlock, 0.25f, 0.75f);
        chain.processBlock (buf, kBlock);
        expectWithinAbsoluteError (buf.getSample (0, 0), 0.25f, 1.0e-5f,
                                   "wet processing continues after rejected proposal");
    }

    // RED today: processBlockInternal calls wideScratch_.setSize() when the
    // caller buffer is narrower than the plugin's input count — allocation on
    // the processing path.
    void testNoRealtimeAllocation()
    {
        beginTest ("narrower caller buffer must not allocate in processBlock");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        auto spec = *makeSpecForUid (kUidStereo);
        auto probe = std::make_unique<MatrixProbeProcessor> (spec);
        auto* probePtr = probe.get();
        const int idx = chain.appendPluginInstanceForTesting (std::move (probe));
        expectEquals (idx, 0, "stereo plugin must insert");
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;

        juce::AudioBuffer<float> mono (1, kBlock);
        mono.clear();
        juce::MidiBuffer midi;
        {
            // Probe instrumentation logging is disabled so the checker measures
            // HOST-path allocation only (the probe's own String log would
            // otherwise count).
            probePtr->quietLogging.store (true, std::memory_order_relaxed);
            juce::UnitTestAllocationChecker alloc (*this);
            slot->processBlock (mono, midi);
        }
        expectEquals ((int) slot->getProcessor()->getTotalNumInputChannels(), 2,
                      "plugin still declares its stereo contract");
    }
};

static PluginBusLayoutMatrixTests pluginBusLayoutMatrixTests;

// ══════════════════════════════════════════════════════════════════════════════
// plugin.project-restore.lifecycle-layout.v1
// ══════════════════════════════════════════════════════════════════════════════

class PluginProjectRestoreLifecycleLayoutTests final : public juce::UnitTest
{
public:
    PluginProjectRestoreLifecycleLayoutTests()
        : UnitTest ("plugin.project-restore.lifecycle-layout.v1", "PluginHost") {}

    void runTest() override
    {
        testPersistNegotiatedLayout();
        testRestoreLifecycleOrderAndHostFields();
        testStateFailureContainment();
    }

    // RED today: getState() persists only the Description and opaque State —
    // the negotiated bus layout is not stored, so restore cannot replay it.
    void testPersistNegotiatedLayout()
    {
        beginTest ("saved slot persists the negotiated bus layout");

        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<MatrixTestPluginFormat>());
        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        juce::String error;
        const int idx = chain.appendPlugin (makeDescription (*makeSpecForUid (kUidStereo)), fm, error, true);
        expectEquals (idx, 0, "production append succeeded: " + error);

        const auto chainTree = chain.getState();
        expectEquals (chainTree.getNumChildren(), 1, "one slot saved");
        if (chainTree.getNumChildren() != 1) return;

        const auto slotTree = chainTree.getChild (0);
        const auto layoutTree = slotTree.getChildWithName ("Layout");
        expect (layoutTree.isValid(), "Slot must persist a <Layout> child");
        if (layoutTree.isValid())
        {
            expectEquals ((int) layoutTree.getProperty ("mainInputChannels", 0), 2,
                          "persisted main input layout");
            expectEquals ((int) layoutTree.getProperty ("mainOutputChannels", 0), 2,
                          "persisted main output layout");
        }
    }

    // Production save/restore: identity, host fields, state order, first
    // process channel/sample contract.
    void testRestoreLifecycleOrderAndHostFields()
    {
        beginTest ("restore replays lifecycle order and host fields");

        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<MatrixTestPluginFormat>());

        // GOOD: create + save under a negotiated stereo layout.
        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        juce::String error;
        const int idx = chain.appendPlugin (makeDescription (*makeSpecForUid (kUidStereo)), fm, error, true);
        expectEquals (idx, 0, "production append succeeded: " + error);
        auto* slot = chain.getSlot (0);
        if (slot == nullptr) return;
        slot->setBypassed (true);
        slot->setSlotMixNormalized (0.4f);
        slot->setSlotMixBypass (true);
        auto savedTree = chain.getState();

        // BAD: reconstruct through the production restore path.
        DAW::PluginChainCore restored;
        restored.prepare (kRate, kBlock);
        restored.restoreState (savedTree, fm);

        expectEquals (restored.getNumActiveSlots(), 1, "restored slot count");
        auto* restoredSlot = restored.getSlot (0);
        expect (restoredSlot != nullptr, "restored slot exists");
        if (restoredSlot == nullptr) return;
        expect (restoredSlot->isPrepared(), "restored slot prepared");
        expectEquals (restoredSlot->getProcessor()->getName(),
                      juce::String ("APEX Matrix Stereo Probe"), "restored identity");
        expect (restoredSlot->isBypassed(), "restored bypass");
        expectWithinAbsoluteError (restoredSlot->getSlotMixNormalized(), 0.4f, 1.0e-5f, "restored mix");
        expect (restoredSlot->getSlotMixBypass(), "restored mix bypass");

        auto log = MatrixTestPluginFormat::lastCreatedLog;
        const int layoutIdx  = firstEventIndex (log, "prepare in=");
        const int stateIdx   = firstEventIndex (log, "state len=");
        const int processIdx = firstEventIndex (log, "process ch=");
        expect (layoutIdx >= 0, "layout negotiated before preparation");
        expect (stateIdx >= 0, "opaque state applied");
        expectEquals (processIdx, -1, "no processing may occur during restore");

        // First process contract after restore (clear restored host bypass AND
        // slot-mix bypass so the DSP path is exercised; the restored values
        // above already prove both were honored). Mix is reset to unity so the
        // audio assertion is the pure DSP passthrough. The bypass crossfade's
        // 5 ms fade occupies the first ~240 samples of the block — assert
        // AFTER the fade has fully settled.
        restoredSlot->setBypassed (false);
        restoredSlot->setSlotMixBypass (false);
        restoredSlot->setSlotMixNormalized (1.0f);
        juce::AudioBuffer<float> buf (2, kBlock);
        fillTonePair (buf, kBlock, 0.25f, 0.75f);
        restored.processBlock (buf, kBlock);
        expect (eventExists (log, "process ch=2 n=512"),
                "restored slot processed with the negotiated stereo layout and active frame count");
        expectEquals (restoredSlot->getLastProcessSamples(), kBlock, "first process frame count");
        expectWithinAbsoluteError (buf.getSample (0, 400), 0.25f, 1.0e-5f, "restored DSP passes audio");
    }

    // RED today: an exception from setStateInformation escapes restoreState
    // (unguarded plugin call) — in production this is an APEX crash during
    // project restore. Contract: contained, slot preserved but not prepared
    // for processing, other slots alive.
    void testStateFailureContainment()
    {
        beginTest ("detectable state-restore failure is contained");

        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<MatrixTestPluginFormat>());

        // Build a saved tree containing one good slot + one throwing slot.
        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        juce::String error;
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidStereo)), fm, error, true), 0,
                      "good slot appended: " + error);
        auto savedTree = chain.getState();

        juce::ValueTree throwingSlot ("Slot");
        throwingSlot.setProperty ("index", 1, nullptr);
        throwingSlot.setProperty ("bypassed", false, nullptr);
        throwingSlot.setProperty ("slotMixNormalized", 1.0f, nullptr);
        throwingSlot.setProperty ("slotMixBypass", false, nullptr);
        throwingSlot.setProperty ("pluginInstanceId", juce::Uuid().toString(), nullptr);
        {
            juce::PluginDescription d = makeDescription (*makeSpecForUid (kUidStateThrow));
            auto descTree = juce::ValueTree ("Description");
            descTree.setProperty ("name", d.name, nullptr);
            descTree.setProperty ("pluginFormatName", d.pluginFormatName, nullptr);
            descTree.setProperty ("fileOrIdentifier", d.fileOrIdentifier, nullptr);
            descTree.setProperty ("uniqueId", d.uniqueId, nullptr);
            descTree.setProperty ("deprecatedUid", d.deprecatedUid, nullptr);
            descTree.setProperty ("version", d.version, nullptr);
            descTree.setProperty ("manufacturerName", d.manufacturerName, nullptr);
            descTree.setProperty ("numInputChannels", d.numInputChannels, nullptr);
            descTree.setProperty ("numOutputChannels", d.numOutputChannels, nullptr);
            throwingSlot.addChild (descTree, -1, nullptr);

            juce::MemoryBlock stateBlock;
            stateBlock.append ("MSTX", 4);
            juce::ValueTree stateTree ("State");
            stateTree.setProperty ("data", juce::Base64::toBase64 (stateBlock.getData(), (int) stateBlock.getSize()), nullptr);
            throwingSlot.addChild (stateTree, -1, nullptr);
        }
        savedTree.addChild (throwingSlot, -1, nullptr);

        DAW::PluginChainCore restored;
        restored.prepare (kRate, kBlock);

        bool threw = false;
        try
        {
            restored.restoreState (savedTree, fm);
        }
        catch (...)
        {
            threw = true;
        }

        expect (! threw, "restoreState must contain a detectable plugin state failure");
        expectEquals (restored.getNumActiveSlots(), 2, "both slots preserved (identity/state kept for recovery)");
        auto* goodSlot = restored.getSlot (0);
        auto* badSlot  = restored.getSlot (1);
        expect (goodSlot != nullptr && goodSlot->isPrepared(), "unaffected slot stays prepared");
        expect (badSlot != nullptr, "failed slot record preserved");
        if (badSlot != nullptr)
        {
            expect (! badSlot->isPrepared(),
                    "failed slot must never be published into the active processing graph");
            expectEquals (badSlot->getProcessor()->getName(),
                          juce::String ("APEX Matrix StateThrow Probe"),
                          "failed slot keeps its identity for future recovery");
        }
    }
};

static PluginProjectRestoreLifecycleLayoutTests pluginProjectRestoreLifecycleLayoutTests;

// ══════════════════════════════════════════════════════════════════════════════
// plugin.identity.similar-name.coexistence.v1
// ══════════════════════════════════════════════════════════════════════════════

class PluginIdentitySimilarNameCoexistenceTests final : public juce::UnitTest
{
public:
    PluginIdentitySimilarNameCoexistenceTests()
        : UnitTest ("plugin.identity.similar-name.coexistence.v1", "PluginHost") {}

    void runTest() override
    {
        testSimilarNameCoexistenceBothOrders();
        testRemoveReinsert();
        testTwoInstancesOfSameComponent();
        testSameNameDifferentUidAutomationIdentity();
        testStateMarkerIsolationAcrossSaveRestore();
    }

    void testSimilarNameCoexistenceBothOrders()
    {
        beginTest ("similar-name components coexist in either insert order");

        {
            DAW::PluginChainCore chain;
            chain.prepare (kRate, kBlock);
            chain.setAutomationContext ("identcoexist", nullptr, nullptr);
            expectEquals (chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidFamilyPro))), 0, "Pro inserts first");
            expectEquals (chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidFamilyBase))), 1, "base inserts second");
            expectEquals (chain.getNumActiveSlots(), 2, "both active");
            auto* s0 = chain.getSlot (0);
            auto* s1 = chain.getSlot (1);
            expect (s0 != nullptr && s1 != nullptr, "distinct slots");
            if (s0 != nullptr && s1 != nullptr)
            {
                expect (s0->getProcessor() != s1->getProcessor(), "distinct instances");
                expectEquals (s0->getProcessor()->getName(), juce::String ("APEX Family Probe Pro"), "slot0 identity");
                expectEquals (s1->getProcessor()->getName(), juce::String ("APEX Family Probe"), "slot1 identity");
            }
        }

        {
            DAW::PluginChainCore chain;
            chain.prepare (kRate, kBlock);
            chain.setAutomationContext ("identcoexist", nullptr, nullptr);
            expectEquals (chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidFamilyBase))), 0, "base inserts first");
            expectEquals (chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidFamilyPro))), 1, "Pro inserts second");
            expectEquals (chain.getNumActiveSlots(), 2, "both active");
        }
    }

    void testRemoveReinsert()
    {
        beginTest ("remove + reinsert keeps identities independent");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        chain.setAutomationContext ("identreinsert", nullptr, nullptr);
        expectEquals (chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidFamilyPro))), 0, "Pro inserted");
        expectEquals (chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidFamilyBase))), 1, "base inserted");

        chain.removePlugin (1);
        expectEquals (chain.getNumActiveSlots(), 1, "base removed");
        auto* s0 = chain.getSlot (0);
        expect (s0 != nullptr, "Pro slot survives");
        if (s0 != nullptr)
            expectEquals (s0->getProcessor()->getName(), juce::String ("APEX Family Probe Pro"), "Pro untouched");

        const int idx = chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidFamilyBase)));
        expectEquals (idx, 1, "base reinserted");
        expectEquals (chain.getNumActiveSlots(), 2, "both active again");
        auto* s1 = chain.getSlot (1);
        expect (s0 != nullptr && s1 != nullptr && s0->getProcessor() != s1->getProcessor(),
                "fresh distinct instance after reinsert");
        expect (s1 != nullptr && s1->isPrepared(), "reinserted slot prepared");

        juce::AudioBuffer<float> buf (2, kBlock);
        fillTonePair (buf, kBlock, 0.25f, 0.75f);
        chain.processBlock (buf, kBlock);
        expectWithinAbsoluteError (buf.getSample (0, kBlock - 1), 0.25f, 1.0e-5f, "chain processes after remove/reinsert");
    }

    void testTwoInstancesOfSameComponent()
    {
        beginTest ("two instances of the same component stay independent");

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        chain.setAutomationContext ("identtwininst", nullptr, nullptr);
        expectEquals (chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidStereo))), 0, "instance A");
        expectEquals (chain.appendPluginInstanceForTesting (std::make_unique<MatrixProbeProcessor> (*makeSpecForUid (kUidStereo))), 1, "instance B");
        expectEquals (chain.getNumActiveSlots(), 2, "both live");
        auto* a = chain.getSlot (0);
        auto* b = chain.getSlot (1);
        if (a == nullptr || b == nullptr) return;
        expect (a->getPluginInstanceId() != b->getPluginInstanceId(), "live instance IDs must differ");
        expect (a->getProcessor() != b->getProcessor(), "distinct processors");
    }

    void testSameNameDifferentUidAutomationIdentity()
    {
        beginTest ("same display name + different UID must not share automation identity");

        using KR = apex::automation::AutomationParameterKeyRegistry;

        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<MatrixTestPluginFormat>());

        const int entriesBefore = KR::getInstance().getState().getNumChildren();

        // Production append path: appendPlugin registers automation identities
        // through configureSlotAutomation (appendPluginInstanceForTesting does not).
        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        chain.setAutomationContext ("identtwin", nullptr, nullptr);

        juce::String error;
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidTwinA)), fm, error, true), 0, "twin A inserted: " + error);
        const auto legacyKey = KR::pluginParamKey ("identtwin", 0, "APEX Twin Probe", "param0");
        const auto idA = KR::getInstance().findID (legacyKey);
        expect (idA != apex::automation::kInvalidParameterID, "twin A registered an automation identity");

        chain.removePlugin (0);
        auto& reg = apex::automation::AutomationSystem::getInstance().getRegistry();
        if (idA != apex::automation::kInvalidParameterID)
            if (auto* apA = reg.find (idA))
                expect (apA->getBoundPluginParameter() == nullptr,
                        "removal unbinds the raw plugin parameter pointer");

        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidTwinB)), fm, error, true), 0, "twin B inserted at the same slot: " + error);

        const int entriesAfter = KR::getInstance().getState().getNumChildren();
        expect (entriesAfter >= entriesBefore + 2,
                "a different-UID component must register a DISTINCT automation identity, "
                "not reuse the previous component's entry");
    }

    void testStateMarkerIsolationAcrossSaveRestore()
    {
        beginTest ("state markers stay with their own component across save/restore");

        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<MatrixTestPluginFormat>());

        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        juce::String error;
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidFamilyPro)), fm, error, true), 0, "Pro appended: " + error);
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidFamilyBase)), fm, error, true), 1, "base appended: " + error);
        auto savedTree = chain.getState();

        DAW::PluginChainCore restored;
        restored.prepare (kRate, kBlock);
        restored.restoreState (savedTree, fm);
        expectEquals (restored.getNumActiveSlots(), 2, "both restored");
        auto* r0 = restored.getSlot (0);
        auto* r1 = restored.getSlot (1);
        if (r0 == nullptr || r1 == nullptr) return;
        auto* p0 = dynamic_cast<MatrixProbeProcessor*> (r0->getProcessor());
        auto* p1 = dynamic_cast<MatrixProbeProcessor*> (r1->getProcessor());
        expect (p0 != nullptr && p1 != nullptr, "restored processors are probes");
        if (p0 != nullptr && p1 != nullptr)
        {
            expectEquals ((juce::uint32) p0->lastStateMarker, (juce::uint32) 0x1DEA1DEA, "Pro marker isolated");
            expectEquals ((juce::uint32) p1->lastStateMarker, (juce::uint32) 0xBA5EBA5E, "base marker isolated");
        }
    }
};

static PluginIdentitySimilarNameCoexistenceTests pluginIdentitySimilarNameCoexistenceTests;

class PluginReorderGainRegressionTests final : public juce::UnitTest
{
public:
    PluginReorderGainRegressionTests()
        : UnitTest("plugin.reorder.gain-preservation.v1", "PluginHost") {}

    void runTest() override
    {
        testManualValuesAreAuthoritative();
        testPassiveMirrorDoesNotCreateRecordingGesture();
        testTouchGestureOverridesLiveDsp();
        testDiscreteParameterSwitchesAtBlockBoundary();
        testDiscreteApexPpqLanesHoldUntilExactKnot();
        testReorderKeepsInstanceValues();
        testReorderKeepsAutomationOwner();
        testLegacyLanesFollowThePlugin();
        testRepeatedMovesDuringPlayback();
    }

private:
    void testManualValuesAreAuthoritative()
    {
        beginTest("five manual unity inserts never acquire a permanent gain dip");
        DAW::PluginChainCore chain;
        chain.prepare(kRate, 64);
        auto spec = *makeSpecForUid(kUidStereo);
        spec.parameterControlsGain = true;
        for (int i = 0; i < 5; ++i)
            chain.appendPluginInstanceForTesting(std::make_unique<MatrixProbeProcessor>(spec));
        chain.setAutomationContext("reorder-manual-unity", nullptr, nullptr);

        // Simulate GUI changes after the host seeded its smoothing history.
        for (int i = 0; i < 5; ++i)
            chain.getSlot(i)->getProcessor()->getParameters()[0]->setValueNotifyingHost(1.0f);
        chain.moveSlot(0, 4);
        // The same manual edit must also work after a reorder.
        for (int i = 0; i < 5; ++i)
            chain.getSlot(i)->getProcessor()->getParameters()[0]->setValueNotifyingHost(0.5f);
        chain.setAutomationContext("reorder-manual-unity", nullptr, nullptr);
        for (int i = 0; i < 5; ++i)
            chain.getSlot(i)->getProcessor()->getParameters()[0]->setValueNotifyingHost(1.0f);

        juce::AudioBuffer<float> buffer(2, 64);
        for (int block = 0; block < 64; ++block)
        {
            chain.applyAutomationAtSample("reorder-manual-unity", nullptr, block * 64, kRate, 120.0, 64);
            fillTonePair(buffer, 64, 0.25f, 0.5f);
            chain.processBlock(buffer, 64);
        }
        expectWithinAbsoluteError(buffer.getSample(0, 63), 0.25f, 1.0e-5f,
                                  "host must not smooth a manual value towards stale history");
        for (int i = 0; i < 5; ++i)
            expectWithinAbsoluteError(chain.getSlot(i)->getProcessor()->getParameters()[0]->getValue(),
                                      1.0f, 1.0e-6f, "manual unity remains authoritative");
    }

    void testPassiveMirrorDoesNotCreateRecordingGesture()
    {
        beginTest("passive host-side VST3 mirror does not fabricate Write movement");
        using KR = apex::automation::AutomationParameterKeyRegistry;
        using Kind = apex::automation::AutomationGestureQueue::EventKind;
        using Source = apex::automation::ChangeSource;

        DAW::PluginChainCore chain;
        chain.prepare(kRate, 64);
        auto spec = *makeSpecForUid(kUidStereo);
        spec.parameterControlsGain = true;
        expectEquals(chain.appendPluginInstanceForTesting(
            std::make_unique<MatrixProbeProcessor>(spec)), 0);
        const juce::String trackId = "apex-passive-mirror-no-fake-write-20261012";
        chain.setAutomationContext(trackId, nullptr, nullptr);
        auto* slot = chain.getSlot(0);
        if (slot == nullptr) { expect(false); return; }
        auto* processor = slot->getProcessor();
        if (processor == nullptr) { expect(false); return; }
        auto* p = processor->getParameters()[0];

        const auto key = KR::pluginParamKey(
            trackId, 0, slot->getName(), "param0", kUidStereo);
        const auto id = KR::getInstance().findID(key);
        auto& sys = apex::automation::AutomationSystem::getInstance();
        auto* mirrored = sys.getRegistry().find(id);
        expect(mirrored != nullptr && mirrored->getBoundPluginParameter() == p);
        if (mirrored == nullptr) return;

        // A private processor change without a host notification can be
        // discovered during audio processing. That is a state-sync operation,
        // not a manual touch or a new point on a recording lane.
        auto& queue = sys.getQueue();
        apex::automation::AutomationGestureQueue::Event event;
        while (queue.pop(event)) {} // isolate this test from earlier fixtures
        p->setValue(0.75f);
        expectWithinAbsoluteError(mirrored->getNormalizedValue(), 0.5f, 1.0e-4f,
            "mirror starts stale before host-side synchronization");
        chain.applyAutomationAtSample(trackId, nullptr, 0, kRate, 120.0, 64);
        expectWithinAbsoluteError(mirrored->getNormalizedValue(), 0.75f, 1.0e-4f,
            "processor authority updates the mirror without an automation lane");
        bool phantomWrite = false;
        while (queue.pop(event))
            if (event.paramID == id && event.kind == Kind::ValueChange
                && event.source == Source::Plugin)
                phantomWrite = true;
        expect(!phantomWrite,
            "host-side synchronization must not enter the producer recording queue");
    }

    void testTouchGestureOverridesLiveDsp()
    {
        beginTest("native plugin editor Touch gesture wins over PPQ lane in DSP");
        using KR = apex::automation::AutomationParameterKeyRegistry;
        using Mode = apex::automation::AutomationMode;
        using Source = apex::automation::ChangeSource;

        const juce::String trackId = "apex-touch-gain-dsp-gesture-20261012";
        DAW::PluginChainCore chain;
        chain.prepare(kRate, 64);
        auto spec = *makeSpecForUid(kUidStereo);
        spec.parameterControlsGain = true;
        const int slotIndex = chain.appendPluginInstanceForTesting(
            std::make_unique<MatrixProbeProcessor>(spec));
        expectEquals(slotIndex, 0);
        if (slotIndex != 0) return;
        chain.setAutomationContext(trackId, nullptr, nullptr);
        auto* slot = chain.getSlot(0);
        expect(slot != nullptr);
        if (slot == nullptr) return;
        auto* plugin = slot->getProcessor();
        expect(plugin != nullptr);
        if (plugin == nullptr) return;
        auto* p = plugin->getParameters()[0];
        const auto key = KR::pluginParamKey(
            trackId, 0, slot->getName(), "param0", kUidStereo);
        const auto id = KR::getInstance().findID(key);
        expect(id != apex::automation::kInvalidParameterID);
        if (id == apex::automation::kInvalidParameterID) return;
        auto& sys = apex::automation::AutomationSystem::getInstance();
        auto* mirror = sys.getRegistry().find(id);
        expect(mirror != nullptr && mirror->getBoundPluginParameter() == p);
        if (mirror == nullptr) return;

        auto& lanes = sys.getLaneStore();
        auto& modes = sys.getModeState();
        modes.setMode(id, Mode::Touch);
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.2f, apex::automation::CurveType::Hold, 0.0f},
            {4.0, 0.2f, apex::automation::CurveType::Hold, 0.0f}
        });

        // Establish live Read playback and the smoother's settled value.
        juce::AudioBuffer<float> audio(2, 64);
        for (int block = 0; block < 128; ++block)
        {
            chain.applyAutomationAtSample(trackId, nullptr, block * 64, kRate, 120.0, 64);
            fillTonePair(audio, 64, 0.5f, 0.5f);
            chain.processBlock(audio, 64);
        }
        expectWithinAbsoluteError(p->getValue(), 0.2f, 1.0e-3f,
            "PPQ lane drives real parameter before gesture");
        expectWithinAbsoluteError(audio.getSample(0, 63), 0.1f, 1.0e-3f,
            "real plugin audio reflects 0.2 gain");

        p->beginChangeGesture();
        p->setValueNotifyingHost(0.9f);
        expect(mirror->isGestureActive(),
            "plugin gesture must reach APEX before next audio callback");
        expect(mirror->getLastSource() == Source::Plugin,
            "plugin editor value is mirrored as Plugin, not Automation");

        for (int block = 128; block < 136; ++block)
        {
            chain.applyAutomationAtSample(trackId, nullptr, block * 64, kRate, 120.0, 64);
            fillTonePair(audio, 64, 0.5f, 0.5f);
            chain.processBlock(audio, 64);
        }
        expectWithinAbsoluteError(p->getValue(), 0.9f, 1.0e-3f,
            "automated lane must not overwrite active plugin gesture");
        expectWithinAbsoluteError(audio.getSample(0, 63), 0.45f, 1.0e-3f,
            "real audio follows plugin editor while Touch is active");

        p->endChangeGesture();
        expect(!mirror->isGestureActive(), "Touch released");
        for (int block = 136; block < 264; ++block)
        {
            chain.applyAutomationAtSample(trackId, nullptr, block * 64, kRate, 120.0, 64);
            fillTonePair(audio, 64, 0.5f, 0.5f);
            chain.processBlock(audio, 64);
        }
        expectWithinAbsoluteError(p->getValue(), 0.2f, 1.0e-3f,
            "Touch release returns DSP to committed PPQ lane");
        expectWithinAbsoluteError(audio.getSample(0, 63), 0.1f, 1.0e-3f,
            "real audio returns to playback value after release");

        lanes.removeLane(id);
        modes.setMode(id, Mode::Read);
    }

    void testDiscreteParameterSwitchesAtBlockBoundary()
    {
        beginTest("discrete plugin switch is not smoothed across automation blocks");
        DAW::AutomationManagerCore manager;
        DAW::PluginChainCore chain;
        chain.setAutomationManager(&manager);
        chain.prepare(kRate, 128);
        auto spec = *makeSpecForUid(kUidStereo);
        spec.parameterControlsGain = true;
        spec.parameterIsBoolean = true;
        expectEquals(chain.appendPluginInstanceForTesting(
            std::make_unique<MatrixProbeProcessor>(spec)), 0);
        const juce::String trackId = "apex-discrete-step-block-boundary-20261012";
        chain.setAutomationContext(trackId, nullptr, nullptr);
        auto* slot = chain.getSlot(0);
        if (slot == nullptr) { expect(false); return; }
        auto* p = slot->getProcessor()->getParameters()[0];
        expect(p->isDiscrete(), "host sees a discrete plugin parameter");
        const auto legacy = "plugin.0." + slot->getPluginInstanceId() + ".param0";
        manager.addPoint(trackId, legacy, 0, 0.0f);
        manager.addPoint(trackId, legacy, 2400, 1.0f);
        manager.addPoint(trackId, legacy, 4800, 0.0f);
        const auto snap = manager.getSnapshotPublisher().get();
        juce::AudioBuffer<float> audio(2, 128);

        chain.applyAutomationAtSample(trackId, snap.get(), 0, kRate, 120.0, 128);
        fillTonePair(audio, 128, 0.5f, 0.5f);
        chain.processBlock(audio, 128);
        expectWithinAbsoluteError(p->getValue(), 0.0f, 1.0e-6f,
            "switch starts OFF");
        expectWithinAbsoluteError(audio.getSample(0, 127), 0.0f, 1.0e-6f,
            "OFF processes silent audio");

        chain.applyAutomationAtSample(trackId, snap.get(), 2400, kRate, 120.0, 128);
        fillTonePair(audio, 128, 0.5f, 0.5f);
        chain.processBlock(audio, 128);
        expectWithinAbsoluteError(p->getValue(), 1.0f, 1.0e-6f,
            "switch turns ON immediately at the automation boundary");
        expectWithinAbsoluteError(audio.getSample(0, 127), 0.5f, 1.0e-6f,
            "ON is audible in the first callback, not delayed by a 10ms ramp");

        chain.applyAutomationAtSample(trackId, snap.get(), 4800, kRate, 120.0, 128);
        fillTonePair(audio, 128, 0.5f, 0.5f);
        chain.processBlock(audio, 128);
        expectWithinAbsoluteError(p->getValue(), 0.0f, 1.0e-6f,
            "switch turns OFF immediately at the next automation boundary");
        expectWithinAbsoluteError(audio.getSample(0, 127), 0.0f, 1.0e-6f,
            "OFF mutes in the first callback");
    }

    void testDiscreteApexPpqLanesHoldUntilExactKnot()
    {
        beginTest("discrete APEX PPQ lane never anticipates a switch breakpoint");
        using KR = apex::automation::AutomationParameterKeyRegistry;
        using apex::automation::CurveType;

        DAW::PluginChainCore chain;
        chain.prepare(kRate, 128);
        auto spec = *makeSpecForUid(kUidStereo);
        spec.parameterControlsGain = true;
        spec.parameterIsBoolean = true;
        expectEquals(chain.appendPluginInstanceForTesting(
            std::make_unique<MatrixProbeProcessor>(spec)), 0);
        const juce::String trackId = "apex-discrete-ppq-hold-probe-20261012";
        chain.setAutomationContext(trackId, nullptr, nullptr);
        auto* slot = chain.getSlot(0);
        if (slot == nullptr) { expect(false); return; }
        auto* parameter = slot->getProcessor()->getParameters()[0];
        expect(parameter->isDiscrete(), "plugin advertises discrete parameter");
        const auto key = KR::pluginParamKey(
            trackId, 0, slot->getName(), "param0", kUidStereo);
        const auto id = KR::getInstance().findID(key);
        expect(id != apex::automation::kInvalidParameterID);
        if (id == apex::automation::kInvalidParameterID) return;

        auto& lanes = apex::automation::AutomationSystem::getInstance().getLaneStore();
        // Deliberately use Linear curves: a discrete target must STILL hold
        // the prior value until the exact breakpoint.
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.0f, CurveType::Linear, 0.0f},
            {1.0, 1.0f, CurveType::Linear, 0.0f},
            {2.0, 0.0f, CurveType::Linear, 0.0f}
        });

        juce::AudioBuffer<float> audio(2, 128);
        struct Probe { double ppq; float value; };
        const Probe probes[] = {
            {0.0, 0.0f}, {0.75, 0.0f}, {1.0, 1.0f},
            {1.75, 1.0f}, {2.0, 0.0f}
        };
        for (const auto& probe : probes)
        {
            // 48kHz @ 120BPM = exactly 24k samples per PPQ.
            const auto sample = static_cast<int64_t>(probe.ppq * 24000.0);
            chain.applyAutomationAtSample(
                trackId, nullptr, sample, kRate, 120.0, 128);
            fillTonePair(audio, 128, 0.5f, 0.5f);
            chain.processBlock(audio, 128);
            expectWithinAbsoluteError(parameter->getValue(), probe.value,
                1.0e-6f, "VST3 discrete value holds at PPQ " + juce::String(probe.ppq));
            expectWithinAbsoluteError(audio.getSample(0, 127),
                0.5f * probe.value, 1.0e-6f,
                "real DSP switches only at PPQ " + juce::String(probe.ppq));
        }
        lanes.removeLane(id);
    }

    void testReorderKeepsInstanceValues()
    {
        beginTest("moving identical plugins cannot write into a neighbouring instance");
        DAW::PluginChainCore chain;
        chain.prepare(kRate, 64);
        const float values[] = { 0.95f, 0.8f, 0.6f, 0.3f, 0.1f };
        std::vector<juce::AudioProcessorParameter*> parameters;
        for (int i = 0; i < 5; ++i)
        {
            chain.appendPluginInstanceForTesting(
                std::make_unique<MatrixProbeProcessor>(*makeSpecForUid(kUidStereo)));
            auto* parameter = chain.getSlot(i)->getProcessor()->getParameters()[0];
            parameter->setValueNotifyingHost(values[i]);
            parameters.push_back(parameter);
        }
        chain.setAutomationContext("reorder-instance-values", nullptr, nullptr);
        chain.moveSlot(0, 4);
        for (int block = 0; block < 64; ++block)
            chain.applyAutomationAtSample("reorder-instance-values", nullptr, block * 64, kRate, 120.0, 64);
        for (int i = 0; i < 5; ++i)
            expectWithinAbsoluteError(parameters[(size_t)i]->getValue(), values[i], 1.0e-6f,
                                      "parameter belongs to its original processor after reorder");
    }

    void testReorderKeepsAutomationOwner()
    {
        beginTest("automation identity and its bound processor follow a moved plugin");
        using KR = apex::automation::AutomationParameterKeyRegistry;
        DAW::PluginChainCore chain;
        chain.prepare(kRate, 64);
        for (int i = 0; i < 2; ++i)
            chain.appendPluginInstanceForTesting(
                std::make_unique<MatrixProbeProcessor>(*makeSpecForUid(kUidStereo)));
        chain.setAutomationContext("reorder-automated-owner", nullptr, nullptr);
        auto* originalParameter = chain.getSlot(0)->getProcessor()->getParameters()[0];
        const auto pluginName = chain.getSlot(0)->getName();
        const auto oldKey = KR::pluginParamKey("reorder-automated-owner", 0, pluginName, "param0", kUidStereo);
        const auto parameterId = KR::getInstance().findID(oldKey);
        expect(parameterId != apex::automation::kInvalidParameterID);
        auto& lanes = apex::automation::AutomationLaneStore::getInstance();
        lanes.getOrCreateLane(parameterId).addPoint({ 0.0, 0.2f });
        DAW::PluginChainMoveCommand move(chain, 0, 1);
        move.execute();
        const auto newKey = KR::pluginParamKey("reorder-automated-owner", 1, pluginName, "param0", kUidStereo);
        expectEquals((int)KR::getInstance().findID(newKey), (int)parameterId,
                     "reorder keeps the automation ID attached to the instance");
        auto* parameter = apex::automation::AutomationSystem::getInstance().getRegistry().find(parameterId);
        expect(parameter != nullptr && parameter->getBoundPluginParameter() == originalParameter,
               "the original automation ID still writes to the original plugin");
        for (int block = 0; block < 128; ++block)
            chain.applyAutomationAtSample("reorder-automated-owner", nullptr, block * 64, kRate, 120.0, 64);
        expectWithinAbsoluteError(originalParameter->getValue(), 0.2f, 1.0e-4f,
                                  "lane still controls the original processor");
        expectWithinAbsoluteError(chain.getSlot(0)->getProcessor()->getParameters()[0]->getValue(),
                                  0.5f, 1.0e-6f, "unautomated neighbour is untouched");
        auto* originalInstance = chain.getSlot(1);
        move.undo();
        expect(chain.getSlot(0) == originalInstance, "undo keeps the live processor instance");
        expectEquals((int)KR::getInstance().findID(oldKey), (int)parameterId, "undo reverses key permutation");
        move.execute();
        expect(chain.getSlot(1) == originalInstance, "redo keeps the live processor instance");
        lanes.removeLane(parameterId);
    }

    void testLegacyLanesFollowThePlugin()
    {
        beginTest("legacy parameter and wet/dry lanes follow the plugin through save/restore");
        DAW::AutomationManagerCore manager;
        DAW::PluginChainCore chain;
        chain.setAutomationManager(&manager);
        chain.prepare(kRate, 64);
        for (int i = 0; i < 2; ++i)
            chain.appendPluginInstanceForTesting(
                std::make_unique<MatrixProbeProcessor>(*makeSpecForUid(kUidStereo)));
        const juce::String trackId = "reorder-legacy-lanes";
        chain.setAutomationContext(trackId, nullptr, nullptr);
        auto* instance = chain.getSlot(0);
        const auto parameterId = "plugin.0." + instance->getPluginInstanceId() + ".param0";
        manager.addPoint(trackId, parameterId, 0, 0.2f);
        manager.addPoint(trackId, DAW::AutomationManagerCore::makePluginSlotMixId(0), 0, 0.8f);
        const auto beforeReorderSnapshot = manager.getSnapshotPublisher().get();
        expect(chain.moveSlot(0, 1));
        const auto remappedId = "plugin.1." + instance->getPluginInstanceId() + ".param0";
        auto snapshot = manager.getSnapshotPublisher().get();
        expect(snapshot->findLaneRT(trackId, remappedId) != nullptr, "parameter lane follows instance");
        expect(snapshot->findLaneRT(trackId, DAW::AutomationManagerCore::makePluginSlotMixId(1)) != nullptr,
               "wet/dry lane follows instance");
        for (int block = 0; block < 128; ++block)
            chain.applyAutomationAtSample(trackId, beforeReorderSnapshot.get(), block * 64, kRate, 120.0, 64);
        expectWithinAbsoluteError(instance->getProcessor()->getParameters()[0]->getValue(),
                                  0.2f, 1.0e-4f, "legacy automation drives moved instance");
        expectWithinAbsoluteError(chain.getSlotMix(1), 0.8f, 1.0e-4f, "wet/dry automation drives moved instance");
        expectWithinAbsoluteError(chain.getSlotMix(0), 1.0f, 1.0e-6f, "neighbour mix remains unity");
        DAW::AutomationManagerCore restored;
        restored.restoreState(manager.getState());
        expect(restored.getSnapshotPublisher().get()->findLaneRT(trackId, remappedId) != nullptr,
               "new lane address survives persistence");
    }

    void testRepeatedMovesDuringPlayback()
    {
        beginTest("200 moves during continuous audio preserve six insert gains and live instances");
        DAW::PluginChainCore chain;
        chain.prepare(kRate, 64);
        auto spec = *makeSpecForUid(kUidStereo);
        spec.parameterControlsGain = true;
        std::vector<MatrixProbeProcessor*> processors;
        for (int i = 0; i < 6; ++i)
        {
            auto processor = std::make_unique<MatrixProbeProcessor>(spec);
            processor->quietLogging.store(true);
            processor->getParameters()[0]->setValueNotifyingHost(1.0f);
            processors.push_back(processor.get());
            chain.appendPluginInstanceForTesting(std::move(processor));
        }
        chain.setAutomationContext("reorder-live-stress", nullptr, nullptr);
        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 }, badBlocks { 0 };
        std::thread audio([&]
        {
            juce::AudioBuffer<float> buffer(2, 64);
            while (!stop.load())
            {
                chain.applyAutomationAtSample("reorder-live-stress", nullptr, 0, kRate, 120.0, 64);
                fillTonePair(buffer, 64, 0.25f, 0.5f);
                chain.processBlock(buffer, 64);
                if (std::abs(buffer.getSample(0, 63) - 0.25f) > 1.0e-5f)
                    badBlocks.fetch_add(1);
                blocks.fetch_add(1);
            }
        });
        while (blocks.load() < 32) juce::Thread::yield();
        int completedMoves = 0;
        for (int moveIndex = 0; moveIndex < 200; ++moveIndex)
            if (chain.moveSlot(moveIndex % 2 == 0 ? 0 : 5, moveIndex % 2 == 0 ? 5 : 0))
                ++completedMoves;
        stop.store(true);
        audio.join();
        expectEquals(completedMoves, 200, "every reorder completed");
        expectEquals(badBlocks.load(), 0, "no permanent or transient gain dip with unity inserts");
        for (auto* processor : processors)
        {
            expectEquals(processor->prepareCount, 1, "order edits never re-prepare plugins");
            expectWithinAbsoluteError(processor->getParameters()[0]->getValue(), 1.0f, 1.0e-6f,
                                      "each live processor retains unity");
        }
    }
};

static PluginReorderGainRegressionTests pluginReorderGainRegressionTests;

class PluginAutomationLargeBlockTests final : public juce::UnitTest
{
public:
    PluginAutomationLargeBlockTests()
        : UnitTest("plugin.automation.large-block-smoothing.v1", "PluginHost") {}

    void runTest() override
    {
        beginTest("2048-sample plugin automation updates in short slices and preserves MIDI offsets");
        testChangingAutomationUsesShortSlices();
        beginTest("static plugin automation retains the full host block");
        testStaticAutomationKeepsFullBlock();
    }

private:
    static constexpr int largeBlock = 2048;
    static constexpr const char* trackId = "automation-large-block";

    void testChangingAutomationUsesShortSlices()
    {
        DAW::AutomationManagerCore manager;
        DAW::PluginChainCore chain;
        chain.setAutomationManager(&manager);
        chain.prepare(kRate, largeBlock);

        auto spec = *makeSpecForUid(kUidStereo);
        spec.parameterControlsGain = true;
        auto probe = std::make_unique<MatrixProbeProcessor>(spec);
        auto* probePtr = probe.get();
        chain.appendPluginInstanceForTesting(std::move(probe));
        chain.setAutomationContext(trackId, nullptr, nullptr);

        const auto parameterId = "plugin.0." + chain.getSlot(0)->getPluginInstanceId() + ".param0";
        manager.addPoint(trackId, parameterId, 0, 0.1f);
        manager.addPoint(trackId, parameterId, largeBlock, 0.9f);
        const auto automation = manager.getSnapshotPublisher().get();

        juce::AudioBuffer<float> audio(2, largeBlock);
        audio.clear();
        probePtr->quietLogging.store(true, std::memory_order_relaxed);

        chain.processBlockWithAutomation(audio, nullptr, nullptr, trackId,
            automation.get(), 0, kRate, 120.0, largeBlock);

        probePtr->processObservationCount = 0;
        audio.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 63);
        midi.addEvent(juce::MidiMessage::noteOn(1, 61, (juce::uint8) 100), 64);
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100), largeBlock - 1);
        chain.processBlockWithAutomation(audio, &midi, nullptr, trackId,
            automation.get(), largeBlock, kRate, 120.0, largeBlock);

        expectEquals(probePtr->processObservationCount, largeBlock / DAW::PluginChainCore::kAutomationSliceSamples,
                     "only an automated large callback is subdivided");
        if (probePtr->processObservationCount >= 32)
        {
            const auto first = probePtr->observedParameterValues[0];
            const auto last = probePtr->observedParameterValues[31];
            expect(last > first, "the plugin sees automation progress within one device callback");
            float largestStep = 0.0f;
            for (int i = 1; i < 32; ++i)
                largestStep = juce::jmax(largestStep,
                    std::abs(probePtr->observedParameterValues[(size_t)i]
                             - probePtr->observedParameterValues[(size_t)i - 1]));
            expect(largestStep < 0.15f, "parameter movement has no host-block-sized jump");

            expectEquals(probePtr->observedMidiEventCounts[0], 1,
                         "event at the end of slice zero appears once");
            expectEquals(probePtr->observedFirstMidiSamplePositions[0], 63,
                         "event at sample 63 keeps its slice-relative position");
            expectEquals(probePtr->observedMidiEventCounts[1], 1,
                         "event at sample 64 moves into slice one exactly once");
            expectEquals(probePtr->observedFirstMidiSamplePositions[1], 0,
                         "event at a slice boundary starts at offset zero");
            expectEquals(probePtr->observedMidiEventCounts[31], 1,
                         "event at the end of the 2048-sample callback appears once");
            expectEquals(probePtr->observedFirstMidiSamplePositions[31], 63,
                         "last MIDI event is aligned to the final slice");
        }
    }

    void testStaticAutomationKeepsFullBlock()
    {
        DAW::AutomationManagerCore manager;
        DAW::PluginChainCore chain;
        chain.setAutomationManager(&manager);
        chain.prepare(kRate, largeBlock);
        auto spec = *makeSpecForUid(kUidStereo);
        auto probe = std::make_unique<MatrixProbeProcessor>(spec);
        auto* probePtr = probe.get();
        chain.appendPluginInstanceForTesting(std::move(probe));
        chain.setAutomationContext(trackId, nullptr, nullptr);
        const auto parameterId = "plugin.0." + chain.getSlot(0)->getPluginInstanceId() + ".param0";
        manager.addPoint(trackId, parameterId, 0, 0.5f);
        const auto automation = manager.getSnapshotPublisher().get();
        juce::AudioBuffer<float> audio(2, largeBlock);
        audio.clear();
        probePtr->quietLogging.store(true, std::memory_order_relaxed);

        chain.processBlockWithAutomation(audio, nullptr, nullptr, trackId,
            automation.get(), 0, kRate, 120.0, largeBlock);

        expectEquals(probePtr->processObservationCount, 1,
                     "a constant automated value does not multiply plugin callbacks");
        expectEquals(probePtr->lastSamples.load(std::memory_order_relaxed), largeBlock,
                     "the plugin receives the negotiated 2048-sample block when no value is moving");
    }
};

static PluginAutomationLargeBlockTests pluginAutomationLargeBlockTests;

// ══════════════════════════════════════════════════════════════════════════════
// plugin.module.coexistence.lifecycle.v1
//
// Generic module/factory retention contract guard. The mock below mirrors the
// proven JUCE 8.0.12 VST3 model (juce_VST3PluginFormatImpl.h):
//   - RefCountedDllHandle registry deduped by EXACT canonical full module path
//   - refcounted handle; unload only at final release
//   - every instance retains its module for its whole lifetime
//   - reinsertion re-fetches the same live handle, or a FRESH handle after a
//     complete release — never a stale factory
// The tests drive the REAL production chain (appendPlugin/removePlugin/process)
// through a real AudioPluginFormatManager, so any future APEX change that drops
// module/factory ownership while instances live turns this suite RED.
// ══════════════════════════════════════════════════════════════════════════════

struct MockModule
{
    explicit MockModule (juce::String fullPath) : path (std::move (fullPath)) {}
    juce::String path;
    std::atomic<bool> alive { true };
    std::atomic<int>  liveInstances { 0 };
};

struct MockModuleRegistry
{
    static MockModuleRegistry& get()
    {
        static MockModuleRegistry r;
        return r;
    }

    std::shared_ptr<MockModule> getOrLoad (const juce::String& fullPath)
    {
        auto it = modules.find (fullPath);
        if (it != modules.end())
            if (auto m = it->second.lock())
                if (m->alive.load())
                    return m;
        auto m = std::make_shared<MockModule> (fullPath);
        modules[fullPath] = m;
        ++loadCounts[fullPath];
        return m;
    }

    std::shared_ptr<MockModule> findLive (const juce::String& fullPath) const
    {
        auto it = modules.find (fullPath);
        return it != modules.end() ? it->second.lock() : nullptr;
    }

    int loadCount (const juce::String& fullPath) const
    {
        auto it = loadCounts.find (fullPath);
        return it != loadCounts.end() ? it->second : 0;
    }

    std::map<juce::String, std::weak_ptr<MockModule>> modules;
    std::map<juce::String, int> loadCounts;

    void resetForTesting()
    {
        modules.clear();
        loadCounts.clear();
    }
};

MatrixProbeProcessor::~MatrixProbeProcessor()
{
    if (retainedModule_ != nullptr)
        retainedModule_->liveInstances.fetch_sub (1);
}

void MatrixProbeProcessor::retainModule (std::shared_ptr<MockModule> module)
{
    retainedModule_ = std::move (module);
    if (retainedModule_ != nullptr)
        retainedModule_->liveInstances.fetch_add (1);
}

/** Deterministic format modelling per-path refcounted module loading. */class ModuleTestPluginFormat final : public juce::AudioPluginFormat
{
public:
    juce::String getName() const override { return "APEX Module Test"; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                              const juce::String&) override
    {
        for (int uid : { kUidModulePro, kUidModuleBase, kUidModuleSibling })
            if (auto spec = makeSpecForUid (uid))
                results.add (new juce::PluginDescription (makeDescription (*spec)));
    }

    bool fileMightContainThisPluginType (const juce::String& identifier) override
    {
        return identifier.startsWith ("APEX::ModuleTest::");
    }

    juce::String getNameOfPluginFromIdentifier (const juce::String& identifier) override
    {
        const int uid = identifier.substring (juce::String ("APEX::ModuleTest::").length()).getIntValue();
        if (auto spec = makeSpecForUid (uid))
            return spec->name;
        return {};
    }

    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& desc) override
    {
        return desc.pluginFormatName == "APEX Module Test" && makeSpecForUid (desc.uniqueId).has_value();
    }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

protected:
    void createPluginInstance (const juce::PluginDescription& description,
                               double initialSampleRate,
                               int initialBufferSize,
                               PluginCreationCallback callback) override
    {
        if (callback == nullptr)
            return;

        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;

        if (auto spec = makeSpecForUid (description.uniqueId))
        {
            try
            {
                instance = std::make_unique<MatrixProbeProcessor> (*spec);
                if (spec->modulePath.isNotEmpty())
                {
                    auto module = MockModuleRegistry::get().getOrLoad (spec->modulePath);
                    dynamic_cast<MatrixProbeProcessor*> (instance.get())->retainModule (std::move (module));
                }
                instance->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
                MatrixTestPluginFormat::lastCreatedLog = dynamic_cast<MatrixProbeProcessor*> (instance.get())->log_;
            }
            catch (...)
            {
                instance.reset();
                error = "APEX Module Test plugin creation failed.";
            }
        }
        else
        {
            error = "Unknown APEX Module Test uid " + juce::String (description.uniqueId);
        }

        callback (std::move (instance), error);
    }
};

class PluginModuleCoexistenceLifecycleTests final : public juce::UnitTest
{
public:
    PluginModuleCoexistenceLifecycleTests()
        : UnitTest ("plugin.module.coexistence.lifecycle.v1", "PluginHost") {}

    void runTest() override
    {
        testBothLoadOrdersKeepModulesIndependent();
        testRemoveKeepsSiblingModuleRetained();
        testReinsertGetsValidModule();
        testSameModuleTwoInstancesRetainedUntilFinalRelease();
    }

    void testBothLoadOrdersKeepModulesIndependent()
    {
        beginTest ("two shells coexist: distinct modules, order-independent");

        for (bool proFirst : { true, false })
        {
            MockModuleRegistry::get().resetForTesting();
            juce::AudioPluginFormatManager fm;
            fm.addFormat (std::make_unique<ModuleTestPluginFormat>());
            DAW::PluginChainCore chain;
            chain.prepare (kRate, kBlock);
            juce::String error;

            const int first  = chain.appendPlugin (makeDescription (*makeSpecForUid (proFirst ? kUidModulePro : kUidModuleBase), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true);
            const int second = chain.appendPlugin (makeDescription (*makeSpecForUid (proFirst ? kUidModuleBase : kUidModulePro), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true);
            expectEquals (first, 0, "first insert: " + error);
            expectEquals (second, 1, "second insert: " + error);
            expectEquals (chain.getNumActiveSlots(), 2, "both alive");

            auto shellA = MockModuleRegistry::get().findLive (kShellAPath);
            auto shellB = MockModuleRegistry::get().findLive (kShellBPath);
            expect (shellA != nullptr, "ShellA module live");
            expect (shellB != nullptr, "ShellB module live");
            if (shellA != nullptr && shellB != nullptr)
            {
                expect (shellA.get() != shellB.get(), "distinct module objects for distinct full paths");
                expect (shellA->alive.load() && shellB->alive.load(), "both modules alive");
                expectEquals ((int) shellA->liveInstances.load(), 1, "ShellA serves exactly one live instance");
                expectEquals ((int) shellB->liveInstances.load(), 1, "ShellB serves exactly one live instance");
            }

            expectEquals ((int) MockModuleRegistry::get().loadCount (kShellAPath), 1, "ShellA loaded once");
            expectEquals ((int) MockModuleRegistry::get().loadCount (kShellBPath), 1, "ShellB loaded once");

            juce::AudioBuffer<float> buf (2, kBlock);
            fillTonePair (buf, kBlock, 0.25f, 0.75f);
            chain.processBlock (buf, kBlock);
            expectWithinAbsoluteError (buf.getSample (0, kBlock - 1), 0.25f, 1.0e-5f, "chain processes with both shells loaded");
        }
    }

    void testRemoveKeepsSiblingModuleRetained()
    {
        beginTest ("removing one component never invalidates the sibling's module");

        MockModuleRegistry::get().resetForTesting();
        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<ModuleTestPluginFormat>());
        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        juce::String error;
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidModulePro), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true), 0, "Pro: " + error);
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidModuleBase), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true), 1, "base: " + error);

        chain.removePlugin (1);

        auto shellA = MockModuleRegistry::get().findLive (kShellAPath);
        expect (shellA != nullptr, "sibling module (ShellA) still retained after removing ShellB's instance");
        if (shellA != nullptr)
            expectEquals ((int) shellA->liveInstances.load(), 1, "ShellA retention unchanged");
    }

    void testReinsertGetsValidModule()
    {
        beginTest ("reinsertion re-fetches a LIVE module handle (never a stale factory)");

        MockModuleRegistry::get().resetForTesting();
        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<ModuleTestPluginFormat>());
        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        juce::String error;
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidModulePro), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true), 0, "Pro: " + error);
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidModuleBase), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true), 1, "base: " + error);

        chain.removePlugin (1);
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidModuleBase), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true), 1, "base reinserted: " + error);

        auto shellB = MockModuleRegistry::get().findLive (kShellBPath);
        expect (shellB != nullptr, "reinserted instance holds a live ShellB module");
        if (shellB != nullptr)
        {
            expect (shellB->alive.load(), "ShellB module alive after reinsert");
            expect (shellB->liveInstances.load() >= 1, "ShellB live-instance count reflects the new instance");
        }
        // A fresh load after a COMPLETE release is the correct JUCE contract
        // (the released handle is erased from the registry); the invariant is
        // that a reinserted instance never touches a STALE module object.
    }

    void testSameModuleTwoInstancesRetainedUntilFinalRelease()
    {
        beginTest ("two components from the SAME module: one load, retained until final release");

        MockModuleRegistry::get().resetForTesting();
        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<ModuleTestPluginFormat>());
        DAW::PluginChainCore chain;
        chain.prepare (kRate, kBlock);
        juce::String error;
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidModuleBase), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true), 0, "component 1: " + error);
        expectEquals (chain.appendPlugin (makeDescription (*makeSpecForUid (kUidModuleSibling), "APEX Module Test", "APEX::ModuleTest::"), fm, error, true), 1, "component 2: " + error);

        expectEquals (MockModuleRegistry::get().loadCount (kShellBPath), 1,
                      "both components share ONE module load (deduped by full path)");
        auto shellB = MockModuleRegistry::get().findLive (kShellBPath);
        expect (shellB != nullptr, "shared module live");
        if (shellB != nullptr)
            expectEquals ((int) shellB->liveInstances.load(), 2, "both instances retain the shared module");

        chain.removePlugin (0);
        shellB = MockModuleRegistry::get().findLive (kShellBPath);
        expect (shellB != nullptr, "module retained while the second instance lives");
        if (shellB != nullptr)
            expect (shellB->liveInstances.load() >= 1, "remaining instance keeps the module alive");
    }
};

static PluginModuleCoexistenceLifecycleTests pluginModuleCoexistenceLifecycleTests;

class ClipRegionPluginReorderRegressionTests final : public juce::UnitTest
{
public:
    ClipRegionPluginReorderRegressionTests()
        : UnitTest ("clip-region-fx.reorder-preserves-instances.v1", "PluginHost") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        juce::AudioPluginFormatManager formatManager;
        formatManager.addFormat (std::make_unique<MatrixTestPluginFormat>());

        DAW::ClipRegionPluginCore clipFx;
        clipFx.prepare (kRate, kBlock);
        const DAW::ClipID clipId = "apex_clip_fx_reorder";

        beginTest ("three instances load and publish in insertion order");
        auto first = clipFx.loadForClip (
            clipId, makeDescription (*makeSpecForUid (kUidStereo)), formatManager);
        auto middle = clipFx.loadForClip (
            clipId, makeDescription (*makeSpecForUid (kUidTwinA)), formatManager);
        auto last = clipFx.loadForClip (
            clipId, makeDescription (*makeSpecForUid (kUidTwinB)), formatManager);
        expect (first.success, first.message);
        expect (middle.success, middle.message);
        expect (last.success, last.message);
        if (!first.success || !middle.success || !last.success)
            return;

        const auto initial = clipFx.getEntriesForClip (clipId);
        expectEquals (static_cast<int> (initial.size()), 3);
        if (initial.size() != 3)
            return;

        const auto firstId = initial[0].instanceId;
        const auto middleId = initial[1].instanceId;
        const auto lastId = initial[2].instanceId;
        auto* firstPtr = first.instance;
        auto* middlePtr = middle.instance;
        auto* lastPtr = last.instance;
        clipFx.setBypassed (clipId, middleId, true);

        beginTest ("moving first to last preserves plugin identities and bypass");
        expect (clipFx.moveEntry (clipId, 0, 2));
        auto moved = clipFx.getEntriesForClip (clipId);
        expectEquals (moved[0].instanceId, middleId);
        expectEquals (moved[1].instanceId, lastId);
        expectEquals (moved[2].instanceId, firstId);
        expect (moved[0].bypassed);
        expect (clipFx.findEntryById (clipId, firstId)->instance.get() == firstPtr);
        expect (clipFx.findEntryById (clipId, middleId)->instance.get() == middlePtr);
        expect (clipFx.findEntryById (clipId, lastId)->instance.get() == lastPtr);
        expect (clipFx.hasPluginsForClip (clipId));

        beginTest ("invalid and no-op moves leave published order unchanged");
        expect (!clipFx.moveEntry (clipId, -1, 0));
        expect (!clipFx.moveEntry (clipId, 0, 3));
        expect (!clipFx.moveEntry (clipId, 1, 1));
        expect (!clipFx.moveEntry ("missing_clip", 0, 1));
        auto unchanged = clipFx.getEntriesForClip (clipId);
        for (size_t i = 0; i < moved.size(); ++i)
            expectEquals (unchanged[i].instanceId, moved[i].instanceId);

        beginTest ("moving last back to first restores the complete original order");
        expect (clipFx.moveEntry (clipId, 2, 0));
        const auto restored = clipFx.getEntriesForClip (clipId);
        expectEquals (restored[0].instanceId, firstId);
        expectEquals (restored[1].instanceId, middleId);
        expectEquals (restored[2].instanceId, lastId);
        expect (restored[1].bypassed);
        clipFx.releaseResources();
    }
};

static ClipRegionPluginReorderRegressionTests clipRegionPluginReorderRegressionTests;

class ClipRegionPluginProjectPersistenceTests final : public juce::UnitTest
{
public:
    ClipRegionPluginProjectPersistenceTests()
        : UnitTest("clip-region-fx.project-roundtrip-and-missing-plugin.v1", "PluginHost") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        juce::AudioPluginFormatManager formatManager;
        formatManager.addFormat(std::make_unique<MatrixTestPluginFormat>());
        const DAW::ClipID clipId = "roundtrip_clip_A";
        const auto exists = [clipId](const DAW::ClipID& id) { return id == clipId; };

        DAW::ClipRegionPluginCore original;
        original.prepare(kRate, kBlock);
        const auto first = original.loadForClip(
            clipId, makeDescription(*makeSpecForUid(kUidTwinA)), formatManager);
        const auto second = original.loadForClip(
            clipId, makeDescription(*makeSpecForUid(kUidTwinB)), formatManager);
        beginTest("save a two-processor clip chain with stable identity and bypass");
        expect(first.success, first.message);
        expect(second.success, second.message);
        if (!first.success || !second.success) return;
        // Distinct non-zero opaque payloads ensure the tests verify actual
        // preset bytes, not merely a restored processor with a default state.
        constexpr juce::uint32 kFirstState = 0x4a5b6c7d;
        constexpr juce::uint32 kSecondState = 0x1a2b3c4d;
        auto* firstProbe = dynamic_cast<MatrixProbeProcessor*>(first.instance);
        auto* secondProbe = dynamic_cast<MatrixProbeProcessor*>(second.instance);
        expect(firstProbe != nullptr && secondProbe != nullptr);
        if (!firstProbe || !secondProbe) return;
        firstProbe->spec_.stateMarker = kFirstState;
        secondProbe->spec_.stateMarker = kSecondState;
        auto ordered = original.getEntriesForClip(clipId);
        expectEquals(static_cast<int>(ordered.size()), 2);
        if (ordered.size() != 2) return;
        const auto firstId = ordered[0].instanceId;
        const auto secondId = ordered[1].instanceId;
        original.setBypassed(clipId, secondId, true);
        juce::ValueTree projectFx;
        juce::String error;
        expect(original.captureProjectState(projectFx, error), error);
        expect(projectFx.isValid());
        if (!projectFx.isValid()) return;
        expectEquals(projectFx.getNumChildren(), 1);
        expectEquals(projectFx.getChild(0).getNumChildren(), 2);

        auto xml = projectFx.createXml();
        expect(xml != nullptr);
        if (!xml) return;
        auto fromDisk = juce::ValueTree::fromXml(*xml);
        beginTest("XML round trip restores both live processors and their order");
        DAW::ClipRegionPluginCore reopened;
        reopened.prepare(kRate, kBlock);
        expect(reopened.restoreProjectState(fromDisk, formatManager, exists, error), error);
        expect(error.isEmpty(), "no missing-plugin warnings expected: " + error);
        const auto live = reopened.getEntriesForClip(clipId);
        expectEquals(static_cast<int>(live.size()), 2);
        if (live.size() == 2)
        {
            expectEquals(live[0].instanceId, firstId);
            expectEquals(live[1].instanceId, secondId);
            expect(!live[0].bypassed);
            expect(live[1].bypassed);
            auto* liveFirst = dynamic_cast<MatrixProbeProcessor*>(
                reopened.findEntryById(clipId, firstId)->instance.get());
            auto* liveSecond = dynamic_cast<MatrixProbeProcessor*>(
                reopened.findEntryById(clipId, secondId)->instance.get());
            expect(liveFirst != nullptr && liveSecond != nullptr);
            if (liveFirst && liveSecond)
            {
                expectEquals(static_cast<int>(liveFirst->lastStateMarker),
                             static_cast<int>(kFirstState),
                             "first opaque VST preset must survive XML/processor restore");
                expectEquals(static_cast<int>(liveSecond->lastStateMarker),
                             static_cast<int>(kSecondState),
                             "second opaque VST preset must survive XML/processor restore");
            }
        }

        beginTest("unavailable plugin is not deleted during subsequent save");
        juce::AudioPluginFormatManager noPlugins;
        DAW::ClipRegionPluginCore missingHost;
        missingHost.prepare(kRate, kBlock);
        expect(missingHost.restoreProjectState(fromDisk, noPlugins, exists, error),
               "missing plugin should produce a preservable placeholder: " + error);
        expect(error.isNotEmpty(), "missing plugins must be reported to project loader");
        const auto missing = missingHost.getEntriesForClip(clipId);
        expectEquals(static_cast<int>(missing.size()), 2);
        if (missing.size() == 2)
        {
            expectEquals(missing[0].instanceId, firstId);
            expectEquals(missing[1].instanceId, secondId);
            expect(missing[1].bypassed);
            auto* missingFirst = missingHost.findEntryById(clipId, firstId);
            auto* missingSecond = missingHost.findEntryById(clipId, secondId);
            expect(missingFirst != nullptr && missingSecond != nullptr);
            if (missingFirst && missingSecond)
            {
                expect(missingFirst->instance == nullptr);
                expect(missingSecond->instance == nullptr);
                expectEquals(static_cast<int>(missingFirst->unresolvedState.getSize()), 4);
                expectEquals(static_cast<int>(missingSecond->unresolvedState.getSize()), 4);
                if (missingFirst->unresolvedState.getSize() == 4)
                {
                    juce::uint32 actual = 0;
                    std::memcpy(&actual, missingFirst->unresolvedState.getData(), sizeof(actual));
                    expectEquals(static_cast<int>(actual), static_cast<int>(kFirstState));
                }
                if (missingSecond->unresolvedState.getSize() == 4)
                {
                    juce::uint32 actual = 0;
                    std::memcpy(&actual, missingSecond->unresolvedState.getData(), sizeof(actual));
                    expectEquals(static_cast<int>(actual), static_cast<int>(kSecondState));
                }
            }
        }
        juce::ValueTree retained;
        expect(missingHost.captureProjectState(retained, error), error);
        expectEquals(retained.toXmlString(), fromDisk.toXmlString(),
                     "resaving unresolved slots must preserve opaque state, IDs, and ordering");

        beginTest("reinstalling a missing plugin recreates the preserved processors");
        DAW::ClipRegionPluginCore reinstalled;
        reinstalled.prepare(kRate, kBlock);
        expect(reinstalled.restoreProjectState(retained, formatManager, exists, error), error);
        expect(error.isEmpty(), error);
        auto recovered = reinstalled.getEntriesForClip(clipId);
        expectEquals(static_cast<int>(recovered.size()), 2);
        if (recovered.size() == 2)
        {
            expectEquals(recovered[0].instanceId, firstId);
            expectEquals(recovered[1].instanceId, secondId);
            expect(recovered[1].bypassed);
            auto* recoveredFirst = dynamic_cast<MatrixProbeProcessor*>(
                reinstalled.findEntryById(clipId, firstId)->instance.get());
            auto* recoveredSecond = dynamic_cast<MatrixProbeProcessor*>(
                reinstalled.findEntryById(clipId, secondId)->instance.get());
            expect(recoveredFirst != nullptr && recoveredSecond != nullptr);
            if (recoveredFirst && recoveredSecond)
            {
                expectEquals(static_cast<int>(recoveredFirst->lastStateMarker),
                             static_cast<int>(kFirstState));
                expectEquals(static_cast<int>(recoveredSecond->lastStateMarker),
                             static_cast<int>(kSecondState));
            }
        }

        beginTest("corrupt state is rejected before disturbing the live chain");
        auto corrupt = retained.createCopy();
        corrupt.getChild(0).getChild(1).setProperty("instanceId", firstId, nullptr);
        expect(!reinstalled.restoreProjectState(corrupt, formatManager, exists, error));
        expectEquals(static_cast<int>(reinstalled.getEntriesForClip(clipId).size()), 2);
        expect(reinstalled.findEntryById(clipId, firstId)->instance != nullptr);

        beginTest("unknown clip reference is rejected instead of dropping an effect");
        expect(!reinstalled.restoreProjectState(retained, formatManager,
                    [](const DAW::ClipID&) { return false; }, error));
        expectEquals(static_cast<int>(reinstalled.getEntriesForClip(clipId).size()), 2);

        beginTest("deleting one clip releases only its FX and preserves other clips");
        const DAW::ClipID otherClipId = "roundtrip_clip_B";
        auto another = reinstalled.loadForClip(
            otherClipId, makeDescription(*makeSpecForUid(kUidStereo)), formatManager);
        expect(another.success, another.message);
        reinstalled.removeAllEntriesForClip(clipId);
        expect(reinstalled.getEntriesForClip(clipId).empty());
        expect(!reinstalled.hasPluginsForClip(clipId));
        expect(reinstalled.hasPluginsForClip(otherClipId));
        if (another.success)
            expect(reinstalled.findEntryById(otherClipId,
                reinstalled.getEntriesForClip(otherClipId)[0].instanceId)->instance != nullptr);
        juce::ValueTree surviving;
        expect(reinstalled.captureProjectState(surviving, error), error);
        expectEquals(surviving.getNumChildren(), 1,
                     "deleted clip FX must not remain in the saved project");
        if (surviving.getNumChildren() == 1)
            expectEquals(surviving.getChild(0).getProperty("clipId").toString(), otherClipId);

        original.releaseResources();
        reopened.releaseResources();
        missingHost.releaseResources();
        reinstalled.releaseResources();
    }
};

static ClipRegionPluginProjectPersistenceTests clipRegionPluginProjectPersistenceTests;


/**
 * Independent real-world integration canary. CI downloads two open-source
 * VST3 binaries and sets the environment variables before this test runs.
 * Unlike MatrixTestPluginFormat, these processors are NOT synthetic mocks.
 */
class ExternalOpenSourceVST3ClipFxTests final : public juce::UnitTest
{
public:
    ExternalOpenSourceVST3ClipFxTests()
        : UnitTest("external-vst3.wet-effects-project-roundtrip.v1", "ExternalVST3") {}

    void runTest() override
    {
        const bool required = juce::SystemStats::getEnvironmentVariable(
            "APEX_EXTERNAL_VST3_REQUIRED", "") == "1";
        const auto reverbPath = juce::SystemStats::getEnvironmentVariable(
            "APEX_EXTERNAL_VST3_WETREVERB", "");
        const auto delayPath = juce::SystemStats::getEnvironmentVariable(
            "APEX_EXTERNAL_VST3_WETDELAY", "");

        beginTest("two independent GitHub release VST3 bundles are present");
        if (reverbPath.isEmpty() || delayPath.isEmpty())
        {
            if (required)
                expect(false, "External VST3 required, but CI download paths are absent");
            else
                logMessage("SKIP external VST3: run pinned-download workflow to execute");
            return;
        }

        juce::ScopedJuceInitialiser_GUI gui;
        juce::AudioPluginFormatManager manager;
        // This JUCE 8 build deliberately deletes addDefaultFormats().
        // Register the exact VST3 format explicitly for this external canary.
        manager.addFormat(std::make_unique<juce::VST3PluginFormat>());
        juce::VST3PluginFormat vst3;
        juce::PluginDescription descriptions[2];
        const juce::String paths[] = { reverbPath, delayPath };
        const juce::String expectedNames[] = { "WetReverb", "WetDelay" };
        for (int i = 0; i < 2; ++i)
        {
            const juce::File bundle(paths[i]);
            expect(bundle.isDirectory(), expectedNames[i] + " VST3 bundle must exist");
            if (!bundle.isDirectory()) return;

            juce::OwnedArray<juce::PluginDescription> found;
            try { vst3.findAllTypesForFile(found, bundle.getFullPathName()); }
            catch (const std::exception& e)
            {
                expect(false, expectedNames[i] + " VST3 enumeration threw: " + juce::String(e.what()));
                return;
            }
            catch (...) { expect(false, expectedNames[i] + " VST3 enumeration threw"); return; }
            expect(found.size() > 0, expectedNames[i] + " must expose a real VST3 component");
            if (found.isEmpty()) return;
            descriptions[i] = *found.getFirst();
            expectEquals(descriptions[i].pluginFormatName, juce::String("VST3"));
            logMessage("External VST3 found: " + descriptions[i].name
                       + " vendor=" + descriptions[i].manufacturerName);
        }

        const DAW::ClipID clipId = "external_mit_effects";
        DAW::ClipRegionPluginCore chain;
        chain.prepare(48000.0, 128);
        beginTest("load third-party VST3 through production ClipRegionPluginCore");
        const auto reverb = chain.loadForClip(clipId, descriptions[0], manager);
        expect(reverb.success && reverb.instance != nullptr,
               "real reverb instantiation: " + reverb.message);
        if (!reverb.success || !reverb.instance) return;
        const auto delay = chain.loadForClip(clipId, descriptions[1], manager);
        expect(delay.success && delay.instance != nullptr,
               "real delay instantiation: " + delay.message);
        if (!delay.success || !delay.instance) return;

        const auto before = chain.getEntriesForClip(clipId);
        expectEquals(static_cast<int>(before.size()), 2);
        if (before.size() != 2) return;
        const auto reverbId = before[0].instanceId;
        const auto delayId = before[1].instanceId;

        beginTest("both third-party VST3 parameter changes are applied before state capture");
        juce::AudioProcessor* processors[] = { reverb.instance, delay.instance };
        const juce::String pluginNames[] = { "WetReverb", "WetDelay" };
        int parameterIndices[] = { -1, -1 };
        float savedParameterValues[] = { 0.0f, 0.0f };
        for (int plugin = 0; plugin < 2; ++plugin)
        {
            const auto& params = processors[plugin]->getParameters();
            for (int i = 0; i < params.size(); ++i)
            {
                // Both of these test plugins expose *discrete* parameters:
                // WetReverb mode has five positions and WetDelay time has
                // six. Select a real automatable control regardless of step
                // count, but choose a canonical grid-aligned target below.
                if (params[i] != nullptr && params[i]->isAutomatable()
                    && !params[i]->isMetaParameter())
                {
                    parameterIndices[plugin] = i;
                    break;
                }
            }
            expect(parameterIndices[plugin] >= 0,
                   pluginNames[plugin] + " must expose an automatable, non-meta parameter");
            if (parameterIndices[plugin] < 0)
                return;

            auto* parameter = params[parameterIndices[plugin]];
            const float oldValue = parameter->getValue();
            const float requested = oldValue > 0.5f ? 0.17f : 0.83f;
            const int steps = parameter->getNumSteps();
            // Normalized VST3 enum values are canonical i/(steps-1).
            // Passing e.g. 0.83 to a five-mode reverb can read back 0.83
            // immediately but serialize mode index 3, which restores as
            // 0.75. Align the actual requested value with the discrete grid.
            // Continuous and unknown-step parameters keep their raw target.
            const float targetValue = steps > 1 && steps <= 1000
                ? std::round (requested * float(steps - 1)) / float(steps - 1)
                : requested;
            parameter->setValueNotifyingHost(targetValue);
            savedParameterValues[plugin] = parameter->getValue();
            logMessage(pluginNames[plugin] + " roundtrip parameter #" 
                + juce::String(parameterIndices[plugin]) + " name=\""
                + parameter->getName(128) + "\" steps="
                + juce::String(steps) + " initial="
                + juce::String(oldValue, 6) + " target="
                + juce::String(targetValue, 6) + " accepted="
                + juce::String(savedParameterValues[plugin], 6));
            expect(std::isfinite(savedParameterValues[plugin])
                       && std::abs(savedParameterValues[plugin] - oldValue) > 0.02f,
                   pluginNames[plugin] + " must actually accept a distinct parameter value");
        }

        beginTest("128-sample stereo DSP with both real plugins yields finite audio");
        juce::AudioBuffer<float> buffer(2, 128);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample(ch, sample, sample == 0 ? 0.3f : 0.0f);
        chain.processClipBlock(clipId, buffer, 128);
        bool finite = true;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                finite = finite && std::isfinite(buffer.getSample(ch, sample));
        expect(finite, "third-party VST3 DSP produced NaN/Inf");
        chain.setBypassed(clipId, delayId, true);

        beginTest("save binary state, identity, order and bypass into project XML");
        juce::ValueTree state;
        juce::String error;
        expect(chain.captureProjectState(state, error), error);
        expect(state.isValid());
        if (!state.isValid()) return;
        expectEquals(state.getNumChildren(), 1);
        expectEquals(state.getChild(0).getNumChildren(), 2);
        const auto firstState = state.getChild(0).getChild(0).getChildWithName("State");
        const auto secondState = state.getChild(0).getChild(1).getChildWithName("State");
        expect(firstState.getProperty("data").toString().isNotEmpty(),
               "real reverb must supply opaque VST3 state");
        expect(secondState.getProperty("data").toString().isNotEmpty(),
               "real delay must supply opaque VST3 state");
        auto xml = state.createXml();
        expect(xml != nullptr);
        if (!xml) return;
        auto saved = juce::ValueTree::fromXml(*xml);

        beginTest("unavailable third-party VST3s retain exact opaque state bytes");
        juce::AudioPluginFormatManager missing;
        DAW::ClipRegionPluginCore offline;
        offline.prepare(48000.0, 128);
        const auto clipExists = [clipId](const DAW::ClipID& id) { return id == clipId; };
        expect(offline.restoreProjectState(saved, missing, clipExists, error), error);
        auto offlineEntries = offline.getEntriesForClip(clipId);
        expectEquals(static_cast<int>(offlineEntries.size()), 2);
        if (offlineEntries.size() != 2)
            return;
        expect(offline.findEntryById(clipId, reverbId)->instance == nullptr);
        expect(offline.findEntryById(clipId, delayId)->instance == nullptr);
        juce::ValueTree preserved;
        expect(offline.captureProjectState(preserved, error), error);
        expectEquals(preserved.toXmlString(), saved.toXmlString(),
                     "lossless missing-VST3 project resave");

        beginTest("reinstall real VST3s and restore processor identity and bypass");
        DAW::ClipRegionPluginCore reopened;
        reopened.prepare(48000.0, 128);
        expect(reopened.restoreProjectState(preserved, manager, clipExists, error), error);
        expect(error.isEmpty(), "real VST3 recovery must not create missing slots: " + error);
        auto after = reopened.getEntriesForClip(clipId);
        expectEquals(static_cast<int>(after.size()), 2);
        if (after.size() == 2)
        {
            expectEquals(after[0].instanceId, reverbId);
            expectEquals(after[1].instanceId, delayId);
            expect(after[1].bypassed);
            expect(reopened.findEntryById(clipId, reverbId)->instance != nullptr);
            expect(reopened.findEntryById(clipId, delayId)->instance != nullptr);
        }

        beginTest("real VST3 parameter values survive missing-plugin save and reinstall");
        const juce::String instanceIds[] = { reverbId, delayId };
        for (int plugin = 0; plugin < 2; ++plugin)
        {
            const auto* entry = reopened.findEntryById(clipId, instanceIds[plugin]);
            expect(entry != nullptr && entry->instance != nullptr,
                   pluginNames[plugin] + " must have a restored processor");
            if (entry == nullptr || entry->instance == nullptr)
                continue;

            const auto& restoredParameters = entry->instance->getParameters();
            expect(restoredParameters.size() > parameterIndices[plugin],
                   pluginNames[plugin] + " parameter ordering must remain stable");
            if (restoredParameters.size() <= parameterIndices[plugin])
                continue;

            expectWithinAbsoluteError(restoredParameters[parameterIndices[plugin]]->getValue(),
                                      savedParameterValues[plugin], 0.015f,
                                      pluginNames[plugin] + " parameter must match the saved value");
        }

        chain.releaseResources();
        offline.releaseResources();
        reopened.releaseResources();
    }
};

static ExternalOpenSourceVST3ClipFxTests externalOpenSourceVST3ClipFxTests;

/**
 * Cross-category, REAL VST3 canary: EQ, dynamics and modulation in addition
 * to the existing independent reverb/delay test. The APEX host handles these
 * purely as AudioProcessors with automatable parameters; never by category,
 * branded plugin name, or hard-coded knob index. The CI manifest is only for
 * locating hash-pinned external test binaries.
 */
class ExternalOpenSourceVST3DiverseTypesTests final : public juce::UnitTest
{
public:
    ExternalOpenSourceVST3DiverseTypesTests()
        : juce::UnitTest("external-vst3.diverse-types-automation-roundtrip.v1",
                         "ExternalVST3") {}

    void runTest() override
    {
        const bool required = juce::SystemStats::getEnvironmentVariable(
            "APEX_EXTERNAL_VST3_REQUIRED", "") == "1";
        const juce::String categories[] = { "EQ", "Compressor", "Chorus" };
        const juce::String envNames[] = {
            "APEX_EXTERNAL_VST3_WETEQ",
            "APEX_EXTERNAL_VST3_WETCOMPRESSOR",
            "APEX_EXTERNAL_VST3_WETCHORUS"
        };
        juce::String paths[3];
        for (int i = 0; i < 3; ++i)
            paths[i] = juce::SystemStats::getEnvironmentVariable(envNames[i], "");

        beginTest("three distinct effect categories available as real VST3 bundles");
        for (int i = 0; i < 3; ++i)
        {
            if (paths[i].isEmpty())
            {
                if (required)
                    expect(false, categories[i] + " VST3 missing from pinned CI manifest");
                else
                    logMessage("SKIP diverse VST3: run external-vst3-smoke workflow");
                return;
            }
        }

        juce::ScopedJuceInitialiser_GUI gui;
        juce::AudioPluginFormatManager manager;
        manager.addFormat(std::make_unique<juce::VST3PluginFormat>());
        juce::VST3PluginFormat vst3;
        juce::PluginDescription descriptions[3];

        for (int i = 0; i < 3; ++i)
        {
            const juce::File bundle(paths[i]);
            expect(bundle.isDirectory(), categories[i] + " bundle missing");
            if (!bundle.isDirectory()) return;
            juce::OwnedArray<juce::PluginDescription> found;
            try { vst3.findAllTypesForFile(found, bundle.getFullPathName()); }
            catch (const std::exception& e)
            {
                expect(false, categories[i] + " enumeration exception: " + juce::String(e.what()));
                return;
            }
            catch (...) { expect(false, categories[i] + " enumeration exception"); return; }
            expect(!found.isEmpty(), categories[i] + " exposes a VST3 component");
            if (found.isEmpty()) return;
            descriptions[i] = *found.getFirst();
            expectEquals(descriptions[i].pluginFormatName, juce::String("VST3"));
            logMessage("External " + categories[i] + " component="
                       + descriptions[i].name + " vendor="
                       + descriptions[i].manufacturerName);
        }

        // Treat all three categories identically inside the production host.
        // A failure in a new plugin type must not be masked by the old
        // reverb/delay fixture passing on its own.
        const DAW::ClipID clipId = "third_party_multi_category";
        DAW::ClipRegionPluginCore chain;
        chain.prepare(48000.0, 128);
        juce::AudioProcessor* processors[3] = {nullptr, nullptr, nullptr};
        juce::String instanceIds[3];
        for (int i = 0; i < 3; ++i)
        {
            const auto loaded = chain.loadForClip(clipId, descriptions[i], manager);
            expect(loaded.success && loaded.instance != nullptr,
                   categories[i] + " native load failed: " + loaded.message);
            if (!loaded.success || loaded.instance == nullptr) return;
            processors[i] = loaded.instance;
        }
        const auto loadedEntries = chain.getEntriesForClip(clipId);
        expectEquals((int)loadedEntries.size(), 3);
        if (loadedEntries.size() != 3) return;
        for (int i = 0; i < 3; ++i)
        {
            instanceIds[i] = loadedEntries[(size_t)i].instanceId;
            for (int j = 0; j < i; ++j)
                expect(instanceIds[i] != instanceIds[j],
                       "VST3 instances of different categories must have unique IDs");
        }

        beginTest("arbitrary automatable controls, no hard-coded parameter names");
        int indices[3] = {-1, -1, -1};
        float recorded[3] = {};
        for (int i = 0; i < 3; ++i)
        {
            const auto& params = processors[i]->getParameters();
            int automatableCount = 0;
            for (int j = 0; j < params.size(); ++j)
                if (params[j] != nullptr && params[j]->isAutomatable()
                    && !params[j]->isMetaParameter())
                {
                    ++automatableCount;
                    if (indices[i] < 0) indices[i] = j;
                }
            expect(automatableCount >= 2,
                   categories[i] + " should expose multiple independent automation targets");
            if (indices[i] < 0) return;
            auto* p = params[indices[i]];
            const float previous = p->getValue();
            const float requested = previous > 0.5f ? 0.15f : 0.85f;
            const int steps = p->getNumSteps();
            const float target = steps > 1 && steps <= 1000
                ? std::round(requested * (float)(steps - 1)) / (float)(steps - 1)
                : requested;
            p->setValueNotifyingHost(target);
            recorded[i] = p->getValue();
            expect(std::isfinite(recorded[i])
                   && std::abs(recorded[i] - previous) > 0.02f,
                   categories[i] + " did not accept host-provided automation value");
            logMessage(categories[i] + " parameter #" + juce::String(indices[i])
                       + " name=" + p->getName(128) + " steps="
                       + juce::String(steps) + " accepted=" + juce::String(recorded[i], 6));
        }

        beginTest("128-sample real processor chain accepts automation changes");
        juce::AudioBuffer<float> audio(2, 128);
        for (int block = 0; block < 3; ++block)
        {
            audio.clear();
            audio.setSample(0, 0, 0.25f);
            audio.setSample(1, 0, -0.25f);
            chain.processClipBlock(clipId, audio, 128);
            bool finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 128; ++s)
                    finite = finite && std::isfinite(audio.getSample(ch, s));
            expect(finite, "EQ/compressor/chorus DSP produced non-finite audio");
        }

        beginTest("cross-category opaque VST3 state round-trips via production project host");
        juce::ValueTree state;
        juce::String error;
        expect(chain.captureProjectState(state, error), error);
        expect(state.isValid());
        if (!state.isValid()) return;
        expectEquals(state.getNumChildren(), 1);
        if (state.getNumChildren() != 1) return;
        expectEquals(state.getChild(0).getNumChildren(), 3);
        for (int i = 0; i < 3; ++i)
        {
            const auto serialized = state.getChild(0).getChild(i);
            expect(serialized.getChildWithName("State").getProperty("data")
                   .toString().isNotEmpty(),
                   categories[i] + " opaque state must be serializable");
        }
        const auto xml = state.createXml();
        expect(xml != nullptr);
        if (!xml) return;
        const auto saved = juce::ValueTree::fromXml(*xml);
        const auto exists = [clipId](const DAW::ClipID& id) { return id == clipId; };

        juce::AudioPluginFormatManager unavailable;
        DAW::ClipRegionPluginCore missing;
        missing.prepare(48000.0, 128);
        expect(missing.restoreProjectState(saved, unavailable, exists, error), error);
        juce::ValueTree preserved;
        expect(missing.captureProjectState(preserved, error), error);
        expectEquals(preserved.toXmlString(), saved.toXmlString(),
                     "missing EQ/compressor/chorus binaries must preserve project bytes");
        DAW::ClipRegionPluginCore reopened;
        reopened.prepare(48000.0, 128);
        expect(reopened.restoreProjectState(preserved, manager, exists, error), error);
        const auto entries = reopened.getEntriesForClip(clipId);
        expectEquals((int)entries.size(), 3);
        if (entries.size() == 3)
            for (int i = 0; i < 3; ++i)
            {
                expectEquals(entries[(size_t)i].instanceId, instanceIds[i]);
                const auto* entry = reopened.findEntryById(clipId, instanceIds[i]);
                expect(entry != nullptr && entry->instance != nullptr,
                       categories[i] + " failed to reopen");
                if (entry != nullptr && entry->instance != nullptr)
                {
                    const auto& restoredParams = entry->instance->getParameters();
                    expect(restoredParams.size() > indices[i],
                           categories[i] + " changed parameter order");
                    if (restoredParams.size() > indices[i])
                        expectWithinAbsoluteError(
                            restoredParams[indices[i]]->getValue(), recorded[i], 0.02f,
                            categories[i] + " automation target did not survive reopening");
                }
            }
        // Beyond parameter state/preset round-tripping, verify the plugin's
        // own editor gesture actually reaches the APEX recorder and evaluator.
        // IDs, control names, ranges and categories are NOT hard-coded in the
        // automation production path; indices were discovered above.
        beginTest("VST3 native editor gestures -> Touch lane -> PPQ evaluator (3 categories)");
        {
            using namespace apex::automation;
            AutomationParameterRegistry registry;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationClock clock;
            AutomationTransportState arm;
            auto queue = std::make_unique<AutomationGestureQueue>();
            AutomationGestureBridge bridge(registry, *queue, clock);
            AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arm);
            AutomationEvaluator evaluator(registry, lanes, modes);
            AutomationParameter* mirrors[3] = {nullptr, nullptr, nullptr};
            const ParameterID ids[3] = {92011u, 92012u, 92013u};
            float expectedRecorded[3] = {};
            const double movePPQs[3] = {1.5, 2.0, 2.5};
            arm.setRecordArmed(true);

            for (int i = 0; i < 3; ++i)
            {
                auto* pluginParam = processors[i]->getParameters()[indices[i]];
                ParameterRange range;
                range.defaultValue = pluginParam->getValue();
                const int steps = pluginParam->getNumSteps();
                range.isStepped = steps > 1 && steps <= 1000;
                range.numSteps = range.isStepped ? steps : 0;
                mirrors[i] = registry.createParameter(
                    ids[i], categories[i] + " hosted control", range,
                    ParameterScope::Plugin, static_cast<PluginInstanceID>(i + 1));
                expect(mirrors[i] != nullptr);
                if (mirrors[i] == nullptr) return;
                mirrors[i]->bindToPluginParameter(pluginParam);
                bridge.attachToParameter(*mirrors[i]); // match production AutomationSystem
                bridge.attachToPlugin(*processors[i], {{indices[i], ids[i]}});
                modes.setMode(ids[i], AutomationMode::Touch);
                lanes.getOrCreateLane(ids[i]).replacePoints({
                    {0.0, 0.1f, CurveType::Linear, 0.0f},
                    {4.0, 0.5f, CurveType::Linear, 0.0f},
                    {8.0, 0.9f, CurveType::Linear, 0.0f}
                });
            }

            clock.publishFromAudioThread(1.0, 0.001, true);
            recorder.drainForTests();
            for (int i = 0; i < 3; ++i)
            {
                auto* pluginParam = processors[i]->getParameters()[indices[i]];
                const float previous = pluginParam->getValue();
                const float requested = previous > 0.5f ? 0.1f : 0.9f;
                const int steps = pluginParam->getNumSteps();
                const float target = steps > 1 && steps <= 1000
                    ? std::round(requested * float(steps - 1)) / float(steps - 1)
                    : requested;

                clock.publishFromAudioThread(movePPQs[i] - 0.1, 0.001, true);
                pluginParam->beginChangeGesture();
                expect(mirrors[i]->isGestureActive(),
                       categories[i] + " plugin begin must suppress Touch read");
                clock.publishFromAudioThread(movePPQs[i], 0.001, true);
                pluginParam->setValueNotifyingHost(target);
                expectedRecorded[i] = pluginParam->getValue();
                expectWithinAbsoluteError(mirrors[i]->getNormalizedValue(),
                    expectedRecorded[i], 0.02f,
                    categories[i] + " plugin move must update APEX atomic");
                evaluator.evaluateBlock(movePPQs[i], true);
                expectWithinAbsoluteError(mirrors[i]->getNormalizedValue(),
                    expectedRecorded[i], 0.02f,
                    categories[i] + " Touch playback must not fight plugin editor");
                clock.publishFromAudioThread(movePPQs[i] + 0.1, 0.001, true);
                pluginParam->endChangeGesture();
                expect(!mirrors[i]->isGestureActive(),
                       categories[i] + " plugin end must release Touch");

                recorder.drainForTests();
                const auto lane = lanes.findLane(ids[i]);
                expect(lane != nullptr, categories[i] + " must create a Touch lane");
                if (lane == nullptr) continue;
                const auto snap = lane->getSnapshot();
                expect(snap != nullptr && !snap->empty(),
                       categories[i] + " must record plugin PPQ movement");
                if (snap == nullptr || snap->empty()) continue;
                expectWithinAbsoluteError(
                    AutomationLane::evaluateAt(*snap, movePPQs[i]),
                    expectedRecorded[i], 0.03f,
                    categories[i] + " Touch sample must survive lane recording");
                // The audio evaluator reads the actual committed PPQ lane,
                // not a copied plugin knob value or hard-coded effect name.
                mirrors[i]->writeValue(0.0f, ChangeSource::Programmatic);
                evaluator.evaluateBlock(movePPQs[i], true);
                expectWithinAbsoluteError(
                    mirrors[i]->getNormalizedValue(), expectedRecorded[i], 0.03f,
                    categories[i] + " committed lane must drive APEX playback atomic");
            }

            beginTest("APEX-originated controls do not echo as duplicate plugin gestures");
            {
                constexpr int i = 0;
                auto* pluginParam = processors[i]->getParameters()[indices[i]];
                const float prior = pluginParam->getValue();
                const float requested = prior > 0.5f ? 0.1f : 0.9f;
                const int steps = pluginParam->getNumSteps();
                const float canonical = steps > 1 && steps <= 1000
                    ? std::round(requested * float(steps - 1)) / float(steps - 1)
                    : requested;
                mirrors[i]->setValueFromUser(canonical);
                int userEvents = 0, echoedEvents = 0;
                AutomationGestureQueue::Event event;
                while (queue->pop(event))
                    if (event.paramID == ids[i]
                        && event.kind == AutomationGestureQueue::EventKind::ValueChange)
                    {
                        if (event.source == ChangeSource::User) ++userEvents;
                        if (event.source == ChangeSource::Plugin) ++echoedEvents;
                    }
                expectEquals(userEvents, 1, "direct APEX User capture must happen once");
                expectEquals(echoedEvents, 0, "plugin callback must not double-record APEX gesture");
            }

            for (int i = 0; i < 3; ++i)
                bridge.detachFromPlugin(*processors[i]);
            // This gate proves editor capture and PPQ evaluator atomics, not
            // sample-accurate VST3 IParameterChanges or hardware audio E2E.
        }

        chain.releaseResources();
        missing.releaseResources();
        reopened.releaseResources();
    }
};
static ExternalOpenSourceVST3DiverseTypesTests externalOpenSourceVST3DiverseTypesTests;

} // namespace
