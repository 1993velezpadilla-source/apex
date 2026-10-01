#pragma once
#include <JuceHeader.h>
#include <unordered_map>
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

    struct EdgeInfo
    {
        juce::String sourceNodeId;
        juce::String destNodeId;
    };

    /** Trigger a full recalculation of the latency graph.
     *  Should be called whenever plugins or routing change.
     *  Propagates cumulative latency through graph edges: each node's
     *  cumulative latency = ownInsertLatency + max(predecessors' cumulativeLatency). */
    void recalculate(const std::vector<juce::String>& topologicalOrder,
                     const std::map<juce::String, int>& nodeInsertLatencies,
                     const std::vector<EdgeInfo>& edges)
    {
        resolved_.clear();

        // Build predecessor map: destNodeId -> list of sourceNodeIds
        std::unordered_map<juce::String, std::vector<juce::String>> predecessors;
        for (const auto& edge : edges)
            predecessors[edge.destNodeId].push_back(edge.sourceNodeId);

        for (const auto& nodeId : topologicalOrder)
        {
            NodeLatencyInfo info;
            info.nodeId = nodeId;

            auto it = nodeInsertLatencies.find(nodeId);
            info.ownInsertLatency = (it != nodeInsertLatencies.end()) ? it->second : 0;

            // Find max cumulative latency among all predecessors
            int maxPredLatency = 0;
            auto predIt = predecessors.find(nodeId);
            if (predIt != predecessors.end())
            {
                for (const auto& predId : predIt->second)
                {
                    auto predInfo = resolved_.find(predId);
                    if (predInfo != resolved_.end())
                        maxPredLatency = std::max(maxPredLatency, predInfo->second.cumulativeLatency);
                }
            }

            info.cumulativeLatency = info.ownInsertLatency + maxPredLatency;
            resolved_[nodeId] = info;
        }

        // Compensation delay = maxCumulativeAcrossGraph - thisNode.cumulativeLatency
        int maxGraphLatency = 0;
        for (const auto& [id, info] : resolved_)
            maxGraphLatency = std::max(maxGraphLatency, info.cumulativeLatency);

        for (auto& [id, info] : resolved_)
            info.compensationDelay = maxGraphLatency - info.cumulativeLatency;

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
    std::unordered_map<juce::String, NodeLatencyInfo> resolved_;
    bool dirty_ = true;
};

} // namespace DAW
