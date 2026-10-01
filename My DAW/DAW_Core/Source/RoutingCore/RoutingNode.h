#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * RoutingNodeType — classification of a node in the audio routing graph.
 */
enum class RoutingNodeType
{
    Track,      // audio/MIDI track output
    Bus,        // aux / bus
    FolderBus,  // folder bus (sums children)
    Master,     // master output
    Hardware    // hardware I/O point
};

/**
 * RoutingNode — a node in the audio routing graph.
 *
 * Each track, bus, folder bus, and the master output is a node.
 * Connections between nodes define the signal flow.
 *
 * `active` is std::atomic<bool> so the message thread can mutate it while
 * the audio thread reads it without a lock. The node itself is therefore
 * non-copyable/non-movable; it's owned by juce::OwnedArray<RoutingNode>.
 */
struct RoutingNode
{
    juce::String      id;
    juce::String      name;
    RoutingNodeType   type         = RoutingNodeType::Track;
    TrackID           trackId;
    int               channelCount = 2;
    std::atomic<bool> active       { true };

    RoutingNode() = default;
    RoutingNode(const RoutingNode&) = delete;
    RoutingNode& operator=(const RoutingNode&) = delete;

    juce::ValueTree getState() const
    {
        juce::ValueTree v("RoutingNode");
        v.setProperty("id",           id,            nullptr);
        v.setProperty("name",         name,          nullptr);
        v.setProperty("type",         (int)type,     nullptr);
        v.setProperty("trackId",      trackId,       nullptr);
        v.setProperty("channelCount", channelCount,  nullptr);
        v.setProperty("active",       active.load(std::memory_order_relaxed), nullptr);
        return v;
    }

    void restoreState(const juce::ValueTree& v)
    {
        id           = v.getProperty("id",           "").toString();
        name         = v.getProperty("name",         "").toString();
        type         = (RoutingNodeType)(int)v.getProperty("type", 0);
        trackId      = v.getProperty("trackId",      "").toString();
        channelCount = (int)v.getProperty("channelCount", 2);
        active.store((bool)v.getProperty("active",  true), std::memory_order_relaxed);
    }
};

} // namespace DAW
