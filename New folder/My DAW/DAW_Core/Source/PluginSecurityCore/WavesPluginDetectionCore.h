#pragma once
#include <JuceHeader.h>

namespace DAW {

class WavesPluginDetectionCore
{
public:
    static bool isWavesPlugin(const juce::String& pluginPath)
    {
        auto lower = pluginPath.toLowerCase();
        return lower.contains("waves") 
            || lower.contains("waveshell")
            || lower.contains("wavesvst");
    }

    static bool isWaveShell(const juce::String& pluginPath)
    {
        auto fileName = juce::File(pluginPath).getFileName().toLowerCase();
        return fileName.startsWith("waveshell") 
            || fileName.contains("waveshell");
    }

    static juce::String getWavesVendor() { return "Waves Audio"; }
};

} // namespace DAW
