#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace apex::automation
{
    /**
        Audio<->message clock bridge.

        The audio thread calls publishFromAudioThread() once per block.
        The message thread reads via getPlayheadPPQ() / isTransportRolling().

        Storage is a pair of atomics with a versioned snapshot so the message
        thread cannot observe a torn read of (ppq, rolling).
    */
    class AutomationClock
    {
    public:
        AutomationClock() = default;

        // ----- Audio thread (lock-free, no allocation) -------------------

        void publishFromAudioThread (double playheadPPQAtBlockStart,
                                     double ppqPerSample,
                                     bool   transportRolling) noexcept
        {
            version.fetch_add (1, std::memory_order_acq_rel);

            blockStartPPQ.store    (playheadPPQAtBlockStart, std::memory_order_release);
            ppqPerSampleRate.store (ppqPerSample,            std::memory_order_release);
            // Count every true Play/Stop transition on the audio writer.
            // A Stop→Play turnaround between two 90 Hz recorder polls has
            // the same final rolling bool, but must still fence old takes.
            if (rolling.exchange (transportRolling, std::memory_order_acq_rel)
                != transportRolling)
                transportTransitionCount.fetch_add (1, std::memory_order_release);

            version.fetch_add (1, std::memory_order_acq_rel);
        }

        // ----- Message thread reads --------------------------------------

        struct Snapshot
        {
            double blockStartPPQ    = 0.0;
            double ppqPerSample     = 0.0;
            bool   transportRolling = false;
            std::uint64_t transportTransitions = 0;
        };

        Snapshot snapshot() const noexcept
        {
            Snapshot s;
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                const auto v0 = version.load (std::memory_order_acquire);
                s.blockStartPPQ    = blockStartPPQ.load    (std::memory_order_acquire);
                s.ppqPerSample     = ppqPerSampleRate.load (std::memory_order_acquire);
                s.transportRolling = rolling.load          (std::memory_order_acquire);
                s.transportTransitions = transportTransitionCount.load (
                    std::memory_order_acquire);
                const auto v1 = version.load (std::memory_order_acquire);
                if (v0 == v1 && (v0 & 1u) == 0u)
                    return s;
            }
            return s;
        }

        double getPlayheadPPQ()     const noexcept { return snapshot().blockStartPPQ; }
        bool   isTransportRolling() const noexcept { return snapshot().transportRolling; }

        // ----- Transport edge detection (message thread) ----------------

        struct TransportEdges
        {
            bool started = false;
            bool stopped = false;
        };

        /**
            Observe the transport exactly ONCE per recorder timer tick.
            Calling the separate legacy stopped/started accessors back-to-back
            consumed the same state transition twice: the stop query changed
            lastObservedRolling before the start query could see Play.
        */
        TransportEdges consumeTransportEdges() noexcept
        {
            const bool nowRolling = isTransportRolling();
            const bool wasRolling = lastObservedRolling.exchange(
                nowRolling, std::memory_order_acq_rel);
            return { !wasRolling && nowRolling, wasRolling && !nowRolling };
        }

        // Backwards-compatible single-edge accessors. Clients that need
        // BOTH edges in one tick MUST use consumeTransportEdges() above.
        bool consumeTransportStoppedEdge() noexcept
        {
            const bool nowRolling = isTransportRolling();
            const bool wasRolling = lastObservedRolling.exchange (nowRolling,
                                        std::memory_order_acq_rel);
            return wasRolling && ! nowRolling;
        }

        bool consumeTransportStartedEdge() noexcept
        {
            const bool nowRolling  = isTransportRolling();
            const bool wasRolling  = lastObservedRolling.load (std::memory_order_acquire);
            if (! wasRolling && nowRolling)
            {
                lastObservedRolling.store (true, std::memory_order_release);
                return true;
            }
            lastObservedRolling.store (nowRolling, std::memory_order_release);
            return false;
        }

        static AutomationClock& getInstance()
        {
            static AutomationClock instance;
            return instance;
        }

    private:
        std::atomic<double>        blockStartPPQ       { 0.0 };
        std::atomic<double>        ppqPerSampleRate    { 0.0 };
        std::atomic<bool>          rolling             { false };
        std::atomic<std::uint64_t> transportTransitionCount { 0 };
        std::atomic<std::uint64_t> version             { 0 };
        std::atomic<bool>          lastObservedRolling { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationClock)
    };
}
