#pragma once
#include <JuceHeader.h>
#include "G10Processor.h"

namespace APEX {
namespace G10 {

// ============================================================================
// G10NativePluginFormat — the "APEX Native" plugin format.
//
// A JUCE 8.0.12 AudioPluginFormat that recognizes exactly one built-in
// plugin: APEX G10. There is no external binary, no filesystem scanning,
// no installation step, and no internet dependency — the plugin is intrinsic
// to the APEX host.
//
// Contract:
//   - createPluginInstance() is synchronous and invokes the callback on
//     EVERY path (success and failure). The JUCE 8.0.12
//     AudioPluginFormatManager::createPluginInstance() sync path waits on
//     the callback; never returning it would hang the host.
//   - canScanForPlugins() == false  -> the scan coordinator never probes it.
//   - isTrivialToScan()   == true   -> safe/instant to "scan".
//   - doesPluginStillExist() == true only for the canonical G10 description.
//   - No filesystem scanning (searchPathsForPlugins / getDefaultLocationsToSearch
//     return empty results).
// ============================================================================

class G10NativePluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* kFormatName = "APEX Native";

    /** The canonical PluginDescription for APEX G10. Must exactly match
        G10Processor::fillInPluginDescription(). */
    static juce::PluginDescription createG10Description()
    {
        juce::PluginDescription desc;
        desc.name = G10Processor::kPluginName;
        desc.descriptiveName = G10Processor::kPluginName;
        desc.pluginFormatName = kFormatName;
        desc.category = G10Processor::kCategory;
        desc.manufacturerName = G10Processor::kManufacturer;
        desc.version = G10Processor::kVersion;
        desc.fileOrIdentifier = G10Processor::kFileOrIdentifier;
        desc.uniqueId = G10Processor::kUniqueId;
        desc.deprecatedUid = G10Processor::kUniqueId;
        desc.isInstrument = false;
        desc.numInputChannels = 2;
        desc.numOutputChannels = 2;
        return desc;
    }

    /** True if the description is the canonical APEX G10 native plugin. */
    static bool isG10Description (const juce::PluginDescription& desc) noexcept
    {
        return desc.uniqueId == G10Processor::kUniqueId
            && desc.pluginFormatName.equalsIgnoreCase (kFormatName)
            && desc.fileOrIdentifier == G10Processor::kFileOrIdentifier;
    }

    // ---- AudioPluginFormat (pure virtuals) --------------------------------

    juce::String getName() const override { return kFormatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                              const juce::String&) override
    {
        // Native plugins have no external files; the type is intrinsic.
        results.add (new juce::PluginDescription (createG10Description()));
    }

    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override
    {
        // JUCE 8.0.12 AudioPluginFormatManager::findFormatForDescription()
        // requires BOTH getName() == pluginFormatName AND
        // fileMightContainThisPluginType(fileOrIdentifier) to match a
        // description to a format. Returning false unconditionally made the
        // manager report "No compatible plug-in format exists for this
        // plug-in" for the canonical G10 description. The intrinsic G10
        // identifier is the only identifier this format can contain; an empty
        // identifier is accepted for descriptions built without one.
        return fileOrIdentifier.isEmpty()
            || fileOrIdentifier == G10Processor::kFileOrIdentifier;
    }

    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override
    {
        return fileOrIdentifier == G10Processor::kFileOrIdentifier
            ? G10Processor::kPluginName
            : juce::String();
    }

    bool pluginNeedsRescanning (const juce::PluginDescription&) override
    {
        return false; // intrinsic; never stale
    }

    bool doesPluginStillExist (const juce::PluginDescription& desc) override
    {
        return isG10Description (desc);
    }

    bool canScanForPlugins() const override { return false; }

    bool isTrivialToScan() const override { return true; }

    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override
    {
        return {};
    }

    juce::FileSearchPath getDefaultLocationsToSearch() override
    {
        return {};
    }

    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override
    {
        return false; // synchronous, message-thread safe
    }

protected:
    // ---- Creation (called by AudioPluginFormatManager) ----------------------

    void createPluginInstance (const juce::PluginDescription& description,
                               double initialSampleRate,
                               int initialBufferSize,
                               PluginCreationCallback callback) override
    {
        if (callback == nullptr)
            return;

        if (! isG10Description (description))
        {
            callback (nullptr,
                      "Not an APEX G10 native plugin (format="
                          + description.pluginFormatName
                          + ", uniqueId=" + juce::String (description.uniqueId) + ").");
            return;
        }

        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;

        try
        {
            instance = std::make_unique<G10Processor>();
            instance->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
        }
        catch (...)
        {
            instance.reset();
            error = "APEX G10 native plugin creation failed.";
        }

        callback (std::move (instance), error);
    }
};

} // namespace G10
} // namespace APEX