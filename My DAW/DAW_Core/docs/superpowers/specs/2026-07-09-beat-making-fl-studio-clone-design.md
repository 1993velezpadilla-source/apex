# Apex Beat-Making System — FL Studio Clone Design Spec

**Date:** 2026-07-09
**Status:** Approved — Ready for Implementation
**Goal:** Clone FL Studio's beat-making workflow into Apex so producers can make beats with the same speed and feel.

---

## Overview

This spec defines 5 new modules and 12 piano roll enhancements that clone FL Studio's beat-making workflow into Apex. The approach: copy FL Studio's exact Channel Rack, FPC, Browser, Pattern System, and Piano Roll tools.

## Scope

**In Scope (4 phases):**
1. StepSequencerCore — FL Studio Channel Rack clone
2. DrumSamplerCore — FL Studio FPC clone
3. SampleBrowserCore — FL Studio Browser clone
4. PatternManagerCore — FL Studio pattern system clone
5. Piano Roll Tools — 12 FL Studio piano roll tools

**Out of Scope:**
- Slicex (audio slicing) — Phase 2
- Playlist pattern clips — Phase 2
- Performance mode — Phase 2
- Gross Beat clone — Phase 2

---

## Module 1: StepSequencerCore

### Purpose
A vertical list of instrument channels, each with a horizontal step grid. The primary beat-making interface, cloned from FL Studio's Channel Rack.

### New Files

```
Source/StepSequencerCore/
├── StepSequencerComponent.h/.cpp          — Main composite component
├── StepSequencerChannelStrip.h/.cpp       — One row: controls + step grid + velocity lane
├── StepSequencerStepGrid.h/.cpp           — Clickable step buttons with beat grouping
├── StepSequencerVelocityLane.h/.cpp       — Per-step velocity bar editor
├── StepSequencerGraphEditor.h/.cpp        — Per-step parameter lanes (vel, pan, pitch, shift)
├── StepSequencerHeader.h/.cpp             — Pattern selector, swing, transport, add/remove
├── StepSequencerToolbar.h/.cpp            — Steps-per-beat, bars, display options
├── StepSequencerModel.h/.cpp              — Data model: patterns, channels, steps
├── StepSequencerPlaybackCore.h/.cpp       — Audio-thread step playback (note scheduling)
├── StepSequencerLookAndFeel.h/.cpp        — Visual styling (beat grouping colors)
└── StepSequencerTypes.h                   — Shared POD structs
```

### Data Model

```cpp
// StepSequencerTypes.h
struct StepEvent {
    bool     active       = false;
    uint8_t  velocity     = 100;    // 0-127
    int8_t   pitchOffset  = 0;      // semitones (-24 to +24)
    uint8_t  pan          = 64;     // 0-127 (64=center)
    int8_t   timingShift  = 0;      // ticks (-48 to +48)
    float    probability  = 1.0f;   // 0.0-1.0
};

struct ChannelData {
    String   name            = "Channel";
    float    volume          = 1.0f;   // 0.0-2.0
    float    pan             = 0.0f;   // -1.0 to 1.0
    bool     muted           = false;
    bool     soloed          = false;
    int      mixerTrackIndex = -1;     // -1=unassigned
    float    swingAmount     = 0.0f;   // 0.0-1.0 per-channel
    int      midiNote        = 60;     // root note
    int      midiChannel     = 0;      // 0-15
    juce::Colour color;
    Array<StepEvent> steps;            // length = totalSteps
    enum class SourceType { None, Sampler, VSTi } sourceType;
    int      vstInstanceId   = -1;
    String   sampleFilePath;
};

struct PatternData {
    String  name            = "Pattern 1";
    int     stepsPerBeat    = 4;       // 4,6,8,12,16,24,32
    int     beatsPerBar     = 4;
    int     barsPerPattern  = 1;       // 1-16
    float   globalSwing     = 0.0f;
    Array<ChannelData> channels;
    int getTotalSteps() const {
        return stepsPerBeat * beatsPerBar * barsPerPattern;
    }
};
```

### UI Layout

```
┌──────────────────────────────────────────────────────────────────┐
│ [Pattern: v Pattern 1]  [+] [Clone] [Swing: ====O=== 55%]      │
│ Steps/Beat: [4 v]  Bars: [1 v]  [Piano Roll] [Graph Editor]   │
├──────────────────────────────────────────────────────────────────┤
│  #  Name      Vol  Pan  M  S  ┌─Step Grid────────────────────┐  │
│  1  Kick      ==   O   []  []  | X X X X | X X _ X | X X X X |  │
│  2  Snare     ==   O   []  []  | X _ _ _ | X _ _ _ | X _ _ _ |  │
│  3  HiHat     ==   O   []  []  | X X X X | X X X X | X X X X |  │
│  4  Clap      ==   O   []  []  | _ _ _ _ | X _ _ _ | _ _ _ _ |  │
├──────────────────────────────────────────────────────────────────┤
│  [Velocity Lane - visible when Graph Editor is open]            │
│  |||| || || |||||| |||| |||| || ||||| |||| |||| |||| |||| ||||  │
│  [Pan Lane]    [Pitch Lane]    [Shift Lane]                    │
└──────────────────────────────────────────────────────────────────┘
```

### Interaction Behaviors

| Action | Behavior |
|--------|----------|
| Left-click step | Toggle on/off |
| Right-click step | Cycle velocity (6 levels: 32, 64, 80, 100, 115, 127) |
| Middle-click step | Mute step (grayed out, keeps state) |
| Click+drag across steps | Toggle multiple |
| Scroll wheel on step | Adjust probability (0-100%) |
| Double-click channel name | Inline rename |
| Drag sample onto channel | Load into sampler |
| Click channel color | Open color picker |
| M button | Mute/unmute channel |
| S button | Solo channel |
| Click pan knob | Drag left/right |
| Click volume fader | Drag up/down |
| Piano Roll button | Opens Piano Roll for this channel |
| Graph Editor toggle | Shows/hides parameter lanes |
| Add channel (+) | Adds new sampler channel |
| Remove channel | Right-click → Delete |
| Reorder channels | Drag up/down |

### Beat Grouping Visual
Steps grouped in beats with alternating brightness:
- Beat 1 (steps 1-4): Bright background
- Beat 2 (steps 5-8): Dim background
- Beat 3 (steps 9-12): Bright background
- Beat 4 (steps 13-16): Dim background
- Downbeat (step 1): Slightly brighter accent line

### Swing
- Swing delays offbeat steps (steps 2, 4 in 4-step/beat)
- Amount = percentage of step duration delay (0% = no delay, 100% = full step delay)
- Global swing × per-channel swing applied at playback time
- Not stored in step data — applied dynamically

### Playback Core
- Schedules note-on/off events from active pattern
- Lock-free snapshot from message thread
- Outputs MidiBuffer to drum sampler or VST
- Handles swing, probability, pitch offset, timing shift
- Tempo-synced to transport position

### Serialization
ValueTree save/load matching existing PianoRollClipModel pattern.

---

## Module 2: DrumSamplerCore

### Purpose
Loads audio files from disk and plays them via MIDI triggers. Each drum pad/channel gets one sampler instance. Cloned from FL Studio's Sampler + FPC.

### New Files

```
Source/DrumSamplerCore/
├── DrumSamplerEngine.h/.cpp             — Top-level: pad array, audio processing
├── DrumSamplerPad.h/.cpp                — Single pad: sample data + params
├── DrumSamplerVoice.h/.cpp              — Active voice: plays one trigger
├── DrumSamplerVoicePool.h/.cpp          — Voice allocator (lock-free)
├── DrumSamplerLookup.h/.cpp             — MIDI note → pad mapping
├── DrumSamplerLoadJob.h/.cpp            — Background file loading
├── DrumSamplerPreview.h/.cpp            — Preview playback
├── DrumSamplerTypes.h                   — Shared POD structs
└── DrumSamplerLookAndFeel.h/.cpp        — Pad grid styling
```

### Data Model

```cpp
struct VelocityLayer {
    float  velocityRange[2];  // {low, high} e.g. {0.0, 0.33}
    String filePath;
    AudioBuffer<float> audioData;
    int sampleRate;
    int64 numSamples;
};

struct DrumPadConfig {
    String   name          = "Pad";
    int      midiNote      = 36;       // C1 (standard drum mapping)
    float    gainDb        = 0.0f;     // -24 to +24
    float    pan           = 0.0f;     // -1.0 to 1.0
    float    tune          = 0.0f;     // semitones (-36 to +36)
    float    attackMs      = 0.1f;     // 0-10000ms
    float    decayMs       = 100.0f;
    float    sustainLevel  = 0.8f;     // 0-1
    float    releaseMs     = 50.0f;
    int      chokeGroup    = -1;       // -1=none, 0+=group
    bool     reverse       = false;
    int64    startSample   = 0;
    int64    endSample     = -1;       // -1=file end
    int      mixerTrack    = -1;
    juce::Colour color;
    int      outputNote    = 36;       // MIDI note this pad outputs
    Array<VelocityLayer> layers;       // up to 4 layers
};

struct DrumSamplerConfig {
    int  numPads        = 16;          // 4x4 default, up to 64
    int  maxPolyphony   = 16;         // voices per pad
    bool roundRobin     = false;
};
```

### Audio Processing (Audio Thread)
```
DrumSamplerEngine::processBlock(MidiBuffer&, AudioBuffer<float>&)
  for each MIDI event:
    if NoteOn:
      find pad by midiNote
      allocate voice from pool
      voice.start(pad, velocity, samplePosition)
      if chokeGroup >= 0: kill other voices in same group
    if NoteOff:
      trigger release phase on active voices

  for each active voice:
    voice.process(audioBuffer, numSamples)
    apply ADSR envelope
    apply gain, pan, tune
    apply interpolation (linear/cubic)
    if voice finished: release to pool
```

### Choke Groups
- Pads in same choke group cannot sound simultaneously
- When pad A triggers, all other pads in same group silenced immediately
- Standard: Open Hi-Hat and Closed Hi-Hat share choke group 0

### File Loading
- Background thread (non-blocking UI)
- JUCE AudioFormatReader: WAV, MP3, FLAC, OGG, AIFF
- Memory-mapped for files > 10MB

---

## Module 3: SampleBrowserCore

### Purpose
Integrated file browser for finding/loading audio samples. Cloned from FL Studio's Browser.

### New Files

```
Source/SampleBrowserCore/
├── SampleBrowserComponent.h/.cpp         — Main panel (tree + list + preview)
├── SampleBrowserTreeModel.h/.cpp         — TreeView for folders
├── SampleBrowserListModel.h/.cpp         — ListBox for files
├── SampleBrowserPreviewPlayer.h/.cpp     — Click-to-audition
├── SampleBrowserSearchBar.h/.cpp         — Search/filter
├── SampleBrowserDragSource.h/.cpp        — Drag-drop to Channel Rack/FPC
├── SampleBrowserCore.h/.cpp              — Data model
├── SampleBrowserScanner.h/.cpp           — Background directory scanner
└── SampleBrowserTypes.h
```

### Key Behaviors
- Tree view: folder hierarchy from configured search paths
- File list: shows audio files with Name, Type, Size, Duration
- Click file = select + auto-preview
- Double-click = load into selected channel
- Drag file onto Channel Rack = load into that channel
- Drag file onto FPC pad = load into that pad
- Search: instant filter across all paths
- Categories: Drums, Bass, Synth, Vocals, Loops, FX
- Favorites: starred folders at top

---

## Module 4: PatternManagerCore

### Purpose
Manages multiple patterns, each with independent channel/step data. Cloned from FL Studio's pattern system.

### New Files

```
Source/PatternManagerCore/
├── PatternManagerCore.h/.cpp             — CRUD operations
├── PatternManagerUI.h/.cpp               — Pattern selector dropdown
└── PatternManagerTypes.h
```

### Key Behaviors
- Pattern selector: dropdown + left/right arrows
- Operations: New, Clone, Delete, Rename, Color
- Each pattern independently sets bars and steps per beat
- Pattern clips in arrangement reference patterns by index
- New project starts with "Pattern 1" (1 bar, 4 steps/beat)

---

## Module 5: Piano Roll Tools

### Purpose
12 FL Studio piano roll tools added to existing PianoRollComponent.

### New Files

```
Source/MidiCore/PianoRollTools/
├── PianoRollStrumTool.h/.cpp
├── PianoRollFlamTool.h/.cpp
├── PianoRollChopTool.h/.cpp
├── PianoRollRandomizeTool.h/.cpp
├── PianoRollArpeggiatorTool.h/.cpp
├── PianoRollChordStampTool.h/.cpp
├── PianoRollScaleStampTool.h/.cpp
├── PianoRollLegatoTool.h/.cpp
├── PianoRollGlueTool.h/.cpp
├── PianoRollSlipTool.h/.cpp
├── PianoRollNoteColorTool.h/.cpp
├── PianoRollVelocityCurveTool.h/.cpp
└── PianoRollToolDialogBase.h/.cpp
```

### Tool Specs

1. **Strum**: Stagger start times + velocity ramp. Dialog: amount, time, curve, direction.
2. **Flam**: Grace note before each note. Dialog: offset, velocity, pitch.
3. **Chop**: Split notes into N pieces. Dialog: count, mode (even/fade/random/alternate).
4. **Randomize**: Random velocity/pan/pitch/timing. Dialog: amounts, seed.
5. **Arpeggiator**: Convert chord to sequence. Dialog: direction, steps, range, gate, speed.
6. **Chord Stamp**: Click chord type → click piano roll → place chord.
7. **Scale Stamp**: Paint scale notes at cursor.
8. **Legato**: Extend notes to fill gaps (no dialog).
9. **Glue**: Merge adjacent notes (no dialog).
10. **Slip**: Slide content within note boundaries (no dialog).
11. **Note Colors**: 16 color groups = MIDI channels.
12. **Velocity Curve**: Apply curve across selected notes.

---

## Integration Points

- **Step Sequencer ↔ Drum Sampler**: StepSequencerPlaybackCore → MidiBuffer → DrumSamplerEngine
- **Step Sequencer ↔ Piano Roll**: Same PianoRollClipModel, two views
- **Sample Browser ↔ Drum Sampler**: Drag-drop loads sample into pad
- **Pattern Manager ↔ Arrangement**: Pattern clips reference patterns by index

---

## Phase Plan

| Phase | Modules | Est. Lines |
|-------|---------|-----------|
| Phase 1 (MVP) | StepSequencerCore, DrumSamplerCore (basic), PatternManagerCore (basic) | ~4,500 |
| Phase 2 (Complete) | SampleBrowserCore, DrumSamplerCore advanced, StepSeq advanced | ~4,000 |
| Phase 3 (Piano Roll) | All 12 Piano Roll tools | ~3,000 |
| Phase 4 (Advanced) | Slicex, Playlist pattern clips, Performance mode | TBD |
| **Total** | | **~11,500+** |

---

## File Count

| Module | Files |
|--------|-------|
| StepSequencerCore | 11 |
| DrumSamplerCore | 9 |
| SampleBrowserCore | 9 |
| PatternManagerCore | 3 |
| Piano Roll Tools | 14 |
| **Total** | **46** |
