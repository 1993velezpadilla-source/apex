#pragma once
#include <JuceHeader.h>
#include "AutomationSnapshotCore.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace DAW {

class AutomationManagerCore;

class AutomationSnapshotPublisherCore
{
public:
    AutomationSnapshotPublisherCore()
        : snapshot_(std::make_shared<AutomationSnapshot>()) {}

    void publish(const AutomationManagerCore& manager);

    std::shared_ptr<const AutomationSnapshot> get() const noexcept
    {
        return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
    }

private:
    // Only the control thread publishes and reclaims. The audio thread loads
    // one shared immutable snapshot and never performs retired cleanup.
    // Keep every superseded generation alive while a realtime callback still
    // holds a reference; a single "retired" slot is insufficient when the UI
    // publishes several edits within one callback.
    void collectRetiredSnapshotsOnControlThread() noexcept
    {
        const auto kept = std::remove_if(retiredSnapshots_.begin(), retiredSnapshots_.end(),
            [](const std::shared_ptr<const AutomationSnapshot>& item) noexcept
            {
                return item.use_count() == 1;
            });
        retiredSnapshots_.erase(kept, retiredSnapshots_.end());
    }

    std::shared_ptr<const AutomationSnapshot> snapshot_;
    std::vector<std::shared_ptr<const AutomationSnapshot>> retiredSnapshots_;
};

} // namespace DAW
