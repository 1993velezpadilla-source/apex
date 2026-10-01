#pragma once
#include <JuceHeader.h>
#include "../PluginSafetyCore/PluginGracefulFailCore.h"

namespace DAW {

class PluginBlockedDllLoggerCore
{
public:
    static void log(const PluginLoadFailureInfo& info)
    {
        auto file = getDefaultFile();
        file.getParentDirectory().createDirectory();

        juce::String line;
        line << juce::Time::getCurrentTime().toString(true, true)
             << " | plugin=" << info.pluginPath
             << " | blockedDll=" << info.blockedDllPath
             << " | host=" << info.hostProcessName
             << " | stage=" << info.loadStage
             << " | category=" << categoryToString(info.category)
             << " | vendor=" << info.protectionVendor
             << " | tempPath=" << (info.isLikelyTempDependencyPath ? "1" : "0")
             << " | error=" << info.rawErrorText << "\r\n";
        file.appendText(line);
    }

    static juce::File getDefaultFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core")
            .getChildFile("plugin_blocked_dll.log");
    }

private:
    static juce::String categoryToString(PluginLoadFailureCategory c)
    {
        switch (c)
        {
            case PluginLoadFailureCategory::SecurityPolicyBlocked: return "security_policy_blocked";
            case PluginLoadFailureCategory::RuntimeDependencyFailure: return "runtime_dependency_failure";
            case PluginLoadFailureCategory::CopyProtectionRuntimeBlocked: return "blocked_copy_protection_runtime";
            case PluginLoadFailureCategory::InvalidBinary: return "invalid_binary";
            case PluginLoadFailureCategory::CrashDuringInitialisation: return "crash_during_init";
            case PluginLoadFailureCategory::TimedOut: return "timed_out";
            case PluginLoadFailureCategory::Unsupported: return "unsupported";
            case PluginLoadFailureCategory::UnknownHostLoadFailure: return "unknown_host_load_failure";
            default: return "none";
        }
    }
};

} // namespace DAW
