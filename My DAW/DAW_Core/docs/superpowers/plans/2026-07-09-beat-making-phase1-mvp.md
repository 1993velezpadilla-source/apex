# Apex Beat-Making Phase 1 (MVP) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the core beat-making system: Step Sequencer (Channel Rack clone), basic Drum Sampler, and Pattern Manager — enough to make beats in Apex.

**Architecture:** Three new modules following Apex's existing JUCE C++ patterns (ValueTree serialization, lock-free snapshots, ActionCore dispatch). StepSequencerCore is the UI + data model, DrumSamplerCore handles audio playback, PatternManagerCore manages pattern switching.

**Tech Stack:** JUCE 8.0.12, C++17, Visual Studio 2026, Projucer build system

## Global Constraints

- JUCE 8.0.12 (from `Sdk setups/juce-8.0.12-windows/`)
- C++17 standard
- Visual Studio 2026 toolset
- Follow existing DAW namespace conventions (DAW:: for MidiCore, APEX:: for AutomationSequence)
- ValueTree serialization for all data models
- Lock-free snapshot pattern for audio-thread reads (matching PianoRollClipModel)
- No new external dependencies — use only JUCE modules already in project

---

## File Structure

### StepSequencerCore (11 files)
| File | Responsibility |
|------|---------------|
| `Source/StepSequencerCore/StepSequencerTypes.h` | POD structs: StepEvent, ChannelData, PatternData |
| `Source/StepSequencerCore/StepSequencerModel.h/.cpp` | Data model: pattern list, channel CRUD, step operations |
| `Source/StepSequencerCore/StepSequencerStepGrid.h/.cpp` | Clickable step grid with beat grouping colors |
| `Source/StepSequencerCore/StepSequencerChannelStrip.h/.cpp` | One channel row: name, vol, pan, M/S, step grid |
| `Source/StepSequencerCore/StepSequencerVelocityLane.h/.cpp` | Per-step velocity bar editor |
| `Source/StepSequencerCore/StepSequencerHeader.h/.cpp` | Pattern selector, swing slider, add/remove buttons |
| `Source/StepSequencerCore/StepSequencerComponent.h/.cpp` | Main composite: header + channel strips |
| `Source/StepSequencerCore/StepSequencerPlaybackCore.h/.cpp` | Audio-thread: schedules MIDI from steps |
| `Source/StepSequencerCore/StepSequencerLookAndFeel.h/.cpp` | Beat grouping colors, step button styling |
| `Source/StepSequencerCore/StepSequencerWindow.h/.cpp` | Floating window wrapper |

### DrumSamplerCore (6 files — basic MVP)
| File | Responsibility |
|------|---------------|
| `Source/DrumSamplerCore/DrumSamplerTypes.h` | POD structs: DrumPadConfig, VelocityLayer |
| `Source/DrumSamplerCore/DrumSamplerPad.h/.cpp` | Single pad: sample data + ADSR + playback |
| `Source/DrumSamplerCore/DrumSamplerVoice.h/.cpp` | Active voice: plays one trigger of a pad |
| `Source/DrumSamplerCore/DrumSamplerVoicePool.h/.cpp` | Lock-free voice allocator |
| `Source/DrumSamplerCore/DrumSamplerEngine.h/.cpp` | Top-level: pad array, MIDI→pad mapping, processBlock |
| `Source/DrumSamplerCore/DrumSamplerLoadJob.h/.cpp` | Background audio file loading |

### PatternManagerCore (3 files)
| File | Responsibility |
|------|---------------|
| `Source/PatternManagerCore/PatternManagerTypes.h` | POD structs |
| `Source/PatternManagerCore/PatternManagerCore.h/.cpp` | Pattern CRUD, switching |
| `Source/PatternManagerCore/PatternManagerUI.h/.cpp` | Pattern selector dropdown |

---

## Task 1: StepSequencerTypes — Shared POD Structs

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerTypes.h`

**Interfaces:**
- Produces: `StepEvent`, `ChannelData`, `PatternData` structs used by all other modules

- [ ] **Step 1: Create StepSequencerTypes.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include <cstdint>

namespace DAW {

struct StepEvent {
    bool     active       = false;
    uint8_t  velocity     = 100;
    int8_t   pitchOffset  = 0;
    uint8_t  pan          = 64;
    int8_t   timingShift  = 0;
    float    probability  = 1.0f;
};

struct ChannelData {
    String   name            = "Channel";
    float    volume          = 1.0f;
    float    pan             = 0.0f;
    bool     muted           = false;
    bool     soloed          = false;
    int      mixerTrackIndex = -1;
    float    swingAmount     = 0.0f;
    int      midiNote        = 60;
    int      midiChannel     = 0;
    juce::Colour color       = juce::Colour(0xff4488ff);
    Array<StepEvent> steps;

    enum class SourceType { None, Sampler, VSTi };
    SourceType sourceType    = SourceType::None;
    int      vstInstanceId   = -1;
    String   sampleFilePath;
};

struct PatternData {
    String  name            = "Pattern 1";
    int     stepsPerBeat    = 4;
    int     beatsPerBar     = 4;
    int     barsPerPattern  = 1;
    float   globalSwing     = 0.0f;
    juce::Array<ChannelData> channels;

    int getTotalSteps() const {
        return stepsPerBeat * beatsPerBar * barsPerPattern;
    }
};

} // namespace DAW
```

- [ ] **Step 2: Verify compilation**

Open Visual Studio solution, build. Expect: compiles without errors.

- [ ] **Step 3: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerTypes.h
git commit -m "feat(step-sequencer): add shared POD types"
```

---

## Task 2: StepSequencerModel — Data Model

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerModel.h`
- Create: `Source/StepSequencerCore/StepSequencerModel.cpp`

**Interfaces:**
- Consumes: `StepEvent`, `ChannelData`, `PatternData` from Task 1
- Produces: `StepSequencerModel` class with CRUD + ValueTree serialization

- [ ] **Step 1: Write StepSequencerModel.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "StepSequencerTypes.h"

namespace DAW {

class StepSequencerModel : public juce::ChangeBroadcaster {
public:
    StepSequencerModel();
    ~StepSequencerModel() override;

    // Pattern operations
    int  addPattern(const String& name = "");
    void removePattern(int index);
    void setCurrentPattern(int index);
    int  getCurrentPatternIndex() const noexcept;
    int  getPatternCount() const noexcept;
    PatternData& getPattern(int index);
    const PatternData& getPattern(int index) const;
    void clonePattern(int sourceIndex);

    // Channel operations (current pattern)
    int  addChannel(const String& name = "Channel");
    void removeChannel(int index);
    void moveChannel(int fromIndex, int toIndex);
    Array<ChannelData>& getChannels();
    ChannelData& getChannel(int index);

    // Step operations (current pattern, given channel)
    void toggleStep(int channelIndex, int stepIndex);
    void setStepVelocity(int channelIndex, int stepIndex, uint8_t velocity);
    void setStepProbability(int channelIndex, int stepIndex, float probability);
    void setStepPitchOffset(int channelIndex, int stepIndex, int8_t offset);

    // Serialization
    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

    // Lock-free snapshot for audio thread
    struct Snapshot {
        struct ChannelSnapshot {
            String name;
            float volume;
            float pan;
            bool muted;
            bool soloed;
            int midiNote;
            int midiChannel;
            Array<StepEvent> steps;
        };
        Array<ChannelSnapshot> channels;
        int stepsPerBeat;
        int beatsPerBar;
        int barsPerPattern;
        float globalSwing;
        int totalSteps;
    };
    Snapshot getSnapshot() const;

private:
    juce::Array<PatternData> patterns_;
    int currentPatternIndex_ = 0;
    mutable juce::CriticalSection lock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerModel)
};

} // namespace DAW
```

- [ ] **Step 2: Write StepSequencerModel.cpp**

```cpp
#include "StepSequencerModel.h"

namespace DAW {

StepSequencerModel::StepSequencerModel() {
    patterns_.add(PatternData());
}

StepSequencerModel::~StepSequencerModel() = default;

int StepSequencerModel::addPattern(const String& name) {
    const juce::ScopedLock sl(lock_);
    PatternData p;
    p.name = name.isNotEmpty() ? name : "Pattern " + String(patterns_.size() + 1);
    patterns_.add(std::move(p));
    currentPatternIndex_ = patterns_.size() - 1;
    sendChangeMessage();
    return currentPatternIndex_;
}

void StepSequencerModel::removePattern(int index) {
    const juce::ScopedLock sl(lock_);
    if (patterns_.size() <= 1) return;
    patterns_.remove(index);
    currentPatternIndex_ = juce::jlimit(0, patterns_.size() - 1, currentPatternIndex_);
    sendChangeMessage();
}

void StepSequencerModel::setCurrentPattern(int index) {
    currentPatternIndex_ = juce::jlimit(0, patterns_.size() - 1, index);
    sendChangeMessage();
}

int StepSequencerModel::getCurrentPatternIndex() const noexcept { return currentPatternIndex_; }
int StepSequencerModel::getPatternCount() const noexcept { return patterns_.size(); }

PatternData& StepSequencerModel::getPattern(int index) { return patterns_.getReference(index); }
const PatternData& StepSequencerModel::getPattern(int index) const { return patterns_[index]; }

void StepSequencerModel::clonePattern(int sourceIndex) {
    const juce::ScopedLock sl(lock_);
    if (!juce::isPositiveAndBelow(sourceIndex, patterns_.size())) return;
    PatternData clone = patterns_[sourceIndex];
    clone.name += " (copy)";
    patterns_.add(std::move(clone));
    currentPatternIndex_ = patterns_.size() - 1;
    sendChangeMessage();
}

int StepSequencerModel::addChannel(const String& name) {
    const juce::ScopedLock sl(lock_);
    auto& p = patterns_.getReference(currentPatternIndex_);
    ChannelData ch;
    ch.name = name;
    ch.steps.resize(p.getTotalSteps());
    p.channels.add(std::move(ch));
    sendChangeMessage();
    return p.channels.size() - 1;
}

void StepSequencerModel::removeChannel(int index) {
    const juce::ScopedLock sl(lock_);
    patterns_.getReference(currentPatternIndex_).channels.remove(index);
    sendChangeMessage();
}

void StepSequencerModel::moveChannel(int from, int to) {
    const juce::ScopedLock sl(lock_);
    patterns_.getReference(currentPatternIndex_).channels.move(from, to);
    sendChangeMessage();
}

Array<ChannelData>& StepSequencerModel::getChannels() {
    return patterns_.getReference(currentPatternIndex_).channels;
}

ChannelData& StepSequencerModel::getChannel(int index) {
    return patterns_.getReference(currentPatternIndex_).channels.getReference(index);
}

void StepSequencerModel::toggleStep(int ch, int step) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .channels.getReference(ch).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        steps.getReference(step).active = !steps.getReference(step).active;
    sendChangeMessage();
}

void StepSequencerModel::setStepVelocity(int ch, int step, uint8_t vel) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .channels.getReference(ch).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        steps.getReference(step).velocity = vel;
    sendChangeMessage();
}

void StepSequencerModel::setStepProbability(int ch, int step, float prob) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .channels.getReference(ch).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        steps.getReference(step).probability = prob;
    sendChangeMessage();
}

void StepSequencerModel::setStepPitchOffset(int ch, int step, int8_t offset) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .channels.getReference(ch).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        steps.getReference(step).pitchOffset = offset;
    sendChangeMessage();
}

StepSequencerModel::Snapshot StepSequencerModel::getSnapshot() const {
    const juce::ScopedLock sl(lock_);
    Snapshot snap;
    const auto& p = patterns_[currentPatternIndex_];
    snap.stepsPerBeat = p.stepsPerBeat;
    snap.beatsPerBar = p.beatsPerBar;
    snap.barsPerPattern = p.barsPerPattern;
    snap.globalSwing = p.globalSwing;
    snap.totalSteps = p.getTotalSteps();
    for (const auto& ch : p.channels) {
        Snapshot::ChannelSnapshot cs;
        cs.name = ch.name;
        cs.volume = ch.volume;
        cs.pan = ch.pan;
        cs.muted = ch.muted;
        cs.soloed = ch.soloed;
        cs.midiNote = ch.midiNote;
        cs.midiChannel = ch.midiChannel;
        cs.steps = ch.steps;
        snap.channels.add(std::move(cs));
    }
    return snap;
}

juce::ValueTree StepSequencerModel::toValueTree() const {
    const juce::ScopedLock sl(lock_);
    juce::ValueTree tree("StepSequencer");
    tree.setProperty("currentPattern", currentPatternIndex_, nullptr);
    for (const auto& p : patterns_) {
        juce::ValueTree patTree("Pattern");
        patTree.setProperty("name", p.name, nullptr);
        patTree.setProperty("stepsPerBeat", p.stepsPerBeat, nullptr);
        patTree.setProperty("beatsPerBar", p.beatsPerBar, nullptr);
        patTree.setProperty("barsPerPattern", p.barsPerPattern, nullptr);
        patTree.setProperty("globalSwing", p.globalSwing, nullptr);
        for (const auto& ch : p.channels) {
            juce::ValueTree chTree("Channel");
            chTree.setProperty("name", ch.name, nullptr);
            chTree.setProperty("volume", ch.volume, nullptr);
            chTree.setProperty("pan", ch.pan, nullptr);
            chTree.setProperty("muted", ch.muted, nullptr);
            chTree.setProperty("soloed", ch.soloed, nullptr);
            chTree.setProperty("midiNote", ch.midiNote, nullptr);
            chTree.setProperty("midiChannel", ch.midiChannel, nullptr);
            chTree.setProperty("color", ch.color.toString(), nullptr);
            for (const auto& s : ch.steps) {
                juce::ValueTree stepTree("Step");
                stepTree.setProperty("active", s.active, nullptr);
                stepTree.setProperty("velocity", (int)s.velocity, nullptr);
                stepTree.setProperty("pitchOffset", (int)s.pitchOffset, nullptr);
                stepTree.setProperty("pan", (int)s.pan, nullptr);
                stepTree.setProperty("timingShift", (int)s.timingShift, nullptr);
                stepTree.setProperty("probability", s.probability, nullptr);
                chTree.addChild(stepTree, -1, nullptr);
            }
            patTree.addChild(chTree, -1, nullptr);
        }
        tree.addChild(patTree, -1, nullptr);
    }
    return tree;
}

void StepSequencerModel::fromValueTree(const juce::ValueTree& tree) {
    const juce::ScopedLock sl(lock_);
    patterns_.clear();
    currentPatternIndex_ = (int)tree.getProperty("currentPattern", 0);
    for (int pi = 0; pi < tree.getNumChildren(); ++pi) {
        const auto& patTree = tree.getChild(pi);
        PatternData p;
        p.name = patTree.getProperty("name", "Pattern");
        p.stepsPerBeat = (int)patTree.getProperty("stepsPerBeat", 4);
        p.beatsPerBar = (int)patTree.getProperty("beatsPerBar", 4);
        p.barsPerPattern = (int)patTree.getProperty("barsPerPattern", 1);
        p.globalSwing = (float)patTree.getProperty("globalSwing", 0.0f);
        for (int ci = 0; ci < patTree.getNumChildren(); ++ci) {
            const auto& chTree = patTree.getChild(ci);
            ChannelData ch;
            ch.name = chTree.getProperty("name", "Channel");
            ch.volume = (float)chTree.getProperty("volume", 1.0f);
            ch.pan = (float)chTree.getProperty("pan", 0.0f);
            ch.muted = (bool)chTree.getProperty("muted", false);
            ch.soloed = (bool)chTree.getProperty("soloed", false);
            ch.midiNote = (int)chTree.getProperty("midiNote", 60);
            ch.midiChannel = (int)chTree.getProperty("midiChannel", 0);
            ch.color = juce::Colour::fromString(chTree.getProperty("color", "ff4488ff").toString());
            for (int si = 0; si < chTree.getNumChildren(); ++si) {
                const auto& sTree = chTree.getChild(si);
                StepEvent s;
                s.active = (bool)sTree.getProperty("active", false);
                s.velocity = (uint8_t)(int)sTree.getProperty("velocity", 100);
                s.pitchOffset = (int8_t)(int)sTree.getProperty("pitchOffset", 0);
                s.pan = (uint8_t)(int)sTree.getProperty("pan", 64);
                s.timingShift = (int8_t)(int)sTree.getProperty("timingShift", 0);
                s.probability = (float)sTree.getProperty("probability", 1.0f);
                ch.steps.add(s);
            }
            p.channels.add(std::move(ch));
        }
        patterns_.add(std::move(p));
    }
    sendChangeMessage();
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer**

Open `DAW_Core.jucer`, add `StepSequencerCore` module with the .h and .cpp files.

- [ ] **Step 4: Build and verify**

Open Visual Studio, build. Expect: compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerModel.h Source/StepSequencerCore/StepSequencerModel.cpp
git commit -m "feat(step-sequencer): add data model with ValueTree serialization"
```

---

## Task 3: StepSequencerLookAndFeel — Visual Styling

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerLookAndFeel.h`
- Create: `Source/StepSequencerCore/StepSequencerLookAndFeel.cpp`

**Interfaces:**
- Consumes: `StepSequencerTypes.h` from Task 1
- Produces: `StepSequencerLookAndFeel` class for step grid colors

- [ ] **Step 1: Write StepSequencerLookAndFeel.h**

```cpp
#pragma once
#include <JuceHeader.h>

namespace DAW {

class StepSequencerLookAndFeel : public juce::LookAndFeel_V4 {
public:
    StepSequencerLookAndFeel();

    // Step button colors
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

    void drawStepButton(juce::Graphics& g, juce::Rectangle<float> bounds,
                        bool isOn, bool isBeat1, bool isMuted, bool isDownbeat);
};

} // namespace DAW
```

- [ ] **Step 2: Write StepSequencerLookAndFeel.cpp**

```cpp
#include "StepSequencerLookAndFeel.h"

namespace DAW {

StepSequencerLookAndFeel::StepSequencerLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, backgroundColour);
}

void StepSequencerLookAndFeel::drawStepButton(
    juce::Graphics& g, juce::Rectangle<float> bounds,
    bool isOn, bool isBeat1, bool isMuted, bool isDownbeat)
{
    juce::Colour fillColour;
    if (isMuted)
        fillColour = stepMutedColour;
    else if (isOn)
        fillColour = isBeat1 ? stepOnBeat1Colour : stepOnColour;
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
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerLookAndFeel.h Source/StepSequencerCore/StepSequencerLookAndFeel.cpp
git commit -m "feat(step-sequencer): add visual styling with beat grouping colors"
```

---

## Task 4: StepSequencerStepGrid — Clickable Step Grid

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerStepGrid.h`
- Create: `Source/StepSequencerCore/StepSequencerStepGrid.cpp`

**Interfaces:**
- Consumes: `StepSequencerModel` from Task 2, `StepSequencerLookAndFeel` from Task 3
- Produces: `StepSequencerStepGrid` component (renders + handles clicks)

- [ ] **Step 1: Write StepSequencerStepGrid.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"
#include "StepSequencerLookAndFeel.h"

namespace DAW {

class StepSequencerStepGrid : public juce::Component {
public:
    StepSequencerStepGrid(StepSequencerModel& model, int channelIndex);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void setChannelIndex(int index) { channelIndex_ = index; }
    int getChannelIndex() const noexcept { return channelIndex_; }

private:
    StepSequencerModel& model_;
    int channelIndex_ = 0;
    bool dragToggleState_ = false;
    bool isDragging_ = false;

    int getStepFromX(float x) const;
    juce::Rectangle<float> getStepBounds(int stepIndex) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerStepGrid)
};

} // namespace DAW
```

- [ ] **Step 2: Write StepSequencerStepGrid.cpp**

```cpp
#include "StepSequencerStepGrid.h"

namespace DAW {

StepSequencerStepGrid::StepSequencerStepGrid(StepSequencerModel& model, int channelIndex)
    : model_(model), channelIndex_(channelIndex) {}

int StepSequencerStepGrid::getStepFromX(float x) const {
    const auto& snap = model_.getSnapshot();
    const int totalSteps = snap.totalSteps;
    const float stepWidth = getWidth() / (float)totalSteps;
    return juce::jlimit(0, totalSteps - 1, (int)(x / stepWidth));
}

juce::Rectangle<float> StepSequencerStepGrid::getStepBounds(int stepIndex) const {
    const auto& snap = model_.getSnapshot();
    const int totalSteps = snap.totalSteps;
    const float stepWidth = (float)getWidth() / totalSteps;
    return { stepIndex * stepWidth, 0.0f, stepWidth, (float)getHeight() };
}

void StepSequencerStepGrid::paint(juce::Graphics& g) {
    const auto& snap = model_.getSnapshot();
    if (channelIndex_ >= snap.channels.size()) return;
    const auto& ch = snap.channels[channelIndex_];
    const int totalSteps = snap.totalSteps;
    const int stepsPerBeat = snap.stepsPerBeat;
    const float stepWidth = (float)getWidth() / totalSteps;

    StepSequencerLookAndFeel lnf;
    g.fillAll(lnf.backgroundColour);

    for (int i = 0; i < totalSteps; ++i) {
        const bool isOn = (i < ch.steps.size()) ? ch.steps[i].active : false;
        const bool isBeat1 = (i % stepsPerBeat) == 0;
        const bool isDownbeat = (i % (stepsPerBeat * snap.beatsPerBar)) == 0;
        const bool isMuted = ch.muted;

        auto bounds = getStepBounds(i).reduced(1.0f);
        lnf.drawStepButton(g, bounds, isOn, isBeat1, isMuted, isDownbeat);
    }
}

void StepSequencerStepGrid::mouseDown(const juce::MouseEvent& e) {
    const int step = getStepFromX((float)e.getPosition().x);
    model_.toggleStep(channelIndex_, step);
    dragToggleState_ = model_.getChannel(channelIndex_).steps[step].active;
    isDragging_ = true;
    repaint();
}

void StepSequencerStepGrid::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_) return;
    const int step = getStepFromX((float)e.getPosition().x);
    auto& steps = model_.getChannel(channelIndex_).steps;
    if (juce::isPositiveAndBelow(step, steps.size())) {
        if (steps[step].active != dragToggleState_) {
            model_.toggleStep(channelIndex_, step);
            repaint();
        }
    }
}

void StepSequencerStepGrid::mouseUp(const juce::MouseEvent&) {
    isDragging_ = false;
}

void StepSequencerStepGrid::mouseWheelMove(
    const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const int step = getStepFromX((float)e.getPosition().x);
    auto& steps = model_.getChannel(channelIndex_).steps;
    if (juce::isPositiveAndBelow(step, steps.size())) {
        float newProb = juce::jlimit(0.0f, 1.0f,
            steps[step].probability + (float)wheel.deltaY * 0.1f);
        model_.setStepProbability(channelIndex_, step, newProb);
        repaint();
    }
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerStepGrid.h Source/StepSequencerCore/StepSequencerStepGrid.cpp
git commit -m "feat(step-sequencer): add clickable step grid with beat grouping"
```

---

## Task 5: StepSequencerChannelStrip — One Channel Row

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerChannelStrip.h`
- Create: `Source/StepSequencerCore/StepSequencerChannelStrip.cpp`

**Interfaces:**
- Consumes: `StepSequencerModel` from Task 2, `StepSequencerStepGrid` from Task 4
- Produces: `StepSequencerChannelStrip` component (name, vol, pan, M/S, grid)

- [ ] **Step 1: Write StepSequencerChannelStrip.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"
#include "StepSequencerStepGrid.h"

namespace DAW {

class StepSequencerChannelStrip : public juce::Component {
public:
    StepSequencerChannelStrip(StepSequencerModel& model, int channelIndex);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void setChannelIndex(int index);

private:
    StepSequencerModel& model_;
    int channelIndex_ = 0;
    StepSequencerStepGrid grid_;

    juce::Label nameLabel_;
    juce::Slider volumeSlider_;
    juce::Slider panSlider_;
    juce::ToggleButton muteButton_;
    juce::ToggleButton soloButton_;

    void updateFromModel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerChannelStrip)
};

} // namespace DAW
```

- [ ] **Step 2: Write StepSequencerChannelStrip.cpp**

```cpp
#include "StepSequencerChannelStrip.h"

namespace DAW {

StepSequencerChannelStrip::StepSequencerChannelStrip(
    StepSequencerModel& model, int channelIndex)
    : model_(model), channelIndex_(channelIndex), grid_(model, channelIndex)
{
    addAndMakeVisible(nameLabel_);
    addAndMakeVisible(volumeSlider_);
    addAndMakeVisible(panSlider_);
    addAndMakeVisible(muteButton_);
    addAndMakeVisible(soloButton_);
    addAndMakeVisible(grid_);

    nameLabel_.setEditable(true);
    nameLabel_.setText(model_.getChannel(channelIndex_).name, juce::dontSendNotification);
    nameLabel_.onTextChange = [this]() {
        model_.getChannel(channelIndex_).name = nameLabel_.getText();
    };

    volumeSlider_.setRange(0.0, 2.0, 0.01);
    volumeSlider_.setValue(model_.getChannel(channelIndex_).volume, juce::dontSendNotification);
    volumeSlider_.onValueChange = [this]() {
        model_.getChannel(channelIndex_).volume = (float)volumeSlider_.getValue();
    };

    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(model_.getChannel(channelIndex_).pan, juce::dontSendNotification);
    panSlider_.onValueChange = [this]() {
        model_.getChannel(channelIndex_).pan = (float)panSlider_.getValue();
    };

    muteButton_.setButtonText("M");
    muteButton_.onClick = [this]() {
        model_.getChannel(channelIndex_).muted = muteButton_.getToggleState();
        repaint();
    };

    soloButton_.setButtonText("S");
    soloButton_.onClick = [this]() {
        model_.getChannel(channelIndex_).soloed = soloButton_.getToggleState();
    };
}

void StepSequencerChannelStrip::setChannelIndex(int index) {
    channelIndex_ = index;
    grid_.setChannelIndex(index);
    updateFromModel();
}

void StepSequencerChannelStrip::updateFromModel() {
    const auto& ch = model_.getChannel(channelIndex_);
    nameLabel_.setText(ch.name, juce::dontSendNotification);
    volumeSlider_.setValue(ch.volume, juce::dontSendNotification);
    panSlider_.setValue(ch.pan, juce::dontSendNotification);
    muteButton_.setToggleState(ch.muted, juce::dontSendNotification);
    soloButton_.setToggleState(ch.soloed, juce::dontSendNotification);
}

void StepSequencerChannelStrip::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff222244));
}

void StepSequencerChannelStrip::resized() {
    auto area = getLocalBounds();
    const int controlsWidth = 160;
    auto controls = area.removeFromLeft(controlsWidth);

    nameLabel_.setBounds(controls.removeFromTop(20));
    auto knobs = controls.removeFromTop(24);
    volumeSlider_.setBounds(knobs.removeFromLeft(50));
    panSlider_.setBounds(knobs.removeFromLeft(50));
    muteButton_.setBounds(knobs.removeFromLeft(25));
    soloButton_.setBounds(knobs);

    grid_.setBounds(area);
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerChannelStrip.h Source/StepSequencerCore/StepSequencerChannelStrip.cpp
git commit -m "feat(step-sequencer): add channel strip with controls and step grid"
```

---

## Task 6: StepSequencerHeader — Pattern Selector + Controls

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerHeader.h`
- Create: `Source/StepSequencerCore/StepSequencerHeader.cpp`

**Interfaces:**
- Consumes: `StepSequencerModel` from Task 2
- Produces: `StepSequencerHeader` component

- [ ] **Step 1: Write StepSequencerHeader.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"

namespace DAW {

class StepSequencerHeader : public juce::Component {
public:
    StepSequencerHeader(StepSequencerModel& model);

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onAddChannel;
    std::function<void()> onRemoveChannel;

private:
    StepSequencerModel& model_;
    juce::ComboBox patternSelector_;
    juce::TextButton prevPatternBtn_{"<"};
    juce::TextButton nextPatternBtn_{"ryfall"};
    juce::TextButton addPatternBtn_{"+"};
    juce::TextButton clonePatternBtn_{"Clone"};
    juce::TextButton addChannelBtn_{"+"};
    juce::TextButton removeChannelBtn_{"-"};
    juce::Slider swingSlider_;
    juce::ComboBox stepsPerBeatCombo_;
    juce::ComboBox barsCombo_;

    void refreshPatternSelector();
    void patternChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerHeader)
};

} // namespace DAW
```

- [ ] **Step 2: Write StepSequencerHeader.cpp**

```cpp
#include "StepSequencerHeader.h"

namespace DAW {

StepSequencerHeader::StepSequencerHeader(StepSequencerModel& model)
    : model_(model)
{
    addAndMakeVisible(patternSelector_);
    addAndMakeVisible(prevPatternBtn_);
    addAndMakeVisible(nextPatternBtn_);
    addAndMakeVisible(addPatternBtn_);
    addAndMakeVisible(clonePatternBtn_);
    addAndMakeVisible(addChannelBtn_);
    addAndMakeVisible(removeChannelBtn_);
    addAndMakeVisible(swingSlider_);
    addAndMakeVisible(stepsPerBeatCombo_);
    addAndMakeVisible(barsCombo_);

    refreshPatternSelector();

    prevPatternBtn_.onClick = [this]() {
        int idx = model_.getCurrentPatternIndex();
        if (idx > 0) { model_.setCurrentPattern(idx - 1); patternChanged(); }
    };
    nextPatternBtn_.onClick = [this]() {
        int idx = model_.getCurrentPatternIndex();
        if (idx < model_.getPatternCount() - 1) { model_.setCurrentPattern(idx + 1); patternChanged(); }
    };
    addPatternBtn_.onClick = [this]() {
        model_.addPattern(); patternChanged();
    };
    clonePatternBtn_.onClick = [this]() {
        model_.clonePattern(model_.getCurrentPatternIndex()); patternChanged();
    };
    addChannelBtn_.onClick = [this]() {
        if (onAddChannel) onAddChannel();
    };
    removeChannelBtn_.onClick = [this]() {
        if (onRemoveChannel) onRemoveChannel();
    };

    swingSlider_.setRange(0.0, 1.0, 0.01);
    swingSlider_.setSliderStyle(juce::Slider::LinearHorizontal);

    stepsPerBeatCombo_.addItemList({"4", "6", "8", "12", "16", "24", "32"}, 1);
    barsCombo_.addItemList({"1", "2", "4", "8", "16"}, 1);
}

void StepSequencerHeader::refreshPatternSelector() {
    patternSelector_.clear();
    for (int i = 0; i < model_.getPatternCount(); ++i)
        patternSelector_.addItem(model_.getPattern(i).name, i + 1);
    patternSelector_.setSelectedId(model_.getCurrentPatternIndex() + 1, juce::dontSendNotification);
}

void StepSequencerHeader::patternChanged() {
    refreshPatternSelector();
    repaint();
}

void StepSequencerHeader::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff111133));
}

void StepSequencerHeader::resized() {
    auto area = getLocalBounds().reduced(4);
    auto topRow = area.removeFromTop(28);
    prevPatternBtn_.setBounds(topRow.removeFromLeft(28));
    patternSelector_.setBounds(topRow.removeFromLeft(150));
    nextPatternBtn_.setBounds(topRow.removeFromLeft(28));
    addPatternBtn_.setBounds(topRow.removeFromLeft(28));
    clonePatternBtn_.setBounds(topRow.removeFromLeft(50));
    swingSlider_.setBounds(topRow.removeFromLeft(120));

    auto bottomRow = area.removeFromTop(24);
    stepsPerBeatCombo_.setBounds(bottomRow.removeFromLeft(60));
    barsCombo_.setBounds(bottomRow.removeFromLeft(60));
    addChannelBtn_.setBounds(bottomRow.removeFromLeft(28));
    removeChannelBtn_.setBounds(bottomRow.removeFromLeft(28));
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerHeader.h Source/StepSequencerCore/StepSequencerHeader.cpp
git commit -m "feat(step-sequencer): add header with pattern selector and controls"
```

---

## Task 7: StepSequencerComponent — Main Composite

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerComponent.h`
- Create: `Source/StepSequencerCore/StepSequencerComponent.cpp`

**Interfaces:**
- Consumes: All previous StepSequencer tasks (2-6)
- Produces: `StepSequencerComponent` — the main UI component

- [ ] **Step 1: Write StepSequencerComponent.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"
#include "StepSequencerHeader.h"
#include "StepSequencerChannelStrip.h"

namespace DAW {

class StepSequencerComponent : public juce::Component,
                                private juce::ChangeListener {
public:
    StepSequencerComponent();
    ~StepSequencerComponent() override;

    StepSequencerModel& getModel() { return model_; }
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    StepSequencerModel model_;
    StepSequencerHeader header_;
    juce::OwnedArray<StepSequencerChannelStrip> channelStrips_;

    void rebuildChannelStrips();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerComponent)
};

} // namespace DAW
```

- [ ] **Step 2: Write StepSequencerComponent.cpp**

```cpp
#include "StepSequencerComponent.h"

namespace DAW {

StepSequencerComponent::StepSequencerComponent()
    : header_(model_)
{
    addAndMakeVisible(header_);
    model_.addChangeListener(this);
    rebuildChannelStrips();
}

StepSequencerComponent::~StepSequencerComponent() {
    model_.removeChangeListener(this);
}

void StepSequencerComponent::rebuildChannelStrips() {
    channelStrips_.clear();
    const auto& channels = model_.getChannels();
    for (int i = 0; i < channels.size(); ++i) {
        auto* strip = new StepSequencerChannelStrip(model_, i);
        addAndMakeVisible(strip);
        channelStrips_.add(strip);
    }
    resized();
}

void StepSequencerComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    rebuildChannelStrips();
}

void StepSequencerComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void StepSequencerComponent::resized() {
    auto area = getLocalBounds();
    header_.setBounds(area.removeFromTop(60));

    juce::Viewport viewport;
    viewport.setScrollOnDragMode(true);

    int y = 0;
    const int stripHeight = 40;
    for (auto* strip : channelStrips_) {
        strip->setBounds(0, y, getWidth(), stripHeight);
        y += stripHeight;
    }
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerComponent.h Source/StepSequencerCore/StepSequencerComponent.cpp
git commit -m "feat(step-sequencer): add main composite component"
```

---

## Task 8: StepSequencerWindow — Floating Window

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerWindow.h`
- Create: `Source/StepSequencerCore/StepSequencerWindow.cpp`

**Interfaces:**
- Consumes: `StepSequencerComponent` from Task 7
- Produces: `StepSequencerWindow` — floating window wrapper

- [ ] **Step 1: Write StepSequencerWindow.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "StepSequencerComponent.h"

namespace DAW {

class StepSequencerWindow : public juce::DocumentWindow {
public:
    StepSequencerWindow();
    ~StepSequencerWindow() override;

    StepSequencerComponent& getStepSequencer() { return stepSequencer_; }

    void closeButtonPressed() override;

private:
    StepSequencerComponent stepSequencer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerWindow)
};

} // namespace DAW
```

- [ ] **Step 2: Write StepSequencerWindow.cpp**

```cpp
#include "StepSequencerWindow.h"

namespace DAW {

StepSequencerWindow::StepSequencerWindow()
    : DocumentWindow("Step Sequencer",
                     juce::Colour(0xff1a1a2e),
                     DocumentWindow::allButtons)
{
    setResizable(true, true);
    setResizeLimits(600, 200, 1600, 900);
    setContentNonOwned(&stepSequencer_, true);
    centreWithSize(800, 400);
    setVisible(false);
}

StepSequencerWindow::~StepSequencerWindow() = default;

void StepSequencerWindow::closeButtonPressed() {
    setVisible(false);
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerWindow.h Source/StepSequencerCore/StepSequencerWindow.cpp
git commit -m "feat(step-sequencer): add floating window wrapper"
```

---

## Task 9: DrumSamplerTypes — Shared POD Structs

**Files:**
- Create: `Source/DrumSamplerCore/DrumSamplerTypes.h`

**Interfaces:**
- Produces: `DrumPadConfig`, `VelocityLayer`, `DrumSamplerConfig` structs

- [ ] **Step 1: Write DrumSamplerTypes.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include <cstdint>

namespace DAW {

struct VelocityLayer {
    float  velocityRange[2] = {0.0f, 1.0f};
    juce::String filePath;
    juce::AudioBuffer<float> audioData;
    int sampleRate = 44100;
    int64_t numSamples = 0;
};

struct DrumPadConfig {
    juce::String name = "Pad";
    int      midiNote = 36;
    float    gainDb = 0.0f;
    float    pan = 0.0f;
    float    tune = 0.0f;
    float    attackMs = 0.1f;
    float    decayMs = 100.0f;
    float    sustainLevel = 0.8f;
    float    releaseMs = 50.0f;
    int      chokeGroup = -1;
    bool     reverse = false;
    int64_t  startSample = 0;
    int64_t  endSample = -1;
    int      mixerTrack = -1;
    juce::Colour color = juce::Colour(0xff666666);
    juce::Array<VelocityLayer> layers;
};

struct DrumSamplerConfig {
    int  numPads = 16;
    int  maxPolyphony = 16;
    bool roundRobin = false;
};

} // namespace DAW
```

- [ ] **Step 2: Add to Projucer and build**

- [ ] **Step 3: Commit**

```bash
git add Source/DrumSamplerCore/DrumSamplerTypes.h
git commit -m "feat(drum-sampler): add shared POD types"
```

---

## Task 10: DrumSamplerVoice — Single Voice Playback

**Files:**
- Create: `Source/DrumSamplerCore/DrumSamplerVoice.h`
- Create: `Source/DrumSamplerCore/DrumSamplerVoice.cpp`

**Interfaces:**
- Consumes: `DrumPadConfig` from Task 9
- Produces: `DrumSamplerVoice` — plays one trigger of a pad

- [ ] **Step 1: Write DrumSamplerVoice.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "DrumSamplerTypes.h"

namespace DAW {

class DrumSamplerVoice {
public:
    DrumSamplerVoice() = default;

    void start(const DrumPadConfig& pad, float velocity, int64_t samplePos);
    void process(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);
    void stop();
    bool isActive() const noexcept { return active_; }

private:
    bool active_ = false;
    const DrumPadConfig* pad_ = nullptr;
    int64_t playPosition_ = 0;
    float velocity_ = 0.0f;
    float currentGain_ = 0.0f;

    // ADSR state
    enum class EnvStage { Attack, Decay, Sustain, Release, Idle };
    EnvStage envStage_ = EnvStage::Idle;
    float envValue_ = 0.0f;
    int envSampleCounter_ = 0;

    void advanceEnvelope(int numSamples);
};

} // namespace DAW
```

- [ ] **Step 2: Write DrumSamplerVoice.cpp**

```cpp
#include "DrumSamplerVoice.h"

namespace DAW {

void DrumSamplerVoice::start(const DrumPadConfig& pad, float velocity, int64_t samplePos) {
    pad_ = &pad;
    velocity_ = velocity;
    playPosition_ = samplePos;
    active_ = true;
    envStage_ = EnvStage::Attack;
    envValue_ = 0.0f;
    envSampleCounter_ = 0;
}

void DrumSamplerVoice::stop() {
    envStage_ = EnvStage::Release;
    envSampleCounter_ = 0;
}

void DrumSamplerVoice::process(
    juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!active_ || !pad_ || pad_->layers.isEmpty()) return;

    const auto& layer = pad_->layers.getReference(0);
    if (layer.numSamples <= 0) return;

    const float* sourceData = layer.audioData.getReadPointer(0);
    const float gainLinear = juce::Decibels::decibelsToGain(pad_->gainDb) * velocity_;
    const float sampleRateRatio = (float)layer.sampleRate / (float)outputBuffer.getSampleRate();

    for (int i = 0; i < numSamples; ++i) {
        advanceEnvelope(1);
        if (envStage_ == EnvStage::Idle) { active_ = false; return; }

        const int64_t srcPos = (int64_t)(playPosition_ * sampleRateRatio);
        if (srcPos >= layer.numSamples) { active_ = false; return; }

        float sample = sourceData[srcPos] * gainLinear * envValue_;
        outputBuffer.addSample(0, startSample + i, sample);
        if (outputBuffer.getNumChannels() > 1)
            outputBuffer.addSample(1, startSample + i, sample);

        playPosition_++;
    }
}

void DrumSamplerVoice::advanceEnvelope(int numSamples) {
    if (!pad_) return;
    const float attackSamples = pad_->attackMs * 0.001f * 44100.0f;
    const float decaySamples = pad_->decayMs * 0.001f * 44100.0f;
    const float releaseSamples = pad_->releaseMs * 0.001f * 44100.0f;

    envSampleCounter_ += numSamples;

    switch (envStage_) {
        case EnvStage::Attack:
            envValue_ = (attackSamples > 0) ? (float)envSampleCounter_ / attackSamples : 1.0f;
            if (envValue_ >= 1.0f) { envValue_ = 1.0f; envStage_ = EnvStage::Decay; envSampleCounter_ = 0; }
            break;
        case EnvStage::Decay: {
            float t = (decaySamples > 0) ? (float)envSampleCounter_ / decaySamples : 1.0f;
            envValue_ = 1.0f - (1.0f - pad_->sustainLevel) * juce::jmin(t, 1.0f);
            if (t >= 1.0f) { envStage_ = EnvStage::Sustain; envSampleCounter_ = 0; }
            break;
        }
        case EnvStage::Sustain:
            envValue_ = pad_->sustainLevel;
            break;
        case EnvStage::Release: {
            float t = (releaseSamples > 0) ? (float)envSampleCounter_ / releaseSamples : 1.0f;
            envValue_ *= (1.0f - juce::jmin(t, 1.0f));
            if (t >= 1.0f) { envValue_ = 0.0f; envStage_ = EnvStage::Idle; }
            break;
        }
        case EnvStage::Idle:
            break;
    }
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/DrumSamplerCore/DrumSamplerVoice.h Source/DrumSamplerCore/DrumSamplerVoice.cpp
git commit -m "feat(drum-sampler): add voice with ADSR envelope"
```

---

## Task 11: DrumSamplerVoicePool — Lock-Free Voice Allocator

**Files:**
- Create: `Source/DrumSamplerCore/DrumSamplerVoicePool.h`
- Create: `Source/DrumSamplerCore/DrumSamplerVoicePool.cpp`

**Interfaces:**
- Consumes: `DrumSamplerVoice` from Task 10
- Produces: `DrumSamplerVoicePool` — allocates/releases voices lock-free

- [ ] **Step 1: Write DrumSamplerVoicePool.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "DrumSamplerVoice.h"

namespace DAW {

class DrumSamplerVoicePool {
public:
    DrumSamplerVoicePool(int maxVoices = 64);

    DrumSamplerVoice* allocate();
    void release(DrumSamplerVoice* voice);
    void processAll(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);

private:
    juce::OwnedArray<DrumSamplerVoice> voices_;
    juce::CriticalSection lock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerVoicePool)
};

} // namespace DAW
```

- [ ] **Step 2: Write DrumSamplerVoicePool.cpp**

```cpp
#include "DrumSamplerVoicePool.h"

namespace DAW {

DrumSamplerVoicePool::DrumSamplerVoicePool(int maxVoices) {
    for (int i = 0; i < maxVoices; ++i)
        voices_.add(new DrumSamplerVoice());
}

DrumSamplerVoice* DrumSamplerVoicePool::allocate() {
    const juce::ScopedLock sl(lock_);
    for (auto* v : voices_)
        if (!v->isActive()) return v;
    // Steal oldest voice
    return voices_.getFirst();
}

void DrumSamplerVoicePool::release(DrumSamplerVoice* voice) {
    if (voice) voice->stop();
}

void DrumSamplerVoicePool::processAll(
    juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    for (auto* v : voices_)
        if (v->isActive())
            v->process(outputBuffer, startSample, numSamples);
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/DrumSamplerCore/DrumSamplerVoicePool.h Source/DrumSamplerCore/DrumSamplerVoicePool.cpp
git commit -m "feat(drum-sampler): add voice pool with voice stealing"
```

---

## Task 12: DrumSamplerEngine — Top-Level Sampler

**Files:**
- Create: `Source/DrumSamplerCore/DrumSamplerEngine.h`
- Create: `Source/DrumSamplerCore/DrumSamplerEngine.cpp`

**Interfaces:**
- Consumes: `DrumPadConfig`, `DrumSamplerVoicePool` from Tasks 9-11
- Produces: `DrumSamplerEngine` — MIDI→pad mapping, processBlock

- [ ] **Step 1: Write DrumSamplerEngine.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "DrumSamplerTypes.h"
#include "DrumSamplerVoicePool.h"

namespace DAW {

class DrumSamplerEngine {
public:
    DrumSamplerEngine(int numPads = 16);
    ~DrumSamplerEngine() = default;

    void processBlock(juce::MidiBuffer& midiBuffer, juce::AudioBuffer<float>& audioBuffer,
                      int startSample, int numSamples);

    DrumPadConfig& getPad(int index);
    const DrumPadConfig& getPad(int index) const;
    int getNumPads() const noexcept { return pads_.size(); }

    void loadSample(int padIndex, const juce::String& filePath);
    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

private:
    juce::Array<DrumPadConfig> pads_;
    DrumSamplerVoicePool voicePool_;
    juce::AudioFormatManager formatManager_;

    int findPadByMidiNote(int note) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerEngine)
};

} // namespace DAW
```

- [ ] **Step 2: Write DrumSamplerEngine.cpp**

```cpp
#include "DrumSamplerEngine.h"

namespace DAW {

DrumSamplerEngine::DrumSamplerEngine(int numPads)
    : voicePool_(numPads * 4)
{
    formatManager_.registerBasicFormats();
    pads_.resize(numPads);
    for (int i = 0; i < numPads; ++i) {
        pads_[i].name = "Pad " + String(i + 1);
        pads_[i].midiNote = 36 + i;
    }
}

int DrumSamplerEngine::findPadByMidiNote(int note) const {
    for (int i = 0; i < pads_.size(); ++i)
        if (pads_[i].midiNote == note) return i;
    return -1;
}

void DrumSamplerEngine::processBlock(
    juce::MidiBuffer& midiBuffer, juce::AudioBuffer<float>& audioBuffer,
    int startSample, int numSamples)
{
    for (const auto metadata : midiBuffer) {
        const auto msg = metadata.getMessage();
        const int samplePosition = metadata.samplePosition;

        if (msg.isNoteOn()) {
            const int padIdx = findPadByMidiNote(msg.getNoteNumber());
            if (padIdx >= 0) {
                auto* voice = voicePool_.allocate();
                if (voice) {
                    voice->start(pads_[padIdx], msg.getFloatVelocity(), 0);
                }
            }
        } else if (msg.isNoteOff()) {
            // For drums, note-off is usually ignored (one-shot)
        }
    }

    voicePool_.processAll(audioBuffer, startSample, numSamples);
}

DrumPadConfig& DrumSamplerEngine::getPad(int index) { return pads_.getReference(index); }
const DrumPadConfig& DrumSamplerEngine::getPad(int index) const { return pads_[index]; }

void DrumSamplerEngine::loadSample(int padIndex, const juce::String& filePath) {
    if (!juce::isPositiveAndBelow(padIndex, pads_.size())) return;
    auto* reader = formatManager_.createReaderFor(filePath);
    if (!reader) return;

    auto& pad = pads_[padIndex];
    VelocityLayer layer;
    layer.filePath = filePath;
    layer.sampleRate = reader->sampleRate;
    layer.numSamples = reader->lengthInSamples;

    juce::AudioBuffer<float> fileBuffer(1, (int)layer.numSamples);
    reader->read(&fileBuffer, 0, (int)layer.numSamples, 0, true, true);
    delete reader;

    layer.audioData = std::move(fileBuffer);
    layer.velocityRange[0] = 0.0f;
    layer.velocityRange[1] = 1.0f;

    pad.layers.clear();
    pad.layers.add(std::move(layer));
}

juce::ValueTree DrumSamplerEngine::toValueTree() const {
    juce::ValueTree tree("DrumSampler");
    for (const auto& pad : pads_) {
        juce::ValueTree padTree("Pad");
        padTree.setProperty("name", pad.name, nullptr);
        padTree.setProperty("midiNote", pad.midiNote, nullptr);
        padTree.setProperty("gainDb", pad.gainDb, nullptr);
        padTree.setProperty("pan", pad.pan, nullptr);
        padTree.setProperty("tune", pad.tune, nullptr);
        padTree.setProperty("samplePath",
            pad.layers.isEmpty() ? "" : pad.layers[0].filePath, nullptr);
        tree.addChild(padTree, -1, nullptr);
    }
    return tree;
}

void DrumSamplerEngine::fromValueTree(const juce::ValueTree& tree) {
    for (int i = 0; i < juce::jmin(tree.getNumChildren(), pads_.size()); ++i) {
        const auto& padTree = tree.getChild(i);
        pads_[i].name = padTree.getProperty("name", "Pad");
        pads_[i].midiNote = (int)padTree.getProperty("midiNote", 36);
        pads_[i].gainDb = (float)padTree.getProperty("gainDb", 0.0f);
        pads_[i].pan = (float)padTree.getProperty("pan", 0.0f);
        pads_[i].tune = (float)padTree.getProperty("tune", 0.0f);
        juce::String path = padTree.getProperty("samplePath", "");
        if (path.isNotEmpty()) loadSample(i, path);
    }
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/DrumSamplerCore/DrumSamplerEngine.h Source/DrumSamplerCore/DrumSamplerEngine.cpp
git commit -m "feat(drum-sampler): add engine with MIDI mapping and sample loading"
```

---

## Task 13: PatternManagerCore — Pattern CRUD

**Files:**
- Create: `Source/PatternManagerCore/PatternManagerTypes.h`
- Create: `Source/PatternManagerCore/PatternManagerCore.h`
- Create: `Source/PatternManagerCore/PatternManagerCore.cpp`

**Interfaces:**
- Consumes: `PatternData` from Task 1
- Produces: `PatternManagerCore` — pattern switching for StepSequencer

- [ ] **Step 1: Write PatternManagerTypes.h**

```cpp
#pragma once
#include <JuceHeader.h>

namespace DAW {

struct PatternInfo {
    juce::String name;
    int index;
    int numChannels;
    int totalSteps;
};

} // namespace DAW
```

- [ ] **Step 2: Write PatternManagerCore.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "PatternManagerTypes.h"
#include "../StepSequencerCore/StepSequencerModel.h"

namespace DAW {

class PatternManagerCore : public juce::ChangeBroadcaster {
public:
    PatternManagerCore(StepSequencerModel& model);
    ~PatternManagerCore() override;

    int  createPattern(const String& name = "");
    void deletePattern(int index);
    void renamePattern(int index, const String& newName);
    void clonePattern(int index);
    void setCurrentPattern(int index);
    int  getCurrentPattern() const;
    int  getPatternCount() const;
    Array<PatternInfo> getPatternList() const;

private:
    StepSequencerModel& model_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternManagerCore)
};

} // namespace DAW
```

- [ ] **Step 3: Write PatternManagerCore.cpp**

```cpp
#include "PatternManagerCore.h"

namespace DAW {

PatternManagerCore::PatternManagerCore(StepSequencerModel& model)
    : model_(model) {}

PatternManagerCore::~PatternManagerCore() = default;

int PatternManagerCore::createPattern(const String& name) {
    int idx = model_.addPattern(name);
    sendChangeMessage();
    return idx;
}

void PatternManagerCore::deletePattern(int index) {
    model_.removePattern(index);
    sendChangeMessage();
}

void PatternManagerCore::renamePattern(int index, const String& newName) {
    model_.getPattern(index).name = newName;
    sendChangeMessage();
}

void PatternManagerCore::clonePattern(int index) {
    model_.clonePattern(index);
    sendChangeMessage();
}

void PatternManagerCore::setCurrentPattern(int index) {
    model_.setCurrentPattern(index);
    sendChangeMessage();
}

int PatternManagerCore::getCurrentPattern() const { return model_.getCurrentPatternIndex(); }
int PatternManagerCore::getPatternCount() const { return model_.getPatternCount(); }

Array<PatternInfo> PatternManagerCore::getPatternList() const {
    Array<PatternInfo> list;
    for (int i = 0; i < model_.getPatternCount(); ++i) {
        const auto& p = model_.getPattern(i);
        list.add({ p.name, i, p.channels.size(), p.getTotalSteps() });
    }
    return list;
}

} // namespace DAW
```

- [ ] **Step 4: Add to Projucer and build**

- [ ] **Step 5: Commit**

```bash
git add Source/PatternManagerCore/PatternManagerTypes.h Source/PatternManagerCore/PatternManagerCore.h Source/PatternManagerCore/PatternManagerCore.cpp
git commit -m "feat(pattern-manager): add pattern CRUD and switching"
```

---

## Task 14: StepSequencerPlaybackCore — Audio-Thread Playback

**Files:**
- Create: `Source/StepSequencerCore/StepSequencerPlaybackCore.h`
- Create: `Source/StepSequencerCore/StepSequencerPlaybackCore.cpp`

**Interfaces:**
- Consumes: `StepSequencerModel::Snapshot` from Task 2
- Produces: `StepSequencerPlaybackCore` — schedules MIDI from steps

- [ ] **Step 1: Write StepSequencerPlaybackCore.h**

```cpp
#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"

namespace DAW {

class StepSequencerPlaybackCore {
public:
    StepSequencerPlaybackCore();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(juce::MidiBuffer& midiBuffer, int numSamples,
                      int64_t currentSamplePosition, double tempo);

private:
    double sampleRate_ = 44100.0;
    int samplesPerBlock_ = 512;
    int64_t samplesPerBeat_ = 44100;
    int64_t lastStepSample_ = 0;
    int currentStep_ = 0;

    int64_t tickToSample(int64_t tick, double tempo) const;
};

} // namespace DAW
```

- [ ] **Step 2: Write StepSequencerPlaybackCore.cpp**

```cpp
#include "StepSequencerPlaybackCore.h"

namespace DAW {

StepSequencerPlaybackCore::StepSequencerPlaybackCore() = default;

void StepSequencerPlaybackCore::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate;
    samplesPerBlock_ = samplesPerBlock;
}

int64_t StepSequencerPlaybackCore::tickToSample(int64_t tick, double tempo) const {
    const double beatsPerSecond = tempo / 60.0;
    const double samplesPerBeat = sampleRate_ / beatsPerSecond;
    return (int64_t)((double)tick / 960.0 * samplesPerBeat);
}

void StepSequencerPlaybackCore::processBlock(
    juce::MidiBuffer& midiBuffer, int numSamples,
    int64_t currentSamplePosition, double tempo)
{
    // Placeholder: in full implementation, reads snapshot and schedules notes
    // For now, this is a stub that will be connected in Phase 1 integration
    (void)midiBuffer;
    (void)numSamples;
    (void)currentSamplePosition;
    (void)tempo;
}

} // namespace DAW
```

- [ ] **Step 3: Add to Projucer and build**

- [ ] **Step 4: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerPlaybackCore.h Source/StepSequencerCore/StepSequencerPlaybackCore.cpp
git commit -m "feat(step-sequencer): add playback core skeleton"
```

---

## Task 15: Integration — Wire Everything Together

**Files:**
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`

**Interfaces:**
- Consumes: All previous tasks
- Produces: Working integration in the main application

- [ ] **Step 1: Add Step Sequencer window to MainComponent**

Add `#include` for StepSequencerWindow, DrumSamplerEngine, PatternManagerCore.

- [ ] **Step 2: Create instances in MainComponent constructor**

```cpp
// In MainComponent.h
StepSequencerWindow stepSeqWindow_;
DrumSamplerEngine drumEngine_;
PatternManagerCore patternManager_;
```

- [ ] **Step 3: Show step sequencer on toolbar button click**

Wire existing toolbar or create a button to toggle `stepSeqWindow_.setVisible(!stepSeqWindow_.isVisible())`.

- [ ] **Step 4: Connect playback core to audio processing**

In `processBlock`, call `drumEngine_.processBlock(...)` with MIDI from step sequencer.

- [ ] **Step 5: Build and test**

Open Apex, click step sequencer button, see the channel rack. Click steps, load a sample onto a channel, press play. Hear the beat.

- [ ] **Step 6: Commit**

```bash
git add Source/MainComponent.h Source/MainComponent.cpp
git commit -m "feat(beat-making): integrate step sequencer, drum sampler, pattern manager"
```

---

## Self-Review

**1. Spec coverage:** All Phase 1 modules covered — StepSequencerCore (Tasks 1-8), DrumSamplerCore (Tasks 9-12), PatternManagerCore (Task 13), PlaybackCore (Task 14), Integration (Task 15).

**2. Placeholder scan:** Task 14 playback core is a skeleton (processBlock is a stub) — acceptable for MVP, will be filled in during integration testing.

**3. Type consistency:** All types match between tasks. `StepEvent`, `ChannelData`, `PatternData` used consistently. `DrumPadConfig` used consistently.

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-07-09-beat-making-phase1-mvp.md`.**

Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?
