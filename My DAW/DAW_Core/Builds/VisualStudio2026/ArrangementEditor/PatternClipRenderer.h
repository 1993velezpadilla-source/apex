// ===========================================================================
// PatternClipRenderer.h
// Draws a mini step grid inside a PatternClip in the arrangement view.
// Stateless utility — all rendering through static paint() method.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"

namespace ArrangementEditor
{

class PatternClipRenderer
{
public:
    /** Draw a mini step grid representing the given pattern lanes.
     *  Called by ClipRenderCore::paint() when patternId is non-empty.
     */
    static void paint (juce::Graphics& g,
                       juce::Rectangle<float> bounds,
                       const juce::Array<ArrangementClipModel::PatternLaneSnapshot>& lanes,
                       int totalSteps,
                       int stepsPerBeat,
                       int beatsPerBar,
                       juce::Colour clipColour)
    {
        if (lanes.isEmpty() || totalSteps <= 0) return;

        // Leave a small name bar at top
        const int nameBarH = 14;
        auto gridArea = bounds.reduced (2.0f, 0.0f);
        gridArea.removeFromTop ((float) nameBarH);

        if (gridArea.isEmpty()) return;

        const int numLanes = lanes.size();
        const float cellW = gridArea.getWidth() / (float) totalSteps;
        const float cellH = gridArea.getHeight() / (float) juce::jmax (1, numLanes);

        // Draw lane rows
        for (int row = 0; row < numLanes; ++row)
        {
            const auto& lane = lanes.getReference (row);
            const float y = gridArea.getY() + row * cellH;

            // Row background (alternating slightly)
            if (row % 2 == 1)
                g.setColour (juce::Colour (0xff222244).withAlpha (0.5f));
            else
                g.setColour (juce::Colour (0xff1a1a2e).withAlpha (0.5f));
            g.fillRect (gridArea.getX(), y, gridArea.getWidth(), cellH);

            const int laneStepCount = (lane.laneLength > 0) ? lane.laneLength : totalSteps;

            // Draw active steps
            for (int step = 0; step < juce::jmin (laneStepCount, totalSteps); ++step)
            {
                if (step >= lane.steps.size()) break;
                if (! lane.steps.getReference(step).active) continue;

                const float x = gridArea.getX() + step * cellW;

                // Step colour: clip colour tinted by velocity
                const float vel = lane.steps.getReference(step).velocity;
                auto stepColour = clipColour.brighter (0.2f);
                g.setColour (stepColour.withAlpha (0.7f + 0.3f * vel));
                g.fillRect (x + 0.5f, y + 0.5f,
                            cellW - 1.0f, cellH - 1.0f);
            }
        }

        // Draw beat/bar lines
        for (int step = 1; step < totalSteps; ++step)
        {
            const float x = gridArea.getX() + step * cellW;
            bool isDownbeat = (step % (stepsPerBeat * beatsPerBar)) == 0;
            bool isBeat     = (step % stepsPerBeat) == 0;

            if (isDownbeat)
            {
                g.setColour (juce::Colour (0xff667788).withAlpha (0.6f));
                g.drawLine (x, gridArea.getY(), x, gridArea.getBottom(), 1.0f);
            }
            else if (isBeat)
            {
                g.setColour (juce::Colour (0xff445566).withAlpha (0.4f));
                g.drawLine (x, gridArea.getY(), x, gridArea.getBottom(), 0.5f);
            }
        }

        // Draw lane separator lines
        for (int row = 1; row < numLanes; ++row)
        {
            const float y = gridArea.getY() + row * cellH;
            g.setColour (juce::Colour (0xff333344).withAlpha (0.5f));
            g.drawLine (gridArea.getX(), y, gridArea.getRight(), y, 0.5f);
        }
    }
};

} // namespace ArrangementEditor
