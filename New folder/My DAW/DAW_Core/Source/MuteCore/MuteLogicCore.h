#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * MuteLogicCore — centralized mute state resolution.
 *
 * Manages the different mute tiers:
 *   - Track mute: kills track before routing
 *   - Bus mute: kills bus before master
 *   - Master mute: kills final mix (global kill switch)
 *   - Monitor mute: kills speakers only (engine stays alive)
 *
 * These are SEPARATE systems with different effects.
 */
class MuteLogicCore
{
public:
    enum class MuteTier { Track, Bus, Master, Monitor };

    /** Returns true if the track/bus should produce audio. */
    static bool isNodeAudible(bool trackMuted, bool masterMuted) noexcept
    {
        // Master mute kills everything; track mute kills just that track
        if (masterMuted) return false;
        return !trackMuted;
    }

    /** Returns true if monitor output should produce sound. */
    static bool isMonitorAudible(bool masterMuted, bool monitorMuted) noexcept
    {
        if (masterMuted) return false;
        return !monitorMuted;
    }

    /** Returns true if render output should produce sound. */
    static bool isRenderAudible(bool masterMuted) noexcept
    {
        // Render respects master mute only (not monitor mute)
        return !masterMuted;
    }
};

} // namespace DAW
