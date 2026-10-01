# APEX Capability Registry

**Last verified:** 2026-07-24
**Brain SHA-256:** ce92abd24f6ed246004047a374cbf791dedc8fe5b2c2ee98d7154cfa6bc9a7ca

## Status Legend

- `VERIFIED_PRODUCTION` — Evidence confirms production use
- `PRODUCTION_CANDIDATE` — Implemented, compiles, some evidence
- `PARTIAL` — Implemented but incomplete or unverified
- `EXPERIMENTAL` — Early-stage or recently added
- `SCAFFOLDED` — Structure exists but not functional
- `NOT_IMPLEMENTED` — No code found
- `UNKNOWN` — Cannot determine from available evidence

---

### CAP-001 Audio Device Callback
- **Status:** VERIFIED_PRODUCTION
- **Code:** `MainComponent.h:81` (`juce::AudioIODeviceCallback`), `MainComponent::getNextAudioBlock()`
- **Test:** Smoke tests confirm callback registration
- **Evidence:** AudioEngine::process() runs on audio thread, transport fade, routing snapshot
- **Risks:** None identified

### CAP-002 ApplicationCore Orchestration
- **Status:** VERIFIED_PRODUCTION
- **Code:** `AppCore/ApplicationCore.h:57` — owns TransportController, TrackManager, ClipManager, RoutingGraph, AudioEngine, RecordingEngine, etc.
- **Test:** Integration via MainComponent
- **Evidence:** All subsystems wired in ApplicationCore constructor

### CAP-003 AudioEngine Processing
- **Status:** VERIFIED_PRODUCTION
- **Code:** `AudioEngineCore/AudioEngine.h:117` — `process()` at line 403
- **Test:** Smoke tests, callback audit
- **Evidence:** Full routing-graph processing, transport fade, plugin chain, metering
- **Risks:** None identified

### CAP-004 Routing Graph + Snapshot
- **Status:** VERIFIED_PRODUCTION
- **Code:** `RoutingCore/RoutingGraph.h`, `RoutingSnapshot.h` — immutable snapshot, zero-lock reads
- **Test:** Routing snapshot validation in AudioEngine
- **Evidence:** Topological sort, edge snapshots, sidechain PDC lines

### CAP-005 Transport Control
- **Status:** VERIFIED_PRODUCTION
- **Code:** `TransportCore/TransportController.h:10` — play/stop/record/loop/pause, atomic RT-safe
- **Test:** Transport state machine in AudioEngine
- **Evidence:** `applyPlayRT()`, `applyStopRecordingRT()` — lock-free audio-thread methods

### CAP-006 Recording (Multitrack)
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `RecordingCore/RecordingEngine.h:30` — unlimited-length, crash-safe multitrack
- **Test:** `Recording/RecordingWriterIntegrityTests.cpp`, `RecordingIdentityRegressionTests.cpp`
- **Evidence:** Disk writer, lifecycle state machine, WAV finalization, undo integration
- **Risks:** Dry-only capture mode; wet recording path diagnostic counters suggest ongoing investigation

### CAP-007 Live Input Monitoring
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `InputMonitorCore/LiveInputMonitorEngine.h`, `TrackInputProcessorCore.h`
- **Test:** None found
- **Evidence:** Live input buffer passed to AudioEngine, per-track monitor gain smoothing
- **Risks:** Monitor jump diagnostics suggest click/pop investigation ongoing

### CAP-008 Plugin Hosting
- **Status:** VERIFIED_PRODUCTION
- **Code:** `PluginHostCore/PluginChainCore.h`, `PluginInstanceCore.h`, `PluginScannerCore.h`
- **Test:** Plugin scan worker subprocess, MessageBox suppressor
- **Evidence:** Per-track plugin chains, plugin playhead info, clip-region plugins, Win32 editor patch

### CAP-009 Plugin Latency / PDC
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `PDCCore/DelayCompensationCore.h`, `PluginLatencyCore.h`, `MasterPdcCore.h`
- **Test:** None found
- **Evidence:** PDC delay lines for sidechain edges, master PDC, pre-allocated scratch buffers
- **Risks:** No dedicated PDC tests

### CAP-010 Step Sequencer
- **Status:** VERIFIED_PRODUCTION
- **Code:** `StepSequencerCore/StepSequencerModel.h`, `StepSequencerPlaybackCore.h` — 19 files
- **Test:** `StepSequencerTests/` — 7 test files (DeterministicTests, PatternClipTests, PlaybackTests, etc.)
- **Evidence:** Ratchet, flam, tie, probability, swing, polymeter, deterministic seeding

### CAP-011 PatternClip (Timeline Integration)
- **Status:** VERIFIED_PRODUCTION
- **Code:** `ClipCore/PatternClip.h` — links to StepSequencerModel via StablePatternId
- **Test:** `StepSequencerTests/PatternClipTests.cpp`
- **Evidence:** Gain, transpose, probability, ValueTree persistence

### CAP-012 Piano Roll
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `MidiCore/PianoRollComponent.h`, `PianoRollPlaybackCore.h` — 15+ files
- **Test:** `StepSequencerTests/PianoRollTests.cpp`
- **Evidence:** Note rendering, grid, keyboard, toolbar, selection, ruler, scale helper
- **Risks:** UI-heavy; playback integration verified via tests

### CAP-013 MIDI Processing
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `MidiCore/MidiInputCore.h`, `MidiClip.h`, `MidiTrackCore.h`, `VirtualMidiKeyboardCore.h`
- **Test:** PianoRollTests
- **Evidence:** MIDI input, MIDI clip model, virtual keyboard

### CAP-014 Automation System
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `Automation/AutomationSystemCore.h` — global clock + evaluator, `AutomationCore/AutomationManagerCore.h` — per-track lanes
- **Test:** None found
- **Evidence:** Audio-thread integration in AudioEngine::process(), read/write/touch/latch modes, snapshot publisher
- **Risks:** No automation-specific tests

### CAP-015 Offline Export / Rendering
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `RenderCore/ExportRenderCore.h` — dedicated export thread
- **Test:** None found
- **Evidence:** Async completion, progress, cancel, shutdown safety, temp file path
- **Risks:** No export-specific tests

### CAP-016 Project Persistence
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `ProjectCore/ProjectManager.h`, `AutosaveManagerCore.h`, `CrashRecoveryCore.h`, `RecoverySessionCore.h`
- **Test:** None found
- **Evidence:** Project save/load, autosave, crash recovery, session recovery
- **Risks:** No persistence-specific tests

### CAP-017 Crash Recovery
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `Main.cpp:26` — `ApexCrashHandler` (MiniDump), `ProjectCore/CrashRecoveryCore.h`
- **Test:** None found
- **Evidence:** Minidump writer, crash summary text, recovery session
- **Risks:** No crash recovery tests

### CAP-018 Bus Routing / Folder Buses
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `FolderBusCore/FolderBusStateModel.h`, `FolderBusMuteCore.h`, `FolderBusSoloCore.h`
- **Test:** None found
- **Evidence:** Pre-baked mute/solo snapshot read by audio thread
- **Risks:** No bus routing tests

### CAP-019 Control Room / Monitoring
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `MonitorCore/MonitorEngine.h` — dim/mute/mono/speaker trim, `ControlRoomEngine.h`
- **Test:** None found
- **Evidence:** Signal flow: render tap → FX → DimMuteMono → SpeakerTrim → meter → HW output
- **Risks:** Monitor FX bypassed in v1

### CAP-020 Mixer
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `MixerCore/`, `MixerScaleCore/FaderRangeCore.h`, `ChannelStripCore/`, `MasterStripCore/`
- **Test:** None found
- **Evidence:** MixerPanel, MixerWindow, fader range, channel strips

### CAP-021 Time/Pitch DSP
- **Status:** PARTIAL
- **Code:** `Builds/VisualStudio2026/ArrangementEditor/TimePitchDSPCore.h`, `ClipIndependentPitchCore.h`, `PitchSmootherCore.h`
- **Test:** None found
- **Evidence:** Per-clip DSP modes 1-6, forensic audit counters for path selection
- **Risks:** Forensic audit suggests independent pitch path may not be executing correctly

### CAP-022 Tape Stop
- **Status:** EXPERIMENTAL
- **Code:** `TapeStop/TapeStopProcessorCore.h` — per-track and per-clip tape stop
- **Test:** None found
- **Evidence:** Integrated in AudioEngine, automation-driven
- **Risks:** No tests

### CAP-023 Vocal Tune
- **Status:** PARTIAL
- **Code:** `VocalTuneCore/ApexTuneIntegrationCore.h`
- **Test:** None found
- **Evidence:** Integration core exists, background job queue
- **Risks:** Adapter pattern suggests external dependency

### CAP-024 Drum Sampler
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `DrumSamplerCore/DrumSamplerEngine.h`
- **Test:** None found
- **Evidence:** Wired into MainComponent, step sequencer integration

### CAP-025 Bubblegum UI System
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `Bubblegum/`, `BubblegumCable/`, `BubblegumFloatingPanelCore/`, `BubblegumKnobCore/` — 7+ modules
- **Test:** None found
- **Evidence:** Cable overlay, orb component, taskbar, merge overlay, V2 panel

### CAP-026 Undo System
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `UndoCore/`, `CommandCore/CommandManager.h`
- **Test:** `Arrangement/BladeUndoPersistenceTests.cpp`
- **Evidence:** Command pattern, undo history panel, recording take undo integration

### CAP-027 Metering
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `MeteringCore/MeteringFacadeCore.h`, `TrackPeakMeterManagerCore.h`
- **Test:** None found
- **Evidence:** Master peak, per-track peaks, live record waveform peaks

### CAP-028 Click / Metronome
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `ClickCore/ClickEngineCore.h`, `ClickStateModel.h`
- **Test:** None found
- **Evidence:** Click engine, state model

### CAP-029 Gamepad Input
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `GamepadCore/GamepadManager.h`, `GamepadMapper.h`
- **Test:** None found
- **Evidence:** Gamepad integration in ApplicationCore

### CAP-030 Keyboard Shortcuts
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `KeyBindingCore/KeyBindingManager.h`, `ShortcutCore/`
- **Test:** None found
- **Evidence:** Key binding manager, shortcut help window

### CAP-031 Markers
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `MarkerCore/MarkerManager.h`
- **Test:** None found
- **Evidence:** Marker manager, marker bubble

### CAP-032 Selection System
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `SelectionCore/MultiSelectionCore.h`, `SelectionBulkActionCore.h`
- **Test:** None found
- **Evidence:** Multi-selection, bulk action

### CAP-033 Theme System
- **Status:** SCAFFOLDED
- **Code:** `ThemeCore/`
- **Test:** None found
- **Evidence:** Module exists but minimal verification

### CAP-034 Plugin Security / Safety
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `PluginSafetyCore/`, `PluginSecurityCore/`, `PluginStorageCore/`
- **Test:** None found
- **Evidence:** MessageBox suppressor, host build trust mode, plugin storage

### CAP-035 Diagnostics / Forensic Audit
- **Status:** PRODUCTION_CANDIDATE
- **Code:** `DiagnosticsCore/CallbackAuditCore.h`, `UICore/ForensicAuditWindow.h`
- **Test:** `Diagnostics/CallbackAuditCoreTests.cpp`
- **Evidence:** Pitch path counters, callback audit, diagnostic logging
