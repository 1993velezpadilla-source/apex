#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * BubblegumCableAudioEnvelopeCore — lightweight per-send envelope follower.
 *
 * Smooths raw send levels into a 0→1 energy signal suitable for driving
 * cable thickness, glow, flow speed, and drip stretch.
 *
 * Attack ~30ms, Release ~180ms at 30Hz update rate.
 */
class BubblegumCableAudioEnvelopeCore
{
public:
    struct SendEnvelope
    {
        TrackID targetId;
        float   raw      = 0.f;
        float   smoothed = 0.f;
    };

    void tick(float deltaMs) noexcept
    {
        float dt = deltaMs * 0.001f; // seconds
        float attackCoeff  = 1.f - std::exp(-dt / 0.030f);  // ~30ms attack
        float releaseCoeff = 1.f - std::exp(-dt / 0.180f);  // ~180ms release

        for (auto& env : envelopes_)
        {
            float coeff = (env.raw > env.smoothed) ? attackCoeff : releaseCoeff;
            env.smoothed += (env.raw - env.smoothed) * coeff;
        }
    }

    void feed(const TrackID& targetId, float rawLevel)
    {
        for (auto& env : envelopes_)
        {
            if (env.targetId == targetId)
            {
                env.raw = juce::jlimit(0.f, 1.f, rawLevel);
                return;
            }
        }
        envelopes_.push_back({ targetId, juce::jlimit(0.f, 1.f, rawLevel), 0.f });
    }

    float getEnergy(const TrackID& targetId) const noexcept
    {
        for (auto& env : envelopes_)
            if (env.targetId == targetId) return env.smoothed;
        return 0.f;
    }

    void clear() noexcept { envelopes_.clear(); }

private:
    std::vector<SendEnvelope> envelopes_;
};

} // namespace DAW
