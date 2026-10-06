#pragma once
#include <JuceHeader.h>
#include "AnalogVuOverloadCore.h"

namespace DAW {

/**
 * AnalogVuOverloadRenderer
 *
 * Paints the OL (overload) bulb on top of the cream face. Reads the latch
 * state from AnalogVuOverloadCore — when latched, the bulb glows; otherwise
 * it shows the dim "ready" colour.
 *
 * The bulb is positioned by an offset relative to the face's top-right
 * corner (mvMeter2 places it just inside the upper-right). The "OL" text
 * label below the bulb is also painted here.
 *
 * Stateless. The state lives in the AnalogVuOverloadCore reference.
 */
class AnalogVuOverloadRenderer
{
public:
    struct Config
    {
        juce::Colour bulbOn        { 0xFFFF3535 };
        juce::Colour bulbOnGlow    { 0x66FF3535 };
        juce::Colour bulbOff       { 0xFF3D2018 };
        juce::Colour bulbHighlight { 0xB35A2F22 };
        juce::Colour bulbEdge      { 0x801A1A1A };
        juce::Colour labelInk      { 0xB31A1A1A };

        float bulbRadius     { 6.5f };
        float glowRadiusMult { 1.6f };
        float edgeThickness  { 0.5f };
        float labelFontSize  { 11.0f };

        // Offset from the face's top-right corner. x is negative (inset from right),
        // y is positive (down from top).
        juce::Point<float> offsetFromTopRight { -45.0f, 25.0f };
    };

    AnalogVuOverloadRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    /** Compute the bulb centre for a given face rectangle. Useful for hit-testing. */
    juce::Point<float> getBulbCentre(juce::Rectangle<float> faceBounds) const noexcept
    {
        return { faceBounds.getRight() + config_.offsetFromTopRight.x,
                 faceBounds.getY()    + config_.offsetFromTopRight.y };
    }

    /** True if the click point hits the bulb (used for click-to-reset). */
    bool hitTest(juce::Point<float> p, juce::Rectangle<float> faceBounds) const noexcept
    {
        return getBulbCentre(faceBounds).getDistanceFrom(p) <= config_.bulbRadius * 1.5f;
    }

    void paint(juce::Graphics& g,
               juce::Rectangle<float> faceBounds,
               const AnalogVuOverloadCore& overload) const
    {
        const auto centre = getBulbCentre(faceBounds);
        const float r = config_.bulbRadius;

        if (overload.isLatched())
        {
            g.setColour(config_.bulbOnGlow);
            g.fillEllipse(centre.x - r * config_.glowRadiusMult,
                          centre.y - r * config_.glowRadiusMult,
                          r * config_.glowRadiusMult * 2.0f,
                          r * config_.glowRadiusMult * 2.0f);

            g.setColour(config_.bulbOn);
            g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
        }
        else
        {
            g.setColour(config_.bulbOff);
            g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);

            g.setColour(config_.bulbHighlight);
            g.fillEllipse(centre.x - r * 0.4f - 1.0f, centre.y - r * 0.4f - 1.0f,
                          2.0f, 2.0f);
        }

        g.setColour(config_.bulbEdge);
        g.drawEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f,
                      config_.edgeThickness);

        g.setColour(config_.labelInk);
        g.setFont(juce::Font(config_.labelFontSize, juce::Font::bold));
        g.drawText("OL",
                   juce::Rectangle<float>(centre.x - 12.0f, centre.y + r + 2.0f,
                                          24.0f, 14.0f),
                   juce::Justification::centred);
    }

private:
    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuOverloadRenderer)
};

} // namespace DAW
