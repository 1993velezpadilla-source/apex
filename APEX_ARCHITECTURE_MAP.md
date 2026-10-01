# APEX Architecture Map

**Last verified:** 2026-07-24

## Source Structure

Root: `My DAW/DAW_Core/Source/` — 97 modules

## Core Audio Path

```
Audio Device (JUCE AudioDeviceManager)
  → MainComponent::getNextAudioBlock()
    → AudioEngine::process()
      → RoutingSnapshot (immutable, lock-free)
        → ApexSoundEngineNucleus (render nodes in topo order)
          → Track nodes → Bus nodes → Master node
            → MonitorEngine (control room)
              → Hardware output
```

## Key Subsystems

| Subsystem | Primary File | Class | Responsibility |
|---|---|---|---|
| Application | `AppCore/ApplicationCore.h` | `ApplicationCore` | Orchestrates all subsystems, owns state |
| Audio Engine | `AudioEngineCore/AudioEngine.h` | `AudioEngine` | Real-time audio processor, routing-graph order |
| Sound Engine Nucleus | `SoundEngineCore/ApexSoundEngineNucleus.h` | `ApexSoundEngineCore` | Node rendering, mix fanout, PDC |
| Transport | `TransportCore/TransportController.h` | `TransportController` | Play/stop/record/loop, atomic RT-safe |
| Recording | `RecordingCore/RecordingEngine.h` | `RecordingEngine` | Multitrack recording, disk writer, lifecycle |
| Routing | `RoutingCore/RoutingSnapshot.h` | `RoutingSnapshot` | Immutable graph snapshot, zero-lock reads |
| Routing Graph | `RoutingCore/RoutingGraph.h` | `RoutingGraph` | Mutable graph, message-thread publisher |
| Tracks | `TrackCore/Track.h` | `Track` | Track state, volume, pan, mute, solo, arm |
| Clips | `ClipCore/Clip.h` | `Clip` | Audio clip state |
| Pattern Clips | `ClipCore/PatternClip.h` | `PatternClip` | Step-sequencer pattern on timeline |
| Plugin Host | `PluginHostCore/PluginChainCore.h` | `PluginChainCore` | Per-track plugin chain |
| Plugin Playhead | `PluginHostCore/PluginPlayheadInfoCore.h` | `PluginPlayheadInfoCore` | Plugin transport snapshot |
| PDC | `PDCCore/DelayCompensationCore.h` | Delay compensation | Plugin latency compensation |
| Monitoring | `MonitorCore/MonitorEngine.h` | `MonitorEngine` | Control room: dim/mute/mono/speaker trim |
| Input Monitoring | `InputMonitorCore/LiveInputMonitorEngine.h` | `LiveInputMonitorEngine` | Live input routing to tracks |
| Master | `MasterCore/MasterBusEngine.h` | `MasterBusEngine` | Master bus processing |
| Automation | `Automation/AutomationSystemCore.h` | `AutomationSystem` | Global automation clock + evaluator |
| Automation Manager | `AutomationCore/AutomationManagerCore.h` | `AutomationManagerCore` | Per-track automation lanes, snapshot |
| Step Sequencer | `StepSequencerCore/StepSequencerModel.h` | `StepSequencerModel` | Pattern model, channels, lanes |
| Step Playback | `StepSequencerCore/StepSequencerPlaybackCore.h` | `StepSequencerPlaybackCore` | RT-safe MIDI generation |
| Piano Roll | `MidiCore/PianoRollComponent.h` | `PianoRollComponent` | Note editor UI |
| MIDI Playback | `MidiCore/PianoRollPlaybackCore.h` | `PianoRollPlaybackCore` | MIDI clip playback |
| Project | `ProjectCore/ProjectManager.h` | `ProjectManager` | Save/load, autosave, crash recovery |
| Export | `RenderCore/ExportRenderCore.h` | `ExportRenderCore` | Offline render thread |
| Crash Handler | `Main.cpp:26` | `ApexCrashHandler` | MiniDump writer |
| Bus Routing | `FolderBusCore/FolderBusStateModel.h` | `FolderBusStateModel` | Folder bus mute/solo snapshot |
| Metering | `MeteringCore/MeteringFacadeCore.h` | `MeteringFacadeCore` | Peak metering |

## Data Flow Pattern

1. **Message thread** builds `RoutingSnapshot`, `AutomationSnapshot`, `FolderBusSnapshot`
2. **Snapshots published** atomically via `RoutingSnapshotPublisher::get()`
3. **Audio thread** reads snapshots with zero locks
4. **Audio thread** advances transport, processes in topo order
5. **Transport fade** gates master output for click-free start/stop

## Thread Model

- **Message thread:** UI, graph mutation, snapshot publication
- **Audio thread:** `AudioEngine::process()`, `RecordingEngine::processBlock()`
- **Disk thread:** `RecordingDiskWriterCore` (TimeSliceThread)
- **Export thread:** `ExportRenderCore` (Thread subclass)
- **Plugin scanner:** Subprocess worker process
