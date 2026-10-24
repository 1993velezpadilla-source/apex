#pragma once

// APEX Precision C: deterministic, allocation-free latency-mode gate.
//
// Changing lookahead or 2x oversampling changes the plugin's physical delay.
// Switching while the transport runs creates a phase jump and may race JUCE
// PDC notification. With a valid host transport, stage the requested mode
// until STOP; apply only after the message thread confirms the new PDC.
// This deliberately does NOT promise click-free switching with a host that
// supplies no transport state: that remains a separate release gate.

#include <algorithm>
#include <cmath>

namespace APEX::PrecisionC3
{
class LatencyModeGuard final
{
public:
    struct Mode
    {
        int lookaheadIndex = 0; // Off / 1 / 2 / 5 / 10 / 20 ms
        bool oversampling2x = false;
        bool operator==(const Mode& other) const noexcept
        {
            return lookaheadIndex == other.lookaheadIndex
                && oversampling2x == other.oversampling2x;
        }
        bool operator!=(const Mode& other) const noexcept
        {
            return !(*this == other);
        }
    };

    struct Decision
    {
        Mode effective;
        int requestedLatency = 0;
        bool pendingStop = false;
    };

    void reset(double sampleRate, Mode initial) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0
                    ? sampleRate : 48000.0;
        active_ = sanitise(initial);
        armed_ = false;
        armedMode_ = active_;
    }

    int latency(Mode m) const noexcept
    {
        constexpr double milliseconds[] = {0.0,1.0,2.0,5.0,10.0,20.0};
        const auto normal = sanitise(m);
        return static_cast<int>(std::lround(
            sampleRate_ * .001 * milliseconds[normal.lookaheadIndex]))
            + (normal.oversampling2x ? 48 : 0);
    }

    // Audio-thread-only state machine. Host latency acknowledgment is written
    // by the JUCE message-thread timer AFTER setLatencySamples(). No audio
    // callback locks/allocations/message posts.
    Decision evaluate(Mode desired, bool knownTransport, bool playing,
                      int acknowledgedLatency) noexcept
    {
        desired = sanitise(desired);

        // Legacy hosts/tests lacking getPosition(): preserve prior behavior
        // rather than making the quality selector permanently nonfunctional.
        if (!knownTransport)
        {
            active_ = desired;
            armed_ = false;
            return {active_, latency(active_), false};
        }

        if (desired == active_)
        {
            armed_ = false;
            return {active_, latency(active_), false};
        }

        // Never *arm* a new audio-latency mode while the playhead is running.
        if (!playing)
        {
            armedMode_ = desired;
            armed_ = true;
        }

        // This may also commit on the first PLAY block after a STOP if the
        // message thread already acknowledged PDC and the mode was armed
        // during STOP. A mode requested only during PLAY cannot enter here.
        if (armed_ && desired == armedMode_
                   && acknowledgedLatency == latency(armedMode_))
        {
            active_ = armedMode_;
            armed_ = false;
            return {active_, latency(active_), false};
        }

        return {active_, (!playing && armed_) ? latency(armedMode_)
                                               : latency(active_), true};
    }

    Mode active() const noexcept { return active_; }

private:
    static Mode sanitise(Mode m) noexcept
    {
        m.lookaheadIndex = std::clamp(m.lookaheadIndex, 0, 5);
        return m;
    }

    double sampleRate_ = 48000.0;
    Mode active_ {};
    Mode armedMode_ {};
    bool armed_ = false;
};
} // namespace APEX::PrecisionC3
