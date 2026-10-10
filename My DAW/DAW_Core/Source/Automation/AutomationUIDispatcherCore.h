#pragma once

#include "AutomationParameterCore.h"
#include "AutomationParameterRegistryCore.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <memory>

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

        // Deterministic message-thread probe used by the Windows test suite.
        // Production still dispatches on the ~60Hz timer.
        void dispatchOnceForTests() { timerCallback(); }

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

            // Listener callbacks can unregister a parameter (for example,
            // when a widget closes during a project change). Iterating the
            // mutable registry while holding its lock lets that callback
            // erase the CURRENT unordered_map iterator: undefined behavior.
            // Use one lifetime-pinning immutable registry snapshot for the
            // whole tick. Callbacks are then free to register/remove controls
            // without invalidating the iteration or holding registryLock.
            auto snapshot = registry.getSnapshotRT();
            if (snapshot == nullptr)
                return;

            for (const auto& [id, parameter] : *snapshot)
            {
                const auto current = parameter->getVersion();
                auto& last = lastSeenVersion[id];
                // A replacement parameter can reuse the same ParameterID
                // AND reach the same version. Do not skip its first update.
                const bool replaced = last.parameter.lock().get() != parameter.get();
                if (replaced || current != last.version)
                {
                    last.parameter = parameter;
                    last.version = current;
                    parameter->dispatchPendingNotifications (
                        parameter->getNormalizedValue(),
                        parameter->getLastSource());
                }
            }

            // The snapshot, not the live registry, determines which IDs were
            // present during this tick. Reclaim stale version records on UI
            // thread so repeated plugin/project removals do not grow the map.
            for (auto it = lastSeenVersion.begin(); it != lastSeenVersion.end();)
                if (snapshot->find (it->first) == snapshot->end())
                    it = lastSeenVersion.erase (it);
                else
                    ++it;
        }

        struct LastSeen
        {
            std::weak_ptr<AutomationParameter> parameter;
            std::uint64_t version = 0;
        };

        AutomationParameterRegistry& registry;
        std::unordered_map<ParameterID, LastSeen> lastSeenVersion;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationUIDispatcher)
    };
}
