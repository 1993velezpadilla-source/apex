#pragma once
#include <JuceHeader.h>
#include "LastTouchedPluginParameterCore.h"

namespace DAW {

enum class GestureState { Idle, ExplicitActive, ImplicitActive };

struct PluginAutomationGesture
{
    LastTouchedPluginParameter target;
    GestureState state = GestureState::Idle;
    double beginTimestampMs = 0.0;
    double lastChangeTimestampMs = 0.0;

    bool isActive() const noexcept { return state != GestureState::Idle; }
};

class PluginAutomationGestureCore
{
public:
    static constexpr double implicitTimeoutMs = 200.0;

    static juce::String makeKey(const LastTouchedPluginParameter& target)
    {
        return target.trackId + "|" + target.pluginInstanceId + "|" + juce::String(target.pluginSlotIndex) + "|" + target.parameterId;
    }

    void beginExplicit(const LastTouchedPluginParameter& target)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        if (!target.isValid())
            return;

        auto& gesture = gestures_[makeKey(target)];
        gesture.target = target;
        gesture.state = GestureState::ExplicitActive;
        gesture.beginTimestampMs = juce::Time::getMillisecondCounterHiRes();
        gesture.lastChangeTimestampMs = gesture.beginTimestampMs;
    }

    bool updateValue(const LastTouchedPluginParameter& target, float normalizedValue)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        if (!target.isValid())
            return false;

        const auto key = makeKey(target);
        auto& gesture = gestures_[key];
        const bool openedImplicit = !gesture.isActive();
        if (openedImplicit)
        {
            gesture.target = target;
            gesture.state = GestureState::ImplicitActive;
            gesture.beginTimestampMs = juce::Time::getMillisecondCounterHiRes();
        }

        gesture.target = target;
        gesture.target.normalizedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);
        gesture.lastChangeTimestampMs = juce::Time::getMillisecondCounterHiRes();
        return openedImplicit;
    }

    void endExplicit(const LastTouchedPluginParameter& target)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        const auto it = gestures_.find(makeKey(target));
        if (it != gestures_.end() && it->second.state == GestureState::ExplicitActive)
        {
            it->second.target = target;
            it->second.state = GestureState::ImplicitActive;
            it->second.lastChangeTimestampMs = juce::Time::getMillisecondCounterHiRes() - implicitTimeoutMs;
        }
    }

    std::vector<PluginAutomationGesture> expireImplicitGestures()
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        std::vector<PluginAutomationGesture> ended;
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto it = gestures_.begin(); it != gestures_.end();)
        {
            if (it->second.state == GestureState::ImplicitActive
                && now - it->second.lastChangeTimestampMs >= implicitTimeoutMs)
            {
                ended.push_back(it->second);
                it = gestures_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return ended;
    }

    bool isActive(const LastTouchedPluginParameter& target) const
    {
        return gestures_.find(makeKey(target)) != gestures_.end();
    }

    std::vector<PluginAutomationGesture> getActiveGestures() const
    {
        std::vector<PluginAutomationGesture> active;
        active.reserve(gestures_.size());
        for (const auto& entry : gestures_)
            if (entry.second.isActive())
                active.push_back(entry.second);
        return active;
    }

private:
    std::map<juce::String, PluginAutomationGesture> gestures_;
};

} // namespace DAW
