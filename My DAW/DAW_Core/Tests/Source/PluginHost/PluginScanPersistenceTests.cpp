// PluginScanPersistenceTests.cpp
//
// BUG B regressions — the plugin scan registry is APPLICATION-GLOBAL state.
//
// Live defect recap:
//   * "Create New Project" cleared the global plugin registry, and the empty
//     list was then persisted over the valid plugin database.
//   * A full rescan exited mid-scan could save an empty cache over valid data.
//   * Cache saves were non-atomic and cache loads destructive on parse error.
//
// These tests drive PluginCacheCore directly (pure persistence core, no
// third-party plugins required) and prove the corrected contracts:
//   - successful scans round-trip durably through the canonical file,
//   - saves are atomic (temp + rename),
//   - a corrupt file never wipes the runtime registry,
//   - an empty in-memory list can never overwrite a valid on-disk database,
//   - the KnownPluginList rebuilt from the cache matches identity/count.

#include <JuceHeader.h>
#include "../../../Source/PluginStorageCore/PluginCacheCore.h"

#include <cstdio>

namespace
{
juce::PluginDescription makeFakeDescription(const juce::String& name, int uid)
{
    juce::PluginDescription d;
    d.name = name;
    d.descriptiveName = name;
    d.pluginFormatName = "APEX Test";
    d.manufacturerName = "APEX";
    d.category = "Dynamics";
    d.version = "1.0";
    d.fileOrIdentifier = "C:/FakePlugins/" + name + ".vst3";
    d.uniqueId = uid;
    d.deprecatedUid = uid;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
    return d;
}

juce::File makeTempCacheFile(const juce::String& tag)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("apex_test_plugin_cache_" + tag + ".xml");
}

// KnownPluginList sorts its types — assert identity by UID, not position.
bool hasTypeNamed(const juce::KnownPluginList& list, int uid, const juce::String& expectedName)
{
    for (const auto& t : list.getTypes())
        if (t.uniqueId == uid)
            return t.name == expectedName;
    return false;
}

class PluginScanRegistryReloadRoundTripTest final : public juce::UnitTest
{
public:
    PluginScanRegistryReloadRoundTripTest()
        : UnitTest("PluginScan.RegistryReloadRoundTrip", "PluginScan") {}

    void runTest() override
    {
        beginTest("plugin registry round-trips durably through the canonical file");

        const auto f = makeTempCacheFile("roundtrip");
        f.deleteFile();

        DAW::PluginCacheCore writer;
        writer.cacheFromDescription(makeFakeDescription("Compressor A", 1111));
        writer.cacheFromDescription(makeFakeDescription("Reverb B", 2222));
        writer.cacheFromDescription(makeFakeDescription("EQ C", 3333));
        expectEquals(writer.getCount(), 3);
        expect(writer.saveToFile(f), "save must succeed");

        DAW::PluginCacheCore reader;
        reader.loadFromFile(f);
        expectEquals(reader.getCount(), 3, "reload must preserve every entry");

        juce::KnownPluginList list;
        reader.populateKnownList(list);
        expectEquals(list.getNumTypes(), 3);
        expect(hasTypeNamed(list, 1111, juce::String("Compressor A")), "Compressor A identity must survive reload");
        expect(hasTypeNamed(list, 2222, juce::String("Reverb B")), "Reverb B identity must survive reload");
        expect(hasTypeNamed(list, 3333, juce::String("EQ C")), "EQ C identity must survive reload");

        f.deleteFile();
    }
};

PluginScanRegistryReloadRoundTripTest pluginScanRegistryReloadRoundTripTest;

class PluginScanAtomicSaveRoundTripTest final : public juce::UnitTest
{
public:
    PluginScanAtomicSaveRoundTripTest()
        : UnitTest("PluginScan.AtomicSaveRoundTrip", "PluginScan") {}

    void runTest() override
    {
        beginTest("save is atomic: no temp leftovers and a parseable canonical file");

        const auto f = makeTempCacheFile("atomic");
        const auto tmp = f.getSiblingFile(f.getFileName() + ".tmp");
        f.deleteFile();
        tmp.deleteFile();

        DAW::PluginCacheCore cache;
        cache.cacheFromDescription(makeFakeDescription("Atomic Test", 7777));
        expect(cache.saveToFile(f));

        expect(f.existsAsFile(), "canonical file must exist after save");
        expect(! tmp.existsAsFile(), "temporary file must not remain after a successful save");
        expect(juce::XmlDocument::parse(f) != nullptr, "canonical file must be valid XML");

        f.deleteFile();
        tmp.deleteFile();
    }
};

PluginScanAtomicSaveRoundTripTest pluginScanAtomicSaveRoundTripTest;

class PluginScanCorruptFileDoesNotClearRuntimeCacheTest final : public juce::UnitTest
{
public:
    PluginScanCorruptFileDoesNotClearRuntimeCacheTest()
        : UnitTest("PluginScan.CorruptFileDoesNotClearRuntimeCache", "PluginScan") {}

    void runTest() override
    {
        beginTest("a corrupt database file never wipes the runtime registry");

        const auto f = makeTempCacheFile("corrupt");
        f.deleteFile();

        DAW::PluginCacheCore cache;
        cache.cacheFromDescription(makeFakeDescription("Keep Me", 5555));
        expectEquals(cache.getCount(), 1);

        // Overwrite the canonical file with garbage.
        f.replaceWithText("<PluginCache><broken>");

        cache.loadFromFile(f);
        expectEquals(cache.getCount(), 1,
                     "corrupt parse must leave the runtime registry untouched");

        f.deleteFile();
    }
};

PluginScanCorruptFileDoesNotClearRuntimeCacheTest pluginScanCorruptFileDoesNotClearRuntimeCacheTest;

class PluginScanEmptyCacheDoesNotOverwriteValidFileTest final : public juce::UnitTest
{
public:
    PluginScanEmptyCacheDoesNotOverwriteValidFileTest()
        : UnitTest("PluginScan.EmptyCacheDoesNotOverwriteValidFile", "PluginScan") {}

    void runTest() override
    {
        beginTest("an empty in-memory list can never overwrite a valid on-disk database");

        const auto f = makeTempCacheFile("nooverwrite");
        f.deleteFile();

        DAW::PluginCacheCore writer;
        writer.cacheFromDescription(makeFakeDescription("Precious Plugin", 9999));
        expect(writer.saveToFile(f));

        // An accidentally-wiped registry (the live New Project bug) must not
        // persist over the valid database.
        DAW::PluginCacheCore wiped;
        expectEquals(wiped.getCount(), 0);
        expect(! wiped.saveToFile(f),
               "saveToFile must refuse to overwrite a valid database with an empty list");

        DAW::PluginCacheCore reader;
        reader.loadFromFile(f);
        expectEquals(reader.getCount(), 1, "the valid on-disk database must survive");
        juce::KnownPluginList list;
        reader.populateKnownList(list);
        expectEquals(list.getNumTypes(), 1);
        expectEquals(list.getType(0)->uniqueId, 9999);

        f.deleteFile();
    }
};

PluginScanEmptyCacheDoesNotOverwriteValidFileTest pluginScanEmptyCacheDoesNotOverwriteValidFileTest;

class PluginScanKnownPluginListIdentityRoundTripTest final : public juce::UnitTest
{
public:
    PluginScanKnownPluginListIdentityRoundTripTest()
        : UnitTest("PluginScan.KnownPluginListIdentityRoundTrip", "PluginScan") {}

    void runTest() override
    {
        beginTest("KnownPluginList rebuilt from cache preserves identity, not just count");

        const auto f = makeTempCacheFile("identity");
        f.deleteFile();

        DAW::PluginCacheCore writer;
        writer.cacheFromDescription(makeFakeDescription("Shell Plugin A", 1001));
        writer.cacheFromDescription(makeFakeDescription("Shell Plugin B", 1002));
        expect(writer.saveToFile(f));

        DAW::PluginCacheCore reader;
        reader.loadFromFile(f);
        juce::KnownPluginList list;
        reader.populateKnownList(list);

        expectEquals(list.getNumTypes(), 2);
        expect(hasTypeNamed(list, 1001, juce::String("Shell Plugin A")), "Shell Plugin A identity must survive reload");
        expect(hasTypeNamed(list, 1002, juce::String("Shell Plugin B")), "Shell Plugin B identity must survive reload");

        f.deleteFile();
    }
};

PluginScanKnownPluginListIdentityRoundTripTest pluginScanKnownPluginListIdentityRoundTripTest;

} // namespace
