#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumKnobBodyRenderer
 *
 * Paints the dark metallic body of the knob with all of its skeuomorphic
 * detail: the radial gradient main disc, the outer edge stroke, the inner
 * bezel ring, the inset face, the top gloss highlight, and the center cap
 * with its small pink accent dot.
 *
 * Stateless. The body never rotates; only the indicator does. Painting the
 * body and indicator separately keeps the gradient/highlight directions
 * consistent regardless of the current knob value.
 */
class BubblegumKnobBodyRenderer
{
public:
    struct Config
    {
        juce::Colour bodyHighlight     { 0xFF2E3445 };
        juce::Colour bodyMid           { 0xFF161A24 };
        juce::Colour bodyShadow        { 0xFF06080D };
        juce::Colour bodyEdge          { 0xFF3B4156 };
        juce::Colour bezelRing         { 0xFF22273A };
        juce::Colour innerFace         { 0xFF0E1117 };
        juce::Colour topGloss          { 0x14FFFFFF };
        juce::Colour centerCapFill     { 0xFF1A1F2A };
        juce::Colour centerCapBorder   { 0xFF3B4156 };
        juce::Colour centerCapDot      { 0xFFFF4F8A };

        float bodyEdgeStroke         { 1.0f  };
        float bezelRingRadiusMult    { 0.85f };
        float bezelRingStroke        { 0.6f  };
        float innerFaceRadiusMult    { 0.80f };

        float topGlossRxMult         { 0.55f };
        float topGlossRyMult         { 0.18f };
        float topGlossYOffsetMult    { -0.30f };

        float centerCapRadius        { 5.0f };
        float centerCapBorderStroke  { 0.5f };
        float centerCapDotRadius     { 1.2f };
        float centerCapDotAlpha      { 0.7f };
        float centerCapDotXOffset    { -1.5f };
        float centerCapDotYOffset    { -1.5f };
    };

    BubblegumKnobBodyRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g,
               juce::Point<float> centre,
               float bodyRadius) const
    {
        paintBody(g, centre, bodyRadius);
        paintBezelRing(g, centre, bodyRadius);
        paintInnerFace(g, centre, bodyRadius);
        paintTopGloss(g, centre, bodyRadius);
        paintCenterCap(g, centre);
    }

private:
    void paintBody(juce::Graphics& g, juce::Point<float> c, float r) const
    {
        const juce::Rectangle<float> rect(c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        juce::ColourGradient grad(config_.bodyHighlight,
                                  c.x,                 c.y - r * 0.4f,
                                  config_.bodyShadow,
                                  c.x + r * 0.7f,      c.y + r * 0.7f,
                                  true);
        grad.addColour(0.6, config_.bodyMid);
        g.setGradientFill(grad);
        g.fillEllipse(rect);

        g.setColour(config_.bodyEdge);
        g.drawEllipse(rect, config_.bodyEdgeStroke);
    }

    void paintBezelRing(juce::Graphics& g, juce::Point<float> c, float r) const
    {
        const float br = r * config_.bezelRingRadiusMult;
        g.setColour(config_.bezelRing);
        g.drawEllipse(c.x - br, c.y - br, br * 2.0f, br * 2.0f,
                      config_.bezelRingStroke);
    }

    void paintInnerFace(juce::Graphics& g, juce::Point<float> c, float r) const
    {
        const float ir = r * config_.innerFaceRadiusMult;
        g.setColour(config_.innerFace);
        g.fillEllipse(c.x - ir, c.y - ir, ir * 2.0f, ir * 2.0f);
    }

    void paintTopGloss(juce::Graphics& g, juce::Point<float> c, float r) const
    {
        const float rx = r * config_.topGlossRxMult;
        const float ry = r * config_.topGlossRyMult;
        const float yOff = r * config_.topGlossYOffsetMult;
        g.setColour(config_.topGloss);
        g.fillEllipse(c.x - rx, c.y + yOff - ry, rx * 2.0f, ry * 2.0f);
    }

    void paintCenterCap(juce::Graphics& g, juce::Point<float> c) const
    {
        const float r = config_.centerCapRadius;
        g.setColour(config_.centerCapFill);
        g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour(config_.centerCapBorder);
        g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f,
                      config_.centerCapBorderStroke);

        const float dr = config_.centerCapDotRadius;
        g.setColour(config_.centerCapDot.withAlpha(config_.centerCapDotAlpha));
        g.fillEllipse(c.x + config_.centerCapDotXOffset - dr,
                      c.y + config_.centerCapDotYOffset - dr,
                      dr * 2.0f, dr * 2.0f);
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumKnobBodyRenderer)
};

} // namespace DAW
