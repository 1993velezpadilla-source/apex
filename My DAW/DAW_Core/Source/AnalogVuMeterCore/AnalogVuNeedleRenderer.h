#pragma once
#include <JuceHeader.h>
#include "AnalogVuScaleCore.h"

namespace DAW {

/**
 * AnalogVuNeedleRenderer
 *
 * Paints the needle and pivot cap on top of the meter face. Stateless: takes
 * a target needle dB and a pivot point, draws the needle at the corresponding
 * angle on the scale.
 *
 * The needle is drawn in two strokes — a wider base segment near the pivot
 * (for visual mass / balance) and a thinner tip segment that reaches the
 * scale arc. Pivot cap is a radial-gradient circle with a small specular
 * highlight to suggest a polished metal screw head.
 *
 * Any needle inertia / spring smoothing belongs upstream in
 * AnalogVuBallisticsCore; by the time the dB reaches this renderer the
 * value is already at the visually-correct position.
 */
class AnalogVuNeedleRenderer
{
public:
    struct Config
    {
        juce::Colour needleInk          { 0xFF0A0C13 };
        juce::Colour pivotCapInner      { 0xFF3D424F };
        juce::Colour pivotCapOuter      { 0xFF0A0C13 };
        juce::Colour pivotCapEdge       { 0xCC000000 };
        juce::Colour pivotCapHighlight  { 0x994A5060 };

        float needleLength       { 160.0f };
        float needleBaseLength   { 30.0f  };
        float needleBaseWidth    { 3.2f   };
        float needleTipWidth     { 1.6f   };

        float pivotCapRadius     { 14.0f };
        float pivotCapEdgeWidth  { 0.8f  };
        float pivotCapHighlightR { 2.5f  };
    };

    AnalogVuNeedleRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g,
               juce::Point<float> pivot,
               float needleDb) const
    {
        const float angleDeg = AnalogVuScaleCore::dbToAngleDeg(needleDb);
        const float angleRad = juce::degreesToRadians(angleDeg);

        const float sinA = std::sin(angleRad);
        const float cosA = std::cos(angleRad);

        const juce::Point<float> tip(pivot.x + sinA * config_.needleLength,
                                     pivot.y - cosA * config_.needleLength);
        const juce::Point<float> base(pivot.x + sinA * config_.needleBaseLength,
                                      pivot.y - cosA * config_.needleBaseLength);

        g.setColour(config_.needleInk);
        g.drawLine({ pivot, base }, config_.needleBaseWidth);
        g.drawLine({ base, tip },   config_.needleTipWidth);

        paintPivotCap(g, pivot);
    }

private:
    void paintPivotCap(juce::Graphics& g, juce::Point<float> pivot) const
    {
        const float r = config_.pivotCapRadius;
        const juce::Rectangle<float> capRect(pivot.x - r, pivot.y - r, r * 2.0f, r * 2.0f);

        juce::ColourGradient grad(config_.pivotCapInner,
                                  pivot.x - r * 0.4f, pivot.y - r * 0.4f,
                                  config_.pivotCapOuter,
                                  pivot.x + r * 0.7f, pivot.y + r * 0.7f,
                                  true);
        g.setGradientFill(grad);
        g.fillEllipse(capRect);

        g.setColour(config_.pivotCapEdge);
        g.drawEllipse(capRect, config_.pivotCapEdgeWidth);

        g.setColour(config_.pivotCapHighlight);
        const float hR = config_.pivotCapHighlightR;
        g.fillEllipse(pivot.x - r * 0.3f - hR,
                      pivot.y - r * 0.3f - hR,
                      hR * 2.0f, hR * 2.0f);
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuNeedleRenderer)
};

} // namespace DAW
