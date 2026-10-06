#pragma once

#include <JuceHeader.h>

namespace DAW {

class PianoRollSelectionRenderer
{
public:
    void paint(juce::Graphics& g, const juce::Rectangle<float>& marquee) const;
};

} // namespace DAW
