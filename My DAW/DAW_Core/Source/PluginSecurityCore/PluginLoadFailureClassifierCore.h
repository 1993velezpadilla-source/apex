#pragma once
#include "../PluginSafetyCore/PluginGracefulFailCore.h"
#include "PluginSecurityPolicyFailureCore.h"
#include "PluginRuntimeDependencyFailureCore.h"
#include "PluginProtectionVendorDetectionCore.h"
#include "PluginDependencyPathAuditCore.h"

namespace DAW {

class PluginLoadFailureClassifierCore
{
public:
    static PluginLoadFailureInfo classify(const juce::String& pluginPath,
                                          const juce::String& rawErrorText,
                                          const juce::String& loadStage)
    {
        auto vendor = PluginProtectionVendorDetectionCore::detectVendor(rawErrorText + " " + pluginPath);
        PluginLoadFailureCategory category = PluginLoadFailureCategory::UnknownHostLoadFailure;

        if (PluginSecurityPolicyFailureCore::isSecurityPolicyFailure(rawErrorText)
            || PluginSecurityPolicyFailureCore::isBadImagePolicyFailure(rawErrorText))
        {
            category = vendor.isNotEmpty()
                ? PluginLoadFailureCategory::CopyProtectionRuntimeBlocked
                : PluginLoadFailureCategory::SecurityPolicyBlocked;
        }
        else if (PluginRuntimeDependencyFailureCore::isRuntimeDependencyFailure(rawErrorText))
        {
            category = vendor.isNotEmpty()
                ? PluginLoadFailureCategory::CopyProtectionRuntimeBlocked
                : PluginLoadFailureCategory::RuntimeDependencyFailure;
        }
        else if (rawErrorText.containsIgnoreCase("timeout"))
        {
            category = PluginLoadFailureCategory::TimedOut;
        }
        else if (rawErrorText.containsIgnoreCase("unsupported"))
        {
            category = PluginLoadFailureCategory::Unsupported;
        }
        else if (rawErrorText.containsIgnoreCase("invalid") || rawErrorText.containsIgnoreCase("not recognised"))
        {
            category = PluginLoadFailureCategory::InvalidBinary;
        }

        auto info = PluginGracefulFailCore::makeFailure(category, pluginPath, rawErrorText, loadStage);
        info.protectionVendor = vendor;
        info.blockedDllPath = PluginDependencyPathAuditCore::extractBlockedDllCandidate(rawErrorText);
        info.isLikelyTempDependencyPath = PluginDependencyPathAuditCore::isLikelyTempPath(info.blockedDllPath)
            || PluginDependencyPathAuditCore::isLikelyTempPath(rawErrorText);
        return info;
    }
};

} // namespace DAW
