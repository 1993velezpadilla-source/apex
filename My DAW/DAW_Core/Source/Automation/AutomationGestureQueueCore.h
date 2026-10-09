#pragma once

#include "AutomationTypes.h"
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>

namespace apex::automation
{
    /**
        Bounded multi-producer / single-consumer automation gesture queue.

        Producers include plugin callbacks (which can run on the audio thread)
        and native UI gestures. There must NEVER be a window in which a producer
        has advanced the visible write cursor but has not published its Event.
        Each slot therefore has a monotonic sequence: claiming a slot and
        publishing its completed payload are separate operations.

        - No locks or allocations in push() or pop().
        - A full queue reports false and increments overflowCount; existing
          callers may decide how to handle lost events.
        - One consumer only: the message-thread AutomationRecorder.
        - pop() never reads a slot until the producer release-publishes it.
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
            // Set only when GestureBegin contains the real pre-drag value.
            // Old/non-gesture producers can omit it; the recorder falls back
            // to its previous behavior rather than assuming zero.
            bool          hasStartValue   = false;
            std::uint64_t sequence        = 0;
        };

        static constexpr std::size_t kCapacity = 8192;

        AutomationGestureQueue() noexcept
        {
            for (std::size_t i = 0; i < kCapacity; ++i)
                slots[i].sequence.store(i, std::memory_order_relaxed);
        }

        // Any producer thread. A successful reservation owns one slot until
        // its payload is fully written and release-published.
        bool push (Event e) noexcept
        {
            auto pos = writeIndex.load(std::memory_order_relaxed);
            for (;;)
            {
                auto& cell = slots[pos % kCapacity];
                const auto seq = cell.sequence.load(std::memory_order_acquire);
                const auto delta = static_cast<std::ptrdiff_t>(seq)
                                 - static_cast<std::ptrdiff_t>(pos);

                if (delta == 0)
                {
                    if (!writeIndex.compare_exchange_weak(
                            pos, pos + 1, std::memory_order_relaxed,
                            std::memory_order_relaxed))
                        continue;

                    e.sequence = nextSequence.fetch_add(1, std::memory_order_relaxed);
                    cell.event = e;
                    // The consumer must not touch cell.event before this store.
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }

                if (delta < 0)
                {
                    overflowCount.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }

                pos = writeIndex.load(std::memory_order_relaxed);
            }
        }

        // Message-thread consumer only. Do not skip a pending producer:
        // gesture begin/value/end ordering must be preserved.
        bool pop (Event& out) noexcept
        {
            const auto pos = readIndex.load(std::memory_order_relaxed);
            auto& cell = slots[pos % kCapacity];
            const auto seq = cell.sequence.load(std::memory_order_acquire);
            if (seq != pos + 1)
                return false;

            out = cell.event;
            readIndex.store(pos + 1, std::memory_order_relaxed);
            cell.sequence.store(pos + kCapacity, std::memory_order_release);
            return true;
        }

        std::uint64_t getOverflowCount() const noexcept
        {
            return overflowCount.load(std::memory_order_acquire);
        }

        static AutomationGestureQueue& getInstance()
        {
            static AutomationGestureQueue instance;
            return instance;
        }

    private:
        struct Slot
        {
            std::atomic<std::size_t> sequence { 0 };
            Event event {};
        };

        std::array<Slot, kCapacity> slots {};
        std::atomic<std::size_t> writeIndex { 0 };
        std::atomic<std::size_t> readIndex { 0 };
        std::atomic<std::uint64_t> nextSequence { 1 };
        std::atomic<std::uint64_t> overflowCount { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationGestureQueue)
    };
}
