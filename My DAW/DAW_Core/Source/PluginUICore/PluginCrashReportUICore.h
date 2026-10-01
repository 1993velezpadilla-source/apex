#pragma once
#include <JuceHeader.h>
#include "../PluginSafetyCore/PluginGracefulFailCore.h"

namespace DAW {

class PluginCrashReportUICore
{
public:
    static juce::String buildReport(const PluginLoadFailureInfo& info)
    {
        juce::String report;
        report << "Plugin failure report" << newLine
               << "Plugin: " << info.pluginPath << newLine
               << "Stage: " << info.loadStage << newLine
               << "Blocked DLL: " << info.blockedDllPath << newLine
               << "Vendor: " << info.protectionVendor << newLine
               << "Error: " << info.rawErrorText << newLine;
        return report;
    }
};

} // namespace DAW
