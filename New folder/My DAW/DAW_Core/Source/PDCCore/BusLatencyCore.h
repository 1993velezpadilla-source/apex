#pragma once

namespace DAW {

/**
 * BusLatencyCore — total latency for a bus node.
 *
 * Includes the bus's own insert chain latency PLUS the maximum
 * latency of all incoming paths. This is needed to align
 * parallel signals at the bus summing point.
 */
class BusLatencyCore
{
public:
    void setOwnInsertLatency(int samples) noexcept { ownLatency_ = samples; }
    int getOwnInsertLatency() const noexcept { return ownLatency_; }

    void setMaxIncomingLatency(int samples) noexcept { maxIncomingLatency_ = samples; }
    int getMaxIncomingLatency() const noexcept { return maxIncomingLatency_; }

    int getTotalLatency() const noexcept { return ownLatency_ + maxIncomingLatency_; }

private:
    int ownLatency_ = 0;
    int maxIncomingLatency_ = 0;
};

} // namespace DAW
