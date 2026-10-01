# APEX Beat-Making Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build FL Studio-inspired beat-making with Channel Rack, Piano Roll, and enhanced step attributes.

**Architecture:** Shared StepEvent model for step grid and piano roll. Enhanced playback with ratchets, flam, ties, conditions, deterministic probability.

**Tech Stack:** C++17, JUCE 7, MSVC (Windows)

## Global Constraints

- No allocation, file I/O, GUI calls in realtime callback
- All model mutations via JUCE UndoManager
- Stable IDs cross persistence boundaries
- Deterministic seed: hash(projectId, patternId, laneId, stepIndex, loopIteration)
- 960 PPQ internal timing
- Max ratchet count: 16
- Build: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
- Tests: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `Source/StepSequencerCore/StepSequencerTypes.h` | Modify | Enhanced StepEvent, LaneData |
| `Source/StepSequencerCore/StepSequencerModel.h/.cpp` | Modify | Lane-based pattern model |
| `Source/StepSequencerCore/StepSequencerPlaybackCore.h/.cpp` | Modify | Ratchets, flam, ties, conditions |
| `Source/StepSequencerCore/StepSequencerComponent.h/.cpp` | Modify | FL-style Channel Rack |
| `Source/StepSequencerCore/StepSequencerLookAndFeel.h/.cpp` | Modify | FL-style colors |
| `Source/StepSequencerCore/PianoRollComponent.h/.cpp` | Create | Piano roll editor |
| `Source/StepSequencerCore/GrooveTemplate.h/.cpp` | Create | Groove presets |
| `Tests/StepSequencerTests/` | Create | Unit tests |

---

## Task 1: Enhanced StepEvent Schema

**Files:**
- Modify: `Source/StepSequencerCore/StepSequencerTypes.h`
- Test: `Tests/StepSequencerTests/StepEventTests.cpp`

**Interfaces:**
- Consumes: None (foundation)
- Produces: `StepEvent`, `LaneData`, `GrooveTemplate` types

- [ ] **Step 1: Write the failing test**

```cpp
// Tests/StepSequencerTests/StepEventTests.cpp
#include <catch2/catch_test_macros.hpp>
#include "../../Source/StepSequencerCore/StepSequencerTypes.h"

using namespace DAW;

TEST_CASE("StepEvent default construction", "[StepEvent]") {
    StepEvent e;
    REQUIRE(e.active == false);
    REQUIRE(e.velocity == 100.0f);
    REQUIRE(e.duration == 0);
    REQUIRE(e.probability == 1.0f);
    REQUIRE(e.ratchetCount == 1);
    REQUIRE(e.flamEnabled == false);
    REQUIRE(e.tieToNext == false);
    REQUIRE(e.condition == StepEvent::Condition::Every);
}

TEST_CASE("LaneData default construction", "[LaneData]") {
    LaneData lane;
    REQUIRE(lane.name == "Channel");
    REQUIRE(lane.volume == 1.0f);
    REQUIRE(lane.muted == false);
    REQUIRE(lane.events.isEmpty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: FAIL

- [ ] **Step 3: Write minimal implementation**

```cpp
// Source/StepSequencerCore/StepSequencerTypes.h
#pragma once
#include <JuceHeader.h>
#include <cstdint>

namespace DAW {

using StablePatternId = juce::Uuid;
using StableLaneId = juce::Uuid;
using StableEventId = juce::Uuid;

struct StepEvent {
    StableEventId   id;
    bool            active          = false;
    float           velocity        = 100.0f;
    int32_t         duration        = 0;
    float           pan             = 0.0f;
    float           probability     = 1.0f;
    int8_t          pitchOffset     = 0;
    uint8_t         ratchetCount    = 1;
    uint8_t         ratchetSpacing  = 0;
    bool            flamEnabled     = false;
    int16_t         flamOffsetTicks = 0;
    bool            tieToNext       = false;
    enum class Condition : uint8_t {
        Every = 0, A, B, AB, Fill, NotFill,
        PrevActive, PrevInactive, First, Last,
        Random4, Random8, Random16
    };
    Condition       condition       = Condition::Every;
    int16_t         microtimingTicks = 0;
    bool            swingBypass     = false;

    StepEvent() : id(StablePatternId()) {}
};

struct LaneData {
    StableLaneId    id;
    juce::String    name            = "Channel";
    float           volume          = 1.0f;
    float           pan             = 0.0f;
    bool            muted           = false;
    bool            soloed          = false;
    float           swingAmount     = 0.0f;
    int             laneLength      = 0;
    int             midiNote        = 60;
    int             midiChannel     = 0;
    juce::Colour    color           = juce::Colour(0xff4488ff);
    juce::Array<StepEvent> events;

    LaneData() : id(StableLaneId()) {}
};

} // namespace DAW
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerTypes.h Tests/StepSequencerTests/StepEventTests.cpp
git commit -m "feat(beat-making): enhance StepEvent schema with ratchets, flam, ties, conditions"
```

---

## Task 2: Lane-Based Pattern Model

**Files:**
- Modify: `Source/StepSequencerCore/StepSequencerModel.h`
- Modify: `Source/StepSequencerCore/StepSequencerModel.cpp`

**Interfaces:**
- Consumes: `StepEvent`, `LaneData` from Task 1
- Produces: `PatternModel` with lane operations, `Snapshot` with lanes

- [ ] **Step 1: Write the failing test**

```cpp
// Add to Tests/StepSequencerTests/StepEventTests.cpp

TEST_CASE("PatternModel lane operations", "[PatternModel]") {
    PatternModel model;

    SECTION("Add lane") {
        LaneData lane;
        lane.name = "Kick";
        model.addLane(lane);
        REQUIRE(model.getLaneCount() == 1);
        REQUIRE(model.getLane(0).name == "Kick");
    }

    SECTION("Toggle step") {
        LaneData lane;
        lane.events.resize(16);
        model.addLane(lane);
        model.toggleStep(0, 5);
        REQUIRE(model.getLane(0).events[5].active == true);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: FAIL

- [ ] **Step 3: Write minimal implementation**

```cpp
// Source/StepSequencerCore/StepSequencerModel.h
#pragma once
#include <JuceHeader.h>
#include "StepSequencerTypes.h"
#include <memory>
#include <mutex>

namespace DAW {

class PatternModel : public juce::ChangeBroadcaster {
public:
    PatternModel();
    ~PatternModel() override;

    void setName(const juce::String& name);
    juce::String getName() const;
    void setLength(int totalSteps);
    int getLength() const;
    void setGlobalSwing(float swing);
    float getGlobalSwing() const;

    int addLane(const LaneData& lane = LaneData());
    void removeLane(int index);
    void moveLane(int fromIndex, int toIndex);
    LaneData& getLane(int index);
    const LaneData& getLane(int index) const;
    int getLaneCount() const noexcept;

    void toggleStep(int laneIndex, int stepIndex);
    void setStepVelocity(int laneIndex, int stepIndex, float velocity);

    struct Snapshot {
        struct LaneSnapshot {
            StableLaneId id;
            juce::String name;
            float volume;
            float pan;
            bool muted;
            bool soloed;
            int midiNote;
            int midiChannel;
            float swingAmount;
            int laneLength;
            juce::Array<StepEvent> events;
        };
        StablePatternId patternId;
        juce::String name;
        int totalSteps;
        int stepsPerBeat;
        int beatsPerBar;
        float globalSwing;
        juce::Array<LaneSnapshot> lanes;
    };

    void publishSnapshot();
    std::shared_ptr<const Snapshot> getSnapshotRT() const;

private:
    StablePatternId id_;
    juce::String name_ = "Pattern 1";
    int totalSteps_ = 16;
    int stepsPerBeat_ = 4;
    int beatsPerBar_ = 4;
    float globalSwing_ = 0.0f;
    juce::Array<LaneData> lanes_;
    mutable std::mutex snapshotMutex_;
    std::shared_ptr<const Snapshot> publishedSnapshot_;
};

} // namespace DAW
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerModel.h Source/StepSequencerCore/StepSequencerModel.cpp
git commit -m "feat(beat-making): implement lane-based PatternModel with snapshot"
```

---

## Task 3: Enhanced Playback — Ratchets, Flam, Ties

**Files:**
- Modify: `Source/StepSequencerCore/StepSequencerPlaybackCore.h`
- Modify: `Source/StepSequencerCore/StepSequencerPlaybackCore.cpp`
- Test: `Tests/StepSequencerTests/PlaybackTests.cpp`

**Interfaces:**
- Consumes: `PatternModel::Snapshot` from Task 2
- Produces: Enhanced MIDI output

- [ ] **Step 1: Write the failing test**

```cpp
// Tests/StepSequencerTests/PlaybackTests.cpp
#include <catch2/catch_test_macros.hpp>
#include "../../Source/StepSequencerCore/StepSequencerPlaybackCore.h"
#include "../../Source/StepSequencerCore/StepSequencerModel.h"

using namespace DAW;

TEST_CASE("Ratchet expansion", "[Ratchet]") {
    StepSequencerPlaybackCore playback;
    playback.prepareToPlay(44100.0, 512);

    PatternModel::Snapshot snapshot;
    snapshot.totalSteps = 4;
    snapshot.stepsPerBeat = 4;
    snapshot.beatsPerBar = 1;
    snapshot.globalSwing = 0.0f;

    PatternModel::Snapshot::LaneSnapshot lane;
    lane.name = "Kick";
    lane.volume = 1.0f;
    lane.muted = false;
    lane.soloed = false;
    lane.midiNote = 60;
    lane.midiChannel = 0;
    lane.swingAmount = 0.0f;
    lane.laneLength = 0;

    StepEvent event;
    event.active = true;
    event.velocity = 100.0f;
    event.ratchetCount = 3;
    lane.events.add(event);

    for (int i = 1; i < 4; ++i) {
        StepEvent inactive;
        inactive.active = false;
        lane.events.add(inactive);
    }

    snapshot.lanes.add(lane);

    juce::MidiBuffer midiBuffer;
    playback.processBlock(midiBuffer, 512, 0, 120.0, snapshot);

    int noteOnCount = 0;
    for (const auto metadata : midiBuffer)
        if (metadata.getMessage().isNoteOn()) noteOnCount++;

    REQUIRE(noteOnCount == 3);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: FAIL

- [ ] **Step 3: Write minimal implementation**

Add ratchet, flam, tie handling to `StepSequencerPlaybackCore.cpp`. See spec §3.3 for details.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerPlaybackCore.h Source/StepSequencerCore/StepSequencerPlaybackCore.cpp Tests/StepSequencerTests/PlaybackTests.cpp
git commit -m "feat(beat-making): add ratchet expansion, flam, and tie handling"
```

---

## Task 4: Deterministic Probability and Conditions

**Files:**
- Modify: `Source/StepSequencerCore/StepSequencerPlaybackCore.cpp`
- Test: `Tests/StepSequencerTests/PlaybackTests.cpp`

**Interfaces:**
- Consumes: `PatternModel::Snapshot` from Task 2
- Produces: Deterministic probability rolls, condition evaluation

- [ ] **Step 1: Write the failing test**

```cpp
// Add to Tests/StepSequencerTests/PlaybackTests.cpp

TEST_CASE("Deterministic probability", "[Deterministic]") {
    StepSequencerPlaybackCore playback;
    playback.prepareToPlay(44100.0, 512);

    PatternModel::Snapshot snapshot;
    snapshot.patternId = StablePatternId();
    snapshot.totalSteps = 16;
    snapshot.stepsPerBeat = 4;
    snapshot.beatsPerBar = 4;
    snapshot.globalSwing = 0.0f;

    PatternModel::Snapshot::LaneSnapshot lane;
    lane.name = "HiHat";
    lane.midiNote = 42;
    lane.midiChannel = 0;

    for (int i = 0; i < 16; ++i) {
        StepEvent event;
        event.active = true;
        event.probability = 0.5f;
        lane.events.add(event);
    }

    snapshot.lanes.add(lane);

    juce::MidiBuffer buffer1, buffer2;
    playback.processBlock(buffer1, 512, 0, 120.0, snapshot);
    playback.processBlock(buffer2, 512, 0, 120.0, snapshot);

    int count1 = 0, count2 = 0;
    for (const auto metadata : buffer1)
        if (metadata.getMessage().isNoteOn()) count1++;
    for (const auto metadata : buffer2)
        if (metadata.getMessage().isNoteOn()) count2++;

    REQUIRE(count1 == count2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: FAIL

- [ ] **Step 3: Write minimal implementation**

Add deterministic seed calculation and condition evaluation to `StepSequencerPlaybackCore.cpp`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerPlaybackCore.cpp Tests/StepSequencerTests/PlaybackTests.cpp
git commit -m "feat(beat-making): add deterministic probability and condition evaluation"
```

---

## Task 5: Per-Lane Swing and Polymeter

**Files:**
- Modify: `Source/StepSequencerCore/StepSequencerPlaybackCore.cpp`
- Test: `Tests/StepSequencerTests/PlaybackTests.cpp`

**Interfaces:**
- Consumes: `PatternModel::Snapshot` from Task 2
- Produces: Per-lane swing, per-lane length (polymeter)

- [ ] **Step 1: Write the failing test**

```cpp
// Add to Tests/StepSequencerTests/PlaybackTests.cpp

TEST_CASE("Polymeter", "[Polymeter]") {
    StepSequencerPlaybackCore playback;
    playback.prepareToPlay(44100.0, 512);

    PatternModel::Snapshot snapshot;
    snapshot.totalSteps = 16;
    snapshot.stepsPerBeat = 4;
    snapshot.beatsPerBar = 4;
    snapshot.globalSwing = 0.0f;

    PatternModel::Snapshot::LaneSnapshot lane1, lane2;
    lane1.name = "Kick";
    lane1.laneLength = 16;
    lane1.midiNote = 60;
    lane2.name = "Perc";
    lane2.laneLength = 12;
    lane2.midiNote = 50;

    for (int i = 0; i < 16; ++i) {
        StepEvent event;
        event.active = true;
        lane1.events.add(event);
    }

    for (int i = 0; i < 12; ++i) {
        StepEvent event;
        event.active = true;
        lane2.events.add(event);
    }

    snapshot.lanes.add(lane1);
    snapshot.lanes.add(lane2);

    juce::MidiBuffer buffer;
    playback.processBlock(buffer, 512, 0, 120.0, snapshot);

    int lane1Notes = 0, lane2Notes = 0;
    for (const auto metadata : buffer) {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            if (msg.getNoteNumber() == 60) lane1Notes++;
            if (msg.getNoteNumber() == 50) lane2Notes++;
        }
    }

    REQUIRE(lane1Notes == 16);
    REQUIRE(lane2Notes == 12);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: FAIL

- [ ] **Step 3: Write minimal implementation**

Update `processBlock` to use per-lane swing and lane length for polymeter.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerPlaybackCore.cpp Tests/StepSequencerTests/PlaybackTests.cpp
git commit -m "feat(beat-making): add per-lane swing and polymeter support"
```

---

## Task 6: FL-Style Channel Rack UI

**Files:**
- Modify: `Source/StepSequencerCore/StepSequencerLookAndFeel.h/.cpp`
- Modify: `Source/StepSequencerCore/StepSequencerComponent.h/.cpp`

**Interfaces:**
- Consumes: `PatternModel` from Task 2
- Produces: FL-style Channel Rack rendering

- [ ] **Step 1: Implement LookAndFeel colors**

Add FL-style color constants to `StepSequencerLookAndFeel`.

- [ ] **Step 2: Implement Channel Rack layout**

Update `StepSequencerComponent` with FL-style layout:
- Channel name on left (120px)
- Step grid on right (30px per step)
- Bar/beat markers in header

- [ ] **Step 3: Build and test visually**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
Expected: BUILD SUCCEEDED

- [ ] **Step 4: Commit**

```bash
git add Source/StepSequencerCore/StepSequencerLookAndFeel.h Source/StepSequencerCore/StepSequencerLookAndFeel.cpp Source/StepSequencerCore/StepSequencerComponent.h Source/StepSequencerCore/StepSequencerComponent.cpp
git commit -m "feat(beat-making): add FL-style Channel Rack UI"
```

---

## Task 7: Piano Roll Component

**Files:**
- Create: `Source/StepSequencerCore/PianoRollComponent.h`
- Create: `Source/StepSequencerCore/PianoRollComponent.cpp`
- Test: `Tests/StepSequencerTests/PianoRollTests.cpp`

**Interfaces:**
- Consumes: `PatternModel` from Task 2
- Produces: Piano roll editor sharing same StepEvent data

- [ ] **Step 1: Write the failing test**

```cpp
// Tests/StepSequencerTests/PianoRollTests.cpp
#include <catch2/catch_test_macros.hpp>
#include "../../Source/StepSequencerCore/PianoRollComponent.h"

using namespace DAW;

TEST_CASE("PianoRoll component creation", "[PianoRoll]") {
    PatternModel model;
    PianoRollComponent pianoRoll(model);

    SECTION("Default state") {
        REQUIRE(pianoRoll.getCurrentLane() == -1);
    }

    SECTION("Set lane") {
        LaneData lane;
        lane.name = "Kick";
        model.addLane(lane);
        pianoRoll.setCurrentLane(0);
        REQUIRE(pianoRoll.getCurrentLane() == 0);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: FAIL

- [ ] **Step 3: Write minimal implementation**

Create `PianoRollComponent` with:
- Note display based on MIDI note number
- Click to add/remove notes
- Shared events with StepEvent model

- [ ] **Step 4: Run test to verify it passes**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add Source/StepSequencerCore/PianoRollComponent.h Source/StepSequencerCore/PianoRollComponent.cpp Tests/StepSequencerTests/PianoRollTests.cpp
git commit -m "feat(beat-making): add Piano Roll component sharing StepEvent data"
```

---

## Task 8: Integration and Build Verification

- [ ] **Step 1: Build Debug**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
Expected: BUILD SUCCEEDED

- [ ] **Step 2: Build Release**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned`
Expected: BUILD SUCCEEDED

- [ ] **Step 3: Run all tests**

Run: `cd "My DAW/DAW_Core"; powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: All tests PASS

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(beat-making): complete Phase 1 integration"
```

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-07-19-beat-making-core.md`.**

**Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**