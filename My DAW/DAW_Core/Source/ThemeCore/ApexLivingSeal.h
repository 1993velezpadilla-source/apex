#pragma once
#include <JuceHeader.h>
#include "Theme.h"

namespace DAW
{
    //==============================================================================
    // ApexLivingEye — standalone living eye (almond + iris + narrow pupil + lids)
    // with cursor-following gaze. Paint-only decoration, never a Component.
    // Used by the FX Living Seal internally and by the MASTER timeline header.
    //==============================================================================
    class ApexLivingEye
    {
    public:
        void setBounds (juce::Rectangle<float> almond)
        {
            if (bounds_ == almond)
                return;
            bounds_ = almond;
            rebuild();
        }

        juce::Rectangle<float> getContentBounds() const noexcept { return bounds_; }

        /** Feed the cursor position in the same coordinate space as bounds_. */
        void setGazeTarget (juce::Point<float> localPos, bool hasTarget)
        {
            if (! hasTarget)
            {
                gazeTarget_ = {};
                hasGazeTarget_ = false;
                return;
            }
            hasGazeTarget_ = true;
            const auto c = bounds_.getCentre();
            auto v = localPos - c;
            const float len = v.getDistanceFromOrigin();
            const float maxR = getMaxGazeRadius();
            if (len > maxR && len > 0.0001f)
                v *= (maxR / len);
            gazeTarget_ = v;
        }

        float getMaxGazeRadius() const noexcept
        {
            return juce::jmax (1.5f, bounds_.getWidth() * 0.055f);
        }

        juce::Point<float> getGazeOffset() const noexcept { return gazeCurrent_; }

        void update (float dtSeconds)
        {
            if (dtSeconds <= 0.0f)
                return;
            const auto goal = hasGazeTarget_ ? gazeTarget_ : juce::Point<float>();
            const float k = juce::jmin (1.0f, dtSeconds * 9.0f); // premium smoothing
            gazeCurrent_ += (goal - gazeCurrent_) * k;
        }

        /** Draws the eye. open01: lid openness; lidClose01: blink amount 0..1. */
        void paint (juce::Graphics& g, float open01, float lidClose01) const
        {
            if (bounds_.getWidth() <= 4.0f || bounds_.getHeight() <= 4.0f || open01 <= 0.0f)
                return;

            auto& a = Theme::getInstance().apex;
            const auto c = bounds_.getCentre();

            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (almond_);

            // Iris — pink core fading to deep wine rim, subtle parallax with gaze
            const auto ic = c + gazeCurrent_ * 0.45f;
            juce::ColourGradient iris (
                juce::Colour (0xFFFFB3D2), ic.x - 8.0f, ic.y - 9.0f,
                juce::Colour (0xFF170A18), ic.x, ic.y, true);
            iris.addColour (0.16, juce::Colour (0xFFF04896));
            iris.addColour (0.38, juce::Colour (0xFFC81C6E));
            iris.addColour (0.60, juce::Colour (0xFF7A1454));
            iris.addColour (0.82, juce::Colour (0xFF3A1030));
            g.setGradientFill (iris);
            g.fillPath (almond_);

            // Pupil — narrow, follows the cursor
            const auto pc = c + gazeCurrent_;
            const float pw = juce::jmax (2.2f, bounds_.getWidth()  * 0.085f);
            const float ph = juce::jmax (5.0f, bounds_.getHeight() * 0.42f);
            juce::ColourGradient pupilG (
                juce::Colour (0xFF02030A), pc.x, pc.y - ph * 0.5f,
                juce::Colour (0xFF0A0414), pc.x, pc.y + ph * 0.5f, false);
            g.setGradientFill (pupilG);
            g.fillEllipse (pc.x - pw * 0.5f, pc.y - ph * 0.5f, pw, ph);

            // Lids — dark panels; closing = blink or not-yet-open
            const float close = juce::jlimit (0.0f, 1.0f, (1.0f - open01) + lidClose01);
            if (close > 0.001f)
            {
                const float lidH = (bounds_.getHeight() * 0.5f + 2.0f) * close;
                g.setColour (juce::Colour (0xFF0B0F19));
                g.fillRect (bounds_.getX() - 1.0f, bounds_.getY() - 1.0f,
                            bounds_.getWidth() + 2.0f, lidH + 1.0f);
                g.fillRect (bounds_.getX() - 1.0f, bounds_.getBottom() - lidH,
                            bounds_.getWidth() + 2.0f, lidH + 1.0f);
            }
        }

    private:
        void rebuild()
        {
            almond_.clear();
            const auto b = bounds_;
            // Approved almond shape (matches the FX seal eye proportions)
            almond_.startNewSubPath (b.getX(), b.getCentreY());
            almond_.cubicTo (b.getX() + b.getWidth() * 0.26f, b.getY(),
                             b.getX() + b.getWidth() * 0.74f, b.getY(),
                             b.getRight(), b.getCentreY());
            almond_.cubicTo (b.getX() + b.getWidth() * 0.74f, b.getBottom(),
                             b.getX() + b.getWidth() * 0.26f, b.getBottom(),
                             b.getX(), b.getCentreY());
            almond_.closeSubPath();
        }

        juce::Rectangle<float> bounds_ { -1.0f, -1.0f, -1.0f, -1.0f };
        juce::Path almond_;
        juce::Point<float> gazeTarget_, gazeCurrent_;
        bool hasGazeTarget_ = false;
    };

    //==============================================================================
    // ApexLivingSeal — the approved FX Living Seal, translated from the approved
    // HTML/SVG/CSS reference into native JUCE rendering.
    //
    // Contract:
    //  - Paint-only decoration. NOT a Component: it cannot intercept input,
    //    hit-testing, drag/drop, scrolling, or plugin controls.
    //  - Normalized 512x620 design space, aspect-fitted to the target bounds.
    //    Geometry is built once per bounds change; ambient atmosphere is baked
    //    into a cached image; only light path strokes run per frame.
    //  - Deterministic explicit timeline: startOpening() resets t to 0; the
    //    complete sequence plays once and settles into a subtle idle state.
    //  - Reduced motion renders the final seal immediately with zero animation.
    //  - No file I/O, no plugin/audio state access, no per-frame path rebuilds.
    //==============================================================================
    class ApexLivingSeal
    {
    public:
        enum class Phase { Closed, Opening, Idle };
        enum class Part  { Aura, Seed, Spine, Fehu, Algiz, Tiwaz, Nexus, Flows, Apex, Eye, Dust };

        //-- lifecycle ---------------------------------------------------------
        void setBounds (juce::Rectangle<float> newBounds)
        {
            if (bounds_ == newBounds)
                return;
            bounds_ = newBounds;
            rebuildCache();
        }

        juce::Rectangle<float> getBounds() const noexcept { return bounds_; }

        void startOpening()
        {
            t_ = 0.0f;
            opened_ = true;
        }

        void setReducedMotion (bool shouldReduce) { reducedMotion_ = shouldReduce; }
        bool isReducedMotion() const noexcept { return reducedMotion_; }

        //-- watermark prominence ------------------------------------------------
        /** Global opacity attenuation applied to EVERYTHING the seal paints
         *  (1.0 = the reference look). The FX rack passes a low value (~0.12)
         *  so the artwork reads as a subtle premium watermark behind plugin
         *  cards: main structural shapes land around 8-15% and secondary
         *  particles/lines around 4-10%. Pure presentation — no interaction,
         *  layout, hit-testing or animation-timeline impact. */
        void setProminence (float p) noexcept { prominence_ = juce::jlimit (0.0f, 1.0f, p); }
        float getProminence() const noexcept { return prominence_; }

        //-- cursor gaze -----------------------------------------------------------
        /** Feed the cursor position in the same coordinate space as setBounds(). */
        void setGazeTarget (juce::Point<float> localPos, bool hasTarget)
        {
            if (! hasTarget || eyeLocal_.isEmpty())
            {
                gazeTarget_ = {};
                hasGazeTarget_ = false;
                return;
            }
            hasGazeTarget_ = true;
            const auto c = eyeLocal_.getBounds().getCentre();
            auto v = localPos - c;
            const float len = v.getDistanceFromOrigin();
            const float maxR = getMaxGazeRadius();
            if (len > maxR && len > 0.0001f)
                v *= (maxR / len);
            gazeTarget_ = v;
        }

        float getMaxGazeRadius() const noexcept
        {
            return juce::jmax (1.5f, eyeLocal_.getBounds().getWidth() * 0.055f);
        }

        juce::Point<float> getGazeOffset() const noexcept { return gazeCurrent_; }

        //-- deterministic timeline ---------------------------------------------
        void update (float dtSeconds)
        {
            if (dtSeconds <= 0.0f)
                return;

            // Gaze smoothing runs whenever the seal is alive (message thread only).
            if (opened_)
            {
                const auto goal = hasGazeTarget_ ? gazeTarget_ : juce::Point<float>();
                const float k = juce::jmin (1.0f, dtSeconds * 9.0f);
                gazeCurrent_ += (goal - gazeCurrent_) * k;
            }

            if (! opened_ || reducedMotion_)
                return;
            t_ += dtSeconds;
        }

        float getTimeSinceStart() const noexcept { return t_; }

        Phase getPhase() const noexcept
        {
            if (! opened_ || fit_.getWidth() <= 0.0f) return Phase::Closed;
            if (reducedMotion_)                       return Phase::Idle;
            return t_ >= kOpeningEnd ? Phase::Idle : Phase::Opening;
        }

        bool needsRepaint() const noexcept
        {
            return opened_ && ! reducedMotion_ && fit_.getWidth() > 0.0f;
        }

        /** Eased 0..1 formation progress of one structural part. */
        float getPartProgress (Part p) const noexcept
        {
            if (reducedMotion_) return 1.0f;
            if (! opened_)      return 0.0f;

            float start = 0.0f, dur = 0.55f;
            switch (p)
            {
                case Part::Aura:  start = 0.00f; dur = 0.55f; break;
                case Part::Seed:  start = 0.10f; dur = 0.55f; break;
                case Part::Spine: start = 0.30f; dur = 0.90f; break;
                case Part::Fehu:  start = 0.85f; dur = 0.70f; break;
                case Part::Algiz: start = 1.16f; dur = 0.72f; break;
                case Part::Tiwaz: start = 1.50f; dur = 0.72f; break;
                case Part::Nexus: start = 1.84f; dur = 0.62f; break;
                case Part::Flows: start = 1.98f; dur = 0.88f; break;
                case Part::Apex:  start = 2.40f; dur = 0.60f; break;
                case Part::Eye:   start = 2.90f; dur = 0.55f; break;
                case Part::Dust:  start = 1.40f; dur = 0.60f; break;
            }
            return easeOutCubic (juce::jlimit (0.0f, 1.0f, (t_ - start) / dur));
        }

        //-- bounds queries (local component coordinates) -------------------------
        juce::Rectangle<float> getContentBounds() const noexcept { return fit_; }

        juce::Rectangle<float> getPartBounds (Part p) const noexcept
        {
            if (fit_.getWidth() <= 0.0f)
                return {};

            if (p == Part::Eye)
                return eyeLocal_.getBounds();

            return fit_;
        }

        //-- rendering -------------------------------------------------------------
        void paint (juce::Graphics& g)
        {
            if (! opened_ || ! (fit_.getWidth() > 0.0f))
                return;

            const float tt = reducedMotion_ ? kOpeningEnd + 10.0f : t_;
            auto& a = Theme::getInstance().apex;
            const float prom = prominence_; // watermark attenuation (1.0 = reference)

            juce::Graphics::ScopedSaveState save (g);

            // 1. Ambient field — baked image, alpha = formation * breathing
            {
                const float auraP = reducedMotion_ ? 1.0f : getPartProgress (Part::Aura);
                float alpha = 0.62f * auraP;
                if (getPhase() == Phase::Idle)
                    alpha *= 0.82f + 0.18f * std::sin (juce::MathConstants<float>::twoPi * tt / 5.4f);
                alpha *= prom;
                if (ambientCache_.isValid() && alpha > 0.003f)
                {
                    g.setOpacity (alpha);
                    g.drawImageAt (ambientCache_, (int) std::round (fit_.getX()),
                                   (int) std::round (fit_.getY()), false);
                    g.setOpacity (1.0f);
                }
            }

            // 2. Dust — tiny drifting particles
            {
                const float dustP = reducedMotion_ ? 1.0f : getPartProgress (Part::Dust);
                if (dustP > 0.0f)
                {
                    const float driftA = reducedMotion_ ? 0.0f : std::sin (tt * 0.8f) * 4.0f;
                    const float driftB = reducedMotion_ ? 0.0f : std::cos (tt * 0.55f) * 5.0f;
                    int i = 0;
                    for (const auto& d : dustLocal_)
                    {
                        const auto off = (i++ % 2 == 0) ? juce::Point<float> (driftA, -driftA)
                                                        : juce::Point<float> (driftB, driftA * 0.6f);
                        g.setColour (d.colour.withMultipliedAlpha (dustP * prom));
                        g.fillEllipse (d.center.x + off.x - d.r, d.center.y + off.y - d.r,
                                       d.r * 2.0f, d.r * 2.0f);
                    }
                }
            }

            // 3. Seed / base — violet core the structure grows from
            {
                const float p = reducedMotion_ ? 1.0f : getPartProgress (Part::Seed);
                if (p > 0.0f)
                {
                    const float s = 0.4f + 0.6f * p;
                    g.setColour (a.color.violet.withAlpha (0.85f * p * prom));
                    g.fillPath (seedLocal_, juce::AffineTransform::scale (
                        s, s, seedAnchor_.x, seedAnchor_.y));
                }
            }

            // 4. Central spine — rises from the seed upward
            {
                const float p = reducedMotion_ ? 1.0f : getPartProgress (Part::Spine);
                if (p > 0.0f)
                {
                    auto full = spineLocal_.getBounds();
                    const float visibleH = full.getHeight() * p;
                    juce::Graphics::ScopedSaveState clip (g);
                    g.reduceClipRegion (juce::Rectangle<float> (full.getX() - 6.0f,
                                                                full.getBottom() - visibleH,
                                                                full.getWidth() + 12.0f,
                                                                visibleH + 1.0f).toNearestInt());
                    g.setOpacity (prom);
                    g.setGradientFill (spineGradient_);
                    g.fillPath (spineLocal_);
                    g.setOpacity (1.0f);
                }
            }

            // 5. Fehu / Algiz / Tiwaz forged structures
            drawPartScaled (g, fehuLocal_,  fehuGradient_,  Part::Fehu,  fehuAnchor_);
            drawPartScaled (g, algizLocal_, algizGradient_, Part::Algiz, algizAnchor_);
            drawPartScaled (g, tiwazLocal_, tiwazGradient_, Part::Tiwaz, tiwazAnchor_);

            // 6. Flow lines — draw into place with a traveling bright tip
            {
                const float base = reducedMotion_ ? 1.0f : getPartProgress (Part::Flows);
                if (base > 0.0f)
                {
                    for (int i = 0; i < (int) flowLocal_.size(); ++i)
                    {
                        const float stagger = juce::jlimit (0.0f, 1.0f, base * 1.4f - 0.14f * (float) i);
                        if (stagger <= 0.0f) continue;
                        g.setColour (a.color.violet.withAlpha (0.55f * stagger * prom));
                        g.strokePath (flowLocal_[(size_t) i],
                                      juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                            juce::PathStrokeType::rounded));
                        if (stagger < 1.0f)
                        {
                            const auto tip = flowLocal_[(size_t) i].getPointAlongPath (
                                flowLocal_[(size_t) i].getLength() * stagger);
                            g.setColour (a.color.cyan.withAlpha (0.85f * prom));
                            g.fillEllipse (tip.x - 2.0f, tip.y - 2.0f, 4.0f, 4.0f);
                        }
                    }
                }
            }

            // 7. APEX crown ignition — strike + flash, then quiet breathing
            {
                const float p = reducedMotion_ ? 1.0f : getPartProgress (Part::Apex);
                if (p > 0.0f)
                {
                    float scale = 0.5f + 0.55f * p;      // 0.5 → 1.05
                    if (p >= 1.0f) scale = 1.0f;
                    float alpha = juce::jlimit (0.0f, 1.0f, p * 1.4f);
                    if (getPhase() == Phase::Idle)
                    {
                        scale = 1.0f + 0.035f * std::sin (juce::MathConstants<float>::twoPi * tt / 4.4f);
                        alpha = 0.88f + 0.10f * std::sin (juce::MathConstants<float>::twoPi * tt / 4.4f + 0.6f);
                    }
                    g.setColour (a.color.magentaBright.withAlpha (alpha * prom));
                    g.fillPath (apexLocal_, juce::AffineTransform::scale (
                        scale, scale, apexAnchor_.x, apexAnchor_.y));

                    // Ignition flash — brief, restrained
                    if (! reducedMotion_ && tt > 2.36f && tt < 3.31f)
                    {
                        const float f = (tt - 2.36f) / 0.95f;
                        const float r = (10.0f + 26.0f * f) * fitScale_;
                        const float flashA = (f < 0.3f ? f / 0.3f * 0.5f : 0.5f * (1.0f - (f - 0.3f) / 0.7f));
                        g.setColour (a.color.pink.withAlpha (flashA * prom));
                        g.fillEllipse (apexAnchor_.x - r, apexAnchor_.y - r, r * 2.0f, r * 2.0f);
                    }
                }
            }

            // 8. Transformation nexus — the eye integrated into the forge
            {
                const float p = reducedMotion_ ? 1.0f : getPartProgress (Part::Nexus);
                if (p > 0.0f)
                {
                    float scale = 0.6f + 0.4f * p;
                    if (getPhase() == Phase::Idle)
                        scale = 1.0f + 0.035f * std::sin (juce::MathConstants<float>::twoPi * tt / 5.6f);
                    g.setOpacity (prom);
                    g.setGradientFill (nexusGradient_);
                    g.fillPath (nexusLocal_, juce::AffineTransform::scale (
                        scale, scale, nexusAnchor_.x, nexusAnchor_.y));
                    g.setOpacity (1.0f);
                }
            }

            // 9. The living eye — dark, narrow pupil, real lids, rare blink
            {
                const float openP = reducedMotion_ ? 1.0f : getPartProgress (Part::Eye);
                if (openP > 0.0f)
                {
                    juce::Graphics::ScopedSaveState eyeSave (g);
                    g.reduceClipRegion (eyeLocal_);
                    g.setOpacity (prom); // watermark: the eye stays a dark subtle shape

                    // Cursor-following gaze — tiny, premium, smoothed.
                    const auto gaze = gazeCurrent_;

                    // Iris — pink core fading to deep wine rim, subtle parallax
                    {
                        auto iris = irisGradient_;
                        iris.point1 += gaze * 0.45f;
                        iris.point2 += gaze * 0.45f;
                        g.setGradientFill (iris);
                        g.fillPath (eyeLocal_);
                    }

                    // Pupil — narrow, subtle breathing, follows the cursor;
                    // falls back to a tiny periodic shift when no cursor is present.
                    float pupilScaleX = 1.0f;
                    auto pupilGaze = gaze;
                    if (getPhase() == Phase::Idle)
                    {
                        const float ph = std::fmod (tt, 9.0f) / 9.0f;
                        pupilScaleX = 1.0f - 0.22f * juce::jmax (0.0f, std::sin (ph * juce::MathConstants<float>::twoPi)) * (ph > 0.4f && ph < 0.65f ? 1.0f : 0.0f);
                        if (! hasGazeTarget_)
                        {
                            const float gph = std::fmod (tt, 12.0f) / 12.0f;
                            if (gph > 0.64f && gph < 0.76f)
                                pupilGaze = { 1.8f * fitScale_, 0.0f };
                        }
                    }
                    const auto pb = pupilLocal_.getBounds();
                    g.setGradientFill (pupilGradient_);
                    g.fillPath (pupilLocal_, juce::AffineTransform::translation (pupilGaze.x, pupilGaze.y)
                        .followedBy (juce::AffineTransform::scale (
                            pupilScaleX, 1.0f, pb.getCentreX() + pupilGaze.x, pb.getCentreY())));

                    // Eyelids — dark panels closing over the almond
                    float lidClose = 1.0f - openP;
                    if (getPhase() == Phase::Idle)
                    {
                        const float c = std::fmod (tt, 6.8f) / 6.8f; // blink at ~93%
                        if (c > 0.910f && c < 0.965f)
                            lidClose = std::sin ((c - 0.910f) / 0.055f * juce::MathConstants<float>::pi);
                    }
                    if (lidClose > 0.001f)
                    {
                        const float lidH = (eyeLocal_.getBounds().getHeight() * 0.5f + 2.0f) * lidClose;
                        auto eb = eyeLocal_.getBounds();
                        g.setColour (lidColour_);
                        g.fillRect (eb.getX() - 1.0f, eb.getY() - 1.0f,
                                    eb.getWidth() + 2.0f, lidH + 1.0f);
                        g.fillRect (eb.getX() - 1.0f, eb.getBottom() - lidH,
                                    eb.getWidth() + 2.0f, lidH + 1.0f);
                    }
                }
            }

            // 10. Runic flicker — faint intensity variation on three nodes
            if (getPhase() == Phase::Idle && ! reducedMotion_)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const float per = 7.0f + 2.0f * (float) i;
                    const float ra = 0.28f + 0.27f * (0.5f + 0.5f * std::sin (
                        juce::MathConstants<float>::twoPi * tt / per + (float) i * 1.7f));
                    g.setColour (a.color.pink.withAlpha (ra * prom));
                    const auto& n = runeNodes_[(size_t) i];
                    g.fillEllipse (n.x - 1.6f, n.y - 1.6f, 3.2f, 3.2f);
                }
            }

            // 11. Motes — occasional rising sparks
            if (getPhase() == Phase::Idle && ! reducedMotion_)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const float per = 7.0f + 2.0f * (float) i;
                    const float c = std::fmod (tt + (float) i * 1.9f, per) / per;
                    const float rise = 26.0f * fitScale_ * c;
                    const float ma = c < 0.15f ? c / 0.15f * 0.6f
                                   : c > 0.70f ? juce::jmax (0.0f, 0.45f * (1.0f - (c - 0.70f) / 0.30f))
                                               : 0.45f;
                    const auto& m = motes_[(size_t) i];
                    g.setColour (a.color.magentaBright.withAlpha (ma * prom));
                    g.fillEllipse (m.x - 1.3f, m.y - rise - 1.3f, 2.6f, 2.6f);
                }
            }

            // 12. Vertical energy — pulses travelling up the spine
            if (getPhase() == Phase::Idle && ! reducedMotion_)
            {
                for (int i = 0; i < 2; ++i)
                {
                    const float per = (i == 0) ? 5.6f : 7.4f;
                    const float c = std::fmod (tt + (i == 0 ? 0.0f : 2.1f), per) / per;
                    const float y = energyBottom_ - (energyBottom_ - energyTop_) * c;
                    float ea = 0.0f;
                    if (c < 0.08f)      ea = c / 0.08f * 0.75f;
                    else if (c < 0.55f) ea = 0.80f;
                    else if (c < 0.90f) ea = 0.80f * (1.0f - (c - 0.55f) / 0.35f);
                    if (ea > 0.0f)
                    {
                        const float h = 26.0f * fitScale_ * (0.8f + 0.35f * std::sin (c * juce::MathConstants<float>::pi));
                        juce::ColourGradient eg (a.color.cyan.withAlpha (ea * prom), energyX_, y - h,
                                                 a.color.violet.withAlpha (0.0f), energyX_, y + h * 0.4f, false);
                        g.setGradientFill (eg);
                        g.fillRoundedRectangle (energyX_ - 1.5f * fitScale_, y - h,
                                                3.0f * fitScale_, h * 1.4f, 1.5f * fitScale_);
                    }
                }
            }
        }

    private:
        //==============================================================================
        struct DustDot { juce::Point<float> center; float r; juce::Colour colour; };

        static float easeOutCubic (float x) noexcept
        {
            const float u = 1.0f - x;
            return 1.0f - u * u * u;
        }

        static juce::Point<float> D (float x, float y) { return { x, y }; }

        /** Design-space → local transform (JUCE transformPoint needs lvalues). */
        juce::Point<float> tp (float x, float y) const noexcept
        {
            fitTransform_.transformPoint (x, y);
            return { x, y };
        }

        //==============================================================================
        void rebuildCache()
        {
            fit_ = {};
            if (bounds_.getWidth() < 24.0f || bounds_.getHeight() < 24.0f)
                return;

            // Aspect-fit the 512x620 design space, centred.
            const float designW = 512.0f, designH = 620.0f;
            fitScale_ = juce::jmin (bounds_.getWidth() / designW, bounds_.getHeight() / designH);
            const float w = designW * fitScale_, h = designH * fitScale_;
            fit_ = juce::Rectangle<float> (bounds_.getCentreX() - w * 0.5f,
                                           bounds_.getCentreY() - h * 0.5f, w, h);
            fitTransform_ = juce::AffineTransform::scale (fitScale_, fitScale_)
                                .translated (fit_.getX(), fit_.getY());

            buildGeometry();
            bakeAmbient();
        }

        void buildGeometry()
        {
            // ── Spine: central vertical forged line (apex y105 → seed y526) ───
            spineLocal_.clear();
            spineLocal_.addRoundedRectangle (252.5f, 105.0f, 7.0f, 421.0f, 3.5f);
            spineLocal_.applyTransform (fitTransform_);
            spineGradient_ = juce::ColourGradient (
                juce::Colour (0xFFF2C4DC), 256.0f, 105.0f,
                juce::Colour (0xFF813CFF), 256.0f, 526.0f, false);
            spineGradient_.addColour (0.11, juce::Colour (0xFFE82E8C));
            spineGradient_.addColour (0.49, juce::Colour (0xFFDF1168));
            spineGradient_ = transformGradient (spineGradient_);

            // ── Seed: violet base node ────────────────────────────────────────
            seedLocal_.clear();
            addOctagon (seedLocal_, 256.0f, 526.0f, 19.0f);
            seedLocal_.applyTransform (fitTransform_);
            seedAnchor_ = tp (256, 526);

            // ── Fehu ᚠ: lower structure (stem + two ascending branches) ───────
            fehuLocal_.clear();
            fehuLocal_.addRoundedRectangle (252.8f, 380.0f, 6.4f, 100.0f, 3.2f);
            addStroke (fehuLocal_, D (256, 392), D (330, 414), 6.0f);
            addStroke (fehuLocal_, D (256, 440), D (330, 462), 6.0f);
            fehuLocal_.applyTransform (fitTransform_);
            fehuGradient_ = transformGradient (juce::ColourGradient (
                juce::Colour (0xFF813CFF), 252.0f, 480.0f,
                juce::Colour (0xFF17AEDD), 334.0f, 380.0f, false));
            fehuAnchor_ = tp (256, 445);

            // ── Algiz ᛉ: protection/ascent arms above the nexus ───────────────
            algizLocal_.clear();
            algizLocal_.addRoundedRectangle (252.8f, 190.0f, 6.4f, 125.0f, 3.2f);
            {
                juce::Path armL, armR;
                armL.startNewSubPath (256, 200);
                armL.quadraticTo (240, 224, 224, 248);
                armR.startNewSubPath (256, 200);
                armR.quadraticTo (272, 224, 288, 248);
                algizLocal_.addPath (armL, juce::AffineTransform());
                algizLocal_.addPath (armR, juce::AffineTransform());
            }
            // Thicken arms into the forged mass
            addStroke (algizLocal_, D (256, 200), D (224, 248), 5.6f);
            addStroke (algizLocal_, D (256, 200), D (288, 248), 5.6f);
            algizLocal_.applyTransform (fitTransform_);
            algizGradient_ = transformGradient (juce::ColourGradient (
                juce::Colour (0xFFA80B58), 256.0f, 315.0f,
                juce::Colour (0xFFC93C8C), 256.0f, 190.0f, false));
            algizGradient_.addColour (0.46, juce::Colour (0xFFD91264));
            algizAnchor_ = tp (256, 305);

            // ── Tiwaz ᛏ: crown above (stem + two downward-out arms) ───────────
            tiwazLocal_.clear();
            tiwazLocal_.addRoundedRectangle (252.8f, 101.0f, 6.4f, 74.0f, 3.2f);
            addStroke (tiwazLocal_, D (256, 105), D (219, 140), 6.2f);
            addStroke (tiwazLocal_, D (256, 105), D (293, 140), 6.2f);
            tiwazLocal_.applyTransform (fitTransform_);
            tiwazGradient_ = transformGradient (juce::ColourGradient (
                juce::Colour (0xFFF7509F), 256.0f, 101.0f,
                juce::Colour (0xFFA80E52), 256.0f, 175.0f, false));
            tiwazGradient_.addColour (0.55, juce::Colour (0xFFDE2280));
            tiwazAnchor_ = tp (256, 124);

            // ── APEX crown mark: small diamond at the very top ────────────────
            apexLocal_.clear();
            addOctagon (apexLocal_, 256.0f, 105.0f, 10.0f);
            apexLocal_.applyTransform (fitTransform_);
            apexAnchor_ = tp (256, 105);

            // ── Nexus: transformation octagon holding the eye ──────────────────
            nexusLocal_.clear();
            addOctagon (nexusLocal_, 256.0f, 315.0f, 40.0f);
            nexusLocal_.applyTransform (fitTransform_);
            nexusGradient_ = transformGradient (juce::ColourGradient (
                juce::Colour (0xFFBE72F5), 256.0f, 275.0f,
                juce::Colour (0xFF4E2492), 256.0f, 355.0f, false));
            nexusGradient_.addColour (0.5, juce::Colour (0xFF8443D6));
            nexusAnchor_ = tp (256, 315);

            // ── Eye: exact approved almond (HTML eyeClip path) ─────────────────
            eyeLocal_.clear();
            eyeLocal_.startNewSubPath (218, 315);
            eyeLocal_.cubicTo (238, 292, 274, 292, 294, 315);
            eyeLocal_.cubicTo (274, 338, 238, 338, 218, 315);
            eyeLocal_.closeSubPath();
            eyeLocal_.applyTransform (fitTransform_);

            irisGradient_ = juce::ColourGradient (
                juce::Colour (0xFFFFB3D2), 0.0f, 0.0f,
                juce::Colour (0xFF170A18), 0.0f, 1.0f, true);
            {
                const auto eb = eyeLocal_.getBounds();
                irisGradient_ = juce::ColourGradient (
                    juce::Colour (0xFFFFB3D2), eb.getCentreX() - 8.0f * fitScale_, eb.getCentreY() - 9.0f * fitScale_,
                    juce::Colour (0xFF170A18), eb.getCentreX(), eb.getCentreY(), true);
                irisGradient_.addColour (0.16, juce::Colour (0xFFF04896));
                irisGradient_.addColour (0.38, juce::Colour (0xFFC81C6E));
                irisGradient_.addColour (0.60, juce::Colour (0xFF7A1454));
                irisGradient_.addColour (0.82, juce::Colour (0xFF3A1030));
            }

            pupilLocal_.clear();
            pupilLocal_.addEllipse (252.5f, 305.5f, 7.0f, 19.0f);
            pupilLocal_.applyTransform (fitTransform_);
            {
                const auto pb = pupilLocal_.getBounds();
                pupilGradient_ = juce::ColourGradient (
                    juce::Colour (0xFF02030A), pb.getCentreX(), pb.getY(),
                    juce::Colour (0xFF0A0414), pb.getCentreX(), pb.getBottom(), false);
            }
            lidColour_ = juce::Colour (0xFF0B0F19);

            // ── Flow lines: restrained diagonal signal strokes ────────────────
            flowLocal_.clear();
            flowLocal_.push_back (makeStroke (D (256, 300), D (330, 250)));
            flowLocal_.push_back (makeStroke (D (256, 330), D (200, 290)));
            flowLocal_.push_back (makeStroke (D (256, 240), D (310, 190)));
            for (auto& f : flowLocal_)
                f.applyTransform (fitTransform_);

            // ── Runic flicker nodes + motes ────────────────────────────────────
            runeNodes_[0] = tp (224, 248);
            runeNodes_[1] = tp (330, 462);
            runeNodes_[2] = tp (293, 140);
            motes_[0] = tp (232, 470);
            motes_[1] = tp (282, 430);
            motes_[2] = tp (248, 250);

            // ── Dust field ──────────────────────────────────────────────────────
            dustLocal_.clear();
            {
                juce::Random rng ((juce::int64) Theme::getInstance().apex.decor.splatterSeed);
                for (int i = 0; i < 12; ++i)
                {
                    const float x = 120.0f + rng.nextFloat() * 280.0f;
                    const float y = 120.0f + rng.nextFloat() * 380.0f;
                    const auto p = tp (x, y);
                    const auto col = (i % 3 == 0) ? Theme::getInstance().apex.color.pink.withAlpha (0.55f)
                                   : (i % 3 == 1) ? Theme::getInstance().apex.color.violet.withAlpha (0.45f)
                                                  : Theme::getInstance().apex.color.cyan.withAlpha (0.35f);
                    dustLocal_.push_back ({ p, juce::jmax (0.7f, 1.4f * fitScale_ * (0.6f + rng.nextFloat() * 0.8f)), col });
                }
            }

            // ── Energy travel corridor ──────────────────────────────────────────
            energyX_      = tp (256, 0).x;
            energyTop_    = tp (0, 130).y;
            energyBottom_ = tp (0, 505).y;
        }

        void bakeAmbient()
        {
            const int w = juce::jmax (1, (int) std::ceil (fit_.getWidth()));
            const int h = juce::jmax (1, (int) std::ceil (fit_.getHeight()));
            ambientCache_ = juce::Image (juce::Image::ARGB, w, h, true);
            juce::Graphics ig (ambientCache_);
            ig.addTransform (juce::AffineTransform::translation (-fit_.getX(), -fit_.getY()));

            const auto c = tp (256, 310);
            const float rx = 202.0f * fitScale_, ry = 226.0f * fitScale_;

            juce::ColourGradient aura (Theme::getInstance().apex.color.magenta.withAlpha (0.12f),
                                       c.x, c.y,
                                       juce::Colours::transparentBlack, c.x + rx, c.y + ry, true);
            aura.addColour (0.44, Theme::getInstance().apex.color.violet.withAlpha (0.055f));
            ig.setGradientFill (aura);
            ig.fillEllipse (c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f);

            // Faint diagonal atmosphere strokes (matches approved ambient field)
            const auto p1a = tp (120, 490);
            const auto p1b = tp (392, 144);
            ig.setColour (Theme::getInstance().apex.color.violet.withAlpha (0.025f));
            ig.drawLine (p1a.x, p1a.y, p1b.x, p1b.y, 36.0f * fitScale_);
            const auto p2a = tp (150, 540);
            const auto p2b = tp (420, 190);
            ig.setColour (Theme::getInstance().apex.color.magenta.withAlpha (0.018f));
            ig.drawLine (p2a.x, p2a.y, p2b.x, p2b.y, 22.0f * fitScale_);
        }

        void drawPartScaled (juce::Graphics& g, const juce::Path& path,
                             const juce::ColourGradient& grad, Part part,
                             juce::Point<float> anchor)
        {
            const float p = reducedMotion_ ? 1.0f : getPartProgress (part);
            if (p <= 0.0f)
                return;

            const float scale = 0.55f + 0.45f * p;
            const float yOff  = (1.0f - p) * 10.0f * fitScale_;
            g.setGradientFill (grad);
            g.setOpacity (p * prominence_);
            g.fillPath (path, juce::AffineTransform::translation (0.0f, yOff)
                .followedBy (juce::AffineTransform::scale (scale, scale, anchor.x, anchor.y)));
            g.setOpacity (1.0f);
        }

        static void addOctagon (juce::Path& p, float cx, float cy, float r)
        {
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * ((float) i / 8.0f)
                              + juce::MathConstants<float>::pi * 0.125f;
                const float x = cx + std::cos (a) * r;
                const float y = cy + std::sin (a) * r;
                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }
            p.closeSubPath();
        }

        static juce::Path makeStroke (juce::Point<float> from, juce::Point<float> to)
        {
            juce::Path p;
            p.startNewSubPath (from);
            p.lineTo (to);
            return p;
        }

        static void addStroke (juce::Path& dst, juce::Point<float> from,
                               juce::Point<float> to, float width)
        {
            juce::Path line = makeStroke (from, to);
            juce::PathStrokeType (width, juce::PathStrokeType::curved,
                                  juce::PathStrokeType::rounded).createStrokedPath (dst, line);
        }

        juce::ColourGradient transformGradient (const juce::ColourGradient& src) const
        {
            auto dst = src;
            dst.point1 = tp (src.point1.x, src.point1.y);
            dst.point2 = tp (src.point2.x, src.point2.y);
            return dst;
        }

        //==============================================================================
        static constexpr float kOpeningEnd = 3.45f;

        juce::Rectangle<float> bounds_, fit_ { -1.0f, -1.0f, -1.0f, -1.0f };
        juce::AffineTransform fitTransform_;
        float fitScale_ = 1.0f;

        float t_ = 0.0f;
        bool opened_ = false;
        bool reducedMotion_ = false;
        float prominence_ = 1.0f;

        juce::Point<float> gazeTarget_, gazeCurrent_;
        bool hasGazeTarget_ = false;

        juce::Image ambientCache_;

        juce::Path spineLocal_, seedLocal_, fehuLocal_, algizLocal_, tiwazLocal_;
        juce::Path apexLocal_, nexusLocal_, eyeLocal_, pupilLocal_;
        std::vector<juce::Path> flowLocal_;
        juce::Point<float> runeNodes_[3], motes_[3];
        juce::Point<float> seedAnchor_, fehuAnchor_, algizAnchor_, tiwazAnchor_, apexAnchor_, nexusAnchor_;
        float energyX_ = 0.0f, energyTop_ = 0.0f, energyBottom_ = 0.0f;

        juce::ColourGradient spineGradient_, fehuGradient_, algizGradient_;
        juce::ColourGradient tiwazGradient_, nexusGradient_, irisGradient_, pupilGradient_;
        juce::Colour lidColour_;
        std::vector<DustDot> dustLocal_;
    };

} // namespace DAW
