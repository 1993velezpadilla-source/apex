#pragma once

#include <JuceHeader.h>
namespace DAW {

class PianoRollKeyboardComponent : public juce::Component
{
public:
    PianoRollKeyboardComponent() = default;
    void setKeyRowHeight(float height)           { keyRowHeight_ = juce::jmax(8.0f, height); repaint(); }
    void setFirstVisibleMidiNote(int midiNote)   { firstVisibleMidiNote_ = juce::jlimit(0, 127, midiNote); repaint(); }
    void setScale(const juce::String& root, const juce::String& name) { scaleRoot_ = root; scaleName_ = name; repaint(); }

    void paint(juce::Graphics& g) override;

private:
    static bool isBlackKey(int midiNote);

    float keyRowHeight_ = 20.0f;
    int firstVisibleMidiNote_ = 127;
    juce::String scaleRoot_ = "C";
    juce::String scaleName_ = "Major";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollKeyboardComponent)
};

} // namespace DAW
