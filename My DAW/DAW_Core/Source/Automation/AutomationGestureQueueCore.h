#pragma once

#include "AutomationTypes.h"
#include <JuceHeader.h>
#include <atomic>
#include <array>

namespace apex::automation
{
    /**
        Lock-free gesture event queue between producers (any thread) and the
        recorder (message thread).

        Uses a CAS-based ring buffer sized at 8192 events — well above the
        worst-case burst from 60Hz drag on many parameters.
    */
    class AutomationGestureQueue
    {
    public:
        enum class EventKind : std::uint8_t
        {
            GestureBegin = 0,
            ValueChange  = 1,
            GestureEnd   = 2
        };

        struct Event
        {
            ParameterID   paramID         = kInvalidParameterID;
            EventKind     kind            = EventKind::ValueChange;
            ChangeSource  source          = ChangeSource::User;
            float         normalizedValue = 0.0f;
            double        ppqAtCapture    = 0.0;
            std::uint64_t sequence        = 0;
        };

        static constexpr std::size_t kCapacity = 8192;

        AutomationGestureQueue() = default;

        // ----- Producer (any thread) -------------------------------------

        bool push (Event e) noexcept
        {
            e.sequence = nextSequence.fetch_add (1, std::memory_order_acq_rel);

            auto tail = writeIndex.load (std::memory_order_relaxed);
            for (;;)
            {
                const auto head = readIndex.load (std::memory_order_acquire);
                if (((tail + 1) % kCapacity) == head)
                {
                    overflowCount.fetch_add (1, std::memory_order_relaxed);
                    return false;
                }
                if (writeIndex.compare_exchange_weak (tail, (tail + 1) % kCapacity,
                                                      std::memory_order_acq_rel))
                {
                    buffer[tail] = e;
                    publishedUpTo.store (tail, std::memory_order_release);
                    return true;
                }
            }
        }

        // ----- Consumer (message thread only) ----------------------------

        bool pop (Event& out) noexcept
        {
            const auto head  = readIndex.load  (std::memory_order_relaxed);
            const auto wIdx  = writeIndex.load (std::memory_order_acquire);
            if (head == wIdx)
                return false;

            out = buffer[head];
            readIndex.store ((head + 1) % kCapacity, std::memory_order_release);
            return true;
        }

        std::uint64_t getOverflowCount() const noexcept
        {
            return overflowCount.load (std::memory_order_acquire);
        }

        static AutomationGestureQueue& getInstance()
        {
            static AutomationGestureQueue instance;
            return instance;
        }

    private:
        std::array<Event, kCapacity> buffer {};
        std::atomic<std::size_t>   writeIndex    { 0 };
        std::atomic<std::size_t>   publishedUpTo { 0 };
        std::atomic<std::size_t>   readIndex     { 0 };
        std::atomic<std::uint64_t> nextSequence  { 1 };
        std::atomic<std::uint64_t> overflowCount { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationGestureQueue)
    };
}
