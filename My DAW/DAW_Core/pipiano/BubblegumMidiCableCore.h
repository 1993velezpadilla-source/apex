#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "../piano_roll/PianoRollClipModel.h"

namespace xziel::midi
{

//==============================================================================
/**
    BubblegumMidiCableCore
    ======================

    The killer-feature subsystem: visualizes MIDI flow as liquid pulses
    traveling along Bubblegum cables.

    Subscribes to PianoRollClipModel::PlaybackListener. Every time a note
    fires during playback, this core spawns a visual pulse with properties
    derived from the note (pitch class color, velocity intensity, lifetime
    proportional to tempo).

    Pulses are stored as a flat vector of immutable structs. The cable
    overlay renderer (BubblegumCableOverlayComponent) reads them per frame
    and draws them traveling along the cable path with easing.

    Architecture rules followed:
      - Pure logic + state. NO drawing.
      - Renderer subscribes via a listener and reads pulse state per frame.
      - Audio thread NEVER touches this core directly — only message thread,
        because PlaybackListener::noteFired is dispatched via AsyncUpdater.
      - Pulse data is double-buffered for the renderer (it gets a stable
        const view per frame, no torn reads).
*/
class BubblegumMidiCableCore
    : public PianoRollClipModel::PlaybackListener,
      public juce::Timer
{
public:
    //==========================================================================
    /** A single visual pulse traveling along a cable. */
    struct Pulse
    {
        juce::int64  id              = 0;
        juce::int64  cableId         = 0;     // identifies which cable to render on
        juce::int64  sourceTrackId   = 0;     // for cable-resolution / debug
        juce::int64  targetTrackId   = 0;
        juce::uint8  pitch           = 60;    // for pitch-class coloring
        float        intensity       = 1.0f;  // 0.0 to 1.0 (from velocity)
        float        progress        = 0.0f;  // 0.0 = at source, 1.0 = at target
        float        speedPerSec     = 1.4f;  // travel speed (1.0 = full cable per second)
        float        lifetimeMs      = 800.0f;
        juce::int64  spawnedAtMs     = 0;
        bool         hitTarget       = false; // toggled when progress reaches 1.0
        // Splash/impact state — for "drop landing" effect on the target
        float        splashProgress  = 0.0f;  // 0.0 = no splash, 1.0 = fully dispersed
    };

    /** Listener for the renderer. */
    struct Listener
    {
        virtual ~Listener() = default;
        /** Called whenever the pulse list changes (new pulse, expired pulse,
            or progress update). Renderer should redraw the cable overlay. */
        virtual void pulseStateChanged() = 0;
    };

    //==========================================================================
    BubblegumMidiCableCore();
    ~BubblegumMidiCableCore() override;

    //==========================================================================
    // SUBSCRIPTION TO MIDI MODELS
    //==========================================================================

    /** Subscribe this core to a clip's note-fired events.
        Call from the message thread when a clip becomes active. */
    void attachToModel    (PianoRollClipModel& model);
    void detachFromModel  (PianoRollClipModel& model);

    //==========================================================================
    // CABLE TARGET RESOLUTION  (called by attachToModel callers)
    //==========================================================================

    /** Maps a clip's PianoRollClipModel pointer to the (sourceTrackId,
        targetTrackId, cableId) triple. The host wires this when it creates
        the playback graph; the cable renderer uses these ids to know which
        path to draw the pulse along. */
    struct RouteInfo
    {
        juce::int64 sourceTrackId = 0;
        juce::int64 targetTrackId = 0;
        juce::int64 cableId       = 0;
    };

    void registerRoute   (PianoRollClipModel* model, RouteInfo route);
    void unregisterRoute (PianoRollClipModel* model);

    //==========================================================================
    // CONFIG  (visual tuning)
    //==========================================================================

    /** When pitch-class coloring is on, each pitch (C, C#, D, ...) gets
        its own color. When off, all pulses use a neutral pulse color. */
    void setPitchClassColoringEnabled (bool shouldEnable);
    bool isPitchClassColoringEnabled() const noexcept { return pitchClassColoring; }

    /** When intensity-by-velocity is on, low-velocity notes spawn smaller
        pulses, high-velocity notes spawn larger pulses. */
    void setIntensityByVelocityEnabled (bool shouldEnable);
    bool isIntensityByVelocityEnabled() const noexcept { return intensityByVelocity; }

    /** Travel-speed multiplier. 1.0 = default. Higher = faster pulses. */
    void  setSpeedMultiplier (float multiplier);
    float getSpeedMultiplier() const noexcept { return speedMultiplier; }

    //==========================================================================
    // PULSE STATE  (read by the renderer per frame)
    //==========================================================================

    /** Returns the current snapshot of active pulses for rendering.
        Caller should not mutate. Safe to call from the message thread
        per repaint. */
    std::vector<Pulse> getActivePulses() const;

    /** Color for a given MIDI pitch class (0-11). */
    juce::Colour getPitchClassColor (int pitchClass) const;

    /** Default pulse color when pitch-class coloring is off. */
    juce::Colour getDefaultPulseColor() const noexcept { return defaultColor; }

    //==========================================================================
    // LISTENERS
    //==========================================================================

    void addListener    (Listener* l)  { listeners.add (l); }
    void removeListener (Listener* l)  { listeners.remove (l); }

    //==========================================================================
    // PianoRollClipModel::PlaybackListener
    //==========================================================================

    void noteFired (const PianoRollClipModel::NoteEvent& note) override;

    //==========================================================================
    // TIMER  (advances pulse progress)
    //==========================================================================

    void timerCallback() override;

    //==========================================================================
private:
    //==========================================================================
    // Pulse list  — message thread only
    std::vector<Pulse> pulses;
    juce::CriticalSection pulseLock;     // for getActivePulses() snapshot read

    std::atomic<juce::int64> nextPulseId { 1 };

    // Identity of the clip → route mapping
    juce::HashMap<PianoRollClipModel*, RouteInfo> routes;

    bool  pitchClassColoring  = true;
    bool  intensityByVelocity = true;
    float speedMultiplier     = 1.0f;

    juce::int64 lastTimerMs = 0;

    juce::Colour defaultColor = juce::Colour (0xff7f77dd);

    juce::ListenerList<Listener> listeners;

    /** 12 pitch-class colors — one per chromatic pitch.
        Bitwig-inspired palette, tuned to match Xziel's bubblegum aesthetic. */
    juce::Colour pitchClassPalette[12] =
    {
        juce::Colour (0xffe05656), // C  - red
        juce::Colour (0xffe07b56), // C# - red-orange
        juce::Colour (0xffe0a356), // D  - orange
        juce::Colour (0xffe0d456), // D# - yellow-orange
        juce::Colour (0xffb8e056), // E  - lime
        juce::Colour (0xff7be056), // F  - green
        juce::Colour (0xff56e09c), // F# - teal
        juce::Colour (0xff56cce0), // G  - cyan
        juce::Colour (0xff5688e0), // G# - blue
        juce::Colour (0xff7b56e0), // A  - violet
        juce::Colour (0xffb856e0), // A# - magenta
        juce::Colour (0xffe056cc), // B  - pink
    };

    void notifyListeners();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BubblegumMidiCableCore)
};

} // namespace xziel::midi
