# APEX Beat-Making Phase 2 — Pattern Clips in Playlist

**Date:** 2026-07-19
**Status:** Draft
**Scope:** Phase 2 — Pattern Clips, Arrangement Integration, Playback

---

## 1. Objective

Connect the Step Sequencer to the Arrangement View by implementing Pattern Clips — clips in the timeline that reference a step sequencer pattern and play back via the existing playback engine.

## 2. Architecture

### 2.1 PatternClip Data Model

```cpp
// ClipCore/PatternClip.h
struct PatternClip : public Clip {
    StablePatternId patternId;      // References StepSequencerModel pattern
    bool            isLinked;       // true = linked (edits propagate), false = independent
    float           gain;           // Instance gain override [0..2]
    float           transpose;      // Instance transpose in semitones
    float           probability;    // Instance probability [0..1]
    
    // Instance overrides (optional per-lane)
    struct LaneOverride {
        StableLaneId laneId;
        bool  muted;
        float volume;
        float pan;
    };
    juce::Array<LaneOverride> laneOverrides;
};
```

### 2.2 Clip Type Extension

Add `Pattern` to the existing `ClipType` enum in `ClipCore/Clip.h`:
```cpp
enum class ClipType { Audio, MIDI, Automation, Pattern };
```

### 2.3 Visual Representation

Pattern Clips render as mini step grids in the ArrangementView:
- Each lane = one row in the clip
- Active steps shown as colored blocks
- Beat/bar markers visible
- Color = lane color or clip color
- Linked clips show a chain icon
- Independent clips show no icon

### 2.4 Playback Path

```
AudioEngine::processTrackNode()
  ├── if track role == MIDI or Instrument:
  │   ├── for each PatternClip on track:
  │   │   ├── Get StepSequencerModel snapshot for patternId
  │   │   ├── Apply instance overrides (gain, transpose, probability)
  │   │   ├── Feed to StepSequencerPlaybackCore::processBlock()
  │   │   └── Output MIDI buffer into track's MidiBuffer
  │   └── Continue with existing MIDI playback path
```

### 2.5 Linked vs Independent

- **Linked:** Multiple clips reference the same `patternId`. Editing the pattern in one clip affects all linked clips.
- **Independent:** "Make Unique" clones the pattern with a new `patternId`. The clip now references the clone.
- **Visual indicator:** Linked clips show a chain link icon; independent clips show nothing.

## 3. Component Architecture

### 3.1 New Files

```
Source/ClipCore/PatternClip.h          — PatternClip class
Source/ClipCore/PatternClip.cpp        — Implementation
Source/ArrangementEditor/PatternClipRenderer.h  — Visual renderer
Source/ArrangementEditor/PatternClipRenderer.cpp — Step grid rendering
```

### 3.2 Modified Files

| File | Change |
|------|--------|
| `ClipCore/Clip.h` | Add `Pattern` to ClipType enum |
| `ClipCore/Clip.h` | Add `createPatternClip()` to ClipManager |
| `ClipCore/Clip.cpp` | Handle Pattern type in `recreateClipFromState()` |
| `AudioEngineCore/AudioEngine.h` | Add PatternClip render path in `renderClipInternal()` |
| `ArrangementEditor/ArrangementClipModel.h` | Add `patternId` field |
| `ArrangementEditor/ClipRenderCore.h` | Add PatternClip rendering branch |
| `Builds/.../DAW_Core_App.vcxproj` | Add new source files |

## 4. Integration Points

### 4.1 ClipManager

- Add `createPatternClip(name, patternId)` factory
- Update `recreateClipFromState()` for `ClipType::Pattern`
- Update serialization/deserialization

### 4.2 AudioEngine

- In `renderClipInternal()`, add branch for `ClipType::Pattern`
- Use `StepSequencerPlaybackCore` to convert pattern to MIDI
- Apply instance overrides (gain, transpose, probability)
- Output into track's MidiBuffer

### 4.3 ArrangementView

- `ClipRenderCore` checks clip type and delegates to `PatternClipRenderer` for Pattern clips
- `PatternClipRenderer` draws mini step grid
- Click on PatternClip opens StepSequencerWindow focused on that pattern

### 4.4 Persistence

- PatternClip serializes: base Clip + patternId + isLinked + gain + transpose + probability + laneOverrides
- Pattern data stored separately in StepSequencerModel (already serialized)

## 5. RT-Safety Rules

- No allocation in audio thread render path
- Pattern snapshot is immutable during audio processing
- Instance overrides are pre-computed on message thread
- StepSequencerPlaybackCore already follows RT-safety rules

## 6. Testing Strategy

### 6.1 Unit Tests
- PatternClip creation and serialization round-trip
- PatternClip type dispatch in ClipManager
- Instance override application

### 6.2 Integration Tests
- PatternClip in arrangement → playback → MIDI output
- Linked clips share pattern data
- Independent clips have separate pattern data
- Make Unique creates new pattern

### 6.3 Visual Tests
- PatternClip renders step grid in arrangement
- Playhead highlights current step in PatternClip
- Linked/independent icon displays correctly

## 7. Acceptance Criteria

- [ ] PatternClip can be created and placed in arrangement
- [ ] PatternClip renders as mini step grid
- [ ] PatternClip plays back via arrangement transport
- [ ] Linked clips share pattern data
- [ ] Make Unique creates independent copy
- [ ] Instance overrides (gain, transpose) work
- [ ] Click on PatternClip opens StepSequencerWindow
- [ ] PatternClip serializes/deserializes correctly
- [ ] All existing tests pass
- [ ] Debug + Release builds succeed

## 8. Non-Goals (Phase 2)

- Scene launching (Phase 4)
- Pattern clip splitting/gluing (Phase 3)
- Pattern clip automation (Phase 3)
- Performance recording (Phase 4)
- Controller mapping (Phase 4)

## 9. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| AudioEngine render path complexity | Medium | High | Start with MIDI-only render, add audio later |
| ArrangementView integration | Medium | Medium | Use existing ClipRenderCore delegation pattern |
| Pattern data synchronization | Low | Medium | Use existing snapshot pattern |
| Undo complexity | Medium | Medium | Use existing ArrangementUndoCore pattern |
