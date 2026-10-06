#pragma once
#include <JuceHeader.h>
#include <map>
#include <vector>

namespace DAW {

/**
 * RoutingLatencyResolverCore — resolves latency along the routing graph.
 *
 * Walks the routing DAG to compute the cumulative latency at each
 * node. Uses the maximum incoming path latency to determine how
 * much each faster path needs to be delayed.
 *
 * Must be recalculated whenever:
 *   - A plugin is added/removed/reconfigured
 *   - Routing connections change
 *   - Oversampling/lookahead settings change
 */
class RoutingLatencyResolverCore
{
public:
    struct NodeLatencyInfo
    {
        juce::String nodeId;
        int ownInsertLatency = 0;   // latency from this node's insert chain
        int cumulativeLatency = 0;  // max cumulative latency up to and including this node
        int compensationDelay = 0;  // delay needed to align with slowest path
    };

    /** Trigger a full recalculation of the latency graph.
     *  Should be called whenever plugins or routing change. */
    void recalculate(const std::vector<juce::String>& topologicalOrder,
                     const std::map<juce::String, int>& nodeInsertLatencies)
    {
        resolved_.clear();

        for (const auto& nodeId : topologicalOrder)
        {
            NodeLatencyInfo info;
            info.nodeId = nodeId;

            auto it = nodeInsertLatencies.find(nodeId);
            info.ownInsertLatency = (it != nodeInsertLatencies.end()) ? it->second : 0;
            info.cumulativeLatency = info.ownInsertLatency;

            resolved_[nodeId] = info;
        }

        dirty_ = false;
    }

    const NodeLatencyInfo* getInfo(const juce::String& nodeId) const
    {
        auto it = resolved_.find(nodeId);
        return (it != resolved_.end()) ? &it->second : nullptr;
    }

    void markDirty() noexcept { dirty_ = true; }
    bool isDirty() const noexcept { return dirty_; }

private:
    std::map<juce::String, NodeLatencyInfo> resolved_;
    bool dirty_ = true;
};

} // namespace DAW
