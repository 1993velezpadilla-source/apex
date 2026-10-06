#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace DAW {

// =====================================================================
// BubblegumCableLiquidCore — cable-local liquid particle system.
//
// ALL particle state is stored in CABLE-LOCAL parametric space:
//   laneT     = [0,1] position along cable centerline
//   normalBias= [-1,1] cross-section bias (0=center)
//
// No world-space (x,y) stored between frames. This means:
//   - Mixer scroll / resize never invalidates particles.
//   - invalidateDroplets() no longer needed (kept for compat, is no-op).
//   - No stale-coordinate smearing on viewport change.
//
// Particles spawn inside the slime body (not below it) and drift along
// the cable in laneT space. They are rendered by the caller via
// forEachAliveParticle().
// =====================================================================
class BubblegumCableLiquidCore
{
public:
    struct Particle
    {
        float laneT      = 0.5f;
        float normalBias = 0.0f;
        float vLane      = 0.0f;   // drift velocity in laneT/sec
        float vNormal    = 0.0f;   // drift velocity in normalBias/sec
        float age        = 0.0f;
        float life       = 1.0f;
        float srcId      = 0.0f;
        float radius     = 2.0f;   // base visual radius (pixels at halfW=1)
        float blobIndex  = 0.0f;   // which blob lane this particle belongs to

        bool  alive() const noexcept { return age < life; }
        float alpha() const noexcept
        {
            // Smooth fade in + fade out
            const float p = age / life;
            const float fadeIn  = juce::jlimit(0.f, 1.f, p * 6.0f);
            const float fadeOut = juce::jlimit(0.f, 1.f, (1.0f - p) * 4.0f);
            return fadeIn * fadeOut;
        }
    };

    void setEnabled(bool e) noexcept { enabled_ = e; }
    bool isEnabled() const noexcept  { return enabled_; }

    // No-op kept for back-compat with any caller that calls invalidateDroplets()
    void invalidateDroplets() noexcept {}

    void reset() noexcept
    {
        particles_.clear();
    }

    // ── Per-frame update ─────────────────────────────────────────────────
    // dt in seconds. Advances all particles in laneT-space only.
    void update(float dt, float /*pulse*/) noexcept
    {
        for (auto& p : particles_)
        {
            p.laneT      += p.vLane   * dt;
            p.normalBias += p.vNormal * dt;
            p.laneT      = juce::jlimit(0.05f, 0.95f, p.laneT);
            p.normalBias = juce::jlimit(-0.55f, 0.55f, p.normalBias);
            p.vLane      *= std::pow(0.97f, dt * 60.0f);
            p.vNormal    *= std::pow(0.97f, dt * 60.0f);
            p.age        += dt;
        }

        particles_.erase(
            std::remove_if(particles_.begin(), particles_.end(),
                [](const Particle& p) { return !p.alive(); }),
            particles_.end());
    }

    // ── Spawn a particle inside a specific blob lane ─────────────────────
    void spawnInBlobLane(float srcId,
                         float blobLaneT,
                         int   blobIndex,
                         float energy,
                         float seed) noexcept
    {
        if (!enabled_) return;
        if (countFor(srcId) >= kMaxPerCable) return;

        auto rnd = [](float x) {
            return BubblegumCableAnimationCore::stable01(x) * 2.0f - 1.0f;
        };

        Particle p;
        p.srcId      = srcId;
        p.blobIndex  = (float)blobIndex;
        p.laneT      = juce::jlimit(0.08f, 0.92f,
                            blobLaneT + rnd(seed + 1.1f) * 0.022f);
        p.normalBias = rnd(seed + 2.3f) * 0.18f;
        // Faster drift in laneT direction
        p.vLane      = rnd(seed + 3.7f) * (0.12f + energy * 0.10f);
        p.vNormal    = rnd(seed + 4.9f) * (0.06f + energy * 0.04f);
        p.age        = 0.0f;
        p.life       = 0.55f + BubblegumCableAnimationCore::stable01(seed + 5.1f) * 0.45f;
        p.radius     = 1.6f + BubblegumCableAnimationCore::stable01(seed + 6.3f) * 1.4f;
        particles_.push_back(p);
    }

    // ── Iterate alive particles for a specific cable ─────────────────────
    template<typename Fn>
    void forEachAliveParticle(float srcId, Fn&& fn) const
    {
        for (const auto& p : particles_)
            if (p.alive() && p.srcId == srcId)
                fn(p);
    }

    // ── Spawn rate ticker — returns true when a new particle should spawn ─
    bool shouldSpawnThisTick(float srcId, float energy,
                              float spawnSeed, float dt) noexcept
    {
        if (!enabled_) return false;
        if (countFor(srcId) >= kMaxPerCable) return false;
        const float chance = (0.35f + energy * 0.45f) * dt;
        return BubblegumCableAnimationCore::stable01(spawnSeed + spawnPhase_++ * 0.0013f)
               < chance;
    }

private:
    int countFor(float srcId) const noexcept
    {
        int n = 0;
        for (const auto& p : particles_)
            if (p.alive() && p.srcId == srcId) ++n;
        return n;
    }

    static constexpr int kMaxPerCable = 6;

    bool enabled_ = true;
    float spawnPhase_ = 0.f;
    std::vector<Particle> particles_;
};

} // namespace DAW
