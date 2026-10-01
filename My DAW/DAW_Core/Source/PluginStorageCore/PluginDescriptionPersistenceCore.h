#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * Persists JUCE plugin-class identity without conflating the canonical JUCE 8
 * uniqueId with the legacy, platform-dependent deprecatedUid.
 */
class PluginDescriptionPersistenceCore
{
public:
    static constexpr int currentIdentitySchemaVersion = 2;

    static juce::ValueTree toValueTree(const juce::PluginDescription& desc)
    {
        juce::ValueTree tree("Description");
        tree.setProperty("identitySchemaVersion", currentIdentitySchemaVersion, nullptr);
        tree.setProperty("name", desc.name, nullptr);
        tree.setProperty("descriptiveName", desc.descriptiveName, nullptr);
        tree.setProperty("pluginFormatName", desc.pluginFormatName, nullptr);
        tree.setProperty("category", desc.category, nullptr);
        tree.setProperty("manufacturerName", desc.manufacturerName, nullptr);
        tree.setProperty("version", desc.version, nullptr);
        tree.setProperty("fileOrIdentifier", desc.fileOrIdentifier, nullptr);
        tree.setProperty("lastFileModTime", (juce::int64) desc.lastFileModTime.toMilliseconds(), nullptr);
        tree.setProperty("lastInfoUpdateTime", (juce::int64) desc.lastInfoUpdateTime.toMilliseconds(), nullptr);
        tree.setProperty("uniqueId", desc.uniqueId, nullptr);
        tree.setProperty("deprecatedUid", desc.deprecatedUid, nullptr);
        tree.setProperty("isInstrument", desc.isInstrument, nullptr);
        tree.setProperty("numInputChannels", desc.numInputChannels, nullptr);
        tree.setProperty("numOutputChannels", desc.numOutputChannels, nullptr);
        tree.setProperty("hasSharedContainer", desc.hasSharedContainer, nullptr);
        return tree;
    }

    static juce::PluginDescription fromValueTree(const juce::ValueTree& tree)
    {
        juce::PluginDescription desc;
        desc.name = tree.getProperty("name", "");
        desc.descriptiveName = tree.getProperty("descriptiveName", "");
        desc.pluginFormatName = tree.getProperty("pluginFormatName", "");
        desc.category = tree.getProperty("category", "");
        desc.manufacturerName = tree.getProperty("manufacturerName", "");
        desc.version = tree.getProperty("version", "");
        desc.fileOrIdentifier = tree.getProperty("fileOrIdentifier", "");
        desc.lastFileModTime = juce::Time((juce::int64) tree.getProperty("lastFileModTime", 0));
        desc.lastInfoUpdateTime = juce::Time((juce::int64) tree.getProperty("lastInfoUpdateTime", 0));

        const int identitySchemaVersion = (int) tree.getProperty("identitySchemaVersion", 1);
        if (identitySchemaVersion >= currentIdentitySchemaVersion || tree.hasProperty("deprecatedUid"))
        {
            desc.uniqueId = (int) tree.getProperty("uniqueId", 0);
            desc.deprecatedUid = (int) tree.getProperty("deprecatedUid", 0);
        }
        else
        {
            // APEX project versions <= 6 stored JUCE's deprecatedUid in the
            // property named "uniqueId". Preserve that interpretation.
            desc.uniqueId = 0;
            desc.deprecatedUid = (int) tree.getProperty("uniqueId", 0);
        }

        desc.isInstrument = (bool) tree.getProperty("isInstrument", false);
        desc.numInputChannels = (int) tree.getProperty("numInputChannels", 2);
        desc.numOutputChannels = (int) tree.getProperty("numOutputChannels", 2);
        desc.hasSharedContainer = (bool) tree.getProperty("hasSharedContainer", false);
        return desc;
    }
};

} // namespace DAW
