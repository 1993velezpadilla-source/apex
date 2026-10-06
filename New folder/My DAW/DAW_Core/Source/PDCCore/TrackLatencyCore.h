#pragma once

namespace DAW {

/**
 * TrackLatencyCore — total latency for a single track's insert chain.
 *
 * Sum of all plugin latencies in the track's insert chain.
 * Updated whenever plugins are added, removed, or reconfigured.
 */
class TrackLatencyCore
{
public:
    void setTotalLatency(int samples) noexcept { totalLatency_ = samples; }
    int getTotalLatency() const noexcept { return totalLatency_; }

    bool hasLatency() const noexcept { return totalLatency_ > 0; }

private:
    int totalLatency_ = 0;
};

} // namespace DAW
