#pragma once
#include <JuceHeader.h>
#include "AnalogVuScaleCore.h"

namespace DAW {

/**
 * AnalogVuRedZoneRenderer
 *
 * Paints the red overload band — the curved red strip that runs from 0 dB to
 * +3 dB on the dial. Two passes: a thick red fill stroke, then a thin darker
 * edge stroke for the inner/outer outline.
 *
 * The angles come from AnalogVuScaleCore. The renderer only needs a pivot
 * point and a centre radius for the band.
 */
class AnalogVuRedZoneRenderer
{
public:
    struct Config
    {
        juce::Colour fillColour { 0xFFC8332A };
        juce::Colour edgeColour { 0xB37A1F18 };

        float centreRadius   { 152.5f };
        float bandWidth      { 22.0f  };
        float edgeThickness  { 0.8f   };
    };

    AnalogVuRedZoneRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g, juce::Point<float> pivot) const
    {
        juce::Path arc;
        arc.addCentredArc(pivot.x, pivot.y,
                          config_.centreRadius, config_.centreRadius,
                          0.0f,
                          juce::degreesToRadians(AnalogVuScaleCore::getRedZoneStartAngle()),
                          juce::degreesToRadians(AnalogVuScaleCore::getRedZoneEndAngle()),
                          true);

        g.setColour(config_.fillColour);
        g.strokePath(arc, juce::PathStrokeType(config_.bandWidth,
                                               juce::PathStrokeType::beveled,
                                               juce::PathStrokeType::butt));

        g.setColour(config_.edgeColour);
        g.strokePath(arc, juce::PathStrokeType(config_.edgeThickness,
                                               juce::PathStrokeType::beveled,
                                               juce::PathStrokeType::butt));
    }

private:
    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuRedZoneRenderer)
};

} // namespace DAW
