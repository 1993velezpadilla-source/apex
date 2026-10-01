#include <JuceHeader.h>
#include "../../../Source/PluginScanCore/PluginScanResultCore.h"
#include "../../../Source/PluginStorageCore/PluginCacheCore.h"
#include "../../../Source/PluginStorageCore/PluginDescriptionPersistenceCore.h"

class PluginIdentityPersistenceTests final : public juce::UnitTest
{
public:
    PluginIdentityPersistenceTests()
        : juce::UnitTest("plugin.identity.persistence.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest("scan IPC round-trip preserves both JUCE identities");
        {
            DAW::PluginScanResult result;
            result.uniqueId = "-19464422";
            result.deprecatedUid = "-1457048775";

            DAW::PluginScannedDescriptionRecord record;
            record.path = "fixture.vst3";
            record.uniqueId = result.uniqueId;
            record.deprecatedUid = result.deprecatedUid;
            result.producedDescriptions.push_back(record);

            const auto restored = DAW::PluginScanResultCore::fromVar(
                DAW::PluginScanResultCore::toVar(result));
            expectEquals(restored.uniqueId, result.uniqueId);
            expectEquals(restored.deprecatedUid, result.deprecatedUid);
            expectEquals((int) restored.producedDescriptions.size(), 1);
            expectEquals(restored.producedDescriptions.front().uniqueId, result.uniqueId);
            expectEquals(restored.producedDescriptions.front().deprecatedUid, result.deprecatedUid);
        }

        beginTest("cache XML and KnownPluginList preserve both identities");
        {
            DAW::PluginCacheCore cache;
            DAW::PluginCacheCore::CachedPlugin entry;
            entry.path = "fixture.vst3";
            entry.format = "VST3";
            entry.name = "Identity Fixture";
            entry.uniqueId = "-19464422";
            entry.deprecatedUid = "-1457048775";
            cache.addOrUpdate(entry);

            const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getNonexistentChildFile("apex-plugin-identity", ".xml", false);
            cache.saveToFile(file);

            DAW::PluginCacheCore loaded;
            loaded.loadFromFile(file);
            file.deleteFile();
            expectEquals(loaded.getCount(), 1);
            expectEquals(loaded.getAll().front().uniqueId, entry.uniqueId);
            expectEquals(loaded.getAll().front().deprecatedUid, entry.deprecatedUid);

            juce::KnownPluginList known;
            loaded.populateKnownList(known);
            const auto types = known.getTypes();
            expectEquals(types.size(), 1);
            expectEquals(types.getReference(0).uniqueId, -19464422);
            expectEquals(types.getReference(0).deprecatedUid, -1457048775);
        }

        beginTest("invalid cached identity is not fabricated with a string hash");
        {
            DAW::PluginCacheCore cache;
            DAW::PluginCacheCore::CachedPlugin entry;
            entry.path = "invalid.vst3";
            entry.format = "VST3";
            entry.name = "Invalid Identity";
            entry.uniqueId = "not-an-integer";
            cache.addOrUpdate(entry);
            expectEquals(cache.getCount(), 0);

            juce::KnownPluginList known;
            cache.populateKnownList(known);
            expectEquals(known.getNumTypes(), 0);
        }

        beginTest("modern project description preserves both identities");
        {
            juce::PluginDescription source;
            source.name = "Identity Fixture";
            source.uniqueId = -19464422;
            source.deprecatedUid = -1457048775;

            const auto tree = DAW::PluginDescriptionPersistenceCore::toValueTree(source);
            const auto restored = DAW::PluginDescriptionPersistenceCore::fromValueTree(tree);
            expectEquals(restored.uniqueId, source.uniqueId);
            expectEquals(restored.deprecatedUid, source.deprecatedUid);
            expectEquals((int) tree.getProperty("identitySchemaVersion"),
                         DAW::PluginDescriptionPersistenceCore::currentIdentitySchemaVersion);
        }

        beginTest("legacy version-6 description keeps old uniqueId meaning");
        {
            juce::ValueTree legacy("Description");
            legacy.setProperty("uniqueId", -1457048775, nullptr);

            const auto restored = DAW::PluginDescriptionPersistenceCore::fromValueTree(legacy);
            expectEquals(restored.uniqueId, 0);
            expectEquals(restored.deprecatedUid, -1457048775);
        }
    }
};

static PluginIdentityPersistenceTests pluginIdentityPersistenceTests;
