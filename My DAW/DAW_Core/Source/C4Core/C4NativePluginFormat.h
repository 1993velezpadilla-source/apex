#pragma once
#include <JuceHeader.h>
#include "C4Processor.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4NativePluginFormat — the "APEX Native" plugin format entry for APEX C4.
//
// Identical architecture to G10NativePluginFormat (the validated APEX native
// plugin architecture): intrinsic to the host, no external binary, no
// filesystem scanning, no installation, synchronous creation, callback on
// every path.
// ============================================================================

class C4NativePluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* kFormatName = "APEX Native";

    /** The canonical PluginDescription for APEX C4. Must exactly match
        C4Processor::fillInPluginDescription(). */
    static juce::PluginDescription createC4Description()
    {
        juce::PluginDescription desc;
        desc.name = C4Processor::kPluginName;
        desc.descriptiveName = C4Processor::kPluginName;
        desc.pluginFormatName = kFormatName;
        desc.category = C4Processor::kCategory;
        desc.manufacturerName = C4Processor::kManufacturer;
        desc.version = C4Processor::kVersion;
        desc.fileOrIdentifier = C4Processor::kFileOrIdentifier;
        desc.uniqueId = C4Processor::kUniqueId;
        desc.deprecatedUid = C4Processor::kUniqueId;
        desc.isInstrument = false;
        desc.numInputChannels = 2;
        desc.numOutputChannels = 2;
        return desc;
    }

    /** True if the description is the canonical APEX C4 native plugin. */
    static bool isC4Description (const juce::PluginDescription& desc) noexcept
    {
        return desc.uniqueId == C4Processor::kUniqueId
            && desc.pluginFormatName.equalsIgnoreCase (kFormatName)
            && desc.fileOrIdentifier == C4Processor::kFileOrIdentifier;
    }

    // ---- AudioPluginFormat (pure virtuals) --------------------------------

    juce::String getName() const override { return kFormatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                              const juce::String&) override
    {
        results.add (new juce::PluginDescription (createC4Description()));
    }

    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override
    {
        // See G10NativePluginFormat: the format manager requires BOTH
        // getName() and fileMightContainThisPluginType() to match. The
        // intrinsic C4 identifier is the only identifier this format can
        // contain; an empty identifier is accepted for descriptions built
        // without one.
        return fileOrIdentifier.isEmpty()
            || fileOrIdentifier == C4Processor::kFileOrIdentifier;
    }

    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override
    {
        return fileOrIdentifier == C4Processor::kFileOrIdentifier
            ? C4Processor::kPluginName
            : juce::String();
    }

    bool pluginNeedsRescanning (const juce::PluginDescription&) override
    {
        return false; // intrinsic; never stale
    }

    bool doesPluginStillExist (const juce::PluginDescription& desc) override
    {
        return isC4Description (desc);
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
    void createPluginInstance (const juce::PluginDescription& description,
                               double initialSampleRate,
                               int initialBufferSize,
                               PluginCreationCallback callback) override
    {
        if (callback == nullptr)
            return;

        if (! isC4Description (description))
        {
            callback (nullptr,
                      "Not an APEX C4 native plugin (format="
                          + description.pluginFormatName
                          + ", uniqueId=" + juce::String (description.uniqueId) + ").");
            return;
        }

        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;

        try
        {
            instance = std::make_unique<C4Processor>();
            instance->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
        }
        catch (...)
        {
            instance.reset();
            error = "APEX C4 native plugin creation failed.";
        }

        callback (std::move (instance), error);
    }
};

} // namespace C4
} // namespace APEX
