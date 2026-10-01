#pragma once
#include <JuceHeader.h>

namespace DAW {

enum class PluginScanState
{
    Pending,
    Scanning,
    Success,
    Failed,
    Skipped,
    Crashed,
    TimedOut
};

class PluginScanStateCore
{
public:
    static bool isTerminal(PluginScanState state)
    {
        switch (state)
        {
            case PluginScanState::Success:
            case PluginScanState::Failed:
            case PluginScanState::Skipped:
            case PluginScanState::Crashed:
            case PluginScanState::TimedOut:
                return true;
            case PluginScanState::Pending:
            case PluginScanState::Scanning:
                return false;
        }

        return false;
    }

    static juce::String toString(PluginScanState state)
    {
        switch (state)
        {
            case PluginScanState::Pending:  return "Pending";
            case PluginScanState::Scanning: return "Scanning";
            case PluginScanState::Success:  return "Success";
            case PluginScanState::Failed:   return "Failed";
            case PluginScanState::Skipped:  return "Skipped";
            case PluginScanState::Crashed:  return "Crashed";
            case PluginScanState::TimedOut: return "TimedOut";
        }

        return "Unknown";
    }

    static PluginScanState fromString(const juce::String& text)
    {
        if (text.equalsIgnoreCase("Pending"))  return PluginScanState::Pending;
        if (text.equalsIgnoreCase("Scanning")) return PluginScanState::Scanning;
        if (text.equalsIgnoreCase("Success"))  return PluginScanState::Success;
        if (text.equalsIgnoreCase("Failed"))   return PluginScanState::Failed;
        if (text.equalsIgnoreCase("Skipped"))  return PluginScanState::Skipped;
        if (text.equalsIgnoreCase("Crashed"))  return PluginScanState::Crashed;
        if (text.equalsIgnoreCase("TimedOut")) return PluginScanState::TimedOut;
        return PluginScanState::Failed;
    }
};

} // namespace DAW
