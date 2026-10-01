#pragma once

namespace DAW {

/**
 * LiveMonitoringBypassCore — low-latency monitoring mode for recording.
 *
 * During recording with live input monitoring, full PDC can create
 * too much latency for the performer. This core allows bypassing
 * or reducing PDC on armed/monitored tracks.
 *
 * Modes:
 *   Full    — normal PDC, may have higher latency
 *   Reduced — limited compensation for live monitoring
 *   Bypass  — no PDC on armed tracks (lowest latency)
 */
class LiveMonitoringBypassCore
{
public:
    enum class Mode { Full, Reduced, Bypass };

    void setMode(Mode m) noexcept { mode_ = m; }
    Mode getMode() const noexcept { return mode_; }

    /** Returns true if PDC should be bypassed for the given track. */
    bool shouldBypassPDC(bool trackIsArmed, bool trackIsMonitoring) const noexcept
    {
        if (mode_ == Mode::Full) return false;
        if (mode_ == Mode::Bypass) return trackIsArmed && trackIsMonitoring;
        // Reduced: bypass only if both armed and monitoring
        return trackIsArmed && trackIsMonitoring;
    }

private:
    Mode mode_ = Mode::Full;
};

} // namespace DAW
