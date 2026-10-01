#include <JuceHeader.h>
#include "C4TestUtils.h"
#include "../../../Source/G10Core/G10NativePluginFormat.h"
#include "../../../Source/PluginScanCore/PluginScanFormatsCore.h"

// ============================================================================
// C4FormatHostTests — the "APEX Native" format contract for C4
// (spec §36): description fidelity, synchronous creation on every path,
// intrinsic existence, no scanning.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4NativePluginFormat;

class C4FormatHostTests final : public juce::UnitTest
{
public:
    C4FormatHostTests() : juce::UnitTest ("C4.FormatHost", "APEX.C4") {}

    void runTest() override
    {
        testDescriptionFidelity();
        testCreateSuccess();
        testCreateRejectsForeign();
        testIntrinsicProperties();
        testMultipleInstances();
        testParameterAutomationIds();
        testUnifiedFamilyManager(); // Phase 8: the REAL host registration path
    }

private:
    /** Create through the REAL host path: AudioPluginFormatManager. */
    static std::unique_ptr<juce::AudioPluginInstance> createViaManager (
        const juce::PluginDescription& desc, juce::String& error)
    {
        juce::AudioPluginFormatManager manager;
        manager.addFormat (std::make_unique<C4NativePluginFormat>());
        return manager.createPluginInstance (desc, 48000.0, 512, error);
    }

    void testDescriptionFidelity()
    {
        beginTest ("C4NativePluginFormat description matches fillInPluginDescription");
        C4Processor proc;
        juce::PluginDescription desc;
        proc.fillInPluginDescription (desc);

        const auto canonical = C4NativePluginFormat::createC4Description();

        expect (canonical.uniqueId == C4Processor::kUniqueId, "uniqueId");
        expect (canonical.name == C4Processor::kPluginName, "name");
        expect (canonical.pluginFormatName == C4NativePluginFormat::kFormatName, "format");
        expect (canonical.category == C4Processor::kCategory, "category");
        expect (canonical.fileOrIdentifier == C4Processor::kFileOrIdentifier, "identifier");
        expect (canonical.numInputChannels == 2 && canonical.numOutputChannels == 2, "channels");

        expect (desc.uniqueId == canonical.uniqueId
                    && desc.name == canonical.name
                    && desc.pluginFormatName == canonical.pluginFormatName
                    && desc.fileOrIdentifier == canonical.fileOrIdentifier,
                "processor description must match the canonical format description");
    }

    void testCreateSuccess()
    {
        beginTest ("createPluginInstance succeeds for the canonical C4 description");
        juce::String error;
        auto instance = createViaManager (C4NativePluginFormat::createC4Description(), error);

        expect (instance != nullptr, "instance must be created");
        expect (error.isEmpty(), "no error on success");
        if (instance != nullptr)
        {
            expect (instance->getName() == C4Processor::kPluginName, "instance name");
            expect (instance->getNumParameters() == APEX::C4::kNumParams,
                    "instance exposes all 19 parameters");
            instance->prepareToPlay (48000.0, 512);
            expect (instance->getLatencySamples() == 0, "zero latency");
        }
    }

    void testCreateRejectsForeign()
    {
        beginTest ("createPluginInstance rejects foreign descriptions with an error");
        juce::PluginDescription foreign = C4NativePluginFormat::createC4Description();
        foreign.uniqueId = 0xDEADBEEF; // not C4

        juce::String error;
        auto instance = createViaManager (foreign, error);
        expect (instance == nullptr, "no instance for a foreign description");
        expect (! error.isEmpty(), "failure must carry an error message");

        // G10's description must NOT be creatable as C4.
        const auto g10Desc = APEX::G10::G10NativePluginFormat::createG10Description();
        expect (! C4NativePluginFormat::isC4Description (g10Desc),
                "G10 description must not be recognized as C4");

        // ...and C4's description must not be recognized as G10.
        expect (! APEX::G10::G10NativePluginFormat::isG10Description (
                    C4NativePluginFormat::createC4Description()),
                "C4 description must not be recognized as G10");
    }

    void testIntrinsicProperties()
    {
        beginTest ("Intrinsic format properties (no scanning, always exists)");
        C4NativePluginFormat format;

        expect (! format.canScanForPlugins(), "native plugins are never scanned");
        expect (format.isTrivialToScan(), "native plugins are trivial to scan");
        expect (! format.pluginNeedsRescanning (C4NativePluginFormat::createC4Description()),
                "intrinsic plugins are never stale");
        expect (format.doesPluginStillExist (C4NativePluginFormat::createC4Description()),
                "canonical C4 always exists");
        expect (format.getNameOfPluginFromIdentifier (C4Processor::kFileOrIdentifier)
                    == C4Processor::kPluginName,
                "identifier resolves to the plugin name");
        expect (format.searchPathsForPlugins ({}, false, false).isEmpty(),
                "no filesystem search paths");
        expect (! format.requiresUnblockedMessageThreadDuringCreation (
                    C4NativePluginFormat::createC4Description()),
                "synchronous message-thread-safe creation");

        // findAllTypesForFile returns exactly the C4 description.
        juce::OwnedArray<juce::PluginDescription> results;
        format.findAllTypesForFile (results, {});
        expect (results.size() == 1 && results[0]->uniqueId == C4Processor::kUniqueId,
                "findAllTypesForFile yields exactly C4");
    }

    void testMultipleInstances()
    {
        beginTest ("Multiple independent instances (state isolation)");
        auto a = C4Test::makePreparedProcessor (48000.0, 128);
        auto b = C4Test::makePreparedProcessor (48000.0, 128);

        a->getC4Parameter (APEX::C4::C4ParamIndex::kSculptGain)->setValue (
            a->getC4Parameter (APEX::C4::C4ParamIndex::kSculptGain)->getValueForText ("9.0"));

        // Instance B must sit at defaults (0 dB gain == normalized 0.5).
        expect (std::abs (b->getC4Parameter (APEX::C4::C4ParamIndex::kSculptGain)->getValue()
                          - 0.5f) < 1.0e-6f,
                "instance B must be unaffected by instance A's settings");
    }

    void testParameterAutomationIds()
    {
        beginTest ("Every parameter carries its stable ID for automation binding");
        auto proc = C4Test::makePreparedProcessor (48000.0, 128);
        int withId = 0;
        for (auto* p : proc->getParameters())
            if (dynamic_cast<juce::AudioProcessorParameterWithID*> (p) != nullptr)
                ++withId;
        expect (withId == APEX::C4::kNumParams,
                "all 19 parameters expose AudioProcessorParameterWithID (APEX automation)");
    }

    void testUnifiedFamilyManager()
    {
        beginTest ("Phase 8 host path: the unified APEX Native format serves G10 AND C4");

        DAW::PluginScanFormatsCore formats;
        const auto names = formats.getRegisteredFormatNames();

        // Exactly ONE "APEX Native" format object (the manager keys by name
        // and would silently drop a duplicate — the Phase 8 fix).
        int apexNativeCount = 0;
        for (const auto& n : names)
            if (n == "APEX Native")
                ++apexNativeCount;
        expectEquals (apexNativeCount, 1, "exactly one registered APEX Native format");

        auto& manager = formats.getManager();

        // C4 instantiates through the REAL manager path.
        {
            juce::String error;
            auto c4 = manager.createPluginInstance (
                C4NativePluginFormat::createC4Description(), 48000.0, 512, error);
            expect (c4 != nullptr, "manager must create C4: " + error);
            expect (dynamic_cast<C4Processor*> (c4.get()) != nullptr,
                    "manager instance must be a C4Processor");
            expect (c4->hasEditor(), "C4 from the manager must expose its editor");
            std::unique_ptr<juce::AudioProcessorEditor> editor (c4->createEditor());
            expect (editor != nullptr, "C4 from the manager must create its editor");
        }

        // G10 still instantiates through the SAME manager (coexistence).
        {
            juce::String error;
            auto g10 = manager.createPluginInstance (
                APEX::G10::G10NativePluginFormat::createG10Description(), 48000.0, 512, error);
            expect (g10 != nullptr, "manager must still create G10: " + error);
            expect (dynamic_cast<APEX::G10::G10Processor*> (g10.get()) != nullptr,
                    "manager instance must be a G10Processor");
        }

        // Discovery lists both family members, deterministically ordered.
        {
            juce::OwnedArray<juce::PluginDescription> found;
            formats.findFormatByName ("APEX Native")->findAllTypesForFile (
                found, "APEX::C4");
            expect (found.size() == 3,
                    "findAllTypesForFile must expose G10, C4 and Parametric EQ");
        }

        // Foreign identifiers still resolve to a clean error, never a crash.
        {
            juce::PluginDescription foreign = C4NativePluginFormat::createC4Description();
            foreign.fileOrIdentifier = "APEX::NotARealPlugin";
            foreign.uniqueId = 0x9999;
            juce::String error;
            auto none = manager.createPluginInstance (foreign, 48000.0, 512, error);
            expect (none == nullptr && error.isNotEmpty(),
                    "foreign identifier must fail with a clean error");
        }
    }
};

static C4FormatHostTests c4FormatHostTests;
