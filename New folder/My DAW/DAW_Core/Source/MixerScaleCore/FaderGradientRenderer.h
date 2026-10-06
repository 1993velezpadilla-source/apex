#pragma once
#include <JuceHeader.h>
#include "FaderRangeCore.h"
#include "MixerScaleColorModel.h"

namespace DAW {

/**
 * FaderGradientRenderer — draws the colored fader track fill and tick marks.
 *
 * All positions come from FaderRangeCore so they automatically adapt when
 * the user flips the +6/+12 format toggle.
 */
namespace FaderGradientRenderer
{
    /**
     * Draws the colored fill bar inside a fader track.
     *
     * @param g          Graphics context
     * @param trackRect  The fader track rectangle (full range)
     * @param thumbPos   Normalized thumb position (0..1, bottom..top)
     * @param trackWidth Width of the thin fader groove
     */
    inline void drawFaderFill(juce::Graphics& g,
                              juce::Rectangle<float> trackRect,
                              float thumbPos,
                              float trackWidth = 4.f)
    {
        float h = trackRect.getHeight();
        float fillH = h * thumbPos;
        float fillTop = trackRect.getBottom() - fillH;
        float cx = trackRect.getCentreX();

        if (fillH < 1.f) return;

        auto fillRect = juce::Rectangle<float>(
            cx - trackWidth * 0.5f, fillTop, trackWidth, fillH);

        juce::ColourGradient grad(
            juce::Colours::white.withAlpha(0.92f), trackRect.getCentreX(), fillTop,
            juce::Colours::white.withAlpha(0.72f), trackRect.getCentreX(), trackRect.getBottom(),
            false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(fillRect, trackWidth * 0.4f);
    }

    /**
     * Draws dB tick marks using FaderRangeCore tick list.
     * Tick Y positions are derived from the core so they move on format change.
     *
     * @param g          Graphics context
     * @param scaleRect  Area beside the fader for tick marks
     * @param trackRect  The fader track rectangle (for Y alignment)
     * @param core       FaderRangeCore reference for format-aware positions
     * @param side       -1 = ticks on left, +1 = ticks on right
     */
    inline void drawFaderTicks(juce::Graphics& g,
                               juce::Rectangle<float> scaleRect,
                               juce::Rectangle<float> trackRect,
                               const FaderRangeCore& core,
                               int side = -1)
    {
        const auto ticks = core.getAllTicks();
        for (const auto& tick : ticks)
        {
            float pos = core.dbToNorm(tick.db);
            float y = trackRect.getBottom() - trackRect.getHeight() * pos;

            bool emphasized = (tick.db == 0.0f);
            float tickLen = emphasized ? scaleRect.getWidth() * 0.8f
                                       : scaleRect.getWidth() * 0.45f;

            g.setColour(juce::Colours::white.withAlpha(emphasized ? 0.92f : 0.55f));
            if (side < 0)
                g.fillRect(scaleRect.getRight() - tickLen, y - 0.5f, tickLen, 1.f);
            else
                g.fillRect(scaleRect.getX(), y - 0.5f, tickLen, 1.f);
        }
    }

    // Global-instance convenience overload
    inline void drawFaderTicks(juce::Graphics& g,
                               juce::Rectangle<float> scaleRect,
                               juce::Rectangle<float> trackRect,
                               int side = -1)
    {
        if (auto* c = FaderRangeCore::getGlobalInstance())
            drawFaderTicks(g, scaleRect, trackRect, *c, side);
    }
}

} // namespace DAW
