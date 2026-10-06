#pragma once

namespace DAW {

/**
 * MasterLatencyCore — total latency for the master bus.
 *
 * Includes the master insert chain latency.
 * Used to ensure render and monitor paths are properly aligned.
 */
class MasterLatencyCore
{
public:
    void setMasterInsertLatency(int samples) noexcept { masterLatency_ = samples; }
    int getMasterInsertLatency() const noexcept { return masterLatency_; }

private:
    int masterLatency_ = 0;
};

} // namespace DAW
