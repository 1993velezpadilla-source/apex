#pragma once
#include <JuceHeader.h>
#include "../PluginSafetyCore/PluginGracefulFailCore.h"

namespace DAW {

class PluginCrashIsolationCore
{
public:
    static PluginLoadFailureInfo classifySubprocessFailure(const juce::String& pluginPath,
                                                           int exitCode,
                                                           const juce::String& output)
    {
        auto info = PluginGracefulFailCore::makeFailure(
            PluginLoadFailureCategory::CrashDuringInitialisation,
            pluginPath,
            "Scanner subprocess exited with code " + juce::String(exitCode) + ". " + output,
            "scan_subprocess");
        return info;
    }
};

} // namespace DAW
