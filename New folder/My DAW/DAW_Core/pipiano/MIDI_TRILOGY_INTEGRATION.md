# MIDI TRILOGY — INTEGRATION GUIDE

## What you have now

```
piano_roll/
├── PianoRollClipModel.h/cpp     (data)
├── MidiClipCore.h/cpp           (clip wrapper)
├── MidiTrackCore.h/cpp          (track type)
└── PianoRollPlaybackCore.h/cpp  (playback)

bubblegum/
└── BubblegumMidiCableCore.h/cpp ← NEW (visual MIDI pulses)

midi/
├── MidiInputCore.h/cpp           ← NEW (physical + virtual MIDI in)
└── VirtualKeyboardCore.h/cpp     ← NEW (touch / gamepad keyboard)
```

You now have the full pipeline:

```
                  ┌──────────────────────────┐
   physical kbd ─→│      MidiInputCore       │
                  │  (devices + FIFO + rec)  │
   touch screen ──┘                          ↑
   gamepad        VirtualKeyboardCore ───────┘
                            ↑
                       (5 layouts:
                        piano / Wicki-Hayden /
                        Janko / fretboard /
                        drum pads)

                  ┌──────────────────────────┐
                  │    Audio engine block    │
                  │  fillBlockBuffer (live)  │
                  │  + processBlock (seq)    │
                  └────────────┬─────────────┘
                               │ midi events
                               ▼
                        Plugin chain (instrument + FX)
                               │
                               │ note fired
                               ▼
                  ┌──────────────────────────┐
                  │  PianoRollClipModel      │
                  │  notifyNoteFired()       │
                  └────────────┬─────────────┘
                               │
                               ▼
                  ┌──────────────────────────┐
                  │ BubblegumMidiCableCore   │ ← THE KILLER FEATURE
                  │  spawn pulse → travel    │
                  │  → impact → splash       │
                  └────────────┬─────────────┘
                               │
                               ▼
                  Cable overlay component renders pulses
```

---

# PATH A — BubblegumMidiCableCore

## Step A1 — Drop into project
```
YourDAW/Source/core/bubblegum/
└── BubblegumMidiCableCore.h/cpp
```

## Step A2 — Own one in ApplicationCore
```cpp
// ApplicationCore.h
#include "core/bubblegum/BubblegumMidiCableCore.h"

class ApplicationCore
{
public:
    xziel::midi::BubblegumMidiCableCore& getMidiCableCore() noexcept
    {
        return midiCableCore;
    }
private:
    xziel::midi::BubblegumMidiCableCore midiCableCore;
};
```

## Step A3 — Wire each clip's model
Wherever you create a `MidiClipCore` and add it to a track:
```cpp
auto& cable = applicationCore.getMidiCableCore();

cable.attachToModel (clip->getModel());

xziel::midi::BubblegumMidiCableCore::RouteInfo route;
route.sourceTrackId = midiTrack.getId();
route.targetTrackId = midiTrack.getInstrumentTargetId() != 0
                      ? midiTrack.getInstrumentTargetId()
                      : midiTrack.getId();   // local instrument
route.cableId       = resolveBubblegumCableId (route.sourceTrackId,
                                                route.targetTrackId);
cable.registerRoute (&clip->getModel(), route);
```

When the clip is removed:
```cpp
cable.unregisterRoute (&clip->getModel());
cable.detachFromModel (clip->getModel());
```

## Step A4 — Subscribe the cable overlay
In `BubblegumCableOverlayComponent` (your existing component):
```cpp
class BubblegumCableOverlayComponent
    : public juce::Component,
      public xziel::midi::BubblegumMidiCableCore::Listener
{
public:
    BubblegumCableOverlayComponent (xziel::midi::BubblegumMidiCableCore& core)
        : cableCore (core)
    {
        cableCore.addListener (this);
    }
    ~BubblegumCableOverlayComponent() override
    {
        cableCore.removeListener (this);
    }

    void pulseStateChanged() override
    {
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // ... draw cables as you already do ...

        // NEW: draw MIDI pulses on top
        for (const auto& pulse : cableCore.getActivePulses())
        {
            const auto cablePath = getCablePathForId (pulse.cableId);
            if (cablePath.isEmpty()) continue;

            const auto position = cablePath.getPointAlongPath (
                pulse.progress * cablePath.getLength());

            const auto color = cableCore.isPitchClassColoringEnabled()
                ? cableCore.getPitchClassColor (pulse.pitch % 12)
                : cableCore.getDefaultPulseColor();

            const float radius = 4.0f + pulse.intensity * 6.0f;

            // Glow
            g.setColour (color.withAlpha (0.35f));
            g.fillEllipse (position.x - radius * 1.8f,
                          position.y - radius * 1.8f,
                          radius * 3.6f, radius * 3.6f);

            // Core
            g.setColour (color);
            g.fillEllipse (position.x - radius,
                          position.y - radius,
                          radius * 2.0f, radius * 2.0f);

            // Splash on impact
            if (pulse.hitTarget && pulse.splashProgress < 1.0f)
            {
                const float splashRadius = radius * (1.0f + pulse.splashProgress * 4.0f);
                g.setColour (color.withAlpha (1.0f - pulse.splashProgress));
                g.drawEllipse (position.x - splashRadius,
                              position.y - splashRadius,
                              splashRadius * 2.0f, splashRadius * 2.0f, 1.5f);
            }
        }
    }

private:
    xziel::midi::BubblegumMidiCableCore& cableCore;
};
```

You will need a helper `getCablePathForId(cableId)` that returns the existing
Bubblegum cable bezier curve for that ID. The pulse simply rides along it.

## Step A5 — Test
1. Compile, hit play on a MIDI clip
2. Pulses should spawn from source track and travel to target
3. On impact, splash ring expands and fades
4. Different notes = different colors (if pitch-class coloring is on)

---

# PATH B — MidiInputCore

## Step B1 — Drop into project
```
YourDAW/Source/core/midi/
└── MidiInputCore.h/cpp
```

## Step B2 — Own one in ApplicationCore
```cpp
xziel::midi::MidiInputCore& getMidiInput() noexcept { return midiInput; }
private:
    xziel::midi::MidiInputCore midiInput;
```

At startup:
```cpp
midiInput.prepare (audioDevice.getCurrentSampleRate(),
                   audioDevice.getCurrentBufferSize());
midiInput.setTempo (transportController.getTempo());
midiInput.setPPQ (960);

// Open all available MIDI devices (or specific ones from settings)
midiInput.openDevice ("");
```

## Step B3 — Hook into audio engine for live monitoring
In your audio callback, BEFORE calling `PianoRollPlaybackCore::processBlock`:
```cpp
juce::MidiBuffer trackMidi;

// 1. Live MIDI from input devices (for armed monitor-through)
applicationCore.getMidiInput().fillBlockBuffer (*midiTrack, trackMidi, numSamples);

// 2. Sequenced MIDI from clips
xziel::midi::PianoRollPlaybackCore::TransportInfo t;
t.isPlaying          = transportController.isPlaying();
t.timelinePosSamples = transportController.getCurrentSamplePosition();
t.numSamples         = numSamples;
applicationCore.getMidiPlayback().processBlock (*midiTrack, t, trackMidi);

// 3. Send merged buffer to the plugin chain
chain->processBlockWithSidechain (trackBuffer, trackMidi, /*sidechain*/);
```

## Step B4 — Recording integration
In your transport's record handler:
```cpp
void TransportController::startRecording()
{
    // ... existing ...

    // For each armed MIDI track, create a fresh clip and target it
    for (int i = 0; i < trackManager.getMidiTrackCount(); ++i)
    {
        auto* track = trackManager.getMidiTrack (i);
        if (! track->isArmed()) continue;

        auto clip = std::make_unique<xziel::midi::MidiClipCore>();
        clip->setName ("Take " + juce::String (track->getClipCount() + 1));
        clip->setTimelineStartSamples (getCurrentSamplePosition());

        auto* clipPtr = clip.get();
        track->addClip (std::move (clip));

        applicationCore.getMidiInput().setRecordingTarget (track, clipPtr);
        applicationCore.getMidiInput().setRecordOriginSamples (
            getCurrentSamplePosition());
    }

    applicationCore.getMidiInput().onRecordingStateChanged (true);
}

void TransportController::stopRecording()
{
    applicationCore.getMidiInput().onRecordingStateChanged (false);
    applicationCore.getMidiInput().clearRecordingTarget();
    // ... existing ...
}
```

## Step B5 — Bubblegum cable for live notes
Subscribe `BubblegumMidiCableCore` to live note events too:
```cpp
class BubblegumMidiCableCore : public ...,
                                public MidiInputCore::LiveNoteListener  // ADD
{
    // Add this method:
    void liveNotePlayed (int channel, int pitch, float velocity,
                        juce::int64 timestampSamples) override
    {
        // Same logic as noteFired, but for live-played notes
        PianoRollClipModel::NoteEvent fakeNote;
        fakeNote.pitch = (juce::uint8) pitch;
        fakeNote.velocity = (juce::uint8) (velocity * 127.0f);
        fakeNote.channel = (juce::uint8) (channel - 1);
        noteFired (fakeNote);  // reuses existing pulse-spawn logic
    }
};
```

Then at startup:
```cpp
applicationCore.getMidiInput().addLiveNoteListener (&midiCableCore);
```

Now BOTH sequenced AND live notes pulse on the cable. 🔥

## Step B6 — Test
1. Plug in a MIDI keyboard
2. Open MIDI device settings, select your device
3. Arm a MIDI track with an instrument loaded
4. Play live → you should hear the instrument
5. Hit record → play a melody → stop → notes appear in the piano roll
6. Bubblegum cable pulses for both live and sequenced notes

---

# PATH C — VirtualKeyboardCore

## Step C1 — Drop into project
```
YourDAW/Source/core/midi/
└── VirtualKeyboardCore.h/cpp
```

## Step C2 — Own one (could be per-window, or global)
```cpp
xziel::midi::VirtualKeyboardCore virtualKeyboard;
virtualKeyboard.setMidiInput (&applicationCore.getMidiInput());
```

## Step C3 — Build the UI component
A `VirtualKeyboardComponent` reads the layout from the core and renders pads:
```cpp
class VirtualKeyboardComponent : public juce::Component,
                                 public juce::ChangeListener
{
public:
    VirtualKeyboardComponent (xziel::midi::VirtualKeyboardCore& kbd)
        : keyboard (kbd)
    {
        keyboard.addChangeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& pads = keyboard.getPads();
        const float padW  = (float) getWidth()  / keyboard.getColumnCount();
        const float padH  = (float) getHeight() / keyboard.getRowCount();

        for (int i = 0; i < pads.size(); ++i)
        {
            const auto& p = pads[i];
            juce::Rectangle<float> rect (
                p.column * padW, p.row * padH, padW, padH);

            const bool inScale = keyboard.isPadInScale (i);
            auto base = p.isAccidental
                          ? juce::Colour (0xff242128)
                          : juce::Colour (0xfff5f0ff);
            if (! inScale) base = base.withMultipliedAlpha (0.4f);

            g.setColour (base);
            g.fillRoundedRectangle (rect.reduced (1.0f), 4.0f);

            g.setColour (p.isAccidental ? juce::Colours::white : juce::Colours::black);
            g.setFont (juce::Font (10.0f));
            g.drawText (p.label, rect, juce::Justification::centred);

            if (i == keyboard.getFocus())
            {
                g.setColour (juce::Colour (0xff7f77dd));
                g.drawRoundedRectangle (rect.reduced (1.0f), 4.0f, 2.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int padIndex = padIndexAt (e.position);
        if (padIndex < 0) return;

        const float yWithinPad = computeYWithin (e.position, padIndex);
        keyboard.touchPad (padIndex, yWithinPad, /*pressure*/ 0.7f, e.eventComponent
                          ? (juce::int64) e.source.getIndex() + 1 : 1);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        keyboard.releasePad ((juce::int64) e.source.getIndex() + 1);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

private:
    int padIndexAt (juce::Point<float>) const { /* ... */ return -1; }
    float computeYWithin (juce::Point<float>, int padIndex) const { return 0.5f; }

    xziel::midi::VirtualKeyboardCore& keyboard;
};
```

For touch on tablet, use `mouseDown/mouseUp` with multi-touch event source ids
to support polyphony.

## Step C4 — Layout switcher
```cpp
juce::ComboBox layoutPicker;
layoutPicker.addItem ("Piano",            1);
layoutPicker.addItem ("Wicki-Hayden",     2);
layoutPicker.addItem ("Janko",            3);
layoutPicker.addItem ("Guitar fretboard", 4);
layoutPicker.addItem ("Drum pads",        5);
layoutPicker.onChange = [this]
{
    using L = xziel::midi::VirtualKeyboardCore::Layout;
    static const L layouts[] = {
        L::PianoStandard, L::WickiHayden, L::Janko,
        L::GuitarFretboard, L::DrumPads };
    keyboard.setLayout (layouts[layoutPicker.getSelectedId() - 1]);
};
```

## Step C5 — Gamepad navigation (you have GamepadManager)
```cpp
// In your GamepadMapper handler
void onDpadEvent (int dx, int dy)
{
    if (virtualKeyboardIsFocused)
        virtualKeyboard.moveFocus (dx, dy);
}

void onTriggerPressed (float analogValue)
{
    if (virtualKeyboardIsFocused)
        virtualKeyboard.gamepadTriggerFocused (analogValue);
}

void onTriggerReleased()
{
    if (virtualKeyboardIsFocused)
        virtualKeyboard.gamepadReleaseFocused();
}
```

This gives you piano-roll-via-controller — something **no other DAW has**.

## Step C6 — Scale highlighting (FL/Logic combined)
```cpp
// Set C major
keyboard.setScale (0, { 0, 2, 4, 5, 7, 9, 11 });

// Set D harmonic minor
keyboard.setScale (2, { 0, 2, 3, 5, 7, 8, 11 });
```

Out-of-scale pads dim automatically in the renderer.

---

# WHAT THIS UNLOCKS

After all three paths integrate, you have:

✅ Sequenced playback (already had)
✅ Live MIDI from physical keyboards
✅ Live MIDI recording into clips
✅ Touch-screen MIDI input with 5 different layouts
✅ Gamepad MIDI input via D-pad navigation
✅ Bubblegum cable visualization for **both** sequenced AND live notes
✅ Pitch-class coloring + intensity-by-velocity
✅ Splash effects on note impact at the target

That makes you the only DAW on Earth with:
- Liquid-cable MIDI flow visualization
- Gamepad-driven piano roll editing
- 5 isomorphic touch layouts
- All in one cohesive design

---

# RECOMMENDED ORDER TO INTEGRATE

If 3 simultaneously is too much, this order minimizes blocking dependencies:

1. **MidiInputCore first** — recording is a core feature, gives you live monitoring
2. **VirtualKeyboardCore second** — depends on MidiInputCore being ready
3. **BubblegumMidiCableCore third** — the visual layer, polishes everything else

Or YOLO all three at once. The cores are designed to be independent — you can
compile each on its own and stub the others.

😈🙏👁️‍🗨️
