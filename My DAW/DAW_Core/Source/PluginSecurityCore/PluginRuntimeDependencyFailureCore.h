#pragma once
#include <JuceHeader.h>

namespace DAW {

class PluginRuntimeDependencyFailureCore
{
public:
    static bool isRuntimeDependencyFailure(const juce::String& text)
    {
        auto s = text.toLowerCase();
        return s.contains("failed to load this library")
            || s.contains("module could not be found")
            || s.contains("dll")
            || s.contains("bad image")
            || s.contains("entry point")
            || s.contains("side-by-side")
            || s.contains("tls initialization failed");
    }
};

} // namespace DAW
