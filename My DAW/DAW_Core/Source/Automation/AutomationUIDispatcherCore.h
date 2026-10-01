#pragma once

#include "AutomationParameterCore.h"
#include "AutomationParameterRegistryCore.h"
#include <JuceHeader.h>
#include <unordered_map>

namespace apex::automation
{
    /**
        Single ~60Hz timer that polls every registered AutomationParameter
        and, when its version counter has advanced since the last tick,
        invokes its listeners on the message thread.

        This is the mechanism by which a knob/fader/button "moves by itself"
        during automation playback: the evaluator (audio thread) bumps the
        parameter's value and version atomically, and on the next dispatcher
        tick the bound widget receives parameterValueChanged and calls
        slider.setValue(v, dontSendNotification).
    */
    class AutomationUIDispatcher : private juce::Timer,
                                   private juce::DeletedAtShutdown
    {
    public:
        explicit AutomationUIDispatcher (AutomationParameterRegistry& reg)
            : registry (reg)
        {
            startTimerHz (kPollHz);
        }

        ~AutomationUIDispatcher() override
        {
            stopTimer();
        }

        static constexpr int kPollHz = 60;

        void pause()  { stopTimer(); }
        void resume() { startTimerHz (kPollHz); }

        static AutomationUIDispatcher& getInstance()
        {
            // Timer services must be destroyed by JUCE before MessageManager.
            // A normal function-static Timer outlives the platform event system
            // and trips juce_Timer.cpp:99 during CRT teardown.
            static auto* instance = new AutomationUIDispatcher (
                AutomationParameterRegistry::getInstance());
            return *instance;
        }

    private:
        void timerCallback() override
        {
            JUCE_ASSERT_MESSAGE_THREAD;

            registry.forEach ([this] (AutomationParameter& p)
            {
                const auto id      = p.getID();
                const auto current = p.getVersion();
                auto&      last    = lastSeenVersion[id];

                if (current != last)
                {
                    last = current;
                    p.dispatchPendingNotifications (
                        p.getNormalizedValue(),
                        p.getLastSource());
                }
            });
        }

        AutomationParameterRegistry& registry;
        std::unordered_map<ParameterID, std::uint64_t> lastSeenVersion;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationUIDispatcher)
    };
}
