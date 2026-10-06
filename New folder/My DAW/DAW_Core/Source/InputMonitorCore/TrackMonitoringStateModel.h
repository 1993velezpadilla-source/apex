#pragma once
#include <atomic>
#include "RecordInputRouter.h"

namespace DAW {

/** Per-track input monitoring mode. */
enum class InputMonitorMode
{
    Off  = 0,   // no live input heard
    On   = 1,   // always hear live input
    Auto = 2    // hear input only when record-armed + stopped/recording (pro-DAW default)
};

/**
 * TrackMonitoringStateModel — per-track input monitoring + recording mode state.
 *
 * This is for LIVE INPUT MONITORING on normal recordable tracks
 * (hear your vocal/instrument in real time while performing).
 *
 * Also carries the recording-mode selection that decides whether
 * plugin effects are only for monitoring or printed into recorded audio.
 *
 * Recording modes (per-track):
 *   - Monitor Dry / Record Dry     — no FX anywhere
 *   - Monitor Wet / Record Dry     — hear FX, record clean
 *   - Monitor Wet / Record Wet     — hear FX, print FX into recording
 *
 * This is NOT Control Room / monitor section monitoring.
 * This is NOT the master track — master never has input monitoring.
 *
 * Thread-safe via atomics for audio↔UI bridge.
 */
struct TrackMonitoringStateModel
{
    // Auto = professional default (Logic/Cubase/Pro Tools behaviour):
    // arming a track lets you hear yourself immediately; disarming goes silent.
    std::atomic<int>  monitorMode   { (int)InputMonitorMode::Auto };
    std::atomic<int>  recordingMode { (int)RecordInputRouter::Mode::MonitorDryRecordDry };
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

    RecordInputRouter::Mode getRecordingMode() const noexcept
    {
        return static_cast<RecordInputRouter::Mode>(recordingMode.load(std::memory_order_relaxed));
    }

    void setRecordingMode(RecordInputRouter::Mode m) noexcept
    {
        recordingMode.store((int)m, std::memory_order_relaxed);
    }
};

} // namespace DAW
