#pragma once
#include <JuceHeader.h>
#include "PluginGracefulFailCore.h"
#include "PluginProtectionRuntimeGuardCore.h"
#include "PluginSehGuardCore.h"
#include "../PluginSecurityCore/PluginLoadFailureClassifierCore.h"
#include "../PluginSecurityCore/PluginBlockedDllLoggerCore.h"
#include "../PluginUICore/PluginUserFacingFailureMessageCore.h"

namespace DAW {

class PluginSafeLoadWrapperCore
{
public:
    struct Result
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        PluginLoadFailureInfo failure;
        juce::String userFacingMessage;
    };

    static Result createPluginInstance(juce::AudioPluginFormatManager& formatManager,
                                       const juce::PluginDescription& desc,
                                       double sampleRate,
                                       int blockSize,
                                       const juce::String& loadStage)
    {
        Result result;
        juce::String errorMsg;

        PluginProtectionRuntimeGuardCore guard;

        // Use the SEH-guarded function from PluginSehGuardCore.cpp (compiled with /EHa).
        // This catches BOTH C++ exceptions AND Windows SEH access violations (0xC0000005)
        // from misbehaving plugins like Antares Auto-Tune EFX+ that corrupt COM
        // interfaces during DLL loading.
        auto sehResult = safelyCreatePluginInstance(formatManager, desc,
                                                     sampleRate, blockSize, errorMsg);
        if (sehResult.succeeded)
        {
            result.instance = std::move(sehResult.instance);
            juce::Logger::writeToLog("[PLUGIN SAFETY] creation succeeded stage=" + loadStage
                + " name=\"" + desc.name + "\" format=" + desc.pluginFormatName
                + " uniqueId=" + juce::String(desc.uniqueId)
                + " deprecatedUid=" + juce::String(desc.deprecatedUid));
        }

        if (!result.instance)
        {
            if (errorMsg.isEmpty() && sehResult.exceptionCode != 0)
                errorMsg = "Plugin crashed during creation (SEH exception 0x"
                    + juce::String::toHexString((int)sehResult.exceptionCode) + ")";
            juce::Logger::writeToLog("[PLUGIN SAFETY] creation failed stage=" + loadStage
                + " name=\"" + desc.name + "\" format=" + desc.pluginFormatName
                + " rawError=\"" + errorMsg + "\"");
            result.failure = PluginLoadFailureClassifierCore::classify(desc.fileOrIdentifier, errorMsg, loadStage);
            result.userFacingMessage = PluginUserFacingFailureMessageCore::makeMessage(result.failure);
            PluginBlockedDllLoggerCore::log(result.failure);
        }

        return result;
    }
};

} // namespace DAW
