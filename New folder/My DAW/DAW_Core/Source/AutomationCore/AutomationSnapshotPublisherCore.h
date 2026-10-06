#pragma once
#include <JuceHeader.h>
#include "AutomationSnapshotCore.h"
#include <atomic>
#include <memory>

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
    std::shared_ptr<AutomationSnapshot> snapshot_;
};

} // namespace DAW
