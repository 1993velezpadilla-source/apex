#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * SendTargetCore — manages the routing destination for a send.
 *
 * Each send routes to exactly one destination node (bus, FX return, etc.).
 * The target must be a valid node in the RoutingGraph.
 */
class SendTargetCore
{
public:
    void setTargetNodeId(const juce::String& nodeId) { targetNodeId_ = nodeId; }
    const juce::String& getTargetNodeId() const noexcept { return targetNodeId_; }

    bool hasTarget() const { return targetNodeId_.isNotEmpty(); }

private:
    juce::String targetNodeId_;
};

} // namespace DAW
