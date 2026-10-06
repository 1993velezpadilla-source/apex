#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace apex::automation
{
    /**
        Global arm state for automation recording.

        The recorder checks isRecordArmed() before writing any breakpoints.
        Without arm set, gestures still drive UI but do not write to lanes.

        Mirrors FL Studio's "Record Notes and Automation" toggle.
    */
    class AutomationTransportState
    {
    public:
        AutomationTransportState() = default;

        void setRecordArmed (bool armed) noexcept
        {
            recordArmed.store (armed, std::memory_order_release);
        }

        bool isRecordArmed() const noexcept
        {
            return recordArmed.load (std::memory_order_acquire);
        }

        // When true, gestures auto-create a lane on first touch even if
        // none existed before. Matches FL Studio's default behavior.
        void setAutoCreateLaneOnTouch (bool enabled) noexcept
        {
            autoCreateLane.store (enabled, std::memory_order_release);
        }

        bool isAutoCreateLaneOnTouchEnabled() const noexcept
        {
            return autoCreateLane.load (std::memory_order_acquire);
        }

        static AutomationTransportState& getInstance()
        {
            static AutomationTransportState instance;
            return instance;
        }

    private:
        std::atomic<bool> recordArmed    { false };
        std::atomic<bool> autoCreateLane { true  };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationTransportState)
    };
}
