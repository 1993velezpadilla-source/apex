#pragma once

namespace DAW {

/**
 * MasterAutomationEngine — automation playback/record for the master channel.
 *
 * Version 1: placeholder architecture.
 * Future: reads/writes automation lanes for master fader, pan, inserts.
 */
class MasterAutomationEngine
{
public:
    enum class Mode { Off, Read, Write, Touch, Latch };

    void setMode(Mode m) noexcept { mode_ = m; }
    Mode getMode() const noexcept { return mode_; }

    /** Returns the automated gain value for the current position. */
    float getAutomatedGain(int64_t /*samplePosition*/) const noexcept
    {
        // v1: no automation — returns unity
        return 1.0f;
    }

private:
    Mode mode_ = Mode::Off;
};

} // namespace DAW
