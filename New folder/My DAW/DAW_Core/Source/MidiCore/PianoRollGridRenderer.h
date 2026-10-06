#pragma once

#include <JuceHeader.h>

namespace DAW {

class PianoRollGridRenderer
{
public:
    void setKeyRowHeight(float height)              { keyRowHeight_ = juce::jmax(8.0f, height); }
    void setTicksPerQuarterNote(int ppq)            { ticksPerQuarterNote_ = juce::jmax(1, ppq); }
    void setTicksPerGridDivision(juce::int64 ticks) { ticksPerGridDivision_ = juce::jmax<juce::int64>(1, ticks); }
    void setPixelsPerTick(float pixels)             { pixelsPerTick_ = juce::jmax(0.0001f, pixels); }
    void setScrollOffset(float x, int firstNote)    { scrollX_ = x; firstVisibleNote_ = juce::jlimit(0, 127, firstNote); }
    void setScale(const juce::String& root, const juce::String& name) { scaleRoot_ = root; scaleName_ = name; }

    void paint(juce::Graphics& g, juce::Rectangle<int> bounds) const;

private:
    float keyRowHeight_ = 20.0f;
    int ticksPerQuarterNote_ = 960;
    juce::int64 ticksPerGridDivision_ = 240;
    float pixelsPerTick_ = 0.1f;
    float scrollX_ = 0.0f;
    int firstVisibleNote_ = 127;
    juce::String scaleRoot_ = "C";
    juce::String scaleName_ = "Major";
};

} // namespace DAW
