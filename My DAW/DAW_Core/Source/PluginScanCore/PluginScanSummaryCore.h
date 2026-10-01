#pragma once
#include <JuceHeader.h>

namespace DAW {

struct PluginScanSummary
{
    int candidatesDiscovered = 0;
    int candidatesAttempted = 0;
    int successCount = 0;
    int failCount = 0;
    int skippedCount = 0;
    int crashCount = 0;
    int timeoutCount = 0;
    int cacheInserts = 0;
    int failureStoreInserts = 0;
    int finalVisiblePluginCount = 0;
};

class PluginScanSummaryCore
{
public:
    static juce::String toLogString(const PluginScanSummary& summary)
    {
        juce::StringArray lines;
        lines.add("candidatesDiscovered=" + juce::String(summary.candidatesDiscovered));
        lines.add("candidatesAttempted=" + juce::String(summary.candidatesAttempted));
        lines.add("successCount=" + juce::String(summary.successCount));
        lines.add("failCount=" + juce::String(summary.failCount));
        lines.add("skippedCount=" + juce::String(summary.skippedCount));
        lines.add("crashCount=" + juce::String(summary.crashCount));
        lines.add("timeoutCount=" + juce::String(summary.timeoutCount));
        lines.add("cacheInserts=" + juce::String(summary.cacheInserts));
        lines.add("failureStoreInserts=" + juce::String(summary.failureStoreInserts));
        lines.add("finalVisiblePluginCount=" + juce::String(summary.finalVisiblePluginCount));
        return lines.joinIntoString("\n");
    }
};

} // namespace DAW
