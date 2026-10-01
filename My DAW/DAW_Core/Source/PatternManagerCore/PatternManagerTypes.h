#pragma once
#include <JuceHeader.h>

namespace DAW {

struct PatternInfo {
    juce::String name;
    int index;
    int numLanes;
    int totalSteps;
};

} // namespace DAW
