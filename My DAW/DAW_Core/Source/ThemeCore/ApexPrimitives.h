#pragma once
#include <JuceHeader.h>
#include "Theme.h"

namespace DAW::ApexPrimitives
{
    //==============================================================================
    // APEX signal-core reusable visual primitives.
    //
    // Design contract:
    //  - All geometry is vector (juce::Path) so it stays crisp at 100/150/200% DPI.
    //  - build*() functions are deterministic and cheap to cache; call them on
    //    resize/theme change, store the Path, and reuse it in paint().
    //  - draw*() functions perform no file I/O, no blur filters, no per-frame
    //    path rebuilds beyond simple strokes, and never query audio/plugin state.
    //  - Glow is semantic: active/selected controls glow; idle controls stay dark.
    //==============================================================================

    enum class PanelDepth { base, raised, floating };

    struct ControlState
    {
        bool hover    = false;
        bool pressed  = false;
        bool selected = false;
        bool active   = false;
        bool disabled = false;
    };

    inline juce::Colour defaultAccent()
    {
        return Theme::getInstance().apex.color.magenta;
    }

    //==============================================================================
    // Official APEX Signal Sigil.
    //
    // "APEX takes raw signal, sees its potential, transforms it, and expands it."
    //   central light     = original signal / idea
    //   eye               = vision / recognizing potential
    //   inner geometry    = controlled processing / transformation
    //   outward points    = expansion / growth
    //   concentric rings  = continuous signal flow: input → transform → expand
    //==============================================================================
    inline juce::Path buildSignalSigilPath (juce::Rectangle<float> b)
    {
        juce::Path p;
        const auto c  = b.getCentre();
        const float r = 0.5f * juce::jmin (b.getWidth(), b.getHeight());
        if (r <= 0.0f)
            return p;

        const float cx = c.x, cy = c.y;

        // Concentric rings — continuous signal flow.
        p.addEllipse (cx - r * 0.94f, cy - r * 0.94f, r * 1.88f, r * 1.88f);
        p.addEllipse (cx - r * 0.70f, cy - r * 0.70f, r * 1.40f, r * 1.40f);

        // Inner geometry — controlled transformation: two rotated squares
        // forming an eight-point star.
        auto addRotatedSquare = [&] (float radius, float rotation)
        {
            juce::Path sq;
            for (int i = 0; i < 4; ++i)
            {
                const float a = rotation + juce::MathConstants<float>::halfPi * (float) i;
                const float x = cx + std::cos (a) * radius;
                const float y = cy + std::sin (a) * radius;
                if (i == 0) sq.startNewSubPath (x, y);
                else        sq.lineTo (x, y);
            }
            sq.closeSubPath();
            p.addPath (sq);
        };
        addRotatedSquare (r * 0.52f, 0.0f);
        addRotatedSquare (r * 0.52f, juce::MathConstants<float>::pi * 0.25f);

        // Outward points — expansion at the four cardinal directions.
        for (int i = 0; i < 4; ++i)
        {
            const float a  = juce::MathConstants<float>::halfPi * (float) i;
            const float dx = std::cos (a), dy = std::sin (a);
            const float px = -dy,        py = dx; // perpendicular

            const float tipR  = r * 0.88f;
            const float baseR = r * 0.70f;
            const float halfW = r * 0.05f;

            juce::Path spike;
            spike.startNewSubPath (cx + dx * tipR, cy + dy * tipR);
            spike.lineTo (cx + dx * baseR + px * halfW, cy + dy * baseR + py * halfW);
            spike.lineTo (cx + dx * baseR - px * halfW, cy + dy * baseR - py * halfW);
            spike.closeSubPath();
            p.addPath (spike);
        }

        // Radial ticks between the rings — signal granularity.
        for (int i = 0; i < 24; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * ((float) i / 24.0f);
            const float dx = std::cos (a), dy = std::sin (a);
            p.startNewSubPath (cx + dx * r * 0.74f, cy + dy * r * 0.74f);
            p.lineTo       (cx + dx * r * 0.90f, cy + dy * r * 0.90f);
        }

        // The eye — vision. Horizontal almond inside the star.
        {
            const float ew = r * 0.34f, eh = r * 0.17f;
            juce::Path eye;
            eye.startNewSubPath (cx - ew, cy);
            eye.quadraticTo (cx, cy - eh * 2.0f, cx + ew, cy);
            eye.quadraticTo (cx, cy + eh * 2.0f, cx - ew, cy);
            eye.closeSubPath();
            p.addPath (eye);
        }

        // Central light — the raw signal.
        p.addEllipse (cx - r * 0.06f, cy - r * 0.06f, r * 0.12f, r * 0.12f);

        return p;
    }

    /** Draws the sigil with magenta → violet → cyan energy layering.
        Decorative only — callers must place it in unused space and ensure
        it never intercepts pointer input. */
    inline void drawSignalSigil (juce::Graphics& g, const juce::Path& cachedSigil,
                                 float opacity = -1.0f)
    {
        auto& t = Theme::getInstance().apex;
        const float o = opacity >= 0.0f ? opacity : t.decor.sigilOpacity;

        juce::Graphics::ScopedSaveState s (g);
        g.setColour (t.color.violet.withAlpha (o));
        g.strokePath (cachedSigil, juce::PathStrokeType (t.metric.strokeThin));
        g.setColour (t.color.magenta.withAlpha (o * 0.85f));
        g.strokePath (cachedSigil, juce::PathStrokeType (t.metric.strokeThin * 0.6f));
        g.setColour (t.color.cyan.withAlpha (o * 0.5f));
        g.fillPath (cachedSigil);
    }

    //==============================================================================
    // Eye motif — subtle Master-track identity mark.
    //==============================================================================
    inline juce::Path buildEyeMotifPath (juce::Rectangle<float> b)
    {
        juce::Path p;
        const auto c  = b.getCentre();
        const float r = 0.5f * juce::jmin (b.getWidth(), b.getHeight());
        if (r <= 0.0f)
            return p;

        const float cx = c.x, cy = c.y;
        const float ew = juce::jmin (b.getWidth()  * 0.48f, r * 1.6f);
        // Peak half-height of the drawn curve. Quadratic hull = 2x the peak, so
        // hp must stay within b.getHeight() * 0.25f to keep getBounds() inside b.
        const float hp = juce::jmin (b.getHeight() * 0.25f, r * 0.6f);

        // Almond outline (control hull stays inside bounds by construction).
        p.startNewSubPath (cx - ew, cy);
        p.quadraticTo (cx, cy - hp * 2.0f, cx + ew, cy);
        p.quadraticTo (cx, cy + hp * 2.0f, cx - ew, cy);
        p.closeSubPath();

        // Iris + pupil — the perceiving core.
        p.addEllipse (cx - hp * 0.62f, cy - hp * 0.62f, hp * 1.24f, hp * 1.24f);
        p.addEllipse (cx - hp * 0.22f, cy - hp * 0.22f, hp * 0.44f, hp * 0.44f);

        return p;
    }

    inline void drawEyeMotif (juce::Graphics& g, const juce::Path& cachedEye,
                              float opacity = -1.0f)
    {
        auto& t = Theme::getInstance().apex;
        const float o = opacity >= 0.0f ? opacity : t.decor.eyeMotifOpacity;
        g.setColour (t.color.magenta.withAlpha (o));
        g.strokePath (cachedEye, juce::PathStrokeType (t.metric.strokeThin));
    }

    //==============================================================================
    // Procedural splatter — original APEX texture vocabulary.
    // Deterministic per seed so panels render identically across frames,
    // DPI scales, and machines. No raster assets, no file I/O.
    //==============================================================================
    inline std::vector<juce::Path> buildSplatterPaths (juce::Rectangle<float> b,
                                                       juce::uint32 seed,
                                                       int maxDroplets = 90)
    {
        std::vector<juce::Path> drops;
        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f || maxDroplets <= 0)
            return drops;

        juce::Random rng ((juce::int64) seed);
        const int count = juce::jmin (maxDroplets, 40 + rng.nextInt (50));
        drops.reserve ((size_t) count);

        for (int i = 0; i < count; ++i)
        {
            const float roll = rng.nextFloat();
            const float maxW = b.getWidth(), maxH = b.getHeight();

            float w, h;
            if (roll < 0.72f)       // spray droplets
            {
                const float s = 0.006f + 0.030f * rng.nextFloat();
                w = h = juce::jmax (1.0f, maxW * s);
            }
            else if (roll < 0.92f)  // ink blobs
            {
                const float s = 0.030f + 0.060f * rng.nextFloat();
                w = juce::jmax (1.5f, maxW * s);
                h = w * (0.6f + 0.8f * rng.nextFloat());
            }
            else                    // dry-brush streaks
            {
                w = juce::jmax (3.0f, maxW * (0.05f + 0.10f * rng.nextFloat()));
                h = juce::jmax (1.0f, w * 0.12f);
            }

            const float x = b.getX() + rng.nextFloat() * juce::jmax (0.0f, maxW - w);
            const float y = b.getY() + rng.nextFloat() * juce::jmax (0.0f, maxH - h);

            juce::Path d;
            if (roll < 0.92f)
            {
                d.addEllipse (x, y, w, h);
            }
            else
            {
                // Fragmented streak: a thin jittered quad.
                const float skew = (rng.nextFloat() - 0.5f) * h * 2.0f;
                d.startNewSubPath (x, y);
                d.lineTo (x + w, y + skew * 0.5f);
                d.lineTo (x + w, y + h + skew * 0.5f);
                d.lineTo (x, y + h);
                d.closeSubPath();
            }
            drops.push_back (std::move (d));
        }
        return drops;
    }

    //==============================================================================
    // Slash / flow — abstract signal-energy streaks.
    // Diagonal brush marks that read as motion and transformation without any
    // emblem or symbol. Deterministic per seed; cache the result.
    //==============================================================================
    inline std::vector<juce::Path> buildSlashFlowPaths (juce::Rectangle<float> b,
                                                        juce::uint32 seed,
                                                        int maxStreaks = 14)
    {
        std::vector<juce::Path> streaks;
        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f || maxStreaks <= 0)
            return streaks;

        juce::Random rng ((juce::int64) seed);
        const int count = juce::jmin (maxStreaks, 6 + rng.nextInt (8));
        streaks.reserve ((size_t) count);

        // Shared diagonal direction (~ -32 degrees, descending left→right).
        const float baseAngle = -0.56f;

        for (int i = 0; i < count; ++i)
        {
            const float len   = b.getWidth()  * (0.22f + 0.45f * rng.nextFloat());
            const float thick = juce::jmax (1.0f, b.getHeight() * (0.012f + 0.030f * rng.nextFloat()));
            const float ang   = baseAngle + (rng.nextFloat() - 0.5f) * 0.16f;
            const float dx    = std::cos (ang) * len;
            const float dy    = std::sin (ang) * len;

            // Start point clamped so the whole streak stays inside bounds.
            const float minX = b.getX() + juce::jmax (0.0f, -dx);
            const float maxX = b.getRight() - juce::jmax (0.0f, dx);
            const float minY = b.getY() + juce::jmax (0.0f, -dy) + thick;
            const float maxY = b.getBottom() - juce::jmax (0.0f, dy) - thick;
            if (maxX <= minX || maxY <= minY)
                continue;

            const float x0 = minX + rng.nextFloat() * (maxX - minX);
            const float y0 = minY + rng.nextFloat() * (maxY - minY);
            const float x1 = x0 + dx;
            const float y1 = y0 + dy;

            // Brush quad with tapered tail — a streak, not a rectangle.
            const float nx = -std::sin (ang), ny = std::cos (ang);
            const float hw0 = thick * 0.5f;
            const float hw1 = thick * 0.12f; // tapered tip

            juce::Path p;
            p.startNewSubPath (x0 + nx * hw0, y0 + ny * hw0);
            p.lineTo        (x1 + nx * hw1, y1 + ny * hw1);
            p.lineTo        (x1 - nx * hw1, y1 - ny * hw1);
            p.lineTo        (x0 - nx * hw0, y0 - ny * hw0);
            p.closeSubPath();
            streaks.push_back (std::move (p));
        }
        return streaks;
    }

    /** Draws cached slash-flow streaks with gradient energy per streak. */
    inline void drawSlashFlow (juce::Graphics& g, const std::vector<juce::Path>& cached,
                               juce::Colour head, juce::Colour tail, float opacity)
    {
        int index = 0;
        for (const auto& p : cached)
        {
            const auto bb = p.getBounds();
            const float fade = 1.0f - 0.55f * ((float) (index++ % 4) / 3.0f);
            juce::ColourGradient grad (head.withAlpha (opacity * fade),
                                       bb.getX(), bb.getY(),
                                       tail.withAlpha (opacity * 0.35f * fade),
                                       bb.getRight(), bb.getBottom(), false);
            g.setGradientFill (grad);
            g.fillPath (p);
        }
    }

    /** Draws cached splatter with capped decorative opacity. */
    inline void drawSplatter (juce::Graphics& g, const std::vector<juce::Path>& cached,
                              juce::Colour colour, float opacity = -1.0f)
    {
        auto& t = Theme::getInstance().apex;
        const float o = juce::jmin (opacity >= 0.0f ? opacity : t.decor.splatterOpacity,
                                    0.35f); // splatter never dominates
        g.setColour (colour.withAlpha (o));
        for (const auto& d : cached)
            g.fillPath (d);
    }

    //==============================================================================
    // Vibrant animated header splatter — colour splash for menu bars and
    // floating-window title bars. Deterministic droplet positions (stable
    // across frames), a slow per-droplet alpha pulse driven by phaseSeconds,
    // and a soft trailing glow so the header feels alive without a timer
    // storm. Pure presentation; no layout or hit-test impact.
    //==============================================================================
    inline void drawAnimatedHeaderSplatter (juce::Graphics& g,
                                            juce::Rectangle<float> bounds,
                                            float phaseSeconds)
    {
        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        // Cache droplet geometry per rounded bounds so frames stay stable.
        struct Key
        {
            int x, y, w, h;
            bool operator== (const Key& o) const noexcept
            {
                return x == o.x && y == o.y && w == o.w && h == o.h;
            }
        };
        struct CacheEntry
        {
            Key key;
            std::vector<juce::Path> paths;
        };
        static CacheEntry cache;

        Key key;
        key.x = (int) std::round (bounds.getX());
        key.y = (int) std::round (bounds.getY());
        key.w = (int) std::round (bounds.getWidth());
        key.h = (int) std::round (bounds.getHeight());
        if (! (cache.key == key))
        {
            cache.key = key;
            cache.paths = buildSplatterPaths (bounds, 0xA9E1C0DEu, 26);
        }

        auto& a = Theme::getInstance().apex;
        const juce::Colour accents[] = { a.color.magenta, a.color.pink,
                                         a.color.violetBright, a.color.cyan };

        const float t = phaseSeconds * 1.4f;   // slow pulse
        int index = 0;
        for (const auto& d : cache.paths)
        {
            const auto bb = d.getBounds();
            const float seed = (float) (index++);
            const float phase = t + seed * 1.7f;
            const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * std::sin (phase));
            const juce::Colour accent = accents[index % 4];

            // Soft halo dot behind each droplet (glow).
            const float r = juce::jmax (bb.getWidth(), bb.getHeight()) * 0.75f;
            g.setColour (accent.withAlpha (0.05f * pulse));
            g.fillEllipse (bb.getCentreX() - r, bb.getCentreY() - r, r * 2.0f, r * 2.0f);

            // The droplet itself — capped so decoration never dominates.
            g.setColour (accent.withAlpha (juce::jmin (0.16f * pulse, 0.16f)));
            g.fillPath (d);
        }

        // Breathing accent line along the bottom of the splash zone.
        const float breath = 0.5f + 0.5f * std::sin (phaseSeconds * 2.2f);
        g.setColour (a.color.pink.withAlpha (0.10f + 0.08f * breath));
        g.fillRect (bounds.getX(), bounds.getBottom() - 1.0f,
                    bounds.getWidth(), 1.0f);
    }

    //==============================================================================
    // Dark panel surface with restrained border; optional accent top edge.
    //==============================================================================
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> b,
                           PanelDepth depth, bool selected,
                           juce::Colour accent = {})
    {
        auto& t = Theme::getInstance().apex;
        const auto a = accent.isTransparent() ? defaultAccent() : accent;

        juce::Colour fill = depth == PanelDepth::base     ? t.color.panelA
                          : depth == PanelDepth::raised   ? t.color.panelB
                          :                                 t.color.panelC;

        const float r = t.metric.radiusPanel;
        g.setColour (fill);
        g.fillRoundedRectangle (b, r);

        if (selected)
        {
            // Accent energy enters from the top edge — selection reads instantly.
            auto strip = b.reduced (r * 0.4f, 0.0f).withHeight (t.metric.strokeNormal * 2.0f);
            juce::ColourGradient grad (a.withAlpha (t.state.selectedStrength + 0.35f),
                                       strip.getX(), strip.getCentreY(),
                                       a.withAlpha (0.10f),
                                       strip.getRight(), strip.getCentreY(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (strip, t.metric.strokeNormal);

            g.setColour (a.withAlpha (t.state.selectedStrength + 0.25f));
            g.drawRoundedRectangle (b.reduced (t.metric.strokeNormal * 0.5f), r,
                                    t.metric.strokeNormal);
        }
        else
        {
            g.setColour (t.color.borderSoftA);
            g.drawRoundedRectangle (b.reduced (t.metric.strokeNormal * 0.5f), r,
                                    t.metric.strokeNormal);
        }
    }

    /** Final-pass frame for top-level APEX client surfaces.
        Call from paintOverChildren() so edge-to-edge child content cannot
        cover the frame. This is presentation only and does not alter hit tests.
        The caller must pass the complete top-level client bounds (e.g.
        getLocalBounds()); every stroke is drawn INSIDE those bounds (each
        rectangle is reduced by half the stroke width) so no part of the frame
        is ever painted outside the component clip or left invisible by window
        border offsets.

        accent is the shell's identity colour:
          - empty → the violet transformation accent (floating window shells);
          - supplied (e.g. Theme::apex.color.shellBorder) → the structural edge
            itself becomes that colour, giving the main DAW window a distinct
            electric steel-blue boundary that separates the shell from the
            near-black interior and from the purple UI accents.
        Inner geometry is unified: the innermost hairline ends at ~5.75 px, so
        a content inset of 6 logical px keeps every child fully inside the frame. */
    inline void drawOuterWindowFrame (juce::Graphics& g, juce::Rectangle<float> bounds,
                                      juce::Colour accent = {})
    {
        if (bounds.isEmpty())
            return;

        const auto& t = Theme::getInstance().apex;
        const bool steel = ! accent.isTransparent();

        // Structural outer edge — the visible boundary of the window. The rect
        // is inset by half the thickness so the complete 4 px stroke lands
        // inside the client area and hugs the exact window edge.
        g.setColour (steel ? accent.darker (0.18f) : t.color.borderSoftB);
        g.drawRect (bounds.reduced (2.0f), 4.0f);

        // Accent line just inside the structural edge — a brighter echo of the
        // shell colour (violet identity for floating shells, steel-blue core
        // for the main window) so the border reads sharp and premium.
        g.setColour (steel ? accent : t.color.violet.withAlpha (0.42f));
        g.drawRect (bounds.reduced (steel ? 3.0f : 4.0f), 1.5f);

        // Innermost hairline — separates the shell border from the content.
        g.setColour (t.color.borderSoftA.withAlpha (0.85f));
        g.drawRect (bounds.reduced (5.0f), t.metric.strokeThin);
    }

    //==============================================================================
    // Semantic glow — layered strokes, no blur filters (repaint-discipline safe).
    //==============================================================================
    inline void drawGlow (juce::Graphics& g, juce::Rectangle<float> b, bool active,
                          juce::Colour accent = {})
    {
        auto& t = Theme::getInstance().apex;
        const auto a = accent.isTransparent() ? defaultAccent() : accent;
        const float base = active ? t.state.glowOpacityActive
                                  : t.state.glowOpacityInactive;

        for (int i = 0; i < 3; ++i)
        {
            const float grow = (float) (i + 1) * (t.metric.glowRadius / 3.0f);
            const float alpha = base * (1.0f - (float) i / 3.0f);
            g.setColour (a.withAlpha (alpha));
            g.drawRoundedRectangle (b.expanded (grow), t.metric.radiusPanel + grow,
                                    t.metric.strokeThin);
        }
    }

    //==============================================================================
    // Trim knob — dark body, thin luminous arc, clear pointer. No ornament.
    //==============================================================================
    struct KnobState
    {
        float value01 = 0.0f;   // normalised parameter position
        bool  active  = false;  // engaged / automated / touched
        bool  hover   = false;
    };

    inline void drawTrimKnob (juce::Graphics& g, juce::Rectangle<float> b,
                              const KnobState& k,
                              juce::Colour accent = {})
    {
        auto& t = Theme::getInstance().apex;
        const auto a = accent.isTransparent() ? defaultAccent() : accent;

        const auto c  = b.getCentre();
        const float r = 0.5f * juce::jmin (b.getWidth(), b.getHeight());
        if (r <= 0.0f)
            return;

        // Dark body.
        g.setColour (t.color.panelC);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (t.color.borderSoftA);
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, t.metric.strokeThin);

        // Rotary travel: -135deg .. +135deg measured from 12 o'clock.
        const float startA = juce::MathConstants<float>::pi * 1.25f;
        const float endA   = juce::MathConstants<float>::pi * 2.75f;
        const float valueA = startA + juce::jlimit (0.0f, 1.0f, k.value01)
                                       * (endA - startA);

        // Track arc (restrained).
        juce::Path track;
        track.addCentredArc (c.x, c.y, r * 0.82f, r * 0.82f, 0.0f, startA, endA, true);
        g.setColour (t.color.borderSoftB);
        g.strokePath (track, juce::PathStrokeType (t.metric.strokeNormal,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Luminous value arc.
        juce::Path value;
        value.addCentredArc (c.x, c.y, r * 0.82f, r * 0.82f, 0.0f, startA, valueA, true);
        const float arcAlpha = k.active ? 0.95f : 0.55f;
        g.setColour (a.withAlpha (arcAlpha));
        g.strokePath (value, juce::PathStrokeType (t.metric.strokeNormal,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Pointer — angle measured like the arc (from 12 o'clock, clockwise).
        // Starts away from the centre so the dark body core stays readable.
        const float pointerA = valueA - juce::MathConstants<float>::halfPi;
        const float ix = c.x + std::cos (pointerA) * r * 0.30f;
        const float iy = c.y + std::sin (pointerA) * r * 0.30f;
        const float px = c.x + std::cos (pointerA) * r * 0.62f;
        const float py = c.y + std::sin (pointerA) * r * 0.62f;
        g.setColour (k.hover ? t.color.textPrimary : a.withAlpha (0.9f));
        g.drawLine (ix, iy, px, py, t.metric.strokeNormal);
    }

    //==============================================================================
    // Fader — thin dark rail, illuminated cap, readable at a glance.
    //==============================================================================
    struct FaderState
    {
        float value01 = 0.0f;   // 0 = bottom, 1 = top
        bool  active  = false;  // touched / automated / selected strip
        bool  hover   = false;
    };

    inline void drawFader (juce::Graphics& g, juce::Rectangle<float> b,
                           const FaderState& f,
                           juce::Colour accent = {})
    {
        auto& t = Theme::getInstance().apex;
        const auto a = accent.isTransparent() ? defaultAccent() : accent;
        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
            return;

        const float cx     = b.getCentreX();
        const float travel = b.getHeight();
        const float v      = juce::jlimit (0.0f, 1.0f, f.value01);
        const float capY   = b.getY() + (1.0f - v) * travel;

        // Thin dark rail.
        g.setColour (t.color.deepestB);
        g.fillRoundedRectangle (cx - t.metric.strokeNormal, b.getY(),
                                t.metric.strokeNormal * 2.0f, travel,
                                t.metric.strokeNormal);

        // Energised portion of the rail below the cap — signal level reads
        // without a meter flood.
        if (v > 0.001f)
        {
            g.setColour (a.withAlpha (f.active ? 0.75f : 0.40f));
            g.fillRoundedRectangle (cx - t.metric.strokeThin * 0.5f, capY,
                                    t.metric.strokeThin, b.getBottom() - capY,
                                    t.metric.strokeThin * 0.5f);
        }

        // Illuminated cap.
        const float capW = juce::jmax (b.getWidth(), 14.0f);
        const float capH = juce::jmax (6.0f, b.getWidth() * 0.45f);
        juce::Rectangle<float> cap (cx - capW * 0.5f, capY - capH * 0.5f, capW, capH);

        juce::ColourGradient grad (a.brighter (0.35f), cap.getX(), cap.getCentreY(),
                                   a,                  cap.getRight(), cap.getCentreY(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (cap, t.metric.radiusControl * 0.5f);

        g.setColour (t.color.textPrimary.withAlpha (f.hover ? 0.95f : 0.55f));
        g.drawRoundedRectangle (cap, t.metric.radiusControl * 0.5f, t.metric.strokeThin);

        if (f.active)
            drawGlow (g, cap, true, a);
    }

    //==============================================================================
    // Shared control surface — identical normal/hover/pressed/selected/active/
    // disabled behaviour across toolbar, mixer, FX chain and transport.
    //==============================================================================
    inline void drawControlSurface (juce::Graphics& g, juce::Rectangle<float> b,
                                    const ControlState& s,
                                    juce::Colour accent = {})
    {
        auto& t = Theme::getInstance().apex;
        const auto a = accent.isTransparent() ? defaultAccent() : accent;

        juce::Colour fill = t.color.panelB;
        if (s.hover)   fill = fill.brighter (t.state.hoverStrength * 4.0f);
        if (s.pressed) fill = fill.brighter (t.state.pressedStrength * 2.0f);

        if (s.selected || s.active)
            fill = fill.interpolatedWith (a, t.state.selectedStrength);

        const float r = t.metric.radiusControl;
        g.setColour (fill.withAlpha (s.disabled ? 0.45f : 1.0f));
        g.fillRoundedRectangle (b, r);

        juce::Colour border = (s.selected || s.active)
                            ? a.withAlpha (0.85f)
                            : t.color.borderSoftA;
        g.setColour (border.withAlpha (s.disabled ? 0.35f : border.getFloatAlpha()));
        g.drawRoundedRectangle (b.reduced (t.metric.strokeNormal * 0.5f), r,
                                t.metric.strokeNormal);
    }

} // namespace DAW::ApexPrimitives
