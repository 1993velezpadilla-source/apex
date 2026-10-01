#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumKnobHaloRenderer
 *
 * Paints the soft pink halo behind the knob body. JUCE has no real Gaussian
 * blur in its standard graphics pipeline, so we fake the bloom with three
 * concentric filled ellipses at decreasing radii and increasing alpha. The
 * eye reads this as a glow.
 *
 * Stateless. Three ellipses, painted in outer-to-inner order so they additive-
 * blend under the body which is drawn after.
 */
class BubblegumKnobHaloRenderer
{
public:
    struct Config
    {
        juce::Colour glowColour    { 0xFFFF4F8A };

        float radiusMult1   { 1.35f };
        float radiusMult2   { 1.25f };
        float radiusMult3   { 1.15f };

        float alphaOuter    { 0.05f };
        float alphaMid      { 0.08f };
        float alphaInner    { 0.14f };

        float ringRadiusMult     { 1.10f };
        float ringStrokeWidth    { 0.6f  };
        float ringAlpha          { 0.50f };
    };

    BubblegumKnobHaloRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g,
               juce::Point<float> centre,
               float bodyRadius) const
    {
        paintGlowDisc(g, centre, bodyRadius * config_.radiusMult1, config_.alphaOuter);
        paintGlowDisc(g, centre, bodyRadius * config_.radiusMult2, config_.alphaMid);
        paintGlowDisc(g, centre, bodyRadius * config_.radiusMult3, config_.alphaInner);

        const float ringR = bodyRadius * config_.ringRadiusMult;
        g.setColour(config_.glowColour.withAlpha(config_.ringAlpha));
        g.drawEllipse(centre.x - ringR, centre.y - ringR,
                      ringR * 2.0f, ringR * 2.0f,
                      config_.ringStrokeWidth);
    }

private:
    void paintGlowDisc(juce::Graphics& g,
                       juce::Point<float> centre,
                       float radius,
                       float alpha) const
    {
        g.setColour(config_.glowColour.withAlpha(alpha));
        g.fillEllipse(centre.x - radius, centre.y - radius,
                      radius * 2.0f, radius * 2.0f);
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumKnobHaloRenderer)
};

} // namespace DAW
