#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * AnalogVuCreamFaceRenderer
 *
 * Paints the cream meter face — the warm vintage rectangle that everything
 * else (ticks, labels, red zone, needle, OL light, VU logo) sits on top of.
 *
 * Stateless. Same bounds + same config produce identical output. Does NOT
 * paint anything besides the face itself; other renderers handle the rest.
 */
class AnalogVuCreamFaceRenderer
{
public:
    struct Config
    {
        juce::Colour creamLight    { 0xFFF4ECD7 };
        juce::Colour creamMid      { 0xFFE8DEC7 };
        juce::Colour creamShadow   { 0xFFCFC2A4 };
        juce::Colour bevelInk      { 0x73000000 };
        juce::Colour topHighlight  { 0xB3FBF3DD };

        float cornerRadius        { 6.0f };
        float bevelStrokeWidth    { 0.6f };
        float highlightInset      { 3.0f };
        float highlightThickness  { 1.5f };
    };

    AnalogVuCreamFaceRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        juce::ColourGradient grad(config_.creamLight, bounds.getX(), bounds.getY(),
                                  config_.creamShadow, bounds.getX(), bounds.getBottom(),
                                  false);
        grad.addColour(0.55, config_.creamMid);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, config_.cornerRadius);

        g.setColour(config_.bevelInk);
        g.drawRoundedRectangle(bounds, config_.cornerRadius, config_.bevelStrokeWidth);

        g.setColour(config_.topHighlight);
        g.fillRect(bounds.getX() + config_.highlightInset,
                   bounds.getY() + config_.highlightInset,
                   bounds.getWidth() - config_.highlightInset * 2.0f,
                   config_.highlightThickness);
    }

private:
    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuCreamFaceRenderer)
};

} // namespace DAW
