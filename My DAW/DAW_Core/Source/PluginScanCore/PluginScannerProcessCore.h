#pragma once
#include <JuceHeader.h>

namespace DAW {

class PluginScannerProcessCore
{
public:
    static juce::String buildCommandLine(const juce::File& exe,
                                         const juce::File& pluginFile,
                                         const juce::String& format)
    {
        return "\"" + exe.getFullPathName() + "\""
            + " --scan-plugin \"" + pluginFile.getFullPathName() + "\""
            + " --format " + format;
    }
};

} // namespace DAW
