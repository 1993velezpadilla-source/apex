#pragma once
#include <JuceHeader.h>
#include "AnalogVuScaleCore.h"

namespace DAW {

/**
 * AnalogVuScaleMarkingsRenderer
 *
 * Paints all the ink markings on top of the cream face and red zone:
 *   - thin baseline arc connecting the inner ends of the ticks
 *   - long ticks at every major (labelled) dB value
 *   - short ticks at every minor (unlabelled) subdivision
 *   - numeric labels at each major tick, in red ink inside the red zone
 *
 * Stateless. Reads tick definitions from AnalogVuScaleCore so the layout is
 * single-sourced.
 */
class AnalogVuScaleMarkingsRenderer
{
public:
    struct Config
    {
        juce::Colour scaleInk          { 0xFF1A1A1A };
        juce::Colour scaleInkOnRed     { 0xFFFAFAFA };
        juce::Colour redLabelInk       { 0xFFC8332A };

        float tickInnerRadius          { 140.0f };
        float tickOuterRadius          { 165.0f };
        float minorTickInnerOffset     { 12.0f  };
        float labelRadius              { 178.0f };
        float baselineArcRadius        { 140.0f };

        float majorTickThickness       { 1.2f };
        float minorTickThickness       { 0.6f };
        float minorTickAlpha           { 0.55f };
        float baselineArcThickness     { 0.8f };

        float labelFontSize            { 12.0f };
        float labelBoxWidth            { 32.0f };
        float labelBoxHeight           { 16.0f };
    };

    AnalogVuScaleMarkingsRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g, juce::Point<float> pivot) const
    {
        paintBaselineArc(g, pivot);
        paintMajorTicks(g, pivot);
        paintMinorTicks(g, pivot);
        paintLabels(g, pivot);
    }

private:
    void paintBaselineArc(juce::Graphics& g, juce::Point<float> pivot) const
    {
        juce::Path arc;
        arc.addCentredArc(pivot.x, pivot.y,
                          config_.baselineArcRadius, config_.baselineArcRadius,
                          0.0f,
                          juce::degreesToRadians(AnalogVuScaleCore::getMinAngleDeg()),
                          juce::degreesToRadians(AnalogVuScaleCore::getMaxAngleDeg()),
                          true);
        g.setColour(config_.scaleInk);
        g.strokePath(arc, juce::PathStrokeType(config_.baselineArcThickness));
    }

    void paintMajorTicks(juce::Graphics& g, juce::Point<float> pivot) const
    {
        for (const auto& t : AnalogVuScaleCore::getMajorTicks())
        {
            const auto outer = AnalogVuScaleCore::pointOnArc(pivot, config_.tickOuterRadius, t.angleDeg);
            const auto inner = AnalogVuScaleCore::pointOnArc(pivot, config_.tickInnerRadius, t.angleDeg);
            g.setColour(t.isRedZone ? config_.scaleInkOnRed : config_.scaleInk);
            g.drawLine({ inner, outer }, config_.majorTickThickness);
        }
    }

    void paintMinorTicks(juce::Graphics& g, juce::Point<float> pivot) const
    {
        const float innerR = config_.tickInnerRadius + config_.minorTickInnerOffset;
        for (const auto& t : AnalogVuScaleCore::getMinorTicks())
        {
            const auto outer = AnalogVuScaleCore::pointOnArc(pivot, config_.tickOuterRadius, t.angleDeg);
            const auto inner = AnalogVuScaleCore::pointOnArc(pivot, innerR, t.angleDeg);
            const auto col = (t.isRedZone ? config_.scaleInkOnRed : config_.scaleInk)
                                .withAlpha(config_.minorTickAlpha);
            g.setColour(col);
            g.drawLine({ inner, outer }, config_.minorTickThickness);
        }
    }

    void paintLabels(juce::Graphics& g, juce::Point<float> pivot) const
    {
        g.setFont(juce::Font(config_.labelFontSize, juce::Font::bold));
        for (const auto& t : AnalogVuScaleCore::getMajorTicks())
        {
            if (t.label == nullptr) continue;
            const auto centre = AnalogVuScaleCore::pointOnArc(pivot, config_.labelRadius, t.angleDeg);
            const juce::Rectangle<float> r(centre.x - config_.labelBoxWidth  * 0.5f,
                                           centre.y - config_.labelBoxHeight * 0.5f,
                                           config_.labelBoxWidth,
                                           config_.labelBoxHeight);
            g.setColour(t.isRedZone ? config_.redLabelInk : config_.scaleInk);
            g.drawText(t.label, r, juce::Justification::centred);
        }
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuScaleMarkingsRenderer)
};

} // namespace DAW
