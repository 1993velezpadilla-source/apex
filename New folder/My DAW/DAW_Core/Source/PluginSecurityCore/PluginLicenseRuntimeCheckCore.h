#pragma once
#include <JuceHeader.h>
#include "PluginProtectionVendorDetectionCore.h"

namespace DAW {

class PluginLicenseRuntimeCheckCore
{
public:
    struct CheckResult
    {
        juce::String protectionVendor;
        bool runtimeLikelyNeeded = false;
        bool runtimeLikelyBlocked = false;
    };

    static CheckResult inspect(const juce::String& pluginPathOrError)
    {
        CheckResult result;
        result.protectionVendor = PluginProtectionVendorDetectionCore::detectVendor(pluginPathOrError);
        result.runtimeLikelyNeeded = result.protectionVendor.isNotEmpty();
        auto s = pluginPathOrError.toLowerCase();
        result.runtimeLikelyBlocked = s.contains("blocked") || s.contains("tls initialization failed");
        return result;
    }
};

} // namespace DAW
