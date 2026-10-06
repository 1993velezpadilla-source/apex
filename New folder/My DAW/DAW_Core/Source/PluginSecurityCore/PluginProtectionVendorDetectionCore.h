#pragma once
#include <JuceHeader.h>

namespace DAW {

class PluginProtectionVendorDetectionCore
{
public:
    static juce::String detectVendor(const juce::String& text)
    {
        auto s = text.toLowerCase();
        if (s.contains("wibu") || s.contains("codemeter") || s.contains("wibu-tls")) return "Wibu/CodeMeter";
        if (s.contains("ilok") || s.contains("pace")) return "PACE/iLok";
        if (s.contains("elicenser") || s.contains("synsopos")) return "eLicenser";
        return {};
    }
};

} // namespace DAW
