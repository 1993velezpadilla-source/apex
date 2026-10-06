#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace DAW {

// =====================================================================
// TrackLavaLampCore — isolated nucleo.
//
// Obsidian sphere selection effect:
//   * Pure-black void background
//   * 3-4 polished black-glass spheres with restrained warm undertones
//   * Crisp white specular highlight + tiny secondary hotspot
//   * Very faint warm rim light on the lower-right edge
//   * Slow heavy drift and fade-in/fade-out transitions
//
// Driven exclusively by:
//   setActive(bool)        — start/stop fade + motion timer
//   setAccentColour(Colour)
//   onRepaintRequired      — wire to the owning component's repaint()
// =====================================================================
class TrackLavaLampCore
{
public:
    std::function<void()> onRepaintRequired;

    TrackLavaLampCore()
    {
        seedParticles();
    }

    ~TrackLavaLampCore() {}

    void setActive(bool shouldBeActive)
    {
        if (active_ == shouldBeActive)
            return;
        if (alwaysVisible_ && !shouldBeActive)
            return; // master pin: never deactivate
        active_ = shouldBeActive;
        if (!alwaysVisible_)
            targetAlpha_ = active_ ? 1.0f : 0.0f;
        triggerRepaint();
    }

    /** Pin fully visible at all times regardless of selection state.
     *  Used for the master strip where spheres must always be shown. */
    void setAlwaysVisible(bool always)
    {
        alwaysVisible_ = always;
        if (always)
        {
            active_       = true;
            targetAlpha_  = 1.0f;
            displayAlpha_ = 1.0f;
        }
        triggerRepaint();
    }

    /** Called by MixerPanel::timerCallback — advances simulation without triggering
        a repaint (the panel repaints all strips in one pass after all steps).
        Returns true if the strip has visible lava content and needs a repaint. */
    bool stepExternal(float dt)
    {
        step(dt);
        // Do NOT call triggerRepaint here — caller handles the single repaint.
        return hasVisibleContent();
    }

    void setAccentColour(juce::Colour c)
    {
        if (accent_ == c) return;
        accent_ = c;
        invalidateFullCache();
        if (active_ || hasVisibleContent()) triggerRepaint();
    }

    void setSphereColour(juce::Colour c)
    {
        if (sphereTint_ == c) return;
        sphereTint_ = c;
        cachedSphere_ = {};
        invalidateFullCache();
        if (active_ || hasVisibleContent()) triggerRepaint();
    }

    void setParticleColour(juce::Colour c)
    {
        if (particleTint_ == c) return;
        particleTint_ = c;
        invalidateFullCache();
        if (active_ || hasVisibleContent()) triggerRepaint();
    }

    bool isActive() const noexcept       { return active_; }
    bool isAlwaysVisible() const noexcept { return alwaysVisible_; }
    bool hasVisibleContent() const noexcept { return displayAlpha_ > 0.001f; }
    bool needsAnimationTick() const noexcept
    {
        return active_ || alwaysVisible_ || displayAlpha_ > 0.001f || targetAlpha_ > 0.001f;
    }

    /** When false, particles (bubbles) are suppressed but spheres/environment still paint. */
    void setParticlesPainted(bool show)
    {
        if (particlesPainted_ != show) { particlesPainted_ = show; if (active_ || hasVisibleContent()) triggerRepaint(); }
    }

    /** When false, large obsidian spheres are hidden but rising particles still paint. */
    void setSpheresPainted(bool show)
    {
        if (alwaysVisible_) return; // master pin: spheres always shown
        if (spheresPainted_ != show) { spheresPainted_ = show; if (active_ || hasVisibleContent()) triggerRepaint(); }
    }

    void setBubbleColours(juce::Colour deep, juce::Colour near)
    {
        if (bubbleDeepColour_ == deep && bubbleNearColour_ == near)
            return;
        bubbleDeepColour_ = deep;
        bubbleNearColour_ = near;
        invalidateFullCache();
        if (active_ || hasVisibleContent()) triggerRepaint();
    }

    /** Store the colors to restore when deselected (strip-specific base state). */
    void setRestBubbleColours(juce::Colour deep, juce::Colour near)
    {
        restBubbleDeep_ = deep;
        restBubbleNear_ = near;
    }

    juce::Colour getRestBubbleDeep() const noexcept { return restBubbleDeep_; }
    juce::Colour getRestBubbleNear() const noexcept { return restBubbleNear_; }
    void setSelectionTint(bool enabled)
    {
        if (selectionTint_ == enabled) return;
        selectionTint_ = enabled;
        invalidateFullCache();
        if (active_ || hasVisibleContent()) triggerRepaint();
    }

    enum class LavaQualityMode { Performance, Cinematic };

    void setQualityMode(LavaQualityMode mode) noexcept { if (lavaQualityMode_ != mode) { lavaQualityMode_ = mode; invalidateFullCache(); } }
    LavaQualityMode getQualityMode() const noexcept    { return lavaQualityMode_; }

    enum class RingColorMode { None, Send, Sidechain, Both };

    /** Set ring-bubble colors driven by routing state.
     *  sendColor   = body color of the send cable
     *  scColor     = body color of the sidechain cable
     *  mode        = which connections exist */
    void setRingColors(juce::Colour sendColor, juce::Colour scColor, RingColorMode mode)
    {
        if (ringColor1_ == sendColor && ringColor2_ == scColor && ringColorMode_ == mode)
            return;
        ringColor1_    = sendColor;
        ringColor2_    = scColor;
        ringColorMode_ = mode;
        invalidateFullCache();
        if (active_ || hasVisibleContent()) triggerRepaint();
    }

    void clearRingColors()
    {
        if (ringColorMode_ == RingColorMode::None)
            return;
        ringColorMode_ = RingColorMode::None;
        invalidateFullCache();
        if (active_ || hasVisibleContent()) triggerRepaint();
    }

    /** Called by the owning component every paint to push the real screen
        position so window movement drives the inertial offset. */
    void setScreenOrigin(juce::Point<int> screenPos) const
    {
        const auto p = screenPos.toFloat();
        if (hasLastScreenOrigin_)
        {
            const float dx = p.x - lastScreenOrigin_.x;
            const float dy = p.y - lastScreenOrigin_.y;

            // Accumulate as a pending impulse; consumed safely in step().
            pendingImpulse_.x = juce::jlimit(-6.0f, 6.0f,
                pendingImpulse_.x + (-dx / 80.0f) * 60.0f);
            pendingImpulse_.y = juce::jlimit(-6.0f, 6.0f,
                pendingImpulse_.y + (-dy / 80.0f) * 44.0f);
        }
        lastScreenOrigin_    = p;
        hasLastScreenOrigin_ = true;
    }

    /** Lightweight paint: rising particles only, no environment, no spheres.
        Used for unselected strips — draws only the first kLightParticleCount
        particles to cap per-strip cost to 6 draw calls instead of 14. */
    static constexpr size_t kLightParticleCount = 6;
    void paintParticlesOnly(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (!hasVisibleContent() || area.isEmpty() || !particlesPainted_)
            return;

        // Performance mode: unselected strips skip lava particles.
        if (lavaQualityMode_ == LavaQualityMode::Performance)
            return;

        juce::Graphics::ScopedSaveState save(g);
        const float alpha = displayAlpha_;
        auto renderedParticles = buildRenderedParticles(area);
        const size_t count = juce::jmin(kLightParticleCount, renderedParticles.size());
        for (size_t i = 0; i < count; ++i)
            paintParticle(g, area, renderedParticles[i], alpha);
    }

    /** Paint polished obsidian spheres into the given rect.
        Caller should apply a rounded-rect clip for soft containment. */
    void paintBackground(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (! hasVisibleContent() || area.isEmpty())
            return;

        if (!buildingFullCache_)
        {
            ensureFullCacheBuilt(area);
            if (cachedFullLava_.isValid() && displayAlpha_ >= 0.9995f)
            {
                g.drawImageAt(cachedFullLava_,
                              (int)std::round(area.getX()),
                              (int)std::round(area.getY()),
                              false);
                ++lavaCacheBlitCount_;
                return;
            }
        }

        juce::Graphics::ScopedSaveState save(g);
        const float alpha = displayAlpha_;
        const float floorY = area.getY() + area.getHeight() * 0.92f;
        auto rendered = buildRenderedSpheres(area);

        ensureEnvironmentCacheBuilt(area, alpha);
        if (cachedEnvironment_.isValid())
        {
            juce::Graphics::ScopedSaveState save(g);
            g.setOpacity(1.0f);  // alpha already baked into image
            g.drawImageAt(cachedEnvironment_,
                          (int) std::round(area.getX()),
                          (int) std::round(area.getY()),
                          false);
        }

        if (particlesPainted_)
        {
            auto renderedParticles = buildRenderedParticles(area);
            for (const auto& particle : renderedParticles)
                if (particle.depth < 0.58f)
                    paintParticle(g, area, particle, alpha);

            if (spheresPainted_)
            {
                if (lavaQualityMode_ == LavaQualityMode::Cinematic)
                {
                    for (const auto& sphere : rendered)
                        paintReflection(g, area, floorY, sphere, alpha);
                }
                for (const auto& sphere : rendered)
                    paintSphere(g, area, floorY, sphere, alpha);
            }

            for (const auto& particle : renderedParticles)
                if (particle.depth >= 0.58f)
                    paintParticle(g, area, particle, alpha);
        }
        else if (spheresPainted_)
        {
            if (lavaQualityMode_ == LavaQualityMode::Cinematic)
            {
                for (const auto& sphere : rendered)
                    paintReflection(g, area, floorY, sphere, alpha);
            }
            for (const auto& sphere : rendered)
                paintSphere(g, area, floorY, sphere, alpha);
        }

        paintVignette(g, area, alpha);
    }

    int consumeLavaCacheBakeCount() const noexcept { const int v = lavaCacheBakeCount_; lavaCacheBakeCount_ = 0; return v; }
    int consumeLavaCacheBlitCount() const noexcept { const int v = lavaCacheBlitCount_; lavaCacheBlitCount_ = 0; return v; }

    /** Returns the normalised (0=top, 1=bottom) average Y of all blobs.
        Used by the mixer strip to position the dB-column glow reflection. */
    float getAverageBlobY() const noexcept
    {
        float sum = 0.f;
        for (const auto& sphere : spheres_)
            sum += juce::jlimit(0.0f, 1.0f,
                                sphere.baseY + std::sin(timeSeconds_ * sphere.verticalSpeed + sphere.verticalPhase)
                                              * sphere.verticalAmplitude);
        return spheres_.empty() ? 0.5f : sum / (float)spheres_.size();
    }

private:
    static constexpr size_t sphereCount = 3;
    static constexpr size_t particleCount = 14;

    struct Sphere
    {
        float baseX = 0.5f;
        float baseY = 0.5f;
        float depth = 0.5f;
        float radius = 0.25f;
        float verticalSpeed = 0.2f;
        float verticalPhase = 0.0f;
        float horizontalSpeed = 0.15f;
        float horizontalPhase = 0.0f;
        float verticalAmplitude = 0.05f;
        float horizontalAmplitude = 0.05f;
        // Physics state — driven by scene velocity impulse
        float velX = 0.0f;
        float velY = 0.0f;
        float physOffsetX = 0.0f;
        float physOffsetY = 0.0f;
    };

    struct FullLavaCacheKey
    {
        int width = 0;
        int height = 0;
        bool particlesPainted = false;
        bool spheresPainted = false;
        bool selectionTint = false;
        int qualityMode = 0;
        int ringMode = 0;
        juce::uint32 sphereARGB = 0;
        juce::uint32 particleARGB = 0;
        juce::uint32 deepARGB = 0;
        juce::uint32 nearARGB = 0;
        juce::uint32 ring1ARGB = 0;
        juce::uint32 ring2ARGB = 0;

        bool operator==(const FullLavaCacheKey& o) const noexcept
        {
            return width == o.width && height == o.height
                && particlesPainted == o.particlesPainted && spheresPainted == o.spheresPainted
                && selectionTint == o.selectionTint && qualityMode == o.qualityMode
                && ringMode == o.ringMode && sphereARGB == o.sphereARGB
                && particleARGB == o.particleARGB && deepARGB == o.deepARGB
                && nearARGB == o.nearARGB && ring1ARGB == o.ring1ARGB
                && ring2ARGB == o.ring2ARGB;
        }
    };

    void invalidateFullCache() const noexcept
    {
        fullLavaCacheValid_ = false;
        cachedFullLava_ = {};
    }

    FullLavaCacheKey makeFullLavaKey(juce::Rectangle<float> area) const noexcept
    {
        FullLavaCacheKey key;
        key.width = (int)std::ceil(area.getWidth());
        key.height = (int)std::ceil(area.getHeight());
        key.particlesPainted = particlesPainted_;
        key.spheresPainted = spheresPainted_;
        key.selectionTint = selectionTint_;
        key.qualityMode = (int)lavaQualityMode_;
        key.ringMode = (int)ringColorMode_;
        key.sphereARGB = sphereTint_.getARGB();
        key.particleARGB = particleTint_.getARGB();
        key.deepARGB = bubbleDeepColour_.getARGB();
        key.nearARGB = bubbleNearColour_.getARGB();
        key.ring1ARGB = ringColor1_.getARGB();
        key.ring2ARGB = ringColor2_.getARGB();
        return key;
    }

    void ensureFullCacheBuilt(juce::Rectangle<float> area) const
    {
        // Skip the full-frame cache while fading in/out — alpha changes every step
        // so the key would be stable but the baked image would have wrong alpha.
        // Once fully opaque (displayAlpha_==1.0f) the cache holds until invalidated.
        if (displayAlpha_ < 0.9995f)
            return;

        const auto key = makeFullLavaKey(area);
        if (fullLavaCacheValid_ && cachedFullLava_.isValid() && key == fullLavaCacheKey_)
            return;

        if (key.width <= 0 || key.height <= 0)
            return;

        cachedFullLava_ = juce::Image(juce::Image::ARGB, key.width, key.height, true);
        fullLavaCacheKey_ = key;
        fullLavaCacheValid_ = true;
        ++lavaCacheBakeCount_;

        juce::Graphics imageG(cachedFullLava_);
        buildingFullCache_ = true;
        paintBackground(imageG, juce::Rectangle<float>(0.0f, 0.0f, (float)key.width, (float)key.height));
        buildingFullCache_ = false;
    }

    struct RenderedSphere
    {
        float cx = 0.0f;
        float cy = 0.0f;
        float radius = 0.0f;
        float depth = 0.5f;
    };

    struct Particle
    {
        float x = 0.5f;
        float y = 1.0f;
        float vx = 0.0f;
        float vy = -0.1f;
        float radius = 0.02f;
        float life = 1.0f;
        float depth = 0.5f;
        float driftPhase = 0.0f;
    };

    struct RenderedParticle
    {
        float cx = 0.0f;
        float cy = 0.0f;
        float radius = 0.0f;
        float depth = 0.5f;
        float alpha = 1.0f;
    };

    struct Light
    {
        float nx = 0.0f;
        float ny = 0.0f;
        float intensity = 0.0f;
        juce::Colour colour;
    };

    void seedParticles()
    {
        spheres_[0] = { 0.52f, 0.16f, 0.88f, 0.28f, 0.09f, 0.00f, 0.08f, 0.00f, 0.18f, 0.06f };
        spheres_[1] = { 0.28f, 0.38f, 0.62f, 0.18f, 0.12f, 1.80f, 0.10f, 1.20f, 0.16f, 0.05f };
        spheres_[2] = { 0.71f, 0.62f, 0.80f, 0.23f, 0.15f, 3.20f, 0.12f, 2.60f, 0.18f, 0.04f };

        for (auto& particle : particles_)
            respawnParticle(particle, true);
    }

    void respawnParticle(Particle& particle, bool initial) const
    {
        particle.x = 0.10f + random_.nextFloat() * 0.80f;
        particle.y = initial ? random_.nextFloat() : (0.96f + random_.nextFloat() * 0.10f);
        particle.vx = (random_.nextFloat() - 0.5f) * 0.040f;
        particle.vy = -(0.14f + random_.nextFloat() * 0.14f);
        particle.radius = 0.018f + random_.nextFloat() * 0.034f;
        particle.life = initial ? (0.30f + random_.nextFloat() * 0.70f) : 1.0f;
        particle.driftPhase = random_.nextFloat() * juce::MathConstants<float>::twoPi;

        // Depth distribution: 40% deep (D<0.4), 40% mid (0.4-0.7), 20% near (>0.7)
        const float r = random_.nextFloat();
        if (r < 0.40f)
            particle.depth = random_.nextFloat() * 0.40f;          // deep
        else if (r < 0.80f)
            particle.depth = 0.40f + random_.nextFloat() * 0.30f;  // mid
        else
            particle.depth = 0.70f + random_.nextFloat() * 0.30f;  // near

        // Near bubbles are larger and drift faster (parallax)
        particle.radius = (0.012f + random_.nextFloat() * 0.018f) * (0.4f + particle.depth * 0.6f);
        particle.vy     = -(0.04f + particle.depth * 0.08f);  // nearer = faster vertical drift
    }

    // Screen-origin diff is now pushed externally via setScreenOrigin().
    // This stub is intentionally empty.

    std::array<RenderedSphere, sphereCount> buildRenderedSpheres(juce::Rectangle<float> area) const
    {
        std::array<RenderedSphere, sphereCount> rendered;

        const float w = area.getWidth();
        const float h = area.getHeight();
        const float cameraSwayX = std::sin(timeSeconds_ * 0.21f) * w * 0.010f;
        const float cameraSwayY = std::cos(timeSeconds_ * 0.17f) * h * 0.006f;

        for (size_t i = 0; i < spheres_.size(); ++i)
        {
            const auto& sphere = spheres_[i];
            auto& out = rendered[i];

            const float parallax = 0.35f + sphere.depth * 0.85f;
            const float perspectiveScale = 0.76f + sphere.depth * 0.34f;

            out.cx = area.getX() + (sphere.baseX + std::sin(timeSeconds_ * sphere.horizontalSpeed + sphere.horizontalPhase)
                                                   * sphere.horizontalAmplitude) * w
                                   + cameraSwayX * parallax
                                   + sphere.physOffsetX * w;
            out.cy = area.getY() + (sphere.baseY + std::sin(timeSeconds_ * sphere.verticalSpeed + sphere.verticalPhase)
                                                   * sphere.verticalAmplitude) * h
                                   + cameraSwayY * parallax
                                   + sphere.physOffsetY * h;
            out.radius = juce::jmax(6.0f, sphere.radius * w * perspectiveScale);
            out.depth = sphere.depth;
        }

        std::sort(rendered.begin(), rendered.end(), [](const auto& a, const auto& b)
        {
            return a.depth < b.depth;
        });

        return rendered;
    }

    std::array<RenderedParticle, particleCount> buildRenderedParticles(juce::Rectangle<float> area) const
    {
        std::array<RenderedParticle, particleCount> rendered;
        const float w = area.getWidth();
        const float h = area.getHeight();
        const float minDim = juce::jmin(w, h);

        for (size_t i = 0; i < particles_.size(); ++i)
        {
            const auto& particle = particles_[i];
            auto& out = rendered[i];

            const float sway = std::sin(timeSeconds_ * (0.8f + particle.depth * 0.6f) + particle.driftPhase)
                             * (0.006f + particle.depth * 0.010f);
            const float perspective = 0.72f + particle.depth * 0.45f;

            out.cx = area.getX() + (particle.x + sway) * w;
            out.cy = area.getY() + (particle.y) * h;
            out.radius = juce::jmax(2.5f, particle.radius * minDim * perspective);
            out.depth = particle.depth;
            out.alpha = particle.life;
        }

        // Only sort when spheres are painted (depth layering matters).  
        // For particles-only path the sort is skipped — saves ~14-item sort every paint.
        if (spheresPainted_)
            std::sort(rendered.begin(), rendered.end(), [](const auto& a, const auto& b)
            {
                return a.depth < b.depth;
            });

        return rendered;
    }

    void ensureEnvironmentCacheBuilt(juce::Rectangle<float> area, float alpha) const
    {
        const int w = (int) std::ceil(area.getWidth());
        const int h = (int) std::ceil(area.getHeight());

        const bool sizeChanged  = (w != cachedEnvironmentWidth_) || (h != cachedEnvironmentHeight_);
        const bool alphaChanged = std::abs(alpha - cachedEnvironmentAlpha_) > 0.02f;

        if (cachedEnvironment_.isValid() && !sizeChanged && !alphaChanged)
            return;

        if (w <= 0 || h <= 0)
            return;

        cachedEnvironment_ = juce::Image(juce::Image::ARGB, w, h, true);
        cachedEnvironmentWidth_  = w;
        cachedEnvironmentHeight_ = h;
        cachedEnvironmentAlpha_  = alpha;

        juce::Graphics gImg(cachedEnvironment_);

        const juce::Rectangle<float> imgArea(0.0f, 0.0f, (float) w, (float) h);
        const float floorY   = imgArea.getY() + imgArea.getHeight() * 0.92f;
        const float horizonY = imgArea.getY() + imgArea.getHeight() * 0.34f;

        juce::ColourGradient envLight(juce::Colour(0xFF1A140D).withAlpha(0.16f * alpha),
                                      imgArea.getCentreX(), imgArea.getY() - imgArea.getHeight() * 0.10f,
                                      juce::Colours::transparentBlack,
                                      imgArea.getCentreX(), imgArea.getY() + imgArea.getHeight() * 0.80f, true);
        envLight.addColour(0.35, juce::Colour(0xFF20170E).withAlpha(0.08f * alpha));
        envLight.addColour(0.5, juce::Colour(0xFF0A0805).withAlpha(0.05f * alpha));
        gImg.setGradientFill(envLight);
        gImg.fillRect(imgArea);

        juce::ColourGradient topLift(juce::Colour(0xFFFFF7EA).withAlpha(0.028f * alpha),
                                     imgArea.getCentreX(), imgArea.getY(),
                                     juce::Colours::transparentBlack,
                                     imgArea.getCentreX(), imgArea.getY() + imgArea.getHeight() * 0.36f, true);
        gImg.setGradientFill(topLift);
        gImg.fillRect(imgArea);

        juce::ColourGradient floorGrad(juce::Colour(0xFF18130D).withAlpha(0.12f * alpha),
                                       imgArea.getX(), floorY,
                                       juce::Colours::transparentBlack,
                                       imgArea.getX(), imgArea.getBottom(), false);
        floorGrad.addColour(0.4, juce::Colour(0xFF100C07).withAlpha(0.08f * alpha));
        gImg.setGradientFill(floorGrad);
        gImg.fillRect(imgArea.getX(), floorY, imgArea.getWidth(), imgArea.getBottom() - floorY);

        gImg.setColour(juce::Colour(0xFFFFF7EA).withAlpha(0.018f * alpha));
        gImg.fillRect(imgArea.getX(), horizonY, imgArea.getWidth(), 1.0f);

        for (int i = -2; i <= 2; ++i)
        {
            const float startX = imgArea.getCentreX() + (float)i * imgArea.getWidth() * 0.22f;
            const float endX   = imgArea.getCentreX() + (float)i * imgArea.getWidth() * 0.06f;
            juce::Path line;
            line.startNewSubPath(startX, imgArea.getBottom());
            line.lineTo(endX, floorY);
            gImg.setColour(juce::Colour(0xFFFFEFD8).withAlpha((0.016f - std::abs((float)i) * 0.002f) * alpha));
            gImg.strokePath(line, juce::PathStrokeType(0.6f));
        }

        for (int i = 0; i < 4; ++i)
        {
            const float t     = (float)(i + 1) / 5.0f;
            const float y     = juce::jmap(t * t, 0.0f, 1.0f, floorY + 1.0f, imgArea.getBottom());
            const float inset = juce::jmap(t, 0.0f, 1.0f, imgArea.getWidth() * 0.30f, 0.0f);
            gImg.setColour(juce::Colour(0xFFFFE5BF).withAlpha((0.018f - t * 0.006f) * alpha));
            gImg.drawLine(imgArea.getX() + inset, y, imgArea.getRight() - inset, y, 0.6f);
        }

        juce::ColourGradient sheen(juce::Colours::transparentBlack,
                                   imgArea.getX(), floorY - 1.0f,
                                   juce::Colours::transparentBlack,
                                   imgArea.getRight(), floorY - 1.0f, false);
        sheen.addColour(0.25, juce::Colour(0xFF6A5436).withAlpha(0.12f * alpha));
        sheen.addColour(0.50, juce::Colour(0xFF8A7045).withAlpha(0.22f * alpha));
        sheen.addColour(0.75, juce::Colour(0xFF6A5436).withAlpha(0.12f * alpha));
        gImg.setGradientFill(sheen);
        gImg.fillRect(imgArea.getX(), floorY - 1.0f, imgArea.getWidth(), 2.0f);

        juce::ColourGradient sideLift(juce::Colours::transparentBlack,
                                      imgArea.getX(), imgArea.getCentreY(),
                                      juce::Colours::transparentBlack,
                                      imgArea.getRight(), imgArea.getCentreY(), false);
        sideLift.addColour(0.08, juce::Colour(0xFFFFF6E8).withAlpha(0.018f * alpha));
        sideLift.addColour(0.92, juce::Colour(0xFFFFF6E8).withAlpha(0.014f * alpha));
        gImg.setGradientFill(sideLift);
        gImg.fillRect(imgArea);
    }

    void paintReflection(juce::Graphics& g,
                         juce::Rectangle<float> area,
                         float floorY,
                         const RenderedSphere& sphere,
                         float alpha) const
    {
        const float mirroredY = floorY + (floorY - sphere.cy);
        if (mirroredY - sphere.radius > area.getBottom())
            return;

        const float depthFade = 0.3f + sphere.depth * 0.7f;
        const float dist = (mirroredY - floorY) / juce::jmax(1.0f, area.getHeight());
        const float reflectionAlpha = juce::jmax(0.0f, 0.40f - dist * 1.45f) * depthFade * alpha;
        if (reflectionAlpha <= 0.0f)
            return;

        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRectangle(area.getX(), floorY, area.getWidth(), area.getBottom() - floorY);
        g.reduceClipRegion(clip);

        juce::ColourGradient base(juce::Colour(0xFF1C150D).withAlpha(reflectionAlpha),
                                  sphere.cx - sphere.radius * 0.18f, mirroredY - sphere.radius * 0.18f,
                                  juce::Colours::transparentBlack,
                                  sphere.cx, mirroredY + sphere.radius, true);
        base.addColour(0.6, juce::Colour(0xFF0C0905).withAlpha(reflectionAlpha * 0.75f));
        g.setGradientFill(base);
        g.fillEllipse(sphere.cx - sphere.radius, mirroredY - sphere.radius,
                      sphere.radius * 2.0f, sphere.radius * 2.0f);

        juce::ColourGradient spec(juce::Colours::white.withAlpha(reflectionAlpha * 0.6f),
                                  sphere.cx - sphere.radius * 0.33f, mirroredY - sphere.radius * 0.34f,
                                  juce::Colours::transparentBlack,
                                  sphere.cx - sphere.radius * 0.33f, mirroredY - sphere.radius * 0.09f, true);
        spec.addColour(0.5, juce::Colour(0xFFFFF8E6).withAlpha(reflectionAlpha * 0.2f));
        g.setGradientFill(spec);
        g.fillEllipse(sphere.cx - sphere.radius * 0.58f, mirroredY - sphere.radius * 0.59f,
                      sphere.radius * 0.50f, sphere.radius * 0.50f);
    }

    void paintParticle(juce::Graphics& g,
                       juce::Rectangle<float> area,
                       const RenderedParticle& particle,
                       float alpha) const
    {
        juce::ignoreUnused(area);

        const float D = particle.depth;
        const float depthFade = 0.45f + D * 0.55f;
        const float fade = juce::jmin(1.0f, particle.alpha * 4.0f) * particle.alpha;
        const float drawAlpha = fade * depthFade * alpha;
        if (drawAlpha <= 0.001f)
            return;

        const float R  = particle.radius;
        const float cx = particle.cx;
        const float cy = particle.cy;

        // Pure obsidian black bubbles — depth only affects size/brightness
        // Bubble colour: driven by setBubbleColours(), defaults to frosted blue-grey
        const juce::Colour bodyColour = bubbleDeepColour_.interpolatedWith(bubbleNearColour_, D * 0.8f);

        const float bodyAlpha = (0.62f + D * 0.28f) * drawAlpha;

        // ── LAYER 1: Outer bloom ─────────────────────────────────────────────
        {
            const float bloomR = R * 2.0f;
            const float bloomAlpha = (0.12f + D * 0.12f) * drawAlpha;
            juce::ColourGradient bloom(
                bubbleNearColour_.withAlpha(bloomAlpha), cx, cy,
                juce::Colours::transparentBlack, cx + bloomR, cy, true);
            g.setGradientFill(bloom);
            g.fillEllipse(cx - bloomR, cy - bloomR, bloomR * 2.0f, bloomR * 2.0f);
        }

        // ── LAYER 1b: Magenta selection bloom (elegant, restrained) ──────────
        if (selectionTint_)
        {
            const float bloomR = R * 2.6f;
            const float bloomAlpha = (0.08f + D * 0.07f) * drawAlpha;
            juce::ColourGradient magBloom(
                juce::Colour(0xFFFF0090).withAlpha(bloomAlpha), cx, cy,
                juce::Colours::transparentBlack, cx + bloomR, cy, true);
            g.setGradientFill(magBloom);
            g.fillEllipse(cx - bloomR, cy - bloomR, bloomR * 2.0f, bloomR * 2.0f);
        }

        // ── LAYER 2: Main body (depth-warmed radial gradient) ────────────────
        {
            juce::ColourGradient body(
                bodyColour.withAlpha(bodyAlpha), cx - R * 0.1f, cy - R * 0.1f,
                bodyColour.withAlpha(0.0f), cx + R, cy + R, true);
            body.addColour(0.70, bodyColour.withAlpha(bodyAlpha * 0.6f));
            g.setGradientFill(body);
            g.fillEllipse(cx - R, cy - R, R * 2.0f, R * 2.0f);
        }

        // ── LAYER 3: Inner highlight (3D specular on black glass) ─────────────
        {
            const float hlR  = R * 0.38f;
            const float hlCx = cx - R * 0.24f;
            const float hlCy = cy - R * 0.28f;
            const float hlAlpha = (0.55f + D * 0.35f) * drawAlpha;
            juce::ColourGradient spec(
                juce::Colours::white.withAlpha(hlAlpha), hlCx, hlCy,
                juce::Colours::transparentWhite, hlCx + hlR, hlCy + hlR, true);
            g.setGradientFill(spec);
            g.fillEllipse(hlCx - hlR, hlCy - hlR, hlR * 2.0f, hlR * 2.0f);
        }

        // ── LAYER 4: Routing ring — send / sidechain / both ──────────────────
        if (ringColorMode_ != RingColorMode::None)
        {
            // Alternate colors per particle using depth as a stable discriminator
            const bool useSend = (ringColorMode_ == RingColorMode::Send)
                              || (ringColorMode_ == RingColorMode::Both && D >= 0.5f);
            const bool useSC   = (ringColorMode_ == RingColorMode::Sidechain)
                              || (ringColorMode_ == RingColorMode::Both && D < 0.5f);

            const juce::Colour ringCol = useSend ? ringColor1_ : ringColor2_;

            // Outer ring glow — wide, low alpha
            const float ringGlowR = R * 1.35f;
            const float ringGlowAlpha = (0.14f + D * 0.10f) * drawAlpha;
            juce::ColourGradient ringGlow(
                ringCol.withAlpha(ringGlowAlpha), cx, cy,
                juce::Colours::transparentBlack, cx + ringGlowR, cy, true);
            g.setGradientFill(ringGlow);
            g.fillEllipse(cx - ringGlowR, cy - ringGlowR, ringGlowR * 2.0f, ringGlowR * 2.0f);

            // Crisp ring stroke — thin, precise
            const float ringAlpha = (0.55f + D * 0.28f) * drawAlpha;
            g.setColour(ringCol.withAlpha(ringAlpha));
            g.drawEllipse(cx - R, cy - R, R * 2.0f, R * 2.0f, 0.9f);

            // If Both mode, second ring with the other color slightly offset
            if (ringColorMode_ == RingColorMode::Both)
            {
                const juce::Colour ring2Col = useSend ? ringColor2_ : ringColor1_;
                const float r2 = R * 0.72f;
                const float r2Alpha = (0.35f + D * 0.18f) * drawAlpha;
                g.setColour(ring2Col.withAlpha(r2Alpha));
                g.drawEllipse(cx - r2, cy - r2, r2 * 2.0f, r2 * 2.0f, 0.7f);
            }
        }

        // ── LAYER 5: Magenta selection highlight over all detail ─────────────
        if (selectionTint_)
        {
            // Rim light — very thin bright magenta arc at top-left (like studio LED)
            const float rimR = R * 0.92f;
            g.setColour(juce::Colour(0xFFFF0090).withAlpha((0.22f + D * 0.12f) * drawAlpha));
            g.drawEllipse(cx - rimR, cy - rimR, rimR * 2.0f, rimR * 2.0f, 0.75f);

            // Tiny magenta specular dot — upper-left, punchy
            const float dotR = R * 0.14f;
            g.setColour(juce::Colour(0xFFFF55C0).withAlpha((0.50f + D * 0.25f) * drawAlpha));
            g.fillEllipse(cx - R * 0.30f, cy - R * 0.36f, dotR * 2.0f, dotR * 2.0f);
        }
    } // end paintParticle

    void ensureSphereCacheBuilt() const
    {
        if (cachedSphere_.isValid())
            return;

        constexpr int sz = 256;
        cachedSphere_ = juce::Image(juce::Image::ARGB, sz, sz, true);
        juce::Graphics g(cachedSphere_);

        const float r  = sz * 0.48f;
        const float cx = sz * 0.5f;
        const float cy = sz * 0.5f;
        const juce::Rectangle<float> bounds(cx - r, cy - r, r * 2.0f, r * 2.0f);

        // base dark body
        juce::ColourGradient base(juce::Colour(0xFF141414),
                                  cx - r * 0.18f, cy - r * 0.18f,
                                  juce::Colour(0xFF020202),
                                  cx, cy + r, true);
        base.addColour(0.36, juce::Colour(0xFF0C0C0C));
        base.addColour(0.50, juce::Colour(0xFF070707));
        base.addColour(0.82, juce::Colour(0xFF020202));
        g.setGradientFill(base);
        g.fillEllipse(bounds);

        // ambient occlusion bottom
        juce::ColourGradient ao(juce::Colours::transparentBlack,
                                cx, cy + r * 0.15f,
                                juce::Colours::black.withAlpha(0.32f),
                                cx, cy + r * 1.05f, true);
        ao.addColour(0.55, juce::Colours::black.withAlpha(0.14f));
        g.setGradientFill(ao);
        g.fillEllipse(bounds);

        // form shadow right-bottom
        juce::ColourGradient form(juce::Colours::transparentBlack,
                                  cx + r * 0.10f, cy + r * 0.08f,
                                  juce::Colours::black.withAlpha(0.34f),
                                  cx + r * 0.55f, cy + r * 0.70f, true);
        form.addColour(0.55, juce::Colours::black.withAlpha(0.14f));
        g.setGradientFill(form);
        g.fillEllipse(bounds);

        const auto sphereLift = sphereTint_.interpolatedWith(juce::Colours::white, 0.70f);
        const auto sphereWarm = sphereTint_.interpolatedWith(juce::Colour(0xFFF5F0E6), 0.35f);

        // upper warm lift
        juce::ColourGradient upper(sphereLift.withAlpha(0.055f),
                                   cx - r * 0.18f, cy - r * 0.34f,
                                   juce::Colours::transparentBlack,
                                   cx + r * 0.05f, cy + r * 0.10f, true);
        upper.addColour(0.5, sphereWarm.withAlpha(0.018f));
        g.setGradientFill(upper);
        g.fillEllipse(bounds);

        // 3 diffuse lights
        struct L { float nx, ny, intensity; juce::Colour c; };
        const std::array<L, 3> lights = {{
            { -0.551f, -0.702f, 0.72f, sphereLift },
            {  0.849f, -0.364f, 0.20f, sphereTint_.interpolatedWith(juce::Colours::white, 0.45f) },
            {  0.112f,  0.953f, 0.14f, sphereTint_.darker(0.2f) }
        }};
        for (const auto& l : lights)
        {
            const float lox = cx - l.nx * r * 0.45f;
            const float loy = cy - l.ny * r * 0.45f;
            juce::ColourGradient diff(l.c.withAlpha(l.intensity * 0.22f),
                                      lox, loy,
                                      juce::Colours::transparentBlack,
                                      cx, cy + r, true);
            diff.addColour(0.45, l.c.withAlpha(l.intensity * 0.09f));
            diff.addColour(0.72, l.c.withAlpha(l.intensity * 0.04f));
            g.setGradientFill(diff);
            g.fillEllipse(bounds);
        }

        // fresnel rim - the key 3D-glass look
        {
            juce::Graphics::ScopedSaveState ss(g);
            juce::Path clip;
            clip.addEllipse(bounds);
            g.reduceClipRegion(clip);

            juce::ColourGradient fresnel(juce::Colours::transparentBlack,
                                         cx, cy,
                                         juce::Colour(0xFF2E2E2E).withAlpha(0.86f),
                                         cx, cy + r, true);
            fresnel.addColour(0.76, juce::Colour(0xFF181818).withAlpha(0.34f));
            fresnel.addColour(0.90, juce::Colour(0xFF101010).withAlpha(0.56f));
            g.setGradientFill(fresnel);
            g.fillEllipse(bounds);

            juce::ColourGradient gloss(juce::Colours::transparentBlack,
                                       cx - r, cy - r * 0.05f,
                                       juce::Colours::transparentBlack,
                                       cx + r, cy + r * 0.12f, false);
            gloss.addColour(0.32, juce::Colour(0xFFF8F8F8).withAlpha(0.06f));
            gloss.addColour(0.50, juce::Colour(0xFFFFFFFF).withAlpha(0.14f));
            gloss.addColour(0.68, juce::Colour(0xFFF8F8F8).withAlpha(0.05f));
            g.setGradientFill(gloss);
            g.fillRect(bounds);
        }

        // primary specular
        juce::ColourGradient primary(juce::Colours::white.withAlpha(0.98f),
                                     cx - r * 0.33f, cy - r * 0.34f,
                                     juce::Colours::transparentBlack,
                                     cx - r * 0.33f, cy - r * 0.04f, true);
        primary.addColour(0.20, juce::Colour(0xFFFFFFFF).withAlpha(0.70f));
        primary.addColour(0.55, juce::Colour(0xFFE8E8E8).withAlpha(0.26f));
        g.setGradientFill(primary);
        g.fillEllipse(cx - r * 0.63f, cy - r * 0.64f, r * 0.60f, r * 0.60f);

        // specular tail
        juce::ColourGradient tail(juce::Colour(0xFFF6F6F6).withAlpha(0.14f),
                                  cx - r * 0.18f, cy - r * 0.10f,
                                  juce::Colours::transparentBlack,
                                  cx - r * 0.05f, cy + r * 0.42f, false);
        tail.addColour(0.55, juce::Colour(0xFFD8D8D8).withAlpha(0.05f));
        g.setGradientFill(tail);
        g.fillEllipse(cx - r * 0.28f, cy - r * 0.20f, r * 0.36f, r * 0.76f);

        // bright hotspot
        juce::ColourGradient hot(juce::Colours::white,
                                 cx - r * 0.42f, cy - r * 0.44f,
                                 juce::Colours::transparentBlack,
                                 cx - r * 0.42f, cy - r * 0.365f, true);
        hot.addColour(0.4, juce::Colours::white.withAlpha(0.75f));
        g.setGradientFill(hot);
        g.fillEllipse(cx - r * 0.495f, cy - r * 0.515f, r * 0.15f, r * 0.15f);

        // secondary fill specular (far side)
        juce::ColourGradient fill(juce::Colour(0xFFFFFFFF).withAlpha(0.13f),
                                  cx + r * 0.28f, cy - r * 0.22f,
                                  juce::Colours::transparentBlack,
                                  cx + r * 0.28f, cy, true);
        fill.addColour(0.5, juce::Colour(0xFFD8D8D8).withAlpha(0.05f));
        g.setGradientFill(fill);
        g.fillEllipse(cx + r * 0.06f, cy - r * 0.44f, r * 0.44f, r * 0.44f);

        // rim light bottom-right
        juce::ColourGradient rim(sphereTint_.interpolatedWith(juce::Colour(0xFF7A7A7A), 0.5f).withAlpha(0.10f),
                                 cx + r * 0.55f, cy + r * 0.50f,
                                 juce::Colours::transparentBlack,
                                 cx + r * 0.45f, cy + r * 1.02f, true);
        rim.addColour(0.45, sphereTint_.darker(1.0f).withAlpha(0.05f));
        g.setGradientFill(rim);
        g.fillEllipse(bounds);

        // dark outline
        g.setColour(juce::Colours::black.withAlpha(0.95f));
        g.drawEllipse(bounds.reduced(0.8f), 1.5f);
    }

    void paintSphere(juce::Graphics& g,
                     juce::Rectangle<float> area,
                     float floorY,
                     const RenderedSphere& sphere,
                     float alpha) const
    {
        const float depthFade = 0.3f + sphere.depth * 0.7f;
        const float r  = sphere.radius;
        const float cx = sphere.cx;
        const float cy = sphere.cy;

        // Live floor contact shadow (position-dependent, can't cache)
        if (cy + r < floorY)
        {
            const float shadowScale = juce::jmax(0.1f,
                1.0f - (floorY - (cy + r)) / (juce::jmax(1.0f, area.getHeight()) * 0.25f));
            juce::ColourGradient shadow(juce::Colours::black.withAlpha(0.78f * shadowScale * depthFade * alpha),
                                        cx, floorY,
                                        juce::Colours::transparentBlack,
                                        cx, floorY + r * 1.2f * shadowScale, true);
            shadow.addColour(0.5, juce::Colours::black.withAlpha(0.28f * shadowScale * depthFade * alpha));
            g.setGradientFill(shadow);
            g.fillEllipse(cx - r * 1.1f * shadowScale, floorY - r * 0.18f * shadowScale,
                          r * 2.2f * shadowScale, r * 0.36f * shadowScale);
        }

        // Blit pre-rendered sphere with depth/alpha modulation (single image draw)
        ensureSphereCacheBuilt();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.setOpacity(depthFade * alpha);
            g.drawImage(cachedSphere_,
                        juce::Rectangle<float>(cx - r, cy - r, r * 2.0f, r * 2.0f),
                        juce::RectanglePlacement::stretchToFit, false);
        }

        // Magenta selection rim on big spheres — live, over cached image
        if (selectionTint_)
        {
            const float rimA = (0.28f + sphere.depth * 0.18f) * alpha;
            // Wide soft halo
            juce::ColourGradient halo(
                juce::Colour(0xFFFF0090).withAlpha(rimA * 0.35f), cx, cy,
                juce::Colours::transparentBlack, cx + r * 1.4f, cy, true);
            g.setGradientFill(halo);
            g.fillEllipse(cx - r * 1.4f, cy - r * 1.4f, r * 2.8f, r * 2.8f);
            // Thin crisp ring
            g.setColour(juce::Colour(0xFFFF0090).withAlpha(rimA * 0.55f));
            g.drawEllipse(cx - r * 0.92f, cy - r * 0.92f, r * 1.84f, r * 1.84f, 0.8f);
            // Bright top-left specular dot
            g.setColour(juce::Colour(0xFFFF55C0).withAlpha((0.45f + sphere.depth * 0.20f) * alpha));
            g.fillEllipse(cx - r * 0.38f, cy - r * 0.44f, r * 0.22f, r * 0.22f);
        }
    } // end paintSphere

    void paintVignette(juce::Graphics& g, juce::Rectangle<float> area, float alpha) const
    {
        juce::ColourGradient vignette(juce::Colours::transparentBlack,
                                      area.getCentreX(), area.getY() + area.getHeight() * 0.42f,
                                      juce::Colours::black.withAlpha(0.38f * alpha),
                                      area.getCentreX(), area.getY() + area.getHeight() * 1.17f, true);
        vignette.addColour(0.65, juce::Colours::black.withAlpha(0.04f * alpha));
        g.setGradientFill(vignette);
        g.fillRect(area);
    }

    void step(float dt)
    {
        timeSeconds_ += dt;

        // Consume any screen-movement impulse accumulated since last tick.
        sceneVelocity_.x = juce::jlimit(-6.0f, 6.0f, sceneVelocity_.x + pendingImpulse_.x);
        sceneVelocity_.y = juce::jlimit(-6.0f, 6.0f, sceneVelocity_.y + pendingImpulse_.y);
        pendingImpulse_   = {};

        sceneVelocity_.x += (-sceneOffset_.x * sceneReturnSpring - sceneVelocity_.x * sceneDamping) * dt;
        sceneVelocity_.y += ((-sceneOffset_.y * sceneReturnSpring) - sceneVelocity_.y * sceneDamping
                            + sceneGravityBias) * dt;
        sceneOffset_.x = juce::jlimit(-0.18f, 0.18f, sceneOffset_.x + sceneVelocity_.x * dt);
        sceneOffset_.y = juce::jlimit(-0.12f, 0.12f, sceneOffset_.y + sceneVelocity_.y * dt);

        const size_t activeParticles = spheresPainted_ ? particles_.size() : kLightParticleCount;
        for (size_t pi = 0; pi < activeParticles; ++pi)
        {
            auto& particle = particles_[pi];
            particle.vx += sceneVelocity_.x * (0.24f + particle.depth * 0.28f) * dt;
            particle.vy += sceneVelocity_.y * 0.18f * dt;
            particle.vy -= particleRise * dt;
            // Nearer particles sway more (parallax horizontal sway)
            const float swayAmp = 0.008f + particle.depth * 0.016f;
            particle.vx += std::sin(timeSeconds_ * (1.4f + particle.depth) + particle.driftPhase) * dt * swayAmp;

            particle.x += particle.vx * dt;
            particle.y += particle.vy * dt;

            particle.vx *= std::pow(particleDrag, dt * 60.0f);
            particle.vy *= std::pow(particleDrag, dt * 60.0f);
            particle.life -= dt * (0.15f + particle.depth * 0.10f);

            if (particle.life <= 0.0f || particle.y < -0.12f || particle.x < -0.20f || particle.x > 1.20f)
                respawnParticle(particle, false);
        }

        if (spheresPainted_)
        for (auto& sphere : spheres_)
        {
            sphere.velX += sceneVelocity_.x * (0.18f + sphere.depth * 0.22f) * dt;
            sphere.velY += sceneVelocity_.y * (0.14f + sphere.depth * 0.18f) * dt;
            // Spring return to natural sinusoidal base
            sphere.velX += -sphere.physOffsetX * 2.8f * dt;
            sphere.velY += -sphere.physOffsetY * 2.8f * dt;
            sphere.physOffsetX += sphere.velX * dt;
            sphere.physOffsetY += sphere.velY * dt;
            sphere.physOffsetX = juce::jlimit(-0.14f, 0.14f, sphere.physOffsetX);
            sphere.physOffsetY = juce::jlimit(-0.14f, 0.14f, sphere.physOffsetY);
            sphere.velX *= std::pow(0.984f, dt * 60.0f);
            sphere.velY *= std::pow(0.984f, dt * 60.0f);
        }

        const float fadeStep = dt / fadeDurationSeconds;
        if (displayAlpha_ < targetAlpha_)
            displayAlpha_ = juce::jmin(targetAlpha_, displayAlpha_ + fadeStep);
        else if (displayAlpha_ > targetAlpha_)
            displayAlpha_ = juce::jmax(targetAlpha_, displayAlpha_ - fadeStep);

        if (alwaysVisible_)
        {
            displayAlpha_ = 1.0f;
            targetAlpha_  = 1.0f;
        }

        if (!active_ && displayAlpha_ <= 0.001f)
            displayAlpha_ = 0.0f;
    }

    void triggerRepaint() const
    {
        if (onRepaintRequired)
            onRepaintRequired();
    }

    bool          active_          { false };
    bool          alwaysVisible_   { false };
    bool          particlesPainted_ { true };
    bool          spheresPainted_   { true };
    juce::Colour  accent_          { 0xFFCC9900 };
    juce::Colour  sphereTint_      { 0xFFB89A7A };
    juce::Colour  particleTint_    { 0xFFFF69B4 };
    juce::Colour  bubbleDeepColour_ { 0xFF3A4455 };
    juce::Colour  bubbleNearColour_ { 0xFF6A7A90 };
    bool          selectionTint_    { false };
    juce::Colour  restBubbleDeep_   { 0xFF3A4455 };  // per-strip rest colors
    juce::Colour  restBubbleNear_   { 0xFF6A7A90 };
    juce::Colour  ringColor1_       { 0xFFEE4FA0 };   // send cable color
    juce::Colour  ringColor2_       { 0xFFE7C98D };   // sidechain cable color
    RingColorMode ringColorMode_    { RingColorMode::None };
    float         timeSeconds_ { 0.0f };
    float         displayAlpha_ { 0.0f };
    float         targetAlpha_  { 0.0f };
    mutable juce::Point<float> lastScreenOrigin_  { 0.0f, 0.0f };
    mutable bool               hasLastScreenOrigin_ { false };
    mutable juce::Point<float> pendingImpulse_    { 0.0f, 0.0f };
    mutable juce::Point<float> sceneOffset_ { 0.0f, 0.0f };
    mutable juce::Point<float> sceneVelocity_ { 0.0f, 0.0f };
    mutable juce::Random       random_ { 0x0B51D00Du };

    static constexpr int tickHz = 60;
    static constexpr float fadeDurationSeconds = 0.40f;
    static constexpr float particleRise = 0.018f;
    static constexpr float particleDrag = 0.988f;
    static constexpr float sceneReturnSpring = 11.0f;
    static constexpr float sceneDamping = 8.5f;
    static constexpr float sceneGravityBias = 0.0f;

    std::array<Sphere, sphereCount> spheres_;
    mutable std::array<Particle, particleCount> particles_;
    mutable juce::Image cachedSphere_;
    mutable juce::Image cachedEnvironment_;
    mutable int         cachedEnvironmentWidth_  = 0;
    mutable int         cachedEnvironmentHeight_ = 0;
    mutable float       cachedEnvironmentAlpha_  = -1.0f;
    mutable juce::Image cachedFullLava_;
    mutable FullLavaCacheKey fullLavaCacheKey_;
    mutable bool fullLavaCacheValid_ = false;
    mutable bool buildingFullCache_ = false;
    mutable int lavaCacheBakeCount_ = 0;
    mutable int lavaCacheBlitCount_ = 0;

    LavaQualityMode lavaQualityMode_ { LavaQualityMode::Performance };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackLavaLampCore)
};

} // namespace DAW
