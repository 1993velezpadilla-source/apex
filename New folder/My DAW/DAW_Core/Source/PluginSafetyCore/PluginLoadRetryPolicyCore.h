#pragma once
#include <JuceHeader.h>
#include <map>

namespace DAW {

class PluginLoadRetryPolicyCore
{
public:
    bool canRetry(const juce::String& pluginPath) const
    {
        auto it = retryCounts_.find(pluginPath);
        return it == retryCounts_.end() || it->second < maxRetriesPerSession_;
    }

    void recordFailure(const juce::String& pluginPath)
    {
        ++retryCounts_[pluginPath];
    }

    void reset(const juce::String& pluginPath)
    {
        retryCounts_.erase(pluginPath);
    }

private:
    int maxRetriesPerSession_ = 1;
    std::map<juce::String, int> retryCounts_;
};

} // namespace DAW
