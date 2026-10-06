#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * MasterStripDripBackgroundRenderer
 *
 * Stateless renderer that paints the distinctive "3D pop-up dripping
 * bubblegum" background that visually elevates the master strip above the
 * regular mixer strips.
 *
 * Three visual layers, painted bottom to top:
 *   1. Drop shadow ellipse below the strip — creates the "floating" effect
 *   2. Dark gradient body fill — vertical, dark-warm tones
 *   3. Pink "drip" ribbons at the top corners — bubblegum dripping down
 *   4. Pink border outline around the entire strip
 *
 * Designed for use ONLY when track_.isMaster() is true. Regular strips use
 * the existing paintLegacyStripBase() / paintPremiumStripBase() paths in
 * MixerStrip.
 *
 * The renderer expects to paint into a juce::Graphics context that is
 * clipped to the strip's local bounds. It will paint the drop shadow
 * BELOW the strip's bottom edge, so the parent component must allow the
 * paint to overflow downward — alternatively, the strip can be sized
 * with extra bottom margin reserved for the shadow.
 */
class MasterStripDripBackgroundRenderer
{
public:
    struct Config
    {
        // Body gradient
        juce::Colour bodyTopWarm        { 0xFF2A1A24 };
        juce::Colour bodyMidWarm        { 0xFF3D1F2E };
        juce::Colour bodyBottomDark     { 0xFF1A0F16 };

        // Pink accents
        juce::Colour pinkPrimary        { 0xFFFF4F8A };
        juce::Colour pinkLight          { 0xFFFFB3D0 };

        // Border
        float borderStroke              { 1.2f };
        float cornerRadius              { 10.0f };

        // Drip geometry
        float dripWidth                 { 8.0f };
        float dripStartHeight           { 0.0f };   // attaches at top edge
        float dripExtendBelowHeader     { 90.0f };  // how far below header it drips
        float dripBulgeRadius           { 6.0f };   // bulb at the bottom of drip

        // Header band
        float headerBandHeight          { 42.0f };
        float headerBandAlpha           { 0.18f };

        // Drop shadow (for "3D pop-up" effect)
        float shadowOffsetBelowStrip    { 10.0f };
        float shadowEllipseWidthMult    { 0.91f };  // relative to strip width
        float shadowEllipseHeightOuter  { 14.0f };
        float shadowEllipseHeightMid    { 10.0f };
        float shadowEllipseHeightInner  { 6.0f };
        float shadowAlphaOuter          { 0.18f };
        float shadowAlphaMid            { 0.25f };
        float shadowAlphaInner          { 0.30f };

        // Floating drips (small standalone droplets in the upper area)
        float floatingDripRadius        { 5.0f };
    };

    MasterStripDripBackgroundRenderer() = default;

    void setConfig(const Config& c) { config_ = c; }
    const Config& getConfig() const { return config_; }

    /**
     * Paint the background for the strip. `bounds` is the strip's local
     * rectangle. `paintShadow` should be true only when the parent allows
     * paint overflow below the strip (otherwise the shadow gets clipped).
     */
    void paint(juce::Graphics& g,
               juce::Rectangle<float> bounds,
               bool paintShadow = false) const
    {
        if (paintShadow)
            paintDropShadow(g, bounds);

        paintBody(g, bounds);
        paintHeaderBandTint(g, bounds);
        paintCornerDrips(g, bounds);
        paintFloatingDrips(g, bounds);
        paintBorder(g, bounds);
    }

    /**
     * Paint just the drop shadow underneath. Use when the parent paints
     * the shadow separately (different clip region) than the strip body.
     */
    void paintShadowOnly(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        paintDropShadow(g, bounds);
    }

private:
    void paintBody(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        juce::ColourGradient grad(config_.bodyTopWarm,
                                  bounds.getX(), bounds.getY(),
                                  config_.bodyBottomDark,
                                  bounds.getX(), bounds.getBottom(), false);
        grad.addColour(0.40, config_.bodyMidWarm);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, config_.cornerRadius);
    }

    void paintHeaderBandTint(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        const auto headerBand = bounds.withHeight(config_.headerBandHeight);
        g.setColour(config_.pinkPrimary.withAlpha(config_.headerBandAlpha));
        g.fillRoundedRectangle(headerBand, config_.cornerRadius);
    }

    void paintCornerDrips(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        // Left drip
        paintSingleDrip(g,
                        bounds.getX(),
                        bounds.getY(),
                        config_.dripWidth,
                        config_.dripExtendBelowHeader);

        // Right drip
        paintSingleDrip(g,
                        bounds.getRight() - config_.dripWidth,
                        bounds.getY(),
                        config_.dripWidth,
                        config_.dripExtendBelowHeader * 0.85f);
    }

    void paintSingleDrip(juce::Graphics& g,
                         float x,
                         float yTop,
                         float width,
                         float length) const
    {
        // Vertical gradient from solid pink at top to transparent at bottom
        juce::ColourGradient drip(
            config_.pinkPrimary.withAlpha(0.95f), x + width * 0.5f, yTop,
            config_.pinkPrimary.withAlpha(0.0f),  x + width * 0.5f, yTop + length, false);
        drip.addColour(0.50, config_.pinkPrimary.withAlpha(0.55f));
        g.setGradientFill(drip);

        juce::Path p;
        p.startNewSubPath(x, yTop);
        p.lineTo(x + width, yTop);
        p.lineTo(x + width, yTop + length * 0.85f);
        p.quadraticTo(x + width * 0.5f, yTop + length,
                      x, yTop + length * 0.85f);
        p.closeSubPath();
        g.fillPath(p);
    }

    void paintFloatingDrips(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        // Single small floating droplet to break up symmetry
        const float r = config_.floatingDripRadius;
        const float x = bounds.getX() + bounds.getWidth() * 0.22f;
        const float y = bounds.getY() + 102.0f;
        g.setColour(config_.pinkPrimary.withAlpha(0.55f));
        g.fillEllipse(x - r, y - r * 1.4f, r * 2.0f, r * 2.8f);
    }

    void paintBorder(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        g.setColour(config_.pinkPrimary);
        g.drawRoundedRectangle(bounds, config_.cornerRadius, config_.borderStroke);
    }

    void paintDropShadow(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getBottom() + config_.shadowOffsetBelowStrip;
        const float w  = bounds.getWidth() * config_.shadowEllipseWidthMult;

        auto paintEllipse = [&](float h, float alpha)
        {
            g.setColour(config_.pinkPrimary.withAlpha(alpha));
            g.fillEllipse(cx - w * 0.5f, cy - h * 0.5f, w, h);
        };

        paintEllipse(config_.shadowEllipseHeightOuter, config_.shadowAlphaOuter);
        paintEllipse(config_.shadowEllipseHeightMid,   config_.shadowAlphaMid);
        paintEllipse(config_.shadowEllipseHeightInner, config_.shadowAlphaInner);
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStripDripBackgroundRenderer)
};

} // namespace DAW
