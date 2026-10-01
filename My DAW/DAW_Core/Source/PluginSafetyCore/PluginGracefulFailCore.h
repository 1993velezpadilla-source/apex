#pragma once
#include <JuceHeader.h>

namespace DAW {

enum class PluginLoadFailureCategory
{
    None,
    SecurityPolicyBlocked,
    RuntimeDependencyFailure,
    CopyProtectionRuntimeBlocked,
    InvalidBinary,
    CrashDuringInitialisation,
    TimedOut,
    Unsupported,
    UnknownHostLoadFailure
};

struct PluginLoadFailureInfo
{
    PluginLoadFailureCategory category = PluginLoadFailureCategory::None;
    juce::String pluginPath;
    juce::String blockedDllPath;
    juce::String hostProcessName;
    juce::String loadStage;
    juce::String rawErrorText;
    juce::String notes;
    juce::String protectionVendor;
    bool isSecurityPolicyRelated = false;
    bool isRuntimeDependencyRelated = false;
    bool isLikelyTempDependencyPath = false;
};

class PluginGracefulFailCore
{
public:
    static PluginLoadFailureInfo makeFailure(PluginLoadFailureCategory category,
                                             const juce::String& pluginPath,
                                             const juce::String& rawErrorText,
                                             const juce::String& loadStage)
    {
        PluginLoadFailureInfo info;
        info.category = category;
        info.pluginPath = pluginPath;
        info.rawErrorText = rawErrorText;
        info.loadStage = loadStage;
        info.hostProcessName = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFileName();
        info.isSecurityPolicyRelated = category == PluginLoadFailureCategory::SecurityPolicyBlocked
            || category == PluginLoadFailureCategory::CopyProtectionRuntimeBlocked;
        info.isRuntimeDependencyRelated = category == PluginLoadFailureCategory::RuntimeDependencyFailure
            || category == PluginLoadFailureCategory::CopyProtectionRuntimeBlocked;
        return info;
    }
};

} // namespace DAW
