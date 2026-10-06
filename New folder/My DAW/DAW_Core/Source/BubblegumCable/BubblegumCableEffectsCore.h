#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableGeometryCore.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace DAW {

// =====================================================================
// BubblegumCableEffectsCore — OPTIONAL POLISH.
//
// Hyperreal effects only: attached drips, restrained detached droplets,
// and subtle wet merge accents. Never gates cable visibility.
// =====================================================================
class BubblegumCableEffectsCore
{
public:
    struct Droplet
    {
        float x = 0.f, y = 0.f;
        float vx = 0.f, vy = 0.f;
        float laneT = 0.5f;
        float normalBias = 0.f;
        float age = 0.f;
        float life = 0.6f;
        float srcId = 0.f;
        float radius = 1.8f;
        bool  alive() const noexcept { return age < life; }
        float alpha() const noexcept { return juce::jmax(0.f, 1.f - age / life); }
    };

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool isEnabled() const noexcept        { return enabled_; }

    /** Returns true when there is live animated content that requires
     *  a repaint: detached droplets still alive, or hub accents pending. */
    bool hasActiveEffects() const noexcept
    {
        if (!enabled_) return false;
        if (!droplets_.empty())   return true;
        if (!pendingHubs_.empty()) return true;
        return false;
    }

    /** Call on mixer viewport move/scroll/resize to immediately purge all
     *  detached droplets. Their world-space coordinates are now stale.
     *  Attached drips are unaffected — they derive position from live geo. */
    void invalidateDroplets() noexcept
    {
        droplets_.clear();
    }

    void update(float deltaMs, float pulseAlpha) noexcept
    {
        const float dt = deltaMs * 0.001f;
        time_  += dt;
        pulse_  = pulseAlpha;

        for (auto& d : droplets_)
        {
            d.laneT += d.vx * dt * 0.012f;
            d.normalBias += d.vy * dt * 0.030f;
            d.laneT = juce::jlimit(0.10f, 0.90f, d.laneT);
            d.normalBias = juce::jlimit(-0.35f, 0.35f, d.normalBias);
            d.vx  *= std::pow(0.985f, dt * 60.0f);
            d.vy  *= std::pow(0.985f, dt * 60.0f);
            d.age += dt;
        }

        droplets_.erase(std::remove_if(droplets_.begin(), droplets_.end(),
            [](const Droplet& d) { return !d.alive(); }), droplets_.end());
    }

    void reset() noexcept
    {
        droplets_.clear();
        pendingHubs_.clear();
        time_ = 0.f;
        pulse_ = 0.f;
    }

    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo,
               const BubblegumCableInput& in) const
    {
        if (!enabled_) return;

        auto* self = const_cast<BubblegumCableEffectsCore*>(this);

        // ── Stale-droplet guard ─────────────────────────────────────────
        // Detached droplets store world-space (overlay-local) coordinates.
        // When the mixer scrolls/resizes, cable endpoints shift in the overlay
        // coordinate system but existing droplets don't move with them.
        // Solution: compare current endpoints against the last known positions
        // for this cable ID and purge stale detached droplets on any movement.
        {
            const float id = in.id();
            auto it = self->lastEndpoints_.find(id);
            const bool endpointsMoved = (it != self->lastEndpoints_.end()) &&
                (std::abs(it->second[0] - geo.sx) > 0.5f ||
                 std::abs(it->second[1] - geo.sy) > 0.5f ||
                 std::abs(it->second[2] - geo.tx) > 0.5f ||
                 std::abs(it->second[3] - geo.ty) > 0.5f);

            if (endpointsMoved)
            {
                // Purge detached droplets belonging to this cable
                self->droplets_.erase(
                    std::remove_if(self->droplets_.begin(), self->droplets_.end(),
                        [id](const Droplet& d) { return d.srcId == id; }),
                    self->droplets_.end());
            }

            self->lastEndpoints_[id] = { geo.sx, geo.sy, geo.tx, geo.ty };
        }

        self->registerHub(geo.sx, geo.sy, in.sendEnergy);
        self->registerHub(geo.tx, geo.ty, in.sendEnergy);

        const auto anchors = self->buildAttachedDripAnchors(geo, in);
        drawAttachedDrips(g, anchors, in.sendEnergy, self->time_, self->pulse_);
        self->spawnFromAttachedAnchors(anchors, geo, in, 0.0f);
        drawEmbeddedDroplets(g, geo, in.id(), in.sendEnergy, self->time_, self->droplets_);
    }

    void paintDroplets(juce::Graphics& g) const
    {
        auto* self = const_cast<BubblegumCableEffectsCore*>(this);
        if (!enabled_)
        {
            self->pendingHubs_.clear();
            return;
        }

        drawPendingHubAccents(g, self->pendingHubs_, self->time_, self->pulse_);
        self->pendingHubs_.clear();

    }

    void maybeSpawnDroplet(const BubblegumCableGeometry& geo,
                           const BubblegumCableInput& in,
                           float triggerBoost) noexcept
    {
        const auto anchors = buildAttachedDripAnchors(geo, in);
        spawnFromAttachedAnchors(anchors, geo, in, triggerBoost);
    }

private:
    struct DripAnchor
    {
        float x = 0.f, y = 0.f;
        float t = 0.f;
        float stemLength = 0.f;
        float bulbRadius = 0.f;
        float phase = 0.f;
    };

    struct HubAccent
    {
        float x = 0.f, y = 0.f;
        float energy = 0.f;
        int cableCount = 0;
        float phase = 0.f;
    };

    static float frac(float v) noexcept
    {
        return v - std::floor(v);
    }

    static float stable01(float seed) noexcept
    {
        return frac(std::sin(seed * 12.9898f + 78.233f) * 43758.5453f);
    }

    static float stableSigned(float seed) noexcept
    {
        return stable01(seed) * 2.0f - 1.0f;
    }

    static float blobLaneTAt(const BubblegumCableGeometry& geo,
                             float srcId,
                             float time,
                             int laneIndex) noexcept
    {
        const float seedBase = srcId * 0.031f + geo.tx * 0.017f + geo.ty * 0.013f;
        const int blobCount  = 3 + (int) std::floor(stable01(seedBase + 1.7f) * 3.f);
        const int idx = juce::jlimit(0, juce::jmax(0, blobCount - 1), laneIndex);
        const float baseT = juce::jmap((float) (idx + 1) / (float) (blobCount + 1), 0.18f, 0.82f);
        const float drift = std::sin(time * (0.18f + 0.03f * (float) idx)
                          + stable01(seedBase + (float) idx * 9.1f)
                          * juce::MathConstants<float>::twoPi) * 0.028f;
        return juce::jlimit(0.08f, 0.92f, baseT + drift);
    }

    static float nearestBlobLaneT(const BubblegumCableGeometry& geo,
                                  float srcId,
                                  float time,
                                  float approxT) noexcept
    {
        const float seedBase = srcId * 0.031f + geo.tx * 0.017f + geo.ty * 0.013f;
        const int blobCount  = 3 + (int) std::floor(stable01(seedBase + 1.7f) * 3.f);

        float bestT = 0.5f;
        float bestD = 1.0e9f;
        for (int i = 0; i < blobCount; ++i)
        {
            const float t = blobLaneTAt(geo, srcId, time, i);
            const float d = std::abs(t - approxT);
            if (d < bestD)
            {
                bestD = d;
                bestT = t;
            }
        }
        return bestT;
    }

    static DripAnchor makeAnchorAt(const BubblegumCableGeometry& geo,
                                   float t,
                                   float sizeJitter,
                                   float phase,
                                   float energy,
                                   float pulse,
                                   float time) noexcept
    {
        const float clampedT = juce::jlimit(0.0f, 1.0f, t);
        const float fIndex = clampedT * (float) BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int) std::floor(fIndex));
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a = fIndex - (float) i0;

        const auto& p0 = geo.points[(size_t) i0];
        const auto& p1 = geo.points[(size_t) i1];

        float x  = juce::jmap(a, p0.x, p1.x);
        float y  = juce::jmap(a, p0.y, p1.y);
        float nx = juce::jmap(a, p0.nx, p1.nx);
        float ny = juce::jmap(a, p0.ny, p1.ny);
        float hw = juce::jmap(a, p0.halfW, p1.halfW);

        const float nLen = std::sqrt(nx * nx + ny * ny);
        if (nLen > 0.0001f)
        {
            nx /= nLen;
            ny /= nLen;
        }
        else
        {
            nx = 0.0f;
            ny = -1.0f;
        }

        DripAnchor anchor;
        anchor.t = clampedT;
        anchor.x = x - nx * hw * 0.96f;
        anchor.y = y - ny * hw * 0.96f;
        anchor.phase = phase;
        // Drips must stay clearly smaller than cable thickness so the cable
        // dominates visually. Cable base = 14 (halfW ~7-9), drip bulb ~2-3.
        anchor.bulbRadius = (1.2f + sizeJitter * 1.1f + energy * 0.35f) * 0.75f;
        anchor.stemLength = (4.0f + sizeJitter * 3.0f + energy * 1.8f
                          + pulse * 0.8f * (0.65f + 0.35f * std::sin(time * 1.5f + phase))) * 0.75f;
        return anchor;
    }

    std::vector<DripAnchor> buildAttachedDripAnchors(const BubblegumCableGeometry& geo,
                                                     const BubblegumCableInput& in) const
    {
        const float e = juce::jlimit(0.0f, 1.0f, in.sendEnergy);
        const float seedBase = in.id() * 0.071f + geo.tx * 0.013f + geo.ty * 0.019f;
        // Fewer drips (1..2) so they read as occasional wet beads, not a row
        // of ornaments competing with the cable body for attention.
        const int   count    = 1 + (int) std::floor(stable01(seedBase + 1.1f) * 2.f);
        std::vector<DripAnchor> anchors;
        anchors.reserve((size_t) count);

        for (int i = 0; i < count; ++i)
        {
            const float baseT   = juce::jmap((float)(i + 1) / (float)(count + 1), 0.40f, 0.62f);
            const float jitter  = stableSigned(seedBase + (float) i * 4.7f) * 0.035f;
            const float anchorT = juce::jlimit(0.36f, 0.66f, baseT + jitter);
            const float sizeJitter = stable01(seedBase + i * 8.3f);
            const float phase = stable01(seedBase + i * 13.1f) * juce::MathConstants<float>::twoPi;
            anchors.push_back(makeAnchorAt(geo, anchorT, sizeJitter, phase, e, pulse_, time_));
        }

        return anchors;
    }

    static juce::Path makeAttachedDripPath(const DripAnchor& a, float stretch)
    {
        const float neckW = juce::jmax(1.1f, a.bulbRadius * 0.52f);
        const float shoulderW = a.bulbRadius * 1.18f;
        const float bulbY = a.y + stretch + a.bulbRadius * 0.62f;

        juce::Path p;
        p.startNewSubPath(a.x - neckW * 0.5f, a.y - 0.10f);
        p.quadraticTo(a.x - shoulderW * 0.72f, a.y + stretch * 0.46f,
                      a.x - a.bulbRadius * 0.92f, bulbY - a.bulbRadius * 0.10f);
        p.quadraticTo(a.x - a.bulbRadius * 0.80f, bulbY + a.bulbRadius * 0.82f,
                      a.x, bulbY + a.bulbRadius * 1.05f);
        p.quadraticTo(a.x + a.bulbRadius * 0.80f, bulbY + a.bulbRadius * 0.82f,
                      a.x + a.bulbRadius * 0.92f, bulbY - a.bulbRadius * 0.10f);
        p.quadraticTo(a.x + shoulderW * 0.72f, a.y + stretch * 0.46f,
                      a.x + neckW * 0.5f, a.y - 0.10f);
        p.closeSubPath();
        return p;
    }

    static void drawAttachedDrips(juce::Graphics& g,
                                  const std::vector<DripAnchor>& anchors,
                                  float sendEnergy,
                                  float time,
                                  float pulse)
    {
        const float e = juce::jlimit(0.0f, 1.0f, sendEnergy);
        const auto fillTop = juce::Colour(0xFFFFD7E8);
        const auto fillMid = juce::Colour(0xFFFF82B8);
        const auto fillBot = juce::Colour(0xFF9B5478);

        for (const auto& a : anchors)
        {
            const float stretch = a.stemLength * (0.95f + 0.08f * std::sin(time * 1.8f + a.phase)
                                                + pulse * 0.04f);
            const auto drip = makeAttachedDripPath(a, stretch);
            const float topY = a.y;
            const float bottomY = a.y + stretch + a.bulbRadius * 1.9f;

            // Drips must read as subtle matte-gloss, never as glowing particles.
            // Global alpha × 0.6 so the cable always dominates visually.
            constexpr float dripAlphaScale = 0.6f;
            juce::ColourGradient grad(fillTop.withAlpha((0.72f + e * 0.08f) * dripAlphaScale), a.x, topY,
                                      fillBot.withAlpha((0.82f + e * 0.06f) * dripAlphaScale), a.x, bottomY,
                                      false);
            grad.addColour(0.42, fillMid.withAlpha((0.80f + e * 0.08f) * dripAlphaScale));
            g.setGradientFill(grad);
            g.fillPath(drip);

            g.setColour(juce::Colour(0xFF050505).withAlpha((0.44f + e * 0.12f) * dripAlphaScale));
            g.strokePath(drip, juce::PathStrokeType(0.95f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Minimal highlight — no strong glow. Keep drips matte-gloss only.
            g.setColour(juce::Colours::white.withAlpha((0.06f + e * 0.02f) * dripAlphaScale));
            g.fillEllipse(a.x - a.bulbRadius * 0.42f,
                          a.y + stretch * 0.58f,
                          a.bulbRadius * 0.58f,
                          a.bulbRadius * 0.92f);
        }
    }

    void spawnFromAttachedAnchors(const std::vector<DripAnchor>& anchors,
                                  const BubblegumCableGeometry& geo,
                                  const BubblegumCableInput& in,
                                  float triggerBoost) noexcept
    {
        if (!enabled_ || anchors.empty()) return;
        if (visibleDropletCountFor(in.id()) >= 4) return;

        const float energy = juce::jlimit(0.0f, 1.0f, in.sendEnergy);
        const float spawnChance = 0.0015f + energy * 0.0025f + juce::jmax(0.0f, triggerBoost) * 0.0040f;
        if (juce::Random::getSystemRandom().nextFloat() > spawnChance) return;

        const int anchorIndex = juce::Random::getSystemRandom().nextInt((int) anchors.size());
        const auto& a = anchors[(size_t) anchorIndex];

        const float seedBase = in.id() * 0.031f + geo.tx * 0.017f + geo.ty * 0.013f;
        const int blobCount  = 3 + (int) std::floor(stable01(seedBase + 1.7f) * 3.f);
        const int blobIndex = juce::Random::getSystemRandom().nextInt(blobCount);
        const float laneT = blobLaneTAt(geo, in.id(), time_, blobIndex);
        const auto spawnPt = sampleInsideBodyAt(geo, laneT,
            -0.05f + stableSigned(in.id() * 0.19f + a.t * 11.0f) * 0.10f);

        Droplet d;
        d.x = spawnPt.x;
        d.y = spawnPt.y;
        d.laneT = laneT;
        d.normalBias = -0.05f + stableSigned(in.id() * 0.19f + a.t * 11.0f) * 0.10f;
        d.vx = stableSigned(in.id() * 0.43f + a.t * 7.0f + time_) * (4.0f + energy * 4.0f);
        d.vy = stableSigned(in.id() * 0.61f + a.t * 9.0f + time_) * (2.0f + energy * 2.0f);
        d.age = 0.0f;
        d.life = 0.60f + energy * 0.24f;
        d.srcId = in.id();
        d.radius = 1.4f + stable01(in.id() * 0.23f + a.t * 5.0f) * 1.1f;
        droplets_.push_back(d);
    }

    static juce::Point<float> sampleInsideBodyAt(const BubblegumCableGeometry& geo,
                                                 float t,
                                                 float normalBias) noexcept
    {
        const float clampedT = juce::jlimit(0.0f, 1.0f, t);
        const float fIndex = clampedT * (float) BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int) std::floor(fIndex));
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a = fIndex - (float) i0;

        const auto& p0 = geo.points[(size_t) i0];
        const auto& p1 = geo.points[(size_t) i1];

        const float x  = juce::jmap(a, p0.x,  p1.x);
        const float y  = juce::jmap(a, p0.y,  p1.y);
        float nx = juce::jmap(a, p0.nx, p1.nx);
        float ny = juce::jmap(a, p0.ny, p1.ny);
        const float tw = juce::jmap(a, p0.topW, p1.topW);
        const float bw = juce::jmap(a, p0.bottomW, p1.bottomW);

        const float nLen = std::sqrt(nx * nx + ny * ny);
        if (nLen > 0.0001f)
        {
            nx /= nLen;
            ny /= nLen;
        }
        else
        {
            nx = 0.0f;
            ny = -1.0f;
        }

        const float innerHalf = 0.5f * (tw + bw) * 0.55f;
        const float bias = juce::jlimit(-0.85f, 0.85f, normalBias);
        return { x + nx * innerHalf * bias, y + ny * innerHalf * bias };
    }

    static void drawEmbeddedDroplets(juce::Graphics& g,
                                     const BubblegumCableGeometry& geo,
                                     float srcId,
                                     float sendEnergy,
                                     float time,
                                     const std::vector<Droplet>& droplets)
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(geo.ribbon);

        const auto beadFill = juce::Colour(0xFFFF6FB0);
        const auto beadCore = juce::Colour(0xFFE04A8A);
        const auto beadEdge = juce::Colour(0xFF9B2055);
        const float e = juce::jlimit(0.0f, 1.0f, sendEnergy);

        for (const auto& d : droplets)
        {
            if (!d.alive() || d.srcId != srcId) continue;

            const float a = d.alpha();
            if (a < 0.01f) continue;

            const float laneT = nearestBlobLaneT(geo, srcId, time, d.laneT);
            const auto center = sampleInsideBodyAt(geo, laneT, d.normalBias);

            const float pulse = 0.86f + 0.14f * std::sin(time * 3.2f + d.laneT * 11.0f + srcId * 0.01f);
            const float w = d.radius * (2.0f + e * 0.28f) * pulse;
            const float h = d.radius * (1.35f + e * 0.22f) * pulse;
            const float x = center.x - w * 0.5f;
            const float y = center.y - h * 0.5f;

            juce::ColourGradient grad(
                beadCore.withAlpha(a * (0.72f + e * 0.10f)), center.x, center.y - h * 0.25f,
                beadFill.withAlpha(a * (0.96f + e * 0.04f)), center.x, center.y + h * 0.5f,
                false);
            grad.addColour(0.55, beadFill.withAlpha(a * (0.86f + e * 0.04f)));
            g.setGradientFill(grad);
            g.fillEllipse(x, y, w, h);

            g.setColour(beadEdge.withAlpha(a * 0.42f));
            g.drawEllipse(x, y, w, h, 0.5f);
            g.setColour(beadCore.withAlpha(a * 0.30f));
            g.fillEllipse(center.x - w * 0.16f, center.y - h * 0.20f, w * 0.22f, h * 0.16f);
        }
    }

    int visibleDropletCountFor(float srcId) const noexcept
    {
        int count = 0;
        for (const auto& d : droplets_)
            if (d.alive() && d.srcId == srcId)
                ++count;
        return count;
    }

    void registerHub(float x, float y, float sendEnergy)
    {
        for (auto& hub : pendingHubs_)
        {
            if (std::abs(hub.x - x) < 1.5f && std::abs(hub.y - y) < 1.5f)
            {
                hub.cableCount += 1;
                hub.energy = juce::jmax(hub.energy, juce::jlimit(0.0f, 1.0f, sendEnergy));
                return;
            }
        }

        HubAccent h;
        h.x = x;
        h.y = y;
        h.energy = juce::jlimit(0.0f, 1.0f, sendEnergy);
        h.cableCount = 1;
        h.phase = stable01(x * 0.017f + y * 0.011f) * juce::MathConstants<float>::twoPi;
        pendingHubs_.push_back(h);
    }

    static void drawPendingHubAccents(juce::Graphics& g,
                                      const std::vector<HubAccent>& hubs,
                                      float time,
                                      float pulse)
    {
        const auto haloColour = juce::Colour(0xFFFF99C8);
        const auto poolColour = juce::Colour(0xFFFFC8DE);
        const auto deepColour = juce::Colour(0xFFB56588);

        for (const auto& hub : hubs)
        {
            const float energy = juce::jlimit(0.0f, 1.0f, hub.energy);
            const float radius = 3.2f + juce::jmin(0.9f, (float) hub.cableCount * 0.22f) + energy * 1.0f;
            const float wobble = 0.55f + pulse * 0.28f + 0.18f * std::sin(time * 1.3f + hub.phase);
            const float skewX = std::cos(hub.phase) * 0.8f;
            const float skewY = std::sin(hub.phase * 1.7f) * 0.6f;

            g.setColour(haloColour.withAlpha(0.09f + energy * 0.04f));
            g.fillEllipse(hub.x - radius - 1.8f + skewX * 0.2f,
                          hub.y - radius - 1.2f + skewY * 0.2f,
                          (radius + 1.8f) * 2.0f,
                          (radius + 1.3f) * 1.8f);

            juce::Path pool;
            pool.addEllipse(hub.x - radius * 1.02f + skewX,
                            hub.y - radius * 0.62f + skewY,
                            radius * 1.95f,
                            radius * (1.05f + wobble * 0.12f));
            pool.addEllipse(hub.x - radius * 0.48f - skewX * 0.35f,
                            hub.y - radius * 0.30f - skewY * 0.25f,
                            radius * 0.92f,
                            radius * 0.66f);

            juce::ColourGradient grad(poolColour.withAlpha(0.26f + energy * 0.08f),
                                      hub.x, hub.y - radius * 0.55f,
                                      deepColour.withAlpha(0.18f + energy * 0.07f),
                                      hub.x, hub.y + radius * 0.90f,
                                      false);
            grad.addColour(0.42, haloColour.withAlpha(0.16f + energy * 0.05f));
            g.setGradientFill(grad);
            g.fillPath(pool);

            g.setColour(juce::Colours::white.withAlpha(0.10f + energy * 0.05f));
            g.fillEllipse(hub.x - radius * 0.22f,
                          hub.y - radius * 0.34f,
                          radius * 0.54f,
                          radius * 0.24f);
        }
    }

    bool    enabled_ = true;
    float   time_    = 0.f;
    float   pulse_   = 0.f;
    mutable std::vector<Droplet>    droplets_;
    mutable std::vector<HubAccent>  pendingHubs_;
    std::map<float, std::array<float, 4>> lastEndpoints_;
};

} // namespace DAW
