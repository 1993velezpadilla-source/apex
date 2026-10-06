#pragma once
#include <JuceHeader.h>
#include <map>

namespace DAW {

class PluginQuarantineCore
{
public:
    void quarantine(const juce::String& pluginPath, const juce::String& reasonCode)
    {
        quarantined_[pluginPath] = reasonCode;
    }

    bool isQuarantined(const juce::String& pluginPath) const
    {
        return quarantined_.find(pluginPath) != quarantined_.end();
    }

    juce::String getReason(const juce::String& pluginPath) const
    {
        auto it = quarantined_.find(pluginPath);
        return it != quarantined_.end() ? it->second : juce::String();
    }

    int getCount() const { return (int)quarantined_.size(); }
    void clear() { quarantined_.clear(); }

private:
    std::map<juce::String, juce::String> quarantined_;
};

} // namespace DAW
