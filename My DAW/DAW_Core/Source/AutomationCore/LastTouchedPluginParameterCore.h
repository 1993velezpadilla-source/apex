#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

struct LastTouchedPluginParameter
{
    TrackID trackId;
    int pluginSlotIndex = -1;
    juce::String pluginInstanceId;
    juce::String parameterId;
    juce::String parameterName;
    juce::String pluginDisplayName;
    float normalizedValue = 0.0f;
    double timestampMs = 0.0;

    bool isValid() const noexcept
    {
        return trackId.isNotEmpty() && pluginSlotIndex >= 0 && parameterId.isNotEmpty();
    }
};

class LastTouchedPluginParameterCore
{
public:
    static LastTouchedPluginParameterCore& getInstance()
    {
        static LastTouchedPluginParameterCore instance;
        return instance;
    }

    void set(LastTouchedPluginParameter value)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        lastTouched_ = std::move(value);
    }

    LastTouchedPluginParameter get() const
    {
        return lastTouched_;
    }

    void clear()
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        lastTouched_ = {};
    }

private:
    LastTouchedPluginParameter lastTouched_;
};

} // namespace DAW
