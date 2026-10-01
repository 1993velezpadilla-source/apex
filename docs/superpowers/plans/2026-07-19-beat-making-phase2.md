# APEX Beat-Making Phase 2 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Connect Step Sequencer to Arrangement View via Pattern Clips.

**Architecture:** PatternClip extends Clip, references StepSequencerModel patterns, renders as mini step grids, plays back via StepSequencerPlaybackCore.

**Tech Stack:** C++17, JUCE 7, MSVC (Windows)

## Global Constraints

- No allocation, file I/O, GUI calls in realtime callback
- All model mutations via JUCE UndoManager
- Stable IDs cross persistence boundaries
- Pattern snapshot is immutable during audio processing
- Build: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
- Tests: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `Source/ClipCore/Clip.h` | Modify | Add Pattern to ClipType enum |
| `Source/ClipCore/PatternClip.h` | Create | PatternClip class |
| `Source/ClipCore/PatternClip.cpp` | Create | PatternClip implementation |
| `Source/ClipCore/Clip.h` | Modify | Add createPatternClip to ClipManager |
| `Source/ClipCore/Clip.cpp` | Modify | Handle Pattern type in recreateClipFromState |
| `Source/AudioEngineCore/AudioEngine.h` | Modify | Add PatternClip render path |
| `Source/ArrangementEditor/ArrangementClipModel.h` | Modify | Add patternId field |
| `Source/ArrangementEditor/PatternClipRenderer.h` | Create | Visual renderer |
| `Source/ArrangementEditor/PatternClipRenderer.cpp` | Create | Step grid rendering |
| `Source/ArrangementEditor/ClipRenderCore.h` | Modify | Delegate to PatternClipRenderer |
| `Tests/StepSequencerTests/PatternClipTests.cpp` | Create | Unit tests |

---

## Task 1: PatternClip Data Model

**Files:**
- Create: `Source/ClipCore/PatternClip.h`
- Create: `Source/ClipCore/PatternClip.cpp`
- Modify: `Source/ClipCore/Clip.h` (add Pattern to ClipType enum)

**Interfaces:**
- Consumes: `StablePatternId` from StepSequencerTypes.h
- Produces: `PatternClip` class extending `Clip`

- [ ] **Step 1: Add Pattern to ClipType enum**

In `Source/ClipCore/Clip.h`, add `Pattern` to the `ClipType` enum:
```cpp
enum class ClipType { Audio, MIDI, Automation, Pattern };
```

- [ ] **Step 2: Create PatternClip.h**

```cpp
#pragma once
#include "Clip.h"
#include "../StepSequencerCore/StepSequencerTypes.h"

namespace DAW {

class PatternClip : public Clip {
public:
    PatternClip(const juce::String& id, const juce::String& name,
                StablePatternId patternId);
    
    StablePatternId getPatternId() const { return patternId_; }
    void setPatternId(StablePatternId id) { patternId_ = id; }
    
    bool isLinked() const { return isLinked_; }
    void setLinked(bool linked) { isLinked_ = linked; }
    
    float getGain() const { return gain_; }
    void setGain(float g) { gain_ = juce::jlimit(0.0f, 2.0f, g); }
    
    float getTranspose() const { return transpose_; }
    void setTranspose(float t) { transpose_ = t; }
    
    float getProbability() const { return probability_; }
    void setProbability(float p) { probability_ = juce::jlimit(0.0f, 1.0f, p); }
    
    juce::ValueTree getState() const override;
    void restoreState(const juce::ValueTree& state) override;

private:
    StablePatternId patternId_;
    bool isLinked_ = true;
    float gain_ = 1.0f;
    float transpose_ = 0.0f;
    float probability_ = 1.0f;
};

} // namespace DAW
```

- [ ] **Step 3: Create PatternClip.cpp**

Implement constructor, getState(), restoreState() following the MidiClip pattern.

- [ ] **Step 4: Add createPatternClip to ClipManager**

In `ClipCore/Clip.h`, add to ClipManager:
```cpp
PatternClip* createPatternClip(const juce::String& name, StablePatternId patternId);
```

In `ClipCore/Clip.cpp`, implement it.

- [ ] **Step 5: Update recreateClipFromState**

In `Clip.cpp`, add case for `ClipType::Pattern` in `recreateClipFromState()`.

- [ ] **Step 6: Write tests**

Create `Tests/StepSequencerTests/PatternClipTests.cpp`:
- PatternClip creation
- PatternClip serialization round-trip
- ClipManager createPatternClip
- ClipType dispatch

- [ ] **Step 7: Build and test**

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(beat-making-phase2): add PatternClip data model"
```

---

## Task 2: ArrangementView Visual Integration

**Files:**
- Modify: `Source/ArrangementEditor/ArrangementClipModel.h` (add patternId)
- Create: `Source/ArrangementEditor/PatternClipRenderer.h`
- Create: `Source/ArrangementEditor/PatternClipRenderer.cpp`
- Modify: `Source/ArrangementEditor/ClipRenderCore.h` (delegate to PatternClipRenderer)

**Interfaces:**
- Consumes: `PatternClip` from Task 1
- Produces: Visual renderer for Pattern Clips

- [ ] **Step 1: Add patternId to ArrangementClipModel**

Add `juce::String patternId;` field to `ArrangementClipModel`.

- [ ] **Step 2: Create PatternClipRenderer**

A juce::Component that draws a mini step grid:
- Takes a reference to StepSequencerModel and a patternId
- Draws lanes as rows, steps as columns
- Active steps shown as colored blocks
- Beat/bar markers visible
- Resizable (fits clip bounds)

- [ ] **Step 3: Update ClipRenderCore**

In `ClipRenderCore::paint()`, check if clip is Pattern type and delegate to PatternClipRenderer.

- [ ] **Step 4: Build and test**

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(beat-making-phase2): add PatternClip visual renderer"
```

---

## Task 3: AudioEngine Playback Integration

**Files:**
- Modify: `Source/AudioEngineCore/AudioEngine.h` (add PatternClip render path)

**Interfaces:**
- Consumes: `PatternClip` from Task 1, `StepSequencerPlaybackCore`
- Produces: MIDI output from pattern clips

- [ ] **Step 1: Add PatternClip render path**

In `renderClipInternal()`, add branch for `ClipType::Pattern`:
- Get the PatternClip's patternId
- Get StepSequencerModel snapshot for that pattern
- Apply instance overrides (gain, transpose, probability)
- Feed to StepSequencerPlaybackCore::processBlock()
- Output MIDI into track's MidiBuffer

- [ ] **Step 2: Write integration tests**

- [ ] **Step 3: Build and test**

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(beat-making-phase2): add PatternClip audio playback"
```

---

## Task 4: Linked vs Independent & Make Unique

**Files:**
- Modify: `Source/ClipCore/PatternClip.cpp` (add MakeUnique)
- Modify: `Source/StepSequencerCore/StepSequencerModel.h` (add clonePattern)

**Interfaces:**
- Consumes: `PatternClip` from Task 1
- Produces: Linked/Independent clip behavior

- [ ] **Step 1: Implement MakeUnique**

When a user selects "Make Unique" on a PatternClip:
1. Clone the pattern in StepSequencerModel (get new patternId)
2. Update the clip's patternId to the new clone
3. Set isLinked = false

- [ ] **Step 2: Implement linked editing**

When a linked PatternClip's pattern is edited:
1. All linked clips see the change (they reference the same patternId)
2. No special handling needed — they all read from the same StepSequencerModel

- [ ] **Step 3: Build and test**

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(beat-making-phase2): add linked/independent clip behavior"
```

---

## Task 5: Integration and Build Verification

- [ ] **Step 1: Build Debug**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
```

- [ ] **Step 2: Build Release**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
```

- [ ] **Step 3: Run all tests**

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(beat-making-phase2): complete Phase 2 integration"
```

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-07-19-beat-making-phase2.md`.**

**Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
