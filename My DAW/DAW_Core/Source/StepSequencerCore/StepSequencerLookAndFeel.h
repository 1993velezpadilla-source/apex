#pragma once
#include <JuceHeader.h>
#include "../UICore/CursorThemeCore.h"

namespace DAW {

class StepSequencerLookAndFeel : public ApexCursorLookAndFeel {
public:
    StepSequencerLookAndFeel();

    juce::Colour stepOnColour          = juce::Colour(0xff4488ff);
    juce::Colour stepOffColour         = juce::Colour(0xff333344);
    juce::Colour stepOnBeat1Colour     = juce::Colour(0xff5599ff);
    juce::Colour stepOffBeat1Colour    = juce::Colour(0xff444455);
    juce::Colour stepMutedColour       = juce::Colour(0xff666666);
    juce::Colour beatDividerColour     = juce::Colour(0xff555566);
    juce::Colour downbeatAccentColour  = juce::Colour(0xff88aaff);
    juce::Colour backgroundColour      = juce::Colour(0xff1a1a2e);
    juce::Colour channelStripBgColour  = juce::Colour(0xff222244);
    juce::Colour headerColour          = juce::Colour(0xff111133);
    juce::Colour barLineColour         = juce::Colour(0xff667788);
    juce::Colour beatLineColour        = juce::Colour(0xff445566);
    juce::Colour stepNumberColour      = juce::Colour(0xff8899aa);
    juce::Colour ratchetIndicatorColour = juce::Colour(0xffffaa00);
    juce::Colour flamIndicatorColour   = juce::Colour(0xffff6688);
    juce::Colour tieIndicatorColour    = juce::Colour(0xff66ccff);
    juce::Colour conditionIndicatorColour = juce::Colour(0xffaa88ff);

    void drawStepButton(juce::Graphics& g, juce::Rectangle<float> bounds,
                        bool isOn, bool isBeat1, bool isMuted, bool isDownbeat,
                        float velocity = 1.0f);
};

} // namespace DAW
