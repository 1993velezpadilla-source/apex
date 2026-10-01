#pragma once
#include <JuceHeader.h>

namespace DAW {

/** Evidence-backed in-process plugin quarantine policy.
 *
 *  This policy is intentionally narrow: product, manufacturer, and format
 *  must all match a runtime-proven fatal path. It is evaluated before vendor
 *  code is entered, because SEH cannot repair latent heap corruption.
 */
struct PluginQuarantineCore
{
    static bool isAntaresVst3(const juce::String& manufacturerName,
                              const juce::String& pluginFormatName) noexcept
    {
        return manufacturerName.containsIgnoreCase("Antares")
            && pluginFormatName.containsIgnoreCase("VST3");
    }

    static bool blocksInProcessInstantiation(const juce::String& pluginName,
                                             const juce::String& manufacturerName,
                                             const juce::String& pluginFormatName) noexcept
    {
        return isAntaresVst3(manufacturerName, pluginFormatName)
            && pluginName.containsIgnoreCase("Auto-Tune EFX");
    }

    static bool blocksNativeEditor(const juce::String& pluginName,
                                   const juce::String& manufacturerName,
                                   const juce::String& pluginFormatName) noexcept
    {
        if (!isAntaresVst3(manufacturerName, pluginFormatName))
            return false;

        return pluginName.containsIgnoreCase("Auto-Tune EFX")
            || pluginName.containsIgnoreCase("Auto-Tune Vocal EQ");
    }
};

} // namespace DAW
