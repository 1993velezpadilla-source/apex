#pragma once
#include <JuceHeader.h>
#include "../PluginHostCore/ApexNativePluginFormat.h"

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
        // APEX native plugins are intrinsic to the host: no binary, no
        // filesystem scanning, no installation. Registered after the external
        // formats so existing VST3/VST ordering is preserved.
        //
        // Phase 8 fix: the format manager keys formats by NAME and rejects a
        // second "APEX Native" format object (G10 and C4 share the family
        // name), so the C4 format was silently dropped and could never
        // instantiate through the manager. ONE unified family format is
        // registered; it contains both G10 and C4 (see ApexNativePluginFormat).
        formatManager_.addFormat(
            std::make_unique<DAW::ApexNativePluginFormat>());
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
