#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * PFLCore — Pre Fader Listen.
 *
 * Taps the signal BEFORE the fader (and optionally before FX).
 * Routes to the monitor system for gain staging and raw signal
 * inspection without affecting the master mix or render path.
 *
 * Use cases:
 *   - Gain staging
 *   - Checking raw/unprocessed signal
 */
class PFLCore
{
public:
    void setActive(bool active) noexcept { active_ = active; }
    bool isActive() const noexcept { return active_; }

    void setSourceNodeId(const juce::String& nodeId) { sourceNodeId_ = nodeId; }
    const juce::String& getSourceNodeId() const { return sourceNodeId_; }

    /** Returns true if PFL should override normal monitor input. */
    bool shouldOverrideMonitor() const noexcept { return active_; }

private:
    bool active_ = false;
    juce::String sourceNodeId_;
};

} // namespace DAW
