#pragma once
#include <atomic>

namespace DAW {

/**
 * MasterMuteCore — master bus mute control (global kill switch).
 *
 * When master is muted:
 *   - ALL audio output is silenced
 *   - Monitor output = silence
 *   - Render output preview = silence
 *   - Meters may still show signal (pre-mute metering optional)
 *
 * Master mute is the final mix kill switch.
 */
class MasterMuteCore
{
public:
    void setMuted(bool muted) noexcept
    {
        muted_.store(muted, std::memory_order_relaxed);
    }

    bool isMuted() const noexcept
    {
        return muted_.load(std::memory_order_relaxed);
    }

    /** Returns gain multiplier: 0.0 if muted, 1.0 otherwise. */
    float getMuteGain() const noexcept
    {
        return muted_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
    }

private:
    std::atomic<bool> muted_{ false };
};

} // namespace DAW
