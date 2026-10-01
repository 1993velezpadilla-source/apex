#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "../AudioEngineCore/ConnectionGainRampCore.h"
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * ConnectionType — the kind of routing connection between two nodes.
 */
enum class ConnectionType
{
    Direct,     // direct output routing (track → bus, track → master)
    Send,       // post-fader send
    PreSend,    // pre-fader send
    Sidechain,  // sidechain input (control signal, not summed into main)
    FolderSum   // implicit summing from folder child to parent bus
};

/**
 * TapPoint — where on the source track the sidechain signal is tapped.
 * Mirrors Ableton's Pre FX / Post FX / Post Mixer selector.
 * Default: PreFX gives the cleanest, most consistent detector trigger.
 */
enum class TapPoint
{
    PreFX,      // raw recorded signal, before source track's plugin chain
    PostFX,     // after source plugin chain, before fader/pan
    PostMixer   // after fader, pan, sends — muting source kills the sidechain
};

/**
 * RoutingConnection — a directed edge in the routing graph.
 *
 * Represents signal flow from sourceNodeId to destNodeId.
 * Gain applies to sends. Direct connections pass at unity.
 *
 * Thread-safety:
 *   active / bypassed are flipped from the message thread (UI toggles) and
 *   read on the audio thread every block. std::atomic<bool> is used so the
 *   individual flag flip does not need to grab the graph lock — the outer
 *   ScopedTryLock in AudioEngine only guards container mutation (add/remove),
 *   not field-level writes to existing edges.
 *
 * RoutingConnection is non-copyable and non-movable (atomics). It is owned
 * by juce::OwnedArray<RoutingConnection> and referenced by raw pointer only.
 */
struct RoutingConnection
{
    RouteID           id;
    juce::String      sourceNodeId;
    juce::String      destNodeId;
    ConnectionType    type     = ConnectionType::Direct;
    std::atomic<float> gain    { 1.0f }; // send level (0..1 for sends)
    std::atomic<bool> active   { true };
    std::atomic<bool> bypassed { false };
    ConnectionGainRampCore gainRamp;

    // ── Sidechain-specific metadata (unused for non-Sidechain connections) ──
    // Identifies which plugin instance and bus index receives the sidechain signal.
    juce::String destinationPluginId;   // plugin instance ID on the destination track
    int          destinationBusIndex = 1; // plugin input bus index (0=main, 1+=sidechain)
    TapPoint     tapPoint = TapPoint::PreFX;

    RoutingConnection() = default;
    RoutingConnection(const RoutingConnection&) = delete;
    RoutingConnection& operator=(const RoutingConnection&) = delete;

    juce::ValueTree getState() const
    {
        juce::ValueTree v("Connection");
        v.setProperty("id",       id,           nullptr);
        v.setProperty("source",   sourceNodeId, nullptr);
        v.setProperty("dest",     destNodeId,   nullptr);
        v.setProperty("type",     (int)type,    nullptr);
        v.setProperty("gain",     gain.load(std::memory_order_relaxed), nullptr);
        v.setProperty("active",   active.load(std::memory_order_relaxed),   nullptr);
        v.setProperty("bypassed", bypassed.load(std::memory_order_relaxed), nullptr);
        if (type == ConnectionType::Sidechain)
        {
            v.setProperty("scPluginId",  destinationPluginId, nullptr);
            v.setProperty("scBusIndex",  destinationBusIndex, nullptr);
            v.setProperty("scTapPoint",  (int)tapPoint,       nullptr);
        }
        return v;
    }

    void restoreState(const juce::ValueTree& v)
    {
        id           = v.getProperty("id",       "").toString();
        sourceNodeId = v.getProperty("source",   "").toString();
        destNodeId   = v.getProperty("dest",     "").toString();
        type         = (ConnectionType)(int)v.getProperty("type", 0);
        gain.store((float)v.getProperty("gain",     1.0f), std::memory_order_relaxed);
        active.store((bool)v.getProperty("active",   true),  std::memory_order_relaxed);
        bypassed.store((bool)v.getProperty("bypassed", false), std::memory_order_relaxed);
        if (type == ConnectionType::Sidechain)
        {
            destinationPluginId = v.getProperty("scPluginId", "").toString();
            destinationBusIndex = (int)v.getProperty("scBusIndex", 1);
            tapPoint            = (TapPoint)(int)v.getProperty("scTapPoint", (int)TapPoint::PreFX);
        }
    }
};

} // namespace DAW
