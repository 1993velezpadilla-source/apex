#pragma once
#include <JuceHeader.h>
#include <map>

namespace DAW {

/**
 * DelayCompensationCore — global PDC engine.
 *
 * Resolves latency across the entire routing graph.
 * The slowest path defines the alignment reference;
 * faster paths are delayed to match.
 *
 * Version 1: tracks per-node latency values.
 * Future: resolves delay lines per-node for sample-accurate alignment.
 */
class DelayCompensationCore
{
public:
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool isEnabled() const noexcept { return enabled_; }

    void setNodeLatency(const juce::String& nodeId, int latencySamples)
    {
        nodeLatencies_[nodeId] = latencySamples;
    }

    int getNodeLatency(const juce::String& nodeId) const
    {
        auto it = nodeLatencies_.find(nodeId);
        return (it != nodeLatencies_.end()) ? it->second : 0;
    }

    /** Get the maximum latency across all nodes. */
    int getMaxLatency() const
    {
        int maxLat = 0;
        for (const auto& [id, lat] : nodeLatencies_)
            if (lat > maxLat) maxLat = lat;
        return maxLat;
    }

    /** Get the compensation delay needed for a given node. */
    int getCompensationDelay(const juce::String& nodeId) const
    {
        if (!enabled_) return 0;
        return getMaxLatency() - getNodeLatency(nodeId);
    }

    void clear() { nodeLatencies_.clear(); }

private:
    bool enabled_ = true;
    std::map<juce::String, int> nodeLatencies_;
};

} // namespace DAW
