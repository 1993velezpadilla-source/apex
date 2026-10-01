#pragma once

#include <JuceHeader.h>

namespace DAW {

class PianoRollRulerComponent : public juce::Component
{
public:
    PianoRollRulerComponent() = default;
    void setTicksPerQuarterNote(int ppq)            { ticksPerQuarterNote_ = juce::jmax(1, ppq); repaint(); }
    void setTicksPerGridDivision(juce::int64 ticks) { ticksPerGridDivision_ = juce::jmax<juce::int64>(1, ticks); repaint(); }
    void setPixelsPerTick(float pixels)             { pixelsPerTick_ = juce::jmax(0.0001f, pixels); repaint(); }
    void setScrollOffset(float x)                   { scrollX_ = x; repaint(); }

    void paint(juce::Graphics& g) override;

private:
    int ticksPerQuarterNote_ = 960;
    juce::int64 ticksPerGridDivision_ = 240;
    float pixelsPerTick_ = 0.1f;
    float scrollX_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollRulerComponent)
};

} // namespace DAW
