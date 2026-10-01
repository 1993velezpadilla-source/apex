#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "CursorThemeCore.h"

namespace DAW {

/**
 * GlassMixerLookAndFeel — APEX signal-core identity.
 *
 * Applied globally to MixerWindow.  Only covers JUCE standard controls
 * (labels, scroll bars, etc.).  PanKnob and SmallButton are custom
 * juce::Component subclasses with their own paint() — they are governed
 * by useLegacyMixerSkin() inside MixerPanel.h.
 *
 * Palette contract: magenta = creative energy / selection, violet =
 * transformation, cyan/blue = signal precision. No champagne, no gold.
 */
class GlassMixerLookAndFeel : public ApexCursorLookAndFeel
{
public:
    GlassMixerLookAndFeel()
    {
        auto& a = Theme::getInstance().apex;

        // Label text: APEX primary text
        setColour(juce::Label::textColourId,          a.color.textPrimary);
        // Scroll bar — magenta thumb on deep rail
        setColour(juce::ScrollBar::thumbColourId,     a.color.magenta);
        setColour(juce::ScrollBar::trackColourId,     a.color.panelA);
        // Tooltip
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xF00A0D15));
        setColour(juce::TooltipWindow::textColourId,       a.color.textPrimary);
        setColour(juce::TooltipWindow::outlineColourId,    a.color.borderSoftB);
    }

    // Minimal scroll bar: thin dark pill with magenta thumb
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar,
                       int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override
    {
        auto& a = Theme::getInstance().apex;
        const float r  = isScrollbarVertical ? (float)width  * 0.45f
                                             : (float)height * 0.45f;
        juce::Rectangle<float> track((float)x, (float)y, (float)width, (float)height);

        // Deep space rail
        g.setColour(a.color.deepestB.withAlpha(0.92f));
        g.fillRoundedRectangle(track, r);
        // Subtle inner edge catch-light
        g.setColour(a.color.textPrimary.withAlpha(0.06f));
        g.drawLine((float)x + 0.8f, (float)y, (float)x + 0.8f, (float)(y + height), 0.8f);

        juce::Rectangle<float> thumb;
        if (isScrollbarVertical)
            thumb = { (float)x + 1.f, (float)(y + thumbStartPosition) + 1.f,
                      (float)width - 2.f, (float)thumbSize - 2.f };
        else
            thumb = { (float)(x + thumbStartPosition) + 1.f, (float)y + 1.f,
                      (float)thumbSize - 2.f, (float)height - 2.f };

        bool hot = isMouseOver || isMouseDown;
        // Thumb body gradient — magenta energy when engaged, slate otherwise
        juce::ColourGradient tg(
            hot ? a.color.magentaBright.withAlpha(0.90f) : a.color.borderSoftB,
            thumb.getCentreX(), thumb.getY(),
            hot ? a.color.magentaDeep.withAlpha(0.85f)   : a.color.panelC,
            thumb.getCentreX(), thumb.getBottom(), false);
        g.setGradientFill(tg);
        g.fillRoundedRectangle(thumb, r - 1.f);
        // Thumb rim
        g.setColour(hot ? a.color.textPrimary.withAlpha(0.45f)
                        : a.color.textPrimary.withAlpha(0.14f));
        g.drawRoundedRectangle(thumb, r - 1.f, 0.8f);
    }

    void drawCornerResizer(juce::Graphics& g, int w, int h, bool /*isMouseOver*/, bool /*isMouseDragging*/) override
    {
        auto& a = Theme::getInstance().apex;
        g.fillAll(a.color.deepestB);

        const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float)w, (float)h).reduced(2.0f);
        g.setColour(a.color.textPrimary.withAlpha(0.18f));
        g.drawLine(area.getRight() - 7.0f, area.getBottom(), area.getRight(), area.getBottom() - 7.0f, 1.0f);
        g.drawLine(area.getRight() - 3.5f, area.getBottom(), area.getRight(), area.getBottom() - 3.5f, 1.0f);
    }
};

} // namespace DAW
