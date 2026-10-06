#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumKnobIndicatorRenderer
 *
 * Paints the rotating pink pointer that shows the current knob value. The
 * indicator is drawn vertically inside a saved-state coordinate transform
 * rotated around the knob centre — this lets us use a vertical gradient on
 * the indicator that ALWAYS reads "light at the tip, dark at the base"
 * regardless of rotation, which would not be true if we transformed a path
 * after applying a screen-space gradient.
 *
 * Stateless. The angle to draw at is supplied by the caller.
 */
class BubblegumKnobIndicatorRenderer
{
public:
    struct Config
    {
        juce::Colour pinkLight     { 0xFFFFB3D0 };
        juce::Colour pinkBase      { 0xFFFF4F8A };
        juce::Colour pinkDark      { 0xFFC2306A };
        juce::Colour tipHighlight  { 0xB3FFFFFF };

        float widthPx             { 3.0f  };
        float lengthPx            { 22.0f };
        float yOffsetFromCentrePx { 4.0f  };  // gap between cap and indicator base
        float tipHighlightHeight  { 3.0f  };
        float cornerRadius        { 1.5f  };

        float gradientMidStop     { 0.4f  };
    };

    BubblegumKnobIndicatorRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g,
               juce::Point<float> centre,
               float angleDeg) const
    {
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians(angleDeg),
                                                       centre.x, centre.y));

        const juce::Rectangle<float> rect(centre.x - config_.widthPx * 0.5f,
                                          centre.y - config_.lengthPx
                                              - config_.yOffsetFromCentrePx,
                                          config_.widthPx,
                                          config_.lengthPx);

        juce::ColourGradient grad(config_.pinkLight, rect.getX(), rect.getY(),
                                  config_.pinkDark,  rect.getX(), rect.getBottom(),
                                  false);
        grad.addColour(config_.gradientMidStop, config_.pinkBase);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(rect, config_.cornerRadius);

        g.setColour(config_.tipHighlight);
        g.fillRoundedRectangle(rect.getX(), rect.getY(),
                               rect.getWidth(), config_.tipHighlightHeight,
                               config_.cornerRadius);
    }

private:
    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumKnobIndicatorRenderer)
};

} // namespace DAW
