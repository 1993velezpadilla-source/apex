#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * AFLCore — After Fader Listen.
 *
 * Taps the signal AFTER fader and FX processing.
 * Routes to the monitor system for listening without affecting
 * the master mix or render path.
 *
 * Use cases:
 *   - Checking final signal level
 *   - Hearing processed output in isolation
 */
class AFLCore
{
public:
    void setActive(bool active) noexcept { active_ = active; }
    bool isActive() const noexcept { return active_; }

    void setSourceNodeId(const juce::String& nodeId) { sourceNodeId_ = nodeId; }
    const juce::String& getSourceNodeId() const { return sourceNodeId_; }

    /** Returns true if AFL should override normal monitor input. */
    bool shouldOverrideMonitor() const noexcept { return active_; }

private:
    bool active_ = false;
    juce::String sourceNodeId_;
};

} // namespace DAW
