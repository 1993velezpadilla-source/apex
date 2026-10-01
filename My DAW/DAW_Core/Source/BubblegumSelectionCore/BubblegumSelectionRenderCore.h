#pragma once
#include "BubblegumSelectionGeometryCore.h"
#include "BubblegumSelectionBubbleCore.h"

namespace DAW::BubblegumSelection
{
    class BubblegumSelectionRenderCore
    {
    public:
        static void paint(juce::Graphics& g,
                          const BubblegumSelectionGeometry& geometry,
                          const BubblegumSelectionSettings& settings,
                          const BubblegumSelectionBubbleCore& bubbleCore,
                          float alpha,
                          float timeSeconds)
        {
            if (alpha <= 0.0f || geometry.outerBounds.isEmpty())
                return;

            juce::Graphics::ScopedSaveState alphaState(g);
            g.setOpacity(alpha);

            if (settings.quality == BubblegumSelectionSettings::Quality::Classic)
            {
                g.setColour(juce::Colour(0xFFFF0090).withAlpha(0.90f));
                g.strokePath(geometry.outerPath, juce::PathStrokeType(1.5f));
                return;
            }

            if (settings.showGlow && settings.quality == BubblegumSelectionSettings::Quality::BubblegumFull)
                paintGlow(g, geometry, settings);

            juce::ColourGradient slime(
                juce::Colour(0xFFFF40B0).withAlpha(0.78f), geometry.outerBounds.getCentreX(), geometry.outerBounds.getY(),
                juce::Colour(0xFF990055).withAlpha(0.68f), geometry.outerBounds.getCentreX(), geometry.outerBounds.getBottom(), false);
            slime.addColour(0.50, juce::Colour(0xFFFF0090).withAlpha(0.74f));
            g.setGradientFill(slime);
            g.fillPath(geometry.framePath);

            // Continuous frame band. The even-odd ring fill can pinch to
            // sub-pixel width wherever the animated outer/inner wave phases
            // oppose, which makes the frame read as broken segments
            // ("line space line space"). A solid gradient stroke along the
            // outer path guarantees the frame is always a complete box.
            g.setGradientFill(slime);
            g.strokePath(geometry.outerPath, juce::PathStrokeType(
                juce::jmax(2.0f, settings.frameThickness * 1.6f),
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(juce::Colour(0xFFFF55C0).withAlpha(0.75f));
            g.strokePath(geometry.outerPath, juce::PathStrokeType(0.85f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (geometry.hasInner)
            {
                g.setColour(juce::Colour(0xFFFF80D0).withAlpha(0.32f));
                g.strokePath(geometry.innerPath, juce::PathStrokeType(0.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            if (settings.showHighlight)
                paintHighlight(g, geometry, settings, timeSeconds);

            if (settings.showBubbles && settings.quality == BubblegumSelectionSettings::Quality::BubblegumFull)
                paintBubbles(g, geometry, bubbleCore, alpha);
        }

    private:
        static void paintGlow(juce::Graphics& g, const BubblegumSelectionGeometry& geometry,
                              const BubblegumSelectionSettings& settings)
        {
            const auto glow = juce::Colour(0xFFFF0090);
            for (int i = 4; i >= 1; --i)
            {
                const float width = settings.glowBlur * (float)i / 5.0f;
                const float a = 0.028f * (float)i;
                g.setColour(glow.withAlpha(a));
                g.strokePath(geometry.outerPath, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        static void paintHighlight(juce::Graphics& g, const BubblegumSelectionGeometry& geometry,
                                   const BubblegumSelectionSettings& settings, float timeSeconds)
        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(geometry.framePath);
            auto shine = BubblegumSelectionGeometryCore::buildHighlightPath(geometry.outerBounds, timeSeconds, settings);
            g.setColour(juce::Colours::white.withAlpha(0.42f));
            g.strokePath(shine, juce::PathStrokeType(0.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(juce::Colour(0xFFFF55C0).withAlpha(0.16f));
            g.strokePath(shine, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        static void paintBubbles(juce::Graphics& g, const BubblegumSelectionGeometry& geometry,
                                 const BubblegumSelectionBubbleCore& bubbleCore, float alpha)
        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(geometry.framePath);

            for (const auto& bubble : bubbleCore.getBubbles())
            {
                const float x = geometry.outerBounds.getX() + bubble.relativeX * geometry.outerBounds.getWidth();
                const float y = geometry.outerBounds.getY() + bubble.relativeY * geometry.outerBounds.getHeight();
                const float r = bubble.size * 0.5f;

                // Soft dark bubble body
                g.setColour(juce::Colours::black.withAlpha(bubble.alpha * 0.18f * alpha));
                g.fillEllipse(x - r, y - r, bubble.size, bubble.size);
                // Magenta specular — faint, elegant inner highlight
                g.setColour(juce::Colour(0xFFFF55C0).withAlpha(bubble.alpha * 0.30f * alpha));
                g.fillEllipse(x - r * 0.35f, y - r * 0.45f, juce::jmax(1.0f, r * 0.55f), juce::jmax(1.0f, r * 0.55f));
            }
        }
    };
}
