#pragma once
#include <JuceHeader.h>
#include "../PluginSafetyCore/PluginGracefulFailCore.h"

namespace DAW {

class PluginProtectionErrorSurfaceCore
{
public:
    static juce::String makeMessage(const PluginLoadFailureInfo& info)
    {
        if (info.category == PluginLoadFailureCategory::CopyProtectionRuntimeBlocked)
            return "Plugin load failed because its protection/runtime component was blocked or unavailable.";
        if (info.category == PluginLoadFailureCategory::SecurityPolicyBlocked)
            return "Plugin load failed because Windows blocked a required file or runtime dependency.";
        if (info.category == PluginLoadFailureCategory::RuntimeDependencyFailure)
            return "Plugin load failed because a required runtime dependency could not be loaded.";
        return "Plugin load failed.";
    }
};

} // namespace DAW
