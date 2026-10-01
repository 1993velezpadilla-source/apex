#include "VirtualKeyboardCore.h"

namespace xziel::midi
{

//==============================================================================
VirtualKeyboardCore::VirtualKeyboardCore()
{
    rebuildPads();
}

//==============================================================================
// LAYOUT
//==============================================================================

void VirtualKeyboardCore::setLayout (Layout newLayout)
{
    if (layout == newLayout) return;
    layout = newLayout;
    rebuildPads();
    sendChangeMessage();
}

void VirtualKeyboardCore::setBaseOctave (int octave)
{
    octave = juce::jlimit (0, 8, octave);
    if (baseOctave == octave) return;
    baseOctave = octave;
    rebuildPads();
    sendChangeMessage();
}

void VirtualKeyboardCore::setIsomorphicGridSize (int c, int r)
{
    c = juce::jlimit (4, 32, c);
    r = juce::jlimit (2, 16, r);
    cols = c; rows = r;
    if (layout == Layout::WickiHayden || layout == Layout::Janko ||
        layout == Layout::GuitarFretboard)
    {
        rebuildPads();
        sendChangeMessage();
    }
}

void VirtualKeyboardCore::setDrumPadCount (int count)
{
    count = juce::jlimit (4, 64, count);
    if (drumPadCount == count) return;
    drumPadCount = count;
    if (layout == Layout::DrumPads)
    {
        rebuildPads();
        sendChangeMessage();
    }
}

//==============================================================================
// VELOCITY
//==============================================================================

void VirtualKeyboardCore::setVelocityMode (VelocityMode m)
{
    if (velocityMode == m) return;
    velocityMode = m;
    sendChangeMessage();
}

void VirtualKeyboardCore::setFixedVelocity (float v)
{
    fixedVelocity = juce::jlimit (0.0f, 1.0f, v);
}

void VirtualKeyboardCore::setMidiChannel (int ch)
{
    channel1based = juce::jlimit (1, 16, ch);
}

//==============================================================================
// LAYOUT BUILDERS
//==============================================================================

void VirtualKeyboardCore::rebuildPads()
{
    pads.clearQuick();
    switch (layout)
    {
        case Layout::PianoStandard:    buildPianoLayout();           break;
        case Layout::WickiHayden:      buildWickiHaydenLayout();     break;
        case Layout::Janko:            buildJankoLayout();           break;
        case Layout::GuitarFretboard:  buildGuitarFretboardLayout(); break;
        case Layout::DrumPads:         buildDrumPadsLayout();        break;
    }
    focusedPadIndex = juce::jlimit (0, juce::jmax (0, pads.size() - 1), focusedPadIndex);
}

void VirtualKeyboardCore::buildPianoLayout()
{
    // 2 octaves of white keys + black keys layered on top.
    // Cols = 14 (2 octaves of white), rows = 2 (white row + black row).
    cols = 14;
    rows = 2;

    static const int whitePitches[7]  = { 0, 2, 4, 5, 7, 9, 11 };  // C D E F G A B
    static const int blackPitches[7]  = { 1, 3, -1, 6, 8, 10, -1 }; // C# D# - F# G# A# -

    int padCol = 0;
    for (int oct = 0; oct < 2; ++oct)
    {
        for (int wk = 0; wk < 7; ++wk)
        {
            const int pitch = (baseOctave + oct + 1) * 12 + whitePitches[wk];
            Pad p;
            p.midiNote     = pitch;
            p.column       = padCol;
            p.row          = 1;       // bottom row = white keys
            p.isAccidental = false;
            p.label        = pitchToNoteName (pitch);
            pads.add (p);

            // Black key (drawn slightly to the right of this white)
            const int bk = blackPitches[wk];
            if (bk >= 0)
            {
                Pad bp;
                bp.midiNote     = (baseOctave + oct + 1) * 12 + bk;
                bp.column       = padCol;
                bp.row          = 0;       // top row = black keys
                bp.isAccidental = true;
                bp.label        = pitchToNoteName (bp.midiNote);
                pads.add (bp);
            }
            ++padCol;
        }
    }
}

void VirtualKeyboardCore::buildWickiHaydenLayout()
{
    // Wicki-Hayden: rows go up by perfect 5th. Within row, +2 semitones per col.
    // Each row offset from the row below by +7 semitones (a fifth).
    const int rootPitch = (baseOctave + 1) * 12;  // C of base octave

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            // Row offset: alternate rows offset by half-step horizontally
            const int hOffset = (r % 2 == 0) ? 0 : 1;
            const int pitch   = rootPitch + r * 7 + c * 2 + hOffset;
            if (pitch < 0 || pitch > 127) continue;

            Pad p;
            p.midiNote     = pitch;
            p.column       = c;
            p.row          = r;
            p.isAccidental = ((pitch % 12) == 1 || (pitch % 12) == 3 ||
                              (pitch % 12) == 6 || (pitch % 12) == 8 ||
                              (pitch % 12) == 10);
            p.label        = pitchToNoteName (pitch);
            pads.add (p);
        }
    }
}

void VirtualKeyboardCore::buildJankoLayout()
{
    // Janko: every row is chromatic. Adjacent rows are offset by 1 semitone.
    // Top rows are pitch+1, bottom rows are pitch+0, alternating.
    const int rootPitch = (baseOctave + 1) * 12;

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            const int rowOffset = (r % 2);
            const int pitch     = rootPitch + c * 2 + rowOffset;
            if (pitch < 0 || pitch > 127) continue;

            Pad p;
            p.midiNote     = pitch;
            p.column       = c;
            p.row          = r;
            p.isAccidental = ((pitch % 12) == 1 || (pitch % 12) == 3 ||
                              (pitch % 12) == 6 || (pitch % 12) == 8 ||
                              (pitch % 12) == 10);
            p.label        = pitchToNoteName (pitch);
            pads.add (p);
        }
    }
}

void VirtualKeyboardCore::buildGuitarFretboardLayout()
{
    // 6 strings in standard tuning: E2 A2 D3 G3 B3 E4
    // Each row = a string, each column = a fret.
    // We use base octave to shift the whole tuning.
    static const int standardTuning[6] = { 28, 33, 38, 43, 47, 52 }; // standard tuning
    const int rowsLocal = juce::jmin (6, rows);
    const int octShift  = (baseOctave - 2) * 12; // 2 = baseline E2

    for (int s = 0; s < rowsLocal; ++s)
    {
        for (int f = 0; f < cols; ++f)
        {
            const int pitch = standardTuning[s] + f + octShift;
            if (pitch < 0 || pitch > 127) continue;

            Pad p;
            p.midiNote       = pitch;
            p.column         = f;
            p.row            = s;
            p.isAccidental   = false; // fretboard treats all positions equal
            p.label          = pitchToNoteName (pitch);
            p.secondaryLabel = "fret " + juce::String (f);
            pads.add (p);
        }
    }
}

void VirtualKeyboardCore::buildDrumPadsLayout()
{
    // General MIDI drum map: starts at note 36 (kick) up to 51+
    static const struct { int pitch; const char* name; } drumMap[] =
    {
        { 36, "Kick"     }, { 38, "Snare"    }, { 42, "HH C"   }, { 46, "HH O"     },
        { 49, "Crash"    }, { 51, "Ride"     }, { 50, "Tom 1"  }, { 47, "Tom 2"    },
        { 45, "Tom 3"    }, { 43, "Tom 4"    }, { 39, "Clap"   }, { 37, "Rim"      },
        { 56, "Cowbell"  }, { 53, "Bell"     }, { 75, "Click"  }, { 76, "Wood Hi"  },
    };
    const int n = juce::jmin (drumPadCount, (int) (sizeof(drumMap) / sizeof(drumMap[0])));

    cols = (int) std::ceil (std::sqrt ((float) n));
    rows = (n + cols - 1) / cols;

    for (int i = 0; i < n; ++i)
    {
        Pad p;
        p.midiNote     = drumMap[i].pitch;
        p.column       = i % cols;
        p.row          = i / cols;
        p.isAccidental = false;
        p.label        = drumMap[i].name;
        pads.add (p);
    }
}

//==============================================================================
// INPUT
//==============================================================================

float VirtualKeyboardCore::computeVelocity (float yWithinPad, float pressure) const
{
    switch (velocityMode)
    {
        case VelocityMode::YPosition:
            // Tap higher = soft, tap lower = hard. y=0 is top, y=1 is bottom.
            return juce::jlimit (0.05f, 1.0f, yWithinPad * 0.95f + 0.15f);

        case VelocityMode::Pressure:
            return juce::jlimit (0.05f, 1.0f, pressure);

        case VelocityMode::Fixed:
        default:
            return fixedVelocity;
    }
}

void VirtualKeyboardCore::touchPad (int padIndex,
                                    float yWithinPad,
                                    float pressure,
                                    juce::int64 touchId)
{
    if (padIndex < 0 || padIndex >= pads.size()) return;
    if (midiInput == nullptr)  return;

    const auto& pad = pads.getReference (padIndex);
    const float vel = computeVelocity (yWithinPad, pressure);

    midiInput->virtualNoteOn (channel1based, pad.midiNote, vel);

    {
        const juce::ScopedLock sl (touchLock);
        activeTouches.set (touchId, padIndex);
    }
}

void VirtualKeyboardCore::releasePad (juce::int64 touchId)
{
    int padIndex = -1;
    {
        const juce::ScopedLock sl (touchLock);
        if (activeTouches.contains (touchId))
        {
            padIndex = activeTouches[touchId];
            activeTouches.remove (touchId);
        }
    }
    if (padIndex < 0 || padIndex >= pads.size()) return;
    if (midiInput == nullptr) return;

    const auto& pad = pads.getReference (padIndex);
    midiInput->virtualNoteOff (channel1based, pad.midiNote);
}

void VirtualKeyboardCore::allNotesOff()
{
    if (midiInput == nullptr) return;

    juce::HashMap<juce::int64, int> snapshot;
    {
        const juce::ScopedLock sl (touchLock);
        snapshot = activeTouches;
        activeTouches.clear();
    }

    for (auto it = snapshot.begin(); it.next();)
    {
        const int padIndex = it.getValue();
        if (padIndex >= 0 && padIndex < pads.size())
            midiInput->virtualNoteOff (channel1based, pads[padIndex].midiNote);
    }
}

//==============================================================================
// SCALE
//==============================================================================

void VirtualKeyboardCore::setScale (int root, juce::Array<int> semis)
{
    rootPitchClass = ((root % 12) + 12) % 12;
    scaleSemitones = std::move (semis);
    sendChangeMessage();
}

void VirtualKeyboardCore::clearScale()
{
    rootPitchClass = -1;
    scaleSemitones.clear();
    sendChangeMessage();
}

bool VirtualKeyboardCore::isPadInScale (int padIndex) const
{
    if (rootPitchClass < 0) return true; // no scale = all notes "in scale"
    if (padIndex < 0 || padIndex >= pads.size()) return false;

    const int pitch = pads.getReference (padIndex).midiNote;
    const int relative = ((pitch - rootPitchClass) % 12 + 12) % 12;
    return scaleSemitones.contains (relative);
}

//==============================================================================
// GAMEPAD NAVIGATION
//==============================================================================

int VirtualKeyboardCore::moveFocus (int dCol, int dRow)
{
    if (pads.isEmpty()) return -1;

    const auto& current = pads.getReference (focusedPadIndex);
    const int targetCol = current.column + dCol;
    const int targetRow = current.row + dRow;

    int bestIdx  = focusedPadIndex;
    int bestDist = INT_MAX;
    for (int i = 0; i < pads.size(); ++i)
    {
        const auto& p = pads.getReference (i);
        const int dC  = p.column - targetCol;
        const int dR  = p.row - targetRow;
        const int dist = dC * dC + dR * dR;
        if (dist < bestDist)
        {
            bestDist = dist;
            bestIdx  = i;
        }
    }
    focusedPadIndex = bestIdx;
    sendChangeMessage();
    return focusedPadIndex;
}

void VirtualKeyboardCore::setFocus (int padIndex)
{
    if (padIndex < 0 || padIndex >= pads.size()) return;
    focusedPadIndex = padIndex;
    sendChangeMessage();
}

void VirtualKeyboardCore::gamepadTriggerFocused (float velocity01)
{
    if (pads.isEmpty() || midiInput == nullptr) return;
    const auto& pad = pads.getReference (focusedPadIndex);
    const float vel = juce::jlimit (0.05f, 1.0f, velocity01);
    midiInput->virtualNoteOn (channel1based, pad.midiNote, vel);

    // Use a fixed touchId for the gamepad
    const juce::ScopedLock sl (touchLock);
    activeTouches.set (-1, focusedPadIndex);
}

void VirtualKeyboardCore::gamepadReleaseFocused()
{
    releasePad (-1);
}

//==============================================================================
// HELPERS
//==============================================================================

juce::String VirtualKeyboardCore::pitchToNoteName (int pitch) const
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int  pc  = ((pitch % 12) + 12) % 12;
    const int  oct = (pitch / 12) - 1;
    return juce::String (names[pc]) + juce::String (oct);
}

} // namespace xziel::midi
