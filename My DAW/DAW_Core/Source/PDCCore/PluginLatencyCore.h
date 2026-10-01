#pragma once

namespace DAW {

/**
 * PluginLatencyCore — reports latency for a single plugin instance.
 *
 * Each plugin can report its processing latency in samples.
 * This changes when the plugin's configuration changes
 * (e.g., oversampling, lookahead, linear-phase mode).
 */
class PluginLatencyCore
{
public:
    void setReportedLatency(int samples) noexcept { latencySamples_ = samples; }
    int getReportedLatency() const noexcept { return latencySamples_; }

    bool hasLatency() const noexcept { return latencySamples_ > 0; }

private:
    int latencySamples_ = 0;
};

} // namespace DAW
