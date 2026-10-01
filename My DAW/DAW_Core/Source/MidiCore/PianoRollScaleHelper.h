#pragma once

#include <JuceHeader.h>

namespace DAW {

class PianoRollScaleHelper
{
public:
    static bool isNoteInScale(int midiNote, const juce::String& root, const juce::String& scaleName);
    static int snapNoteToScale(int midiNote, const juce::String& root, const juce::String& scaleName);
    static juce::String getDisplayNoteName(int midiNote, const juce::String& root);

private:
    static int getRootPitchClass(const juce::String& root);
    static juce::Array<int> getScaleIntervals(const juce::String& scaleName);
    static bool prefersFlats(const juce::String& root);
};

} // namespace DAW
