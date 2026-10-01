#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * BubblegumVisualCore — computes per-track visual state for rendering.
 *
 * Source track: pink outline + subtle glow.
 * Target ON:    pink filled circle, radial ring shows level, soft glow.
 * Target OFF:   grey circle, low opacity.
 *
 * Matches Harrison-style analog workflow, modernized visually.
 */
class BubblegumVisualCore
{
public:
    enum class TrackVisualRole { None, Source, TargetOff, TargetOn };

    struct TrackVisualState
    {
        TrackID         trackId;
        TrackVisualRole role          = TrackVisualRole::None;
        float           sendLevel    = 0.0f;   // 0..2 for ring indicator
        float           glowIntensity = 0.0f;  // 0..1 base glow
        float           touchBoost   = 0.0f;   // 0..1 active-touch extra glow
        bool            isPulsing    = false;
    };

    static constexpr juce::uint32 kPinkColor  = 0xFFFF7DB8;
    static constexpr juce::uint32 kPinkDark   = 0xFFC44E88;
    static constexpr juce::uint32 kGreyOff    = 0xFF666666;
    static constexpr float kSourceGlow        = 0.4f;
    static constexpr float kTargetOnGlow      = 0.3f;
    static constexpr float kTouchBoostGlow    = 0.5f;

    TrackVisualState computeState(const TrackID& trackId,
                                  const TrackID& sourceId,
                                  bool  hasSend,
                                  float sendLevel) const
    {
        TrackVisualState s;
        s.trackId = trackId;

        if (trackId == sourceId)
        {
            s.role          = TrackVisualRole::Source;
            s.glowIntensity = kSourceGlow;
        }
        else if (hasSend)
        {
            s.role          = TrackVisualRole::TargetOn;
            s.sendLevel     = sendLevel;
            s.glowIntensity = kTargetOnGlow;
            s.isPulsing     = true;
        }
        else
        {
            s.role          = TrackVisualRole::TargetOff;
            s.glowIntensity = 0.0f;
        }
        return s;
    }

    void applyTouchBoost(TrackVisualState& state) const
    {
        state.touchBoost = kTouchBoostGlow;
    }

    void clearTouchBoost(TrackVisualState& state) const
    {
        state.touchBoost = 0.0f;
    }
};

} // namespace DAW
