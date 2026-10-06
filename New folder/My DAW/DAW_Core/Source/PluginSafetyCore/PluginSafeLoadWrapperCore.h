#pragma once
#include <JuceHeader.h>
#include "PluginGracefulFailCore.h"
#include "PluginProtectionRuntimeGuardCore.h"
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
        PluginProtectionRuntimeGuardCore guard;
        juce::String errorMsg;

        try
        {
            result.instance = formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMsg);
        }
        catch (const std::exception& e)
        {
            errorMsg = juce::String("Exception: ") + e.what();
        }
        catch (...)
        {
            errorMsg = "Unknown exception during plugin load";
        }

        if (!result.instance)
        {
            result.failure = PluginLoadFailureClassifierCore::classify(desc.fileOrIdentifier, errorMsg, loadStage);
            result.userFacingMessage = PluginUserFacingFailureMessageCore::makeMessage(result.failure);
            PluginBlockedDllLoggerCore::log(result.failure);
        }

        return result;
    }
};

} // namespace DAW
