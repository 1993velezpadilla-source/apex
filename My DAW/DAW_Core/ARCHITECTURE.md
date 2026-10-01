# DAW Core Architecture Documentation

## Overview
This document describes the modular, nucleus-based architecture of the professional DAW application.

## Design Philosophy

### Core Principles
1. **Extreme Modularity**: Every major feature is isolated into its own nucleus/module
2. **Separation of Concerns**: UI, logic, and data are strictly separated
3. **Single Responsibility**: Each module has one clear purpose
4. **Easy Debugging**: Bugs can be isolated without touching unrelated code
5. **Future-Proof**: Architecture supports adding features without major refactoring
6. **Maintainability**: Clean interfaces, minimal coupling, no god objects

## Project Structure

```
/Source
    /AppCore                    - Application lifecycle and coordination
    /AudioEngineCore            - Real-time audio processor, track mixing, metering
    /AudioDeviceCore            - Device management (ASIO/WASAPI) [placeholder]
    /TransportCore              - Transport state and control
    /ProjectCore                - Project save/load/autosave/backup
    /TimelineCore               - Timeline state, zoom, scroll [placeholder]
    /ClipCore                   - Clip data models (Audio, MIDI)
    /ClipEditorCore             - Clip editing windows/panels [placeholder]
    /TrackCore                  - Track data models (universal roles, atomic peak metering)
    /MixerCore                  - Mixer layout and management [placeholder]
    /ChannelStripCore           - LevelMeter (VU with peak hold)
    /RoutingCore                - Routing graph, nodes, connections (DAG)
    /SidechainCore              - Sidechain routing [placeholder]
    /PluginHostCore             - VST3 hosting [placeholder]
    /WindowCore                 - Window/panel management [placeholder]
    /CommandCore                - Undo/redo command stack
    /StateCore                  - Central observable state
    /UICore                     - Main UI layout
    /ControlsCore               - Reusable UI controls
    /ThemeCore                  - Visual style system
    /UtilityCore                - Helpers, types, utilities
```

## Module Dependencies

### Dependency Graph (Top to Bottom)

```
Layer 0 (Foundation):
- UtilityCore (no dependencies)
- ThemeCore (no dependencies)

Layer 1 (Core State & Actions):
- StateCore (depends on: UtilityCore)
- ActionCore (depends on: nothing) — canonical ActionID + ActionManager
- CommandCore (depends on: ActionCore) — undo/redo stack, self-wires to EditUndo/EditRedo

Layer 2 (Data Models):
- TrackCore (depends on: UtilityCore, StateCore)
- ClipCore (depends on: UtilityCore, StateCore)
- TransportCore (depends on: UtilityCore, StateCore)
- MarkerCore (depends on: UtilityCore)

Layer 3 (Controllers & Logic):
- AudioDeviceCore (depends on: StateCore) [placeholder]
- RoutingCore (depends on: UtilityCore) — DAG graph with cycle detection, topo sort
- SidechainCore (depends on: RoutingCore) [placeholder]
- PluginHostCore (depends on: UtilityCore) [placeholder]
- ProjectCore (depends on: TrackCore, ClipCore, TransportCore, MarkerCore, RoutingCore)
- KeyBindingCore (depends on: ActionCore) — profile system, context-aware dispatch

Layer 4 (Audio Engine):
- AudioEngineCore (depends on: TrackCore, ClipCore, RoutingCore, TransportCore) [placeholder]

Layer 5 (UI Components):
- ControlsCore (depends on: ThemeCore, UtilityCore)
- ChannelStripCore (depends on: TrackCore, ControlsCore, ThemeCore) [placeholder]
- MixerCore (depends on: TrackCore, ChannelStripCore) [placeholder]
- TimelineCore (depends on: ClipCore, TrackCore, StateCore, ThemeCore) [placeholder]
- ClipEditorCore (depends on: ClipCore, ControlsCore) [placeholder]

Layer 6 (Application UI & Bubbles):
- UICore (depends on: All UI modules, TrackCore, ClipCore, TransportCore, MarkerCore)
- FloatingWindowCore (depends on: ThemeCore)
- BubbleCore (depends on: ThemeCore, MarkerCore, ActionCore)

Layer 7 (Interaction):
- GamepadCore (depends on: ActionCore) — XInput polling + ActionManager dispatch
- GestureCore (no dependencies) — touch gesture recognition
- InteractionModeCore (no dependencies) — Touch/Hybrid/Desktop detection
- CursorCore (depends on: InteractionModeCore)

Layer 8 (Application):
- AppCore (depends on: All modules, orchestrates everything, listens to TrackManager)
```

## Avoiding Circular Dependencies

### Rules:
1. Lower layers never depend on higher layers
2. Use interfaces for cross-layer communication when needed
3. Use observer pattern (listeners) for upward communication
4. StateCore provides shared observable state without coupling

### Example: Clip Double-Click Flow
```
ClipViewComponent (UI)
    → ClipInteractionController (Controller)
        → ClipEditorCore (Logic)
            → WindowCore (Window Management)
```

No circular dependency because each layer only calls downward or uses listener callbacks.

## Key Interfaces

### ISerializable
All data models implement serialization:
```cpp
class ISerializable
{
    virtual juce::ValueTree getState() const = 0;
    virtual void restoreState(const juce::ValueTree& state) = 0;
};
```

### ITransportListener
Components that need transport updates:
```cpp
class ITransportListener
{
    virtual void transportStateChanged() = 0;
    virtual void tempoChanged(double newTempo) = 0;
    virtual void positionChanged(SamplePosition pos) = 0;
};
```

## Event Flow Examples

### 1. Play/Stop Flow
```
User clicks Play button (UI)
    → TransportButtonComponent::onClick()
        → TransportController::play()
            → ApplicationState::isPlaying.setValue(true)
                → [Listeners notified]
                    → AudioEngineCore receives notification
                    → Transport UI updates
                    → Timeline starts scrolling
```

### 2. Save Project Flow
```
User clicks Save (UI)
    → ProjectMenuComponent::onSave()
        → ProjectManager::save(path)
            → trackManager_->getState()
            → clipManager_->getState()
            → transportController_->getState()
            → routingManager_->getState() [future]
            → ValueTree serialized to XML
            → File written to disk
```

### 3. Double-Click Clip Flow
```
User double-clicks clip (UI)
    → ClipViewComponent::mouseDoubleClick()
        → ClipInteractionController::handleDoubleClick(clipID)
            → ClipEditorCore::openEditor(clipID)
                → ClipEditorWindow created
                → WindowCore::registerWindow(window)
                → Window shown
```

### 4. Add Routing Flow
```
User drags from send to input (UI)
    → RoutingCanvasComponent::endDrag()
        → RoutingController::createRoute(source, dest)
            → RoutingCore::addRoute(route)
                → Route validated
                → RoutingGraph updated
                → [Listeners notified]
                    → Audio engine updates graph
                    → UI updates visual cables
```

## Reusable Control Base Classes

### ControlBase
```cpp
class ControlBase : public juce::Component
{
public:
    virtual void setEnabled(bool shouldBeEnabled);
    virtual void setHighlighted(bool shouldBeHighlighted);
    virtual void setValue(float value) = 0;
    virtual float getValue() const = 0;
    
    class Listener
    {
        virtual void controlValueChanged(ControlBase* control) = 0;
    };
};
```

### KnobControl (extends ControlBase)
- Rotary knob
- Value range mapping
- Sensitivity control
- Label display
- Modulation visualization [future]

### FaderControl (extends ControlBase)
- Vertical/horizontal fader
- dB scale for volume
- Peak hold indicator
- Touch automation [future]

### MeterControl (extends ControlBase)
- Level meter
- Peak detection
- Clip indication
- Multiple color zones

### TransportButton (extends juce::Button)
- Play/Stop/Record states
- Icon rendering
- State-specific colors

### ClipBlockComponent
- Visual representation of clip
- Waveform rendering [future]
- Selection state
- Drag/resize handles

## Serialization Strategy

### ValueTree-Based System
All data is stored in JUCE ValueTrees for:
- Type safety
- Hierarchical structure
- Easy XML/binary serialization
- Undo/redo support [future]

### Example Structure:
```xml
<Project>
    <State>
        <tempo>120.0</tempo>
        <isPlaying>false</isPlaying>
    </State>
    <Tracks>
        <Track id="TRK_1" name="Track 1">
            <volume>0.8</volume>
            <pan>0.0</pan>
        </Track>
    </Tracks>
    <Clips>
        <AudioClip id="CLIP_1" name="Audio 1">
            <startPosition>0</startPosition>
            <length>44100</length>
            <sourceFile>/path/to/file.wav</sourceFile>
        </AudioClip>
    </Clips>
</Project>
```

## Phase 1 Implementation Roadmap

### Phase 1A - Foundation (Week 1)
- [x] UtilityCore types and helpers
- [x] ThemeCore color system
- [x] StateCore observable state
- [x] Basic project structure

### Phase 1B - Core Models (Week 2)
- [x] TransportCore controller
- [x] TrackCore data model
- [x] ClipCore data model
- [ ] AudioDeviceCore manager

### Phase 1C - Basic UI (Week 3)
- [ ] MainWindow structure
- [ ] Transport bar UI
- [ ] Track header list
- [ ] Timeline skeleton
- [ ] Basic controls (knobs, faders, buttons)

### Phase 1D - Audio Integration (Week 4)
- [ ] AudioEngineCore basic graph
- [ ] Audio device selection
- [ ] Basic playback (no clips yet)
- [ ] Monitor audio through

### Phase 1E - Basic Functionality (Week 5)
- [ ] Simple audio clip playback
- [ ] Track volume/pan
- [ ] Transport play/stop
- [ ] Basic mixer layout

### Phase 1F - Project Management (Week 6)
- [ ] Project save/load
- [ ] New project
- [ ] Recent projects list

## Phase 2 - Expansion

### Phase 2A - Clip Editing
- [ ] Clip drag/drop
- [ ] Clip split/trim
- [ ] Fade in/out
- [ ] Clip editor window
- [ ] Waveform rendering

### Phase 2B - Routing
- [ ] RoutingCore implementation
- [ ] Send/return system
- [ ] Basic routing UI

### Phase 2C - Plugin Hosting
- [ ] VST3 scanner
- [ ] Plugin insert slots
- [ ] Plugin editor windows
- [ ] Plugin state save/load

## Phase 3 - Advanced Features

### Phase 3A - Sidechain
- [ ] SidechainCore implementation
- [ ] Sidechain routing UI
- [ ] Quick sidechain buttons

### Phase 3B - Visual Routing
- [ ] Cable rendering
- [ ] Interactive routing canvas
- [ ] Routing presets

### Phase 3C - Automation
- [ ] Automation lanes
- [ ] Automation recording
- [ ] Automation editing

## Testing Strategy

### Unit Tests (Per Module)
- Track creation/deletion
- Clip positioning
- Transport state changes
- Serialization/deserialization

### Integration Tests
- Audio engine + transport
- UI + state synchronization
- Save/load roundtrip

### Manual Testing Checklist
- Audio device selection
- Play/stop/record
- Track operations
- Clip operations
- Save/load projects

## Performance Considerations

### Real-Time Audio Thread
- Never allocate memory
- Never lock UI mutexes
- Use lock-free data structures
- Process in fixed block sizes

### UI Thread
- Use Value listeners for state updates
- Batch repaints
- Async loading for large files

## Conclusion

This architecture provides a solid foundation for a professional DAW that can scale from basic functionality to advanced production features while maintaining clean separation of concerns and easy maintainability.
