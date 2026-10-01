#pragma once
#include <atomic>

namespace DAW {

/** Per-track input monitoring mode. */
enum class InputMonitorMode
{
    Off  = 0,   // no live input heard
    On   = 1,   // always hear live input
    Auto = 2    // hear input only when record-armed + stopped/recording (pro-DAW default)
};

/**
 * TrackMonitoringStateModel — per-track input monitoring state.
 *
 * Recording mode is always dry (no plugin effects printed to file).
 * Wet/printed recording was removed to eliminate the zipper noise
 * from redundant signal paths — no major DAW offers per-track
 * wet-printing as a toggle.
 *
 * Thread-safe via atomics for audio↔UI bridge.
 */
struct TrackMonitoringStateModel
{
    // Auto = professional default (Logic/Cubase/Pro Tools behaviour):
    // arming a track lets you hear yourself immediately; disarming goes silent.
    std::atomic<int>  monitorMode   { (int)InputMonitorMode::Auto };
    std::atomic<bool> inputFxActive { false };

    InputMonitorMode getMode() const noexcept
    {
        return static_cast<InputMonitorMode>(monitorMode.load(std::memory_order_relaxed));
    }

    void setMode(InputMonitorMode m) noexcept
    {
        monitorMode.store((int)m, std::memory_order_relaxed);
    }

    bool isMonitoring() const noexcept
    {
        return getMode() != InputMonitorMode::Off;
    }
};

} // namespace DAW
