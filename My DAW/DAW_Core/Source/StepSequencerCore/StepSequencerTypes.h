#pragma once
#include <JuceHeader.h>
#include <cstdint>

namespace DAW {

using StablePatternId = juce::Uuid;
using StableLaneId    = juce::Uuid;
using StableEventId   = juce::Uuid;

struct StepEvent {
    StableEventId id;
    bool     active          = false;
    float    velocity        = 1.0f;
    int8_t   pitchOffset     = 0;
    float    pan             = 0.0f;
    int8_t   timingShift     = 0;
    float    probability     = 1.0f;
    int32_t  duration        = 0;
    int      ratchetCount    = 1;
    int      ratchetSpacing  = 0;
    bool     flamEnabled     = false;
    int      flamOffsetTicks = 0;
    bool     tieToNext       = false;

    enum class Condition : uint8_t {
        Every = 0,
        NotFirst, NotSecond, NotThird,
        FirstOf,
        Fill, NotFill,
        PrevActive, PrevInactive,
        First, Last,
        Random4, Random8, Random16
    };
    Condition condition = Condition::Every;

    int      microtimingTicks = 0;
    bool     swingBypass      = false;
};

struct ChannelData {
    juce::String name            = "Channel";
    float    volume          = 1.0f;
    float    pan             = 0.0f;
    bool     muted           = false;
    bool     soloed          = false;
    int      mixerTrackIndex = -1;
    float    swingAmount     = 0.0f;
    int      midiNote        = 60;
    int      midiChannel     = 0;
    juce::Colour color       = juce::Colour(0xff4488ff);
    juce::Array<StepEvent> steps;

    enum class SourceType { None, Sampler, VSTi };
    SourceType sourceType    = SourceType::None;
    int      vstInstanceId   = -1;
    juce::String sampleFilePath;
};

struct LaneData {
    StableLaneId id;
    juce::String name       = "Lane";
    int      stepsPerBeat   = 4;
    int      beatsPerBar    = 4;
    int      barsPerLane    = 1;
    float    swingAmount    = 0.0f;
    int      midiNote       = 60;
    int      midiChannel    = 0;
    float    volume         = 1.0f;
    float    pan            = 0.0f;
    bool     muted          = false;
    bool     soloed         = false;
    juce::Array<StepEvent> steps;

    int      laneLength     = 0;
    int      mixerTrackIndex = -1;
    int      vstInstanceId  = -1;
    juce::String sampleFilePath;

    enum class SourceType { None, Sampler, VSTi };
    SourceType sourceType   = SourceType::None;

    int getTotalSteps() const {
        if (laneLength > 0)
            return laneLength;
        return stepsPerBeat * beatsPerBar * barsPerLane;
    }
};

struct PatternData {
    StablePatternId id;
    juce::String name            = "Pattern 1";
    int     stepsPerBeat    = 4;
    int     beatsPerBar     = 4;
    int     barsPerPattern  = 1;
    float   globalSwing     = 0.0f;
    juce::Array<LaneData> lanes;

    int getTotalSteps() const {
        return stepsPerBeat * beatsPerBar * barsPerPattern;
    }
};

struct GrooveTemplate {
    StablePatternId id;
    juce::String name = "Default";
    float swingAmount = 0.0f;
    float shuffleAmount = 0.0f;
    int microTimingPreset = 0;
};

} // namespace DAW
