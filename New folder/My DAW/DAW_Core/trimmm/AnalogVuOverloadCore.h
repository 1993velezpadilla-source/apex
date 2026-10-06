#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace DAW {

/**
 * AnalogVuOverloadCore
 *
 * Latch state for the OL (overload) light. Once the input peak crosses 0 dBFS,
 * `latched` becomes true and stays true until reset() is called — typically
 * when the user clicks the OL bulb on the panel.
 *
 * Also tracks the worst overshoot in dB above 0 dBFS since last reset, so the
 * panel can display "+1.4 dB" next to the bulb if desired.
 *
 * Per-instance state. Each panel/track gets its own latch. Reset on track
 * rebind (the panel calls reset() in resetAll-style flow).
 */
class AnalogVuOverloadCore
{
public:
    /** Feed one peak gain sample (linear, 1.0 = 0 dBFS). */
    void feed(float peakGain) noexcept
    {
        if (peakGain >= 1.0f)
        {
            latched_ = true;
            const float overDb = 20.0f * std::log10(juce::jmax(1.0f, peakGain));
            if (overDb > maxOverDb_) maxOverDb_ = overDb;
        }
    }

    bool  isLatched()    const noexcept { return latched_; }
    float getMaxOverDb() const noexcept { return maxOverDb_; }

    /** Clear the latch and the overshoot value. */
    void reset() noexcept
    {
        latched_   = false;
        maxOverDb_ = 0.0f;
    }

private:
    bool  latched_   { false };
    float maxOverDb_ { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuOverloadCore)
};

} // namespace DAW
