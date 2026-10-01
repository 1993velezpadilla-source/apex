# APEX Beat-Making Core — Design Spec

**Date:** 2026-07-19
**Status:** Approved
**Scope:** Phase 1 — Core Step Grid, Piano Roll, Enhanced Playback

---

## 1. Objective

Build an FL Studio-inspired beat-making workflow for APEX with:

- Channel Rack as primary step sequencer view
- Piano Roll as alternate view over same event data
- Enhanced step attributes (duration, ratchets, flam, ties, conditions)
- Deterministic probability and groove
- Shared event model (no conversion loss between views)

## 2. Architecture

### 2.1 Data Model

```
Pattern
  ├─ id: StablePatternId
  ├─ name: String
  ├─ length: MusicalDuration (total steps)
  ├─ stepsPerBeat: int (4 = 1/16 grid)
  ├─ beatsPerBar: int
  ├─ globalSwing: float [0..1]
  ├─ grooveTemplate: GrooveTemplateId
  └─ lanes: EventLane[]
       ├─ id: StableLaneId
       ├─ target: MusicalTargetId (pad/note/channel)
       ├─ name: String
       ├─ color: Colour
       ├─ volume: float
       ├─ pan: float
       ├─ muted: bool
       ├─ soloed: bool
       ├─ swingAmount: float (per-lane override)
       ├─ laneLength: int (for polymeter, 0 = use pattern length)
       └─ events: StepEvent[]
```

### 2.2 StepEvent Schema

```cpp
struct StepEvent {
    StableEventId   id;              // Unique, persistent across undo
    MusicalPosition start;           // In ticks (960 PPQ)
    MusicalDuration duration;        // In ticks (0 = grid length)
    float           velocity;        // [0..1]
    float           pan;             // [-1..1]
    float           probability;     // [0..1], 1 = always
    int8_t          pitchOffset;     // Semitones from lane root

    // Ratchet
    uint8_t         ratchetCount;    // 1 = no ratchet, max 16
    uint8_t         ratchetSpacing;  // 0 = even, 1 =加速, 2 =减速

    // Flam
    bool            flamEnabled;
    int16_t         flamOffsetTicks; // Negative = before beat

    // Tie
    bool            tieToNext;       // Extend note, no retrigger

    // Condition (FL Studio A:B style)
    enum class Condition : uint8_t {
        Every = 0,
        A, B, AB,              // A:B ratio
        Fill, NotFill,         // Fill mode
        PrevActive, PrevInactive,
        First, Last,
        Random4, Random8, Random16
    };
    Condition       condition;

    // Microtiming
    int16_t         microtimingTicks; // Offset from grid position

    // Swing bypass
    bool            swingBypass;     // If true, ignore global swing

    // Playback state (not serialized)
    mutable float   currentProbabilityRoll; // For deterministic check
};
```

### 2.3 PatternClip (Phase 2 placeholder)

```cpp
struct PatternClip {
    StableClipId    id;
    StablePatternId patternId;
    MusicalPosition start;
    MusicalDuration length;
    bool            isLinked;       // true = linked, false = independent
    float           gain;
    float           transpose;
    // Instance overrides (Phase 2)
};
```

## 3. Component Architecture

### 3.1 StepSequencerComponent (Channel Rack)

**Layout (FL Studio style):**

```
┌─────────────────────────────────────────────────────────┐
│  [Pattern: Pattern 1 ▼]  [Steps: 16 ▼] [Swing: 0%]    │
├──────────┬──────┬──────┬──────┬──────┬──────┬──────────┤
│ Channel  │  1   │  2   │  3   │  4   │ ...  │  16      │
├──────────┼──────┼──────┼──────┼──────┼──────┼──────────┤
│ Kick     │ ■■■■ │ ■■■■ │ ■■■■ │ ■■■■ │      │          │
│ Snare    │      │ ■■   │      │ ■■   │      │          │
│ HiHat    │ ■ ■ ■│ ■ ■ ■│ ■ ■ ■│ ■ ■ ■│      │          │
│ Clap     │      │      │ ■    │      │      │          │
│ ...      │      │      │      │      │      │          │
└──────────┴──────┴──────┴──────┴──────┴──────┴──────────┘
```

**Interactions:**
- Click step = toggle on/off
- Right-click step = delete
- Shift+click = set velocity (vertical drag)
- Ctrl+click = set probability
- Scroll wheel on step = microtiming offset
- Double-click channel name = rename
- Drag channel = reorder
- Right-click channel = context menu (Piano Roll, Color, Delete, Clone)

### 3.2 PianoRollComponent (shared events)

**Layout:**

```
┌─────────────────────────────────────────────────────────┐
│  [Channel: Kick ▼]  [Grid: 1/16 ▼]  [Snap: ▼]         │
├──────────┬──────────────────────────────────────────────┤
│   C5     │                                              │
│   B4     │                                              │
│   A4     │                                              │
│   G4     │                                              │
│   F4     │                                              │
│   E4     │                                              │
│   D4     │                                              │
│   C4     │  ████████                                    │
│   B3     │                                              │
│   A3     │                                              │
│   ...    │                                              │
├──────────┴──────────────────────────────────────────────┤
│ Velocity: ▐▌▐▌▐▌▐▌▐▌▐▌▐▌▐▌                             │
│ Note Off: ──────────────────────                        │
└─────────────────────────────────────────────────────────┘
```

**Key Rule:** Piano Roll reads/writes same `StepEvent[]` as step grid. No conversion.

### 3.3 StepSequencerPlaybackCore (enhanced)

**New capabilities:**
1. Ratchet expansion (bounded, deterministic)
2. Flam offset (note-on before/after beat)
3. Tie handling (extend duration, no retrigger)
4. Condition evaluation (A:B, Fill, Prev, Random)
5. Deterministic probability (seeded RNG)
6. Per-lane swing
7. Per-lane length (polymeter)

**Deterministic Seed Model:**

```
seed = hash(projectId, patternId, laneId, stepIndex, loopIteration)
```

Same seed → same probability roll → identical realtime/offline output.

## 4. File Structure

```
Source/
  StepSequencerCore/
    StepSequencerTypes.h          (enhanced StepEvent, PatternData, LaneData)
    StepSequencerModel.h/.cpp     (enhanced with lanes, polymeter)
    StepSequencerPlaybackCore.h/.cpp (enhanced with ratchets, flam, ties, conditions)
    StepSequencerComponent.h/.cpp (Channel Rack UI)
    StepSequencerChannelStrip.h/.cpp (channel row in rack)
    StepSequencerHeader.h/.cpp    (step numbers, bar markers)
    StepSequencerStepGrid.h/.cpp  (grid rendering, hit testing)
    StepSequencerLookAndFeel.h/.cpp (FL-style colors, fonts)
    StepSequencerWindow.h/.cpp    (floating window wrapper)
    PianoRollComponent.h/.cpp     (NEW — piano roll editor)
    PianoRollNoteComponent.h/.cpp (NEW — individual note rendering)
    PianoRollVelocityLane.h/.cpp  (NEW — velocity editor below piano roll)
    GrooveTemplate.h/.cpp         (NEW — swing/groove presets)
    StepSequencerTypes.h          (moved to top)
```

## 5. Integration Points

### 5.1 DrumSamplerEngine

- Receives MIDI from `StepSequencerPlaybackCore`
- Voice pool handles choke groups (already has `chokeGroup` in config)
- Velocity layers (already has `VelocityLayer` struct)
- No changes needed in Phase 1

### 5.2 PatternManagerCore

- Already supports create/delete/clone/rename
- Add: color, favorite, category tags
- Add: linked clip tracking

### 5.3 Transport

- `StepSequencerPlaybackCore::processBlock()` called from main audio loop
- Receives current sample position, tempo, snapshot
- Outputs MIDI buffer

### 5.4 Undo

- All model mutations go through `UndoManager`
- Step toggle, velocity change, rename = separate undo actions
- Pattern clone = single undo action

## 6. RT-Safety Rules

- No allocation in `processBlock()`
- Snapshot is immutable during audio processing
- Probability RNG is deterministic and seeded (no `std::random_device`)
- Ratchet expansion bounded by `maxRatchetCount` (16)
- No UI calls from audio thread
- No file I/O from audio thread

## 7. Persistence

- Patterns stored as `ValueTree` in project
- Stable IDs (`StablePatternId`, `StableLaneId`, `StableEventId`) cross save/load
- Pattern clips reference pattern by ID, not pointer
- Groove templates stored as named presets

## 8. Testing Strategy

### 8.1 Unit Tests

- StepEvent serialization round-trip
- Ratchet expansion produces correct sub-event count and timing
- Flam offset applied correctly
- Tie suppresses note-off
- Condition evaluation matches FL Studio A:B behavior
- Deterministic seed produces identical output across block sizes
- Per-lane polymeter renders correctly

### 8.2 Integration Tests

- Step grid edit → snapshot → processBlock → MIDI output
- Piano roll edit → same snapshot → same MIDI output
- Pattern clone → independent events
- Mute/solo correctly suppresses MIDI

### 8.3 Visual Tests

- Channel Rack renders correct step count
- Active steps show correct velocity color
- Piano roll notes align with grid
- Swing offset visible in step positions

## 9. Acceptance Criteria

- [ ] Step grid renders FL-style Channel Rack layout
- [ ] Click toggles step on/off
- [ ] Right-click deletes step
- [ ] Velocity adjustable via shift+drag
- [ ] Piano roll opens for any channel (right-click → Piano Roll)
- [ ] Piano roll shows same events as step grid
- [ ] Editing in piano roll updates step grid
- [ ] Ratchets produce correct sub-events
- [ ] Flam offsets note-on by user-specified ticks
- [ ] Ties extend note without retrigger
- [ ] Conditions evaluate correctly (A:B, Fill, Random)
- [ ] Probability is deterministic across block sizes
- [ ] Swing applies correctly (global + per-lane)
- [ ] Per-lane polymeter works (e.g., 15 steps in lane 1, 16 in lane 2)
- [ ] All tests pass
- [ ] Build succeeds (Debug + Release)

## 10. Non-Goals (Phase 1)

- Pattern clips in playlist (Phase 2)
- Scene launching (Phase 4)
- Sampler choke/velocity layers (Phase 3, already in config)
- Browser/preview (Phase 3)
- Beat FX (Phase 3)
- Performance recording (Phase 4)
- MIDI learn for step parameters
- Controller mapping

## 11. Primary Sources

- FL Studio Manual: Channel Rack, Step Sequencer, Piano Roll
- DAW Brain §35: Pattern Sequencing & Beat Making
- Existing APEX implementation: StepSequencerCore, DrumSamplerCore

## 12. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Piano roll complexity | Medium | High | Start with minimal note display, iterate |
| Polymeter edge cases | Medium | Medium | Test with 12, 15, 16, 32 step lanes |
| Deterministic seed collisions | Low | High | Use full hash, test with 10k iterations |
| Performance regression | Low | Medium | Profile ratchet expansion, bound count |
| Undo complexity | Medium | Medium | Use JUCE ValueTree undo integration |
