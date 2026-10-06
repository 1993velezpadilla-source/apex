#pragma once
#include <JuceHeader.h>

namespace DAW {

class PluginSecurityPolicyFailureCore
{
public:
    static bool isSecurityPolicyFailure(const juce::String& text)
    {
        auto s = text.toLowerCase();
        return s.contains("application control policy has blocked this file")
            || s.contains("blocked this file")
            || s.contains("smart app control")
            || s.contains("wdac")
            || s.contains("tls initialization failed")
            || s.contains("0xc0e90002");
    }

    static bool isBadImagePolicyFailure(const juce::String& text)
    {
        auto s = text.toLowerCase();
        return s.contains("bad image") && (s.contains("blocked") || s.contains("tls"));
    }
};

} // namespace DAW
