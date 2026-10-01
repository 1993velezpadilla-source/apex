#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include "BubblegumCableGravityCore.h"
#include <cmath>
#include <array>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeSplashCore — splash particles at drip tip impact
//
// When each drip tip reaches maximum extension, renders a brief splash:
//   - 5-8 small arc particles shooting outward from the tip
//   - Each particle follows a parabolic arc under gravity
//   - They fade out quickly (0.3-0.5s lifetime)
//   - The impact point gets a brief bright flash ring
//
// Splash positions are CABLE-LOCAL (derived from drip t position +
// gravity drop distance). Scroll-safe.
//
// DYNAMIC: call every frame.
// =====================================================================
class BubblegumCableSlimeSplashCore
{
public:
    struct SplashParticle
    {
        float dripT    = 0.5f;   // which drip t position spawned this
        float angle    = 0.f;    // emission angle (radians from straight down)
        float speed    = 1.f;    // emission speed
        float age      = 0.f;
        float life     = 0.35f;
        float radius   = 1.4f;
        float srcId    = 0.f;
        bool  alive() const noexcept { return age < life; }
        float alpha()  const noexcept
        {
            const float p = age / life;
            return juce::jlimit(0.f, 1.f, (1.0f - p) * 2.5f);
        }
    };

    void update(float dt) noexcept
    {
        for (auto& p : particles_)
            p.age += dt;
        particles_.erase(
            std::remove_if(particles_.begin(), particles_.end(),
                [](const SplashParticle& p) { return !p.alive(); }),
            particles_.end());
    }

    void spawnSplash(float dripT, float srcId,
                     float energy, float spawnSeed) noexcept
    {
        const int count = 5 + (int)(BubblegumCableAnimationCore::stable01(spawnSeed) * 4.f);
        for (int i = 0; i < count; ++i)
        {
            if (particles_.size() >= kMaxParticles) break;
            const float si = spawnSeed + (float)i * 3.7f;
            SplashParticle p;
            p.dripT  = dripT;
            p.srcId  = srcId;
            p.angle  = juce::jmap(BubblegumCableAnimationCore::stable01(si + 0.1f),
                                  -1.2f, 1.2f);   // ±70 degrees from down
            p.speed  = 18.f + BubblegumCableAnimationCore::stable01(si + 1.3f) * 22.f
                     + energy * 12.f;
            p.life   = 0.28f + BubblegumCableAnimationCore::stable01(si + 2.1f) * 0.22f;
            p.radius = 1.0f + BubblegumCableAnimationCore::stable01(si + 3.4f) * 1.6f;
            p.age    = 0.f;
            particles_.push_back(p);
        }
    }

    void paintSplashes(juce::Graphics& g,
                       const BubblegumCableGeometry& geo,
                       float srcId,
                       float energy) const
    {
        const float e = juce::jlimit(0.f, 1.f, energy);

        for (const auto& p : particles_)
        {
            if (!p.alive() || p.srcId != srcId) continue;
            const float a = p.alpha();
            if (a < 0.01f) continue;

            // Tip world position (straight down from drip attachment)
            const float dripH = 18.f;   // approx drip length at normal size
            const auto  tip   = BubblegumCableGravityCore::gravityDrop(geo, p.dripT, dripH);

            // Particle position: parabolic arc from tip
            const float t   = p.age / p.life;
            const float dx  = std::sin(p.angle) * p.speed * p.age;
            const float dy  = std::cos(p.angle) * p.speed * p.age
                            + 0.5f * 280.f * p.age * p.age;   // gravity

            const float px  = tip.x + dx;
            const float py  = tip.y + dy;
            const float r   = juce::jmax(0.6f, p.radius * (1.0f - t * 0.4f));

            juce::ColourGradient grad(
                kDropCore.withAlpha(a * (0.80f + e * 0.10f)), px, py - r * 0.2f,
                kDropEdge.withAlpha(a * (0.55f + e * 0.06f)), px, py + r, false);
            g.setGradientFill(grad);
            g.fillEllipse(px - r, py - r * 0.85f, r * 2.0f, r * 1.7f);
        }
    }

    void invalidate(float srcId) noexcept
    {
        particles_.erase(
            std::remove_if(particles_.begin(), particles_.end(),
                [srcId](const SplashParticle& p) { return p.srcId == srcId; }),
            particles_.end());
    }

    void reset() noexcept { particles_.clear(); }

private:
    static constexpr size_t kMaxParticles = 60;
    std::vector<SplashParticle> particles_;

    const juce::Colour kDropCore { 0xFFFF9AC6 };
    const juce::Colour kDropEdge { 0xFF9B2055 };
};

} // namespace DAW
