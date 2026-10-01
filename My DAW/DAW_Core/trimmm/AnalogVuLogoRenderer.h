#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * AnalogVuLogoRenderer
 *
 * Paints the "VU" italic logo at the lower-centre of the cream face, the
 * small mode subtitle below it (e.g. "peak · -17 dB/s"), and the small
 * "VU" tag in the upper-left corner used by skeuomorphic VU meters to
 * indicate the meter's mode/personality.
 *
 * Stateless. Pure paint.
 */
class AnalogVuLogoRenderer
{
public:
    struct Config
    {
        juce::Colour ink          { 0xFF1A1A1A };

        juce::String logoFontName { "Georgia" };
        float        logoFontSize { 26.0f };
        float        logoAlpha    { 0.85f };

        float        subtitleFontSize { 11.0f };
        float        subtitleAlpha    { 0.5f };

        float        cornerTagFontSize { 11.0f };
        float        cornerTagAlpha    { 0.55f };
        float        cornerTagInsetX   { 10.0f };
        float        cornerTagInsetY   { 12.0f };

        // Vertical position of the logo, expressed as a fraction of face height
        float        logoYFraction     { 0.66f };
        float        subtitleYOffset   { 28.0f };
    };

    AnalogVuLogoRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g,
               juce::Rectangle<float> faceBounds,
               const juce::String& subtitleText,
               const juce::String& cornerTagText = "VU") const
    {
        paintLogo(g, faceBounds);
        paintSubtitle(g, faceBounds, subtitleText);
        paintCornerTag(g, faceBounds, cornerTagText);
    }

private:
    void paintLogo(juce::Graphics& g, juce::Rectangle<float> faceBounds) const
    {
        g.setColour(config_.ink.withAlpha(config_.logoAlpha));
        g.setFont(juce::Font(config_.logoFontName,
                             config_.logoFontSize,
                             juce::Font::italic));
        const juce::Rectangle<float> r(faceBounds.getCentreX() - 30.0f,
                                       faceBounds.getY() + faceBounds.getHeight()
                                           * config_.logoYFraction,
                                       60.0f, 30.0f);
        g.drawText("VU", r, juce::Justification::centred);
    }

    void paintSubtitle(juce::Graphics& g,
                       juce::Rectangle<float> faceBounds,
                       const juce::String& text) const
    {
        if (text.isEmpty()) return;
        g.setColour(config_.ink.withAlpha(config_.subtitleAlpha));
        g.setFont(juce::Font(config_.subtitleFontSize, juce::Font::plain));
        const juce::Rectangle<float> r(faceBounds.getCentreX() - 80.0f,
                                       faceBounds.getY() + faceBounds.getHeight()
                                           * config_.logoYFraction
                                       + config_.subtitleYOffset,
                                       160.0f, 14.0f);
        g.drawText(text, r, juce::Justification::centred);
    }

    void paintCornerTag(juce::Graphics& g,
                        juce::Rectangle<float> faceBounds,
                        const juce::String& text) const
    {
        if (text.isEmpty()) return;
        g.setColour(config_.ink.withAlpha(config_.cornerTagAlpha));
        g.setFont(juce::Font(config_.cornerTagFontSize, juce::Font::bold));
        const juce::Rectangle<float> r(faceBounds.getX() + config_.cornerTagInsetX,
                                       faceBounds.getY() + config_.cornerTagInsetY,
                                       40.0f, 14.0f);
        g.drawText(text, r, juce::Justification::left);
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuLogoRenderer)
};

} // namespace DAW
