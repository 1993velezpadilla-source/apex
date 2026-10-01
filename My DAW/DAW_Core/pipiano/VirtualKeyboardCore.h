#pragma once

#include <JuceHeader.h>
#include "../piano_roll/MidiInputCore.h"

namespace xziel::midi
{

//==============================================================================
/**
    VirtualKeyboardCore
    ===================

    Data model for the on-screen / touch / gamepad-driven MIDI keyboard.

    Pure logic — no painting. The UI component (VirtualKeyboardComponent)
    reads the layout from this core and forwards touch/click events back
    in (touchPad/releasePad), which translates to MIDI and pushes to
    MidiInputCore::pushFromVirtualKeyboard.

    Supports multiple layouts:
      - PianoStandard    : traditional piano (best for tablet / desktop)
      - WickiHayden      : isomorphic, perfect-fifth columns
      - Janko            : isomorphic, two-row chromatic
      - GuitarFretboard  : strings-and-frets layout
      - DrumPads         : 4x4 or 8x8 trigger grid

    All layouts share the same Pad abstraction, so the renderer is uniform.

    Velocity strategies (configurable):
      - YPosition  : tap higher on pad = lower velocity, lower = higher
      - Pressure   : use OS-reported touch pressure if available
      - Fixed      : always send a configured value

    This solves problems no other DAW does:
      - Touch-first MIDI input with sensible velocity
      - Gamepad-driven note entry via D-pad navigation
      - Same data model serves all input modalities
*/
class VirtualKeyboardCore : public juce::ChangeBroadcaster
{
public:
    //==========================================================================
    // LAYOUTS
    //==========================================================================

    enum class Layout
    {
        PianoStandard,    // traditional white/black keys
        WickiHayden,      // isomorphic — fifths up, whole tones across
        Janko,            // isomorphic — two staggered chromatic rows
        GuitarFretboard,  // string × fret grid, like a fretboard
        DrumPads          // 4x4 trigger grid mapped to drum notes
    };

    //==========================================================================
    // VELOCITY STRATEGIES
    //==========================================================================

    enum class VelocityMode
    {
        YPosition,  // tap Y within pad → velocity (mobile-DAW classic)
        Pressure,   // OS-reported pressure (Apple Pencil / 3D Touch)
        Fixed       // always send fixedVelocity
    };

    //==========================================================================
    // PAD — a single tappable region
    //==========================================================================

    struct Pad
    {
        int    midiNote     = 60;         // MIDI pitch this pad triggers
        int    column       = 0;          // grid column
        int    row          = 0;          // grid row
        bool   isAccidental = false;      // true for black keys / off-scale
        juce::String label;               // displayed text (note name, drum name, etc.)
        juce::String secondaryLabel;      // octave number, fret number, etc.
    };

    //==========================================================================
    // CONSTRUCTION
    //==========================================================================

    VirtualKeyboardCore();
    ~VirtualKeyboardCore() override = default;

    /** Connect this core to a MidiInputCore so taps generate MIDI events. */
    void setMidiInput (MidiInputCore* input)  { midiInput = input; }

    //==========================================================================
    // LAYOUT
    //==========================================================================

    void   setLayout (Layout newLayout);
    Layout getLayout() const noexcept   { return layout; }

    /** For piano: which octave is the leftmost C in. Default = 3 (C3). */
    void setBaseOctave (int octave);
    int  getBaseOctave() const noexcept  { return baseOctave; }

    /** For isomorphic layouts: number of pads across × down. */
    void setIsomorphicGridSize (int columns, int rows);

    /** For drum pads: number of pads (default 16 = 4x4). */
    void setDrumPadCount (int count);

    /** Returns the current set of pads for the active layout. */
    const juce::Array<Pad>& getPads() const noexcept  { return pads; }

    /** Number of columns and rows for the current layout. */
    int getColumnCount() const noexcept   { return cols; }
    int getRowCount()    const noexcept   { return rows; }

    //==========================================================================
    // VELOCITY
    //==========================================================================

    void          setVelocityMode (VelocityMode mode);
    VelocityMode  getVelocityMode() const noexcept  { return velocityMode; }

    void  setFixedVelocity (float v01);
    float getFixedVelocity() const noexcept  { return fixedVelocity; }

    //==========================================================================
    // CHANNEL
    //==========================================================================

    void setMidiChannel (int channel1based);
    int  getMidiChannel() const noexcept  { return channel1based; }

    //==========================================================================
    // INPUT  (called by the UI component on touch/click)
    //==========================================================================

    /** Touch begins on a pad.

        @param padIndex     index into getPads()
        @param yWithinPad   0.0 (top) to 1.0 (bottom), used for YPosition velocity
        @param pressure     0.0 to 1.0, used for Pressure velocity
        @param touchId      stable id so multi-touch can release the right note
    */
    void touchPad (int padIndex,
                   float yWithinPad,
                   float pressure,
                   juce::int64 touchId);

    /** Touch ends. Sends note-off for the originally-touched pad. */
    void releasePad (juce::int64 touchId);

    /** Convenience: emit a MIDI panic to release any stuck notes. */
    void allNotesOff();

    //==========================================================================
    // SCALE HIGHLIGHTING  (so users see in-scale pads)
    //==========================================================================

    /** Set the active scale for highlighting in-scale pads.
        rootPitchClass: 0=C, 1=C#, ... 11=B
        scaleSemitones: e.g. {0,2,4,5,7,9,11} for major */
    void setScale (int rootPitchClass, juce::Array<int> scaleSemitones);
    void clearScale();
    bool isPadInScale (int padIndex) const;

    //==========================================================================
    // GAMEPAD NAVIGATION  (NO DAW HAS THIS)
    //==========================================================================

    /** D-pad navigation through the grid.
        Returns the new focused pad index. */
    int  moveFocus (int columnDelta, int rowDelta);
    void setFocus  (int padIndex);
    int  getFocus() const noexcept  { return focusedPadIndex; }

    /** Trigger note via gamepad button (alternative to touch).
        velocity01 typically derived from analog trigger pressure. */
    void gamepadTriggerFocused (float velocity01);
    void gamepadReleaseFocused();

    //==========================================================================
private:
    //==========================================================================

    Layout       layout         = Layout::PianoStandard;
    int          baseOctave     = 3;
    int          cols           = 14;       // for piano: 14 white keys (2 octaves)
    int          rows           = 1;
    int          drumPadCount   = 16;
    VelocityMode velocityMode   = VelocityMode::YPosition;
    float        fixedVelocity  = 0.78f;
    int          channel1based  = 1;

    juce::Array<Pad> pads;
    int focusedPadIndex = 0;

    int rootPitchClass = -1;          // -1 = no scale set
    juce::Array<int> scaleSemitones;

    MidiInputCore* midiInput = nullptr;

    // Active touches: which touchId is holding which pad index
    juce::HashMap<juce::int64, int> activeTouches;
    juce::CriticalSection touchLock;

    void rebuildPads();
    void buildPianoLayout();
    void buildWickiHaydenLayout();
    void buildJankoLayout();
    void buildGuitarFretboardLayout();
    void buildDrumPadsLayout();

    float computeVelocity (float yWithinPad, float pressure) const;
    juce::String pitchToNoteName (int pitch) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualKeyboardCore)
};

} // namespace xziel::midi
