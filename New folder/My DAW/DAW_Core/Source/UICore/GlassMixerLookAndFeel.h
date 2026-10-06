#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * GlassMixerLookAndFeel — dark glass / champagne-gold aesthetic.
 *
 * Applied globally to MixerWindow.  Only covers JUCE standard controls
 * (labels, scroll bars, etc.).  PanKnob and SmallButton are custom
 * juce::Component subclasses with their own paint() — they are governed
 * by useLegacyMixerSkin() inside MixerPanel.h.
 */
class GlassMixerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlassMixerLookAndFeel()
    {
        // Label text: warm champagne (MNTÑ gold)
        setColour(juce::Label::textColourId,          juce::Colour(0xFFD4AF37));
        // Scroll bar — gold thumb on dark rail
        setColour(juce::ScrollBar::thumbColourId,     juce::Colour(0xFFD4AF37));
        setColour(juce::ScrollBar::trackColourId,     juce::Colour(0xFF1A1C22));
        // Tooltip
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xF0101218));
        setColour(juce::TooltipWindow::textColourId,       juce::Colour(0xFFD4AF37));
        setColour(juce::TooltipWindow::outlineColourId,    juce::Colour(0xFF3A3D48));
    }

    // Minimal scroll bar: thin dark pill with gold thumb
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar,
                       int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override
    {
        const float r  = isScrollbarVertical ? (float)width  * 0.45f
                                             : (float)height * 0.45f;
        juce::Rectangle<float> track((float)x, (float)y, (float)width, (float)height);

        // Deep obsidian rail
        g.setColour(juce::Colour(0xFF080A10).withAlpha(0.90f));
        g.fillRoundedRectangle(track, r);
        // Subtle inner edge catch-light
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawLine((float)x + 0.8f, (float)y, (float)x + 0.8f, (float)(y + height), 0.8f);

        juce::Rectangle<float> thumb;
        if (isScrollbarVertical)
            thumb = { (float)x + 1.f, (float)(y + thumbStartPosition) + 1.f,
                      (float)width - 2.f, (float)thumbSize - 2.f };
        else
            thumb = { (float)(x + thumbStartPosition) + 1.f, (float)y + 1.f,
                      (float)thumbSize - 2.f, (float)height - 2.f };

        bool hot = isMouseOver || isMouseDown;
        // Thumb body gradient
        juce::ColourGradient tg(
            hot ? juce::Colour(0xFFD4AF37).withAlpha(0.90f) : juce::Colour(0xFF4A4E5E),
            thumb.getCentreX(), thumb.getY(),
            hot ? juce::Colour(0xFF8B6914).withAlpha(0.85f) : juce::Colour(0xFF25283A),
            thumb.getCentreX(), thumb.getBottom(), false);
        g.setGradientFill(tg);
        g.fillRoundedRectangle(thumb, r - 1.f);
        // Thumb rim
        g.setColour(hot ? juce::Colours::white.withAlpha(0.45f)
                        : juce::Colours::white.withAlpha(0.14f));
        g.drawRoundedRectangle(thumb, r - 1.f, 0.8f);
    }

    void drawCornerResizer(juce::Graphics& g, int w, int h, bool /*isMouseOver*/, bool /*isMouseDragging*/) override
    {
        g.fillAll(juce::Colour(0xFF080A10));

        const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float)w, (float)h).reduced(2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawLine(area.getRight() - 7.0f, area.getBottom(), area.getRight(), area.getBottom() - 7.0f, 1.0f);
        g.drawLine(area.getRight() - 3.5f, area.getBottom(), area.getRight(), area.getBottom() - 3.5f, 1.0f);
    }
};

} // namespace DAW
