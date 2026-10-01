// ===========================================================================
// ClipFadeRenderCore.h
// Renders fade-in and fade-out gradient triangles inside clips.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include "ClipFadeCore.h"

namespace ArrangementEditor
{
    class ClipFadeRenderCore
    {
    public:
        // Sample the fade gain at a normalized position t in [0,1] for the
        // given curve value (-1..+1). t=0 = silent end, t=1 = full volume.
        static float sampleFadeCurve(float t, float curve)
        {
            t = juce::jlimit(0.0f, 1.0f, t);
            // curve = 0 → linear, curve > 0 → exponential (fast start),
            // curve < 0 → logarithmic (slow start).
            const float k = juce::jlimit(-0.95f, 0.95f, curve);
            if (std::abs(k) < 0.01f) return t;
            const float p = std::pow(2.0f, -3.0f * k); // exponent
            return std::pow(t, p);
        }

        // Build a curved fade ramp path across the fade rectangle.
        // The path starts at the silent edge (bottom) and rises to the
        // full-volume edge (top), tracing the fade curve.
        static juce::Path buildFadePath(const juce::Rectangle<float>& r,
                                        float curve,
                                        bool isFadeIn)
        {
            juce::Path path;
            const int steps = juce::jmax(8, (int)std::round(r.getWidth() * 0.5f));
            for (int i = 0; i <= steps; ++i)
            {
                const float t   = (float)i / (float)steps;
                const float g   = sampleFadeCurve(t, curve);
                const float xT  = isFadeIn ? t : (1.0f - t);
                const float x   = r.getX() + xT * r.getWidth();
                const float y   = r.getBottom() - g * r.getHeight();
                if (i == 0) path.startNewSubPath(x, y);
                else        path.lineTo(x, y);
            }
            return path;
        }

        // Draw fade-in: simple linear triangle wedge + straight ramp line.
        // clipBounds should already exclude the clip header strip so the
        // fade visual never overlaps the V/M buttons or clip name.
        static void drawFadeIn(juce::Graphics& g,
                                const juce::Rectangle<float>& clipBounds,
                                const ArrangementClipModel& clip,
                                double pixelsPerSecond,
                                juce::Colour /*baseColour*/)
        {
            if (clip.fadeInLength <= 0.0)
                return;

            float fadeWidthPx = (float)(clip.fadeInLength * pixelsPerSecond);
            fadeWidthPx = juce::jmin(fadeWidthPx, clipBounds.getWidth());
            if (fadeWidthPx <= 0.5f) return;

            const juce::Rectangle<float> fadeArea = clipBounds.withWidth(fadeWidthPx);

            // Dim wedge under the ramp = silenced part of the signal.
            juce::Path wedge;
            wedge.startNewSubPath(fadeArea.getX(),     fadeArea.getBottom());
            wedge.lineTo         (fadeArea.getRight(), fadeArea.getY());
            wedge.lineTo         (fadeArea.getX(),     fadeArea.getY());
            wedge.closeSubPath();
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.fillPath(wedge);

            // Straight white ramp line from bottom-left to top-right.
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawLine(fadeArea.getX(),     fadeArea.getBottom(),
                       fadeArea.getRight(), fadeArea.getY(), 1.6f);
        }

        // Draw fade-out: simple linear triangle wedge + straight ramp line.
        static void drawFadeOut(juce::Graphics& g,
                                 const juce::Rectangle<float>& clipBounds,
                                 const ArrangementClipModel& clip,
                                 double pixelsPerSecond,
                                 juce::Colour /*baseColour*/)
        {
            if (clip.fadeOutLength <= 0.0)
                return;

            float fadeWidthPx = (float)(clip.fadeOutLength * pixelsPerSecond);
            fadeWidthPx = juce::jmin(fadeWidthPx, clipBounds.getWidth());
            if (fadeWidthPx <= 0.5f) return;

            const juce::Rectangle<float> fadeArea = clipBounds.withLeft(clipBounds.getRight() - fadeWidthPx);

            juce::Path wedge;
            wedge.startNewSubPath(fadeArea.getRight(), fadeArea.getBottom());
            wedge.lineTo         (fadeArea.getX(),     fadeArea.getY());
            wedge.lineTo         (fadeArea.getRight(), fadeArea.getY());
            wedge.closeSubPath();
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.fillPath(wedge);

            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawLine(fadeArea.getX(),     fadeArea.getY(),
                       fadeArea.getRight(), fadeArea.getBottom(), 1.6f);
        }
    };

} // namespace ArrangementEditor
