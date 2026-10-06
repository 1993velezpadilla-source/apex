#pragma once
#include <JuceHeader.h>

namespace DAW {

class PluginScanFormatsCore
{
public:
    PluginScanFormatsCore()
    {
#if JUCE_PLUGINHOST_VST3
        formatManager_.addFormat(new juce::VST3PluginFormat());
#endif
#if JUCE_PLUGINHOST_VST
        formatManager_.addFormat(new juce::VSTPluginFormat());
#endif
    }

    juce::AudioPluginFormatManager& getManager() { return formatManager_; }
    const juce::AudioPluginFormatManager& getManager() const { return formatManager_; }

    juce::StringArray getRegisteredFormatNames() const
    {
        juce::StringArray names;
        for (int i = 0; i < formatManager_.getNumFormats(); ++i)
            if (auto* format = formatManager_.getFormat(i))
                names.add(format->getName());
        return names;
    }

    juce::AudioPluginFormat* findFormatByName(const juce::String& name) const
    {
        for (int i = 0; i < formatManager_.getNumFormats(); ++i)
            if (auto* format = formatManager_.getFormat(i))
                if (format->getName().equalsIgnoreCase(name))
                    return format;
        return nullptr;
    }

    juce::String detectFormatForPath(const juce::File& candidate) const
    {
        auto fullPath = candidate.getFullPathName();
        for (int i = 0; i < formatManager_.getNumFormats(); ++i)
        {
            if (auto* format = formatManager_.getFormat(i))
                if (format->fileMightContainThisPluginType(fullPath))
                    return format->getName();
        }
        return {};
    }

private:
    juce::AudioPluginFormatManager formatManager_;
};

} // namespace DAW
