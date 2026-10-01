#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * SendMuteCore — manages mute state for individual sends.
 *
 * A muted send outputs silence to its destination without
 * affecting the source track's main output or other sends.
 */
class SendMuteCore
{
public:
    void setMuted(bool muted) noexcept { muted_ = muted; }
    bool isMuted() const noexcept { return muted_; }

    /** Returns effective gain: 0.0 if muted, 1.0 otherwise. */
    float getGainMultiplier() const noexcept { return muted_ ? 0.0f : 1.0f; }

private:
    bool muted_ = false;
};

} // namespace DAW
