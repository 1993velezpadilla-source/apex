#include <JuceHeader.h>
#include "G10TestUtils.h"

#include "../../../Source/PluginScanCore/PluginScanFormatsCore.h"
#include "../../../Source/PluginHostCore/PluginScannerCore.h"
#include "../../../Source/PluginStorageCore/PluginCacheCore.h"
#include "../../../Source/PluginScanCore/PluginBrowserFeedCore.h"
#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/Automation/AutomationSystemCore.h"
#include "../../../Source/Automation/AutomationParameterKeyCore.h"

#include <cmath>
#include <utility>
#include <vector>

// ============================================================================
// G10FormatHostTests — the "APEX Native" format and the existing APEX host
// integration: format manager registration, cache/browser seeding, chain
// loading, state round-trip, and the AudioProcessorParameterWithID automation
// binding path. No parallel test-only host is created.
// ============================================================================

namespace
{

using APEX::G10::G10Processor;
using APEX::G10::G10NativePluginFormat;
using APEX::G10::G10Parameter;
using APEX::G10::kNumParams;

const juce::PluginDescription& canonicalDescription()
{
    static const juce::PluginDescription desc = G10NativePluginFormat::createG10Description();
    return desc;
}

void setDb (G10Parameter* p, float db)
{
    jassert (p != nullptr);
    p->setValue (p->getValueForText (juce::String (db, 1)));
}

} // namespace

class G10FormatHostTests : public juce::UnitTest
{
public:
    G10FormatHostTests() : juce::UnitTest ("G10.Format.Host", "APEX.G10") {}

    void runTest() override
    {
        testFormatIdentity();
        testFormatManagerRegistration();
        testCacheAndBrowser();
        testChainIntegration();
        testAutomationBinding();
    }

    // ------------------------------------------------------------------

    void testFormatIdentity()
    {
        beginTest ("Native format identity and synchronous creation");

        G10NativePluginFormat format;
        expectEquals (format.getName(), juce::String ("APEX Native"), "format name");

        const auto& desc = canonicalDescription();
        expectEquals (desc.name, juce::String ("APEX G10"), "desc name");
        expectEquals (desc.pluginFormatName, juce::String ("APEX Native"), "desc format");
        expectEquals (desc.uniqueId, (int) G10Processor::kUniqueId, "desc unique ID");
        expectEquals (desc.fileOrIdentifier, juce::String ("APEX::G10"), "desc identifier");
        expectEquals (desc.category, juce::String ("EQ"), "desc category");
        expectEquals (desc.manufacturerName, juce::String ("APEX"), "desc manufacturer");
        expectEquals (desc.version, juce::String ("1.0.0"), "desc version");
        expect (! desc.isInstrument, "desc not instrument");

        expect (G10NativePluginFormat::isG10Description (desc), "canonical desc recognized");
        juce::PluginDescription other;
        other.uniqueId = 0x12345678;
        other.fileOrIdentifier = "some.other";
        other.pluginFormatName = "APEX Native"; // route to the G10 format for rejection
        expect (! G10NativePluginFormat::isG10Description (other), "unknown desc rejected");

        expect (! format.canScanForPlugins(), "canScanForPlugins must be false");
        expect (format.isTrivialToScan(), "isTrivialToScan must be true");
        expect (format.doesPluginStillExist (desc), "canonical G10 must exist");
        expect (! format.doesPluginStillExist (other), "unknown desc must not exist");

        // fileMightContainThisPluginType() recognizes the virtual built-in
        // identifier: JUCE 8.0.12 AudioPluginFormatManager::findFormatForDescription()
        // requires BOTH getName() == pluginFormatName AND
        // fileMightContainThisPluginType(fileOrIdentifier) before it selects a
        // format. Recognizing a built-in identifier is NOT filesystem
        // scanning — arbitrary paths and unrelated identifiers stay rejected.
        expect (format.fileMightContainThisPluginType ("APEX::G10"),
                "canonical built-in identifier recognized");
        expect (format.fileMightContainThisPluginType (juce::String()),
                "empty identifier recognized (descriptions predating identifier assignment)");
        expect (! format.fileMightContainThisPluginType ("C:\\some\\folder\\plugin.vst3"),
                "arbitrary .vst3 path rejected (no filesystem scanning)");
        expect (! format.fileMightContainThisPluginType ("C:\\some\\folder\\plugin.dll"),
                "arbitrary .dll path rejected (no filesystem scanning)");
        expect (! format.fileMightContainThisPluginType ("some.other"),
                "unrelated identifier rejected");

        // No filesystem scanning: no search paths, no default locations, and
        // no external plugins discovered.
        expectEquals (format.searchPathsForPlugins (juce::FileSearchPath(), true, true).size(), 0,
                      "searchPathsForPlugins finds no external plugins");
        expectEquals (format.getDefaultLocationsToSearch().getNumPaths(), 0,
                      "no default search locations");

        juce::OwnedArray<juce::PluginDescription> results;
        format.findAllTypesForFile (results, juce::String());
        expectEquals (results.size(), 1, "findAllTypesForFile returns exactly the canonical G10");
        if (results.size() == 1)
            expect (G10NativePluginFormat::isG10Description (*results[0]), "found type is canonical G10");

        // Synchronous creation success through the public manager API.
        {
            juce::AudioPluginFormatManager manager;
            manager.addFormat (std::make_unique<G10NativePluginFormat>());
            juce::String error;
            auto created = manager.createPluginInstance (desc, 48000.0, 512, error);
            expect (created != nullptr, "successful creation must return an instance: " + error);
            expect (error.isEmpty(), "no error on success");
            expect (dynamic_cast<G10Processor*> (created.get()) != nullptr, "created instance is a G10Processor");
            if (created != nullptr)
            {
                expectEquals (created->getSampleRate(), 48000.0, "initial sample rate applied");
                expectEquals (created->getBlockSize(), 512, "initial block size applied");
            }
        }

        // Synchronous failure for an unknown description.
        {
            juce::AudioPluginFormatManager manager;
            manager.addFormat (std::make_unique<G10NativePluginFormat>());
            juce::String error;
            auto created = manager.createPluginInstance (other, 48000.0, 512, error);
            expect (created == nullptr, "no instance on failure");
            expect (error.isNotEmpty(), "failure must report an error");
        }
    }

    // ------------------------------------------------------------------

    void testFormatManagerRegistration()
    {
        beginTest ("Format-manager registration");

        // External plugin hosts are compile-time conditional
        // (JUCE_PLUGINHOST_* module config; see juce_PluginFormatDefs.h).
        // This test build enables none of them, so no external format may be
        // asserted unconditionally. Derive the expected set from the same
        // macros PluginScanFormatsCore uses: the test then proves that adding
        // APEX Native removed no format that this build actually supports,
        // without hard-coding a format that is compiled out.
        const juce::StringArray expectedExternal =
        {
#if JUCE_PLUGINHOST_VST3
            "VST3",
#endif
#if JUCE_PLUGINHOST_VST
            "VST",
#endif
        };

        DAW::PluginScanFormatsCore formats;
        const auto names = formats.getRegisteredFormatNames();

        for (const auto& formatName : expectedExternal)
            expect (names.contains (formatName), formatName + " format must remain registered");

        expect (names.contains ("APEX Native"), "APEX Native must be registered");
        int apexNativeCount = 0;
        for (const auto& n : names)
            if (n == "APEX Native")
                ++apexNativeCount;
        expectEquals (apexNativeCount, 1, "APEX Native registered exactly once");
        expect (formats.findFormatByName ("APEX Native") != nullptr, "APEX Native findable by name");

        // Instantiate through the real AudioPluginFormatManager.
        auto& fm = formats.getManager();
        juce::String error;
        auto created = fm.createPluginInstance (canonicalDescription(), 48000.0, 512, error);
        expect (created != nullptr, "manager created the G10 instance: " + error);
        expect (dynamic_cast<G10Processor*> (created.get()) != nullptr, "manager instance is G10Processor");
    }

    // ------------------------------------------------------------------

    void testCacheAndBrowser()
    {
        beginTest ("Cache and browser seeding");

        // Direct cache seeding: repeated seeding must not duplicate.
        {
            DAW::PluginCacheCore cache;
            cache.cacheFromDescription (canonicalDescription());
            expectEquals (cache.getCount(), 1, "first seed inserts one entry");
            cache.cacheFromDescription (canonicalDescription());
            expectEquals (cache.getCount(), 1, "repeated seed must not duplicate");
            expect (cache.hasCachedEntry ("APEX::G10"), "G10 cached under its identifier");
        }

        // Browser feed rebuild from a cache containing G10.
        {
            DAW::PluginCacheCore cache;
            cache.cacheFromDescription (canonicalDescription());
            juce::KnownPluginList known;
            DAW::PluginBrowserFeedCore feed;
            feed.rebuildFromCache (cache, known);
            expectEquals (known.getNumTypes(), 1, "browser feed contains exactly the G10 type");
            if (known.getNumTypes() == 1)
                expectEquals (known.getTypes()[0].name, juce::String ("APEX G10"), "browser feed type name");
        }

        // Startup flow: the real PluginScannerCore seeds every intrinsic APEX
        // Native family member into the cache exactly once and preserves
        // existing entries.
        {
            DAW::PluginScannerCore scanner;
            expect (scanner.getCache().hasCachedEntry ("APEX::G10"), "scanner seeded G10 into the cache");
            expect (scanner.getCache().hasCachedEntry ("APEX::C4"), "scanner seeded C4 into the cache");
            expect (scanner.getCache().hasCachedEntry ("APEX::ParametricEQ"),
                    "scanner seeded Parametric EQ into the cache");
            int apexNativeCount = 0;
            for (const auto& c : scanner.getCache().getAll())
                if (c.format.equalsIgnoreCase ("APEX Native"))
                    ++apexNativeCount;
            expectEquals (apexNativeCount, 3,
                          "exactly three APEX Native entries after startup");
            expect (scanner.getCache().getCount() >= 3, "existing cache entries preserved");
        }
    }

    // ------------------------------------------------------------------

    void testChainIntegration()
    {
        beginTest ("Chain integration");

        DAW::PluginScanFormatsCore formats;
        auto& manager = formats.getManager();

        // Production creation path: loadPlugin through the format manager.
        {
            DAW::PluginChainCore chain;
            chain.prepare (48000.0, 128);

            juce::String error;
            const bool ok = chain.loadPlugin (0, canonicalDescription(), manager, error);
            expect (ok, "loadPlugin failed: " + error);
            expectEquals (chain.getNumActiveSlots(), 1, "one active slot");
            auto* slot = chain.getSlot (0);
            expect (slot != nullptr, "slot exists");
            auto* proc = slot != nullptr ? slot->getProcessor() : nullptr;
            expect (dynamic_cast<G10Processor*> (proc) != nullptr, "slot processor is G10Processor");

            // Audio passes through the chain.
            juce::AudioBuffer<float> buf (2, 128);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    buf.setSample (ch, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0)));
            chain.processBlock (buf, 128);
            float rms = 0.0f;
            bool finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) finite = false;
                    rms += v * v;
                }
            rms = std::sqrt (rms / 256.0f);
            expect (finite, "chain output finite");
            expect (rms > 0.1f, "audio passes through the chain, RMS=" + juce::String (rms, 3));

            // Description persistence through chain state.
            const auto chainState = chain.getState();
            expect (chainState.hasType ("PluginChain"), "chain state type");
            expectEquals (chainState.getNumChildren(), 1, "one slot in chain state");
            if (chainState.getNumChildren() == 1)
            {
                const auto slotTree = chainState.getChild (0);
                expect (slotTree.hasType ("Slot"), "slot state type");
                const auto descTree = slotTree.getChildWithName ("Description");
                expect (descTree.isValid(), "slot state contains a Description");
                if (descTree.isValid())
                {
                    const auto persisted = DAW::PluginDescriptionPersistenceCore::fromValueTree (descTree);
                    expectEquals (persisted.name, juce::String ("APEX G10"), "persisted description name");
                    expectEquals (persisted.pluginFormatName, juce::String ("APEX Native"), "persisted format name");
                }
            }

            // Parameter state round-trip through chain save/restore.
            setDb (G10Test::findParam (*proc, "g10.band31"), 6.0f);
            const auto savedState = chain.getState();

            DAW::PluginChainCore restored;
            restored.prepare (48000.0, 128);
            restored.restoreState (savedState, formats.getManager());
            expectEquals (restored.getNumActiveSlots(), 1, "restored slot count");
            auto* restoredProc = restored.getSlot (0) != nullptr ? restored.getSlot (0)->getProcessor() : nullptr;
            expect (dynamic_cast<G10Processor*> (restoredProc) != nullptr, "restored processor is G10Processor");
            if (restoredProc != nullptr)
                expectWithinAbsoluteError (G10Test::findParam (*restoredProc, "g10.band31")->getUnitsValue(), 6.0f, 1.0e-3f,
                                      "band31 restored through chain state");
        }

        // Test seam: appendPluginInstanceForTesting.
        {
            DAW::PluginChainCore chain;
            chain.prepare (48000.0, 128);
            auto instance = std::make_unique<G10Processor>();
            const int idx = chain.appendPluginInstanceForTesting (std::move (instance));
            expectEquals (idx, 0, "test seam slot index");
            auto* slot = chain.getSlot (idx);
            expect (slot != nullptr, "test seam slot exists");
            expect (dynamic_cast<G10Processor*> (slot != nullptr ? slot->getProcessor() : nullptr) != nullptr,
                    "test seam processor is G10Processor");
        }
    }

    // ------------------------------------------------------------------

    void testAutomationBinding()
    {
        beginTest ("Automation binding through AudioProcessorParameterWithID");

        DAW::PluginScanFormatsCore formats;
        DAW::PluginChainCore chain;
        chain.prepare (48000.0, 128);
        chain.setAutomationContext ("g10testtrack", nullptr, nullptr);

        juce::String error;
        const bool ok = chain.loadPlugin (0, canonicalDescription(), formats.getManager(), error);
        expect (ok, "loadPlugin failed: " + error);

        auto* slot = chain.getSlot (0);
        auto* proc = slot != nullptr ? slot->getProcessor() : nullptr;
        expect (proc != nullptr, "slot processor present");

        using KR = apex::automation::AutomationParameterKeyRegistry;
        auto& sys = apex::automation::AutomationSystem::getInstance();

        const char* expected[kNumParams] =
        {
            "g10.input",
            "g10.band31", "g10.band63", "g10.band125", "g10.band250",
            "g10.band500", "g10.band1k", "g10.band2k", "g10.band4k",
            "g10.band8k", "g10.band16k",
            "g10.output",
            "g10.analog",
            "g10.quality",
            "g10.bypass",
            "g10.hpf",
            "g10.lpf",
            "g10.bell1.enabled", "g10.bell1.freq", "g10.bell1.gain", "g10.bell1.q",
            "g10.bell2.enabled", "g10.bell2.freq", "g10.bell2.gain", "g10.bell2.q",
            "g10.bell3.enabled", "g10.bell3.freq", "g10.bell3.gain", "g10.bell3.q",
            "g10.bell1.bypass", "g10.bell2.bypass", "g10.bell3.bypass",
        };

        int bound = 0;
        for (int i = 0; i < kNumParams; ++i)
        {
            auto* p = proc->getParameters()[i];
            auto* pwid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
            expect (pwid != nullptr, "chain-loaded parameter " + juce::String (i) + " not castable");
            if (pwid == nullptr)
                continue;
            expectEquals (pwid->paramID, juce::String (expected[i]), "stable automation ID at index " + juce::String (i));

            const auto key = KR::pluginParamKey ("g10testtrack", 0, "APEX G10", pwid->paramID);
            const auto id = KR::getInstance().findID (key);
            expect (id != apex::automation::kInvalidParameterID,
                    "no automation key for " + pwid->paramID);
            if (id == apex::automation::kInvalidParameterID)
                continue;
            expect (sys.getRegistry().find (id) != nullptr,
                    "no automation parameter registered for " + pwid->paramID);
            ++bound;
        }
        expectEquals (bound, (int) kNumParams, "all parameters bound to the automation system");
    }
};

static G10FormatHostTests g10FormatHostTests;
