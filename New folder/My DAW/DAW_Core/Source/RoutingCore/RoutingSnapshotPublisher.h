#pragma once
#include <JuceHeader.h>
#include <memory>
#include <atomic>
#include <cstdint>
#include "RoutingSnapshot.h"

// Forward declaration — full include pulled in via RoutingGraph.h users.
namespace DAW { class RoutingGraph; }

namespace DAW {

/**
 * RoutingSnapshotPublisher — lock-free bridge between message thread and audio thread.
 *
 * Message thread calls publish(graph) after any graph or connection-state change.
 * Audio thread calls get() — zero locks, zero graph pointer dereference.
 *
 * Pattern mirrors FolderBusStateModel (see Source/FolderBusCore/FolderBusStateModel.h).
 */
class RoutingSnapshotPublisher
{
public:
    RoutingSnapshotPublisher()
        : snapshot_(std::make_shared<RoutingSnapshot>()) {}

    /**
     * Called from the message thread after any graph or connection-state change.
     * Builds a fresh RoutingSnapshot from current graph state and publishes atomically.
     * graph must be held under its graphLock_ by the caller.
     */
    void publish(const RoutingGraph& graph);

    /**
     * Called from the audio thread.  Zero locks.
     */
    std::shared_ptr<const RoutingSnapshot> get() const noexcept
    {
        return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
    }

private:
    std::shared_ptr<RoutingSnapshot> snapshot_;
    uint64_t nextVersion_ = 1;
};

} // namespace DAW
