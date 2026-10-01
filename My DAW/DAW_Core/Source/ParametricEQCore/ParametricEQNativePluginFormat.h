#pragma once

#include <JuceHeader.h>

#include "ParametricEQProcessor.h"

namespace APEX::ParametricEQ
{

class NativePluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* kFormatName = "APEX Native";

    static juce::PluginDescription createDescription()
    {
        juce::PluginDescription description;
        description.name = Processor::kPluginName;
        description.descriptiveName = Processor::kPluginName;
        description.pluginFormatName = kFormatName;
        description.category = Processor::kCategory;
        description.manufacturerName = Processor::kManufacturer;
        description.version = Processor::kVersion;
        description.fileOrIdentifier = Processor::kFileOrIdentifier;
        description.uniqueId = Processor::kUniqueId;
        description.deprecatedUid = Processor::kUniqueId;
        description.isInstrument = false;
        description.numInputChannels = 2;
        description.numOutputChannels = 2;
        return description;
    }

    static bool isDescription (const juce::PluginDescription& description) noexcept
    {
        return description.uniqueId == Processor::kUniqueId
            && description.pluginFormatName.equalsIgnoreCase (kFormatName)
            && description.fileOrIdentifier == Processor::kFileOrIdentifier;
    }

    juce::String getName() const override { return kFormatName; }

    void findAllTypesForFile (
        juce::OwnedArray<juce::PluginDescription>& results,
        const juce::String&) override
    {
        results.add (new juce::PluginDescription (createDescription()));
    }

    bool fileMightContainThisPluginType (
        const juce::String& fileOrIdentifier) override
    {
        return fileOrIdentifier.isEmpty()
            || fileOrIdentifier == Processor::kFileOrIdentifier;
    }

    juce::String getNameOfPluginFromIdentifier (
        const juce::String& fileOrIdentifier) override
    {
        return fileOrIdentifier == Processor::kFileOrIdentifier
             ? Processor::kPluginName : juce::String {};
    }

    bool pluginNeedsRescanning (const juce::PluginDescription&) override
    {
        return false;
    }

    bool doesPluginStillExist (const juce::PluginDescription& description) override
    {
        return isDescription (description);
    }

    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }

    juce::StringArray searchPathsForPlugins (
        const juce::FileSearchPath&, bool, bool) override
    {
        return {};
    }

    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }

    bool requiresUnblockedMessageThreadDuringCreation (
        const juce::PluginDescription&) const override
    {
        return false;
    }

protected:
    void createPluginInstance (const juce::PluginDescription& description,
                               double initialSampleRate,
                               int initialBufferSize,
                               PluginCreationCallback callback) override
    {
        if (callback == nullptr)
            return;
        if (! isDescription (description))
        {
            callback (nullptr,
                      "Not an APEX Parametric EQ native plugin (identifier="
                          + description.fileOrIdentifier + ").");
            return;
        }

        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;
        try
        {
            instance = std::make_unique<Processor>();
            instance->setRateAndBufferSizeDetails (initialSampleRate,
                                                   initialBufferSize);
        }
        catch (...)
        {
            instance.reset();
            error = "APEX Parametric EQ native plugin creation failed.";
        }
        callback (std::move (instance), error);
    }
};

} // namespace APEX::ParametricEQ
