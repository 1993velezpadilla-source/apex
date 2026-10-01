#include "StepSequencerLookAndFeel.h"

namespace DAW {

StepSequencerLookAndFeel::StepSequencerLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, backgroundColour);
}

void StepSequencerLookAndFeel::drawStepButton(
    juce::Graphics& g, juce::Rectangle<float> bounds,
    bool isOn, bool isBeat1, bool isMuted, bool isDownbeat,
    float velocity)
{
    juce::Colour fillColour;
    if (isMuted)
        fillColour = stepMutedColour;
    else if (isOn)
    {
        juce::Colour baseColour = isBeat1 ? stepOnBeat1Colour : stepOnColour;
        float brightness = 0.5f + velocity * 0.5f;
        fillColour = baseColour.withBrightness(baseColour.getBrightness() * brightness);
    }
    else
        fillColour = isBeat1 ? stepOffBeat1Colour : stepOffColour;

    g.setColour(fillColour);
    g.fillRoundedRectangle(bounds, 3.0f);

    if (isDownbeat) {
        g.setColour(downbeatAccentColour);
        g.drawLine(bounds.getX(), bounds.getY(),
                   bounds.getX(), bounds.getBottom(), 2.0f);
    }

    if (isOn && !isMuted) {
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.0f);
    }
}

} // namespace DAW
