#pragma once
#include <JuceHeader.h>
#include "../PluginSafetyCore/PluginGracefulFailCore.h"
#include "../PluginSecurityCore/HostBuildTrustModeCore.h"
#include "../BuildTrustCore/TrustedInstallPathPolicyCore.h"

namespace DAW {

class PluginUserFacingFailureMessageCore
{
public:
    static juce::String makeMessage(const PluginLoadFailureInfo& info)
    {
        juce::String msg;
        if (info.category == PluginLoadFailureCategory::CopyProtectionRuntimeBlocked
            || info.category == PluginLoadFailureCategory::SecurityPolicyBlocked)
        {
            msg = "Plugin failed to load because Windows blocked one of its required runtime/protection DLLs.";
        }
        else if (info.category == PluginLoadFailureCategory::RuntimeDependencyFailure)
        {
            msg = "Plugin failed to load because a required dependency/runtime component could not be loaded.";
        }
        else
        {
            msg = "Plugin failed to load.";
        }

        auto trustMode = HostBuildTrustModeCore::detectCurrentMode();
        if (trustMode == HostBuildTrustMode::DebugUnsignedLocal || trustMode == HostBuildTrustMode::ReleaseUnsigned)
            msg += " This current host build is likely not trusted enough by Windows policy. " + TrustedInstallPathPolicyCore::getRecommendation();
        return msg;
    }
};

} // namespace DAW
