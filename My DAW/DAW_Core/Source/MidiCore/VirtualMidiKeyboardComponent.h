#pragma once
#include <JuceHeader.h>
#include "VirtualMidiKeyboardCore.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

//==============================================================================
/**
    VirtualMidiKeyboardComponent
    ============================

    A piano keyboard drawn with DAW theme colours.

    - Multitouch: each finger/pointer gets its own note-on; releasing each
      finger fires the matching note-off.  Works with touchscreens and
      multiple simultaneous mouse buttons.
    - Velocity from Y-position: pressing near the bottom of a key = loud (127),
      near the top = soft (30).
    - Octave range is adjustable via setOctaveOffset().
    - Computer keyboard input (optional): keys Z-M / Q-U play a two-octave span.
*/
class VirtualMidiKeyboardComponent : public juce::Component
{
public:
    VirtualMidiKeyboardComponent()
    {
        setMultiTouchEnabled (true);
        setWantsKeyboardFocus (true);
    }

    ~VirtualMidiKeyboardComponent() override { allNotesOff(); }

    void setKeyboardCore (VirtualMidiKeyboardCore* core) noexcept { core_ = core; }
    VirtualMidiKeyboardCore* getCore() const noexcept { return core_; }

    void setOctaveOffset (int octaves)
    {
        octaveOffset_ = juce::jlimit (-3, 5, octaves);
        allNotesOff();
        repaint();
    }
    int getOctaveOffset() const noexcept { return octaveOffset_; }

    void setMidiChannel (int ch) noexcept { midiChannel_ = juce::jlimit (1, 16, ch); }

    // ── painting ──────────────────────────────────────────────────────────

    void paint (juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        const float w = (float) getWidth();
        const float h = (float) getHeight();

        // Background
        g.setColour (theme.colors.obsidian);
        g.fillRect (getLocalBounds());

        // White keys first
        for (int i = 0; i < kNumWhiteKeys; ++i)
        {
            const int midi   = whiteKeyMidi (i);
            const auto rect  = whiteKeyRect (i);
            const bool held  = heldNotes_.count (midi) > 0;
            const bool hover = (hoveredNote_ == midi);

            juce::Colour fill = held  ? theme.colors.champagne
                              : hover ? theme.colors.smoke.brighter (0.25f)
                                      : theme.colors.pearl.darker (0.07f);
            g.setColour (fill);
            g.fillRoundedRectangle (rect.reduced (0.5f), 3.0f);

            g.setColour (theme.colors.pewter);
            g.drawRoundedRectangle (rect.reduced (0.5f), 3.0f, 1.0f);

            // Note label (C notes only)
            if (midi % 12 == 0)
            {
                g.setColour (held ? theme.colors.obsidian : theme.colors.steel);
                g.setFont (theme.fonts.small.withHeight (10.f));
                g.drawText ("C" + juce::String (midi / 12 - 1),
                            rect.withTrimmedTop (rect.getHeight() * 0.72f),
                            juce::Justification::centredBottom, false);
            }
        }

        // Black keys on top
        for (int i = 0; i < kNumWhiteKeys; ++i)
        {
            const int midi = blackKeyMidiForWhiteIndex (i);
            if (midi < 0) continue;

            const auto rect  = blackKeyRect (i);
            const bool held  = heldNotes_.count (midi) > 0;
            const bool hover = (hoveredNote_ == midi);

            juce::Colour fill = held  ? theme.colors.champagne.darker (0.2f)
                              : hover ? theme.colors.charcoal.brighter (0.3f)
                                      : theme.colors.graphite;
            g.setColour (fill);
            g.fillRoundedRectangle (rect, 2.0f);
            g.setColour (theme.colors.pewter.darker (0.3f));
            g.drawRoundedRectangle (rect, 2.0f, 1.0f);
        }

        // Octave marker line
        g.setColour (theme.colors.separatorSoft.withAlpha (0.5f));
        g.drawHorizontalLine (0, 0.f, w);
        g.drawHorizontalLine ((int) h - 1, 0.f, w);
    }

    void resized() override { repaint(); }

    // ── mouse / touch ──────────────────────────────────────────────────────

    void mouseDown  (const juce::MouseEvent& e) override { fingerDown  (e.eventComponent == this ? e : e, e.source.getIndex()); }
    void mouseDrag  (const juce::MouseEvent& e) override { fingerMoved (e.source.getIndex(), e.position); }
    void mouseUp    (const juce::MouseEvent& e) override { fingerUp    (e.source.getIndex()); }
    void mouseMove  (const juce::MouseEvent& e) override
    {
        hoveredNote_ = noteFromPoint (e.position);
        repaint();
    }
    void mouseExit  (const juce::MouseEvent&) override { hoveredNote_ = -1; repaint(); }

    // ── computer keyboard ──────────────────────────────────────────────────

    bool keyPressed (const juce::KeyPress& key) override
    {
        auto midi = computerKeyToMidi (key.getKeyCode());
        if (midi < 0) return false;
        if (kbdHeld_.count (midi) == 0)
        {
            kbdHeld_.insert (midi);
            noteOn (midi, 90);
        }
        return true;
    }

    bool keyStateChanged (bool /*isKeyDown*/) override
    {
        // Check for released computer keys
        juce::Array<int> toRelease;
        for (int midi : kbdHeld_)
        {
            auto kc = midiToComputerKey (midi);
            if (kc >= 0 && !juce::KeyPress::isKeyCurrentlyDown (kc))
                toRelease.add (midi);
        }
        for (int midi : toRelease) { kbdHeld_.erase (midi); noteOff (midi); }
        return !toRelease.isEmpty();
    }

    void allNotesOff()
    {
        for (auto& [note, _] : heldNotes_) noteOff (note);
        heldNotes_.clear();
        kbdHeld_.clear();
        repaint();
    }

private:
    //==========================================================================
    // Layout constants
    static constexpr int kNumOctaves    = 5;   // C2..C7 (default)
    static constexpr int kNumWhiteKeys  = kNumOctaves * 7;
    static constexpr float kBlackWidthRatio  = 0.6f;
    static constexpr float kBlackHeightRatio = 0.60f;

    int octaveOffset_  = 0;
    int midiChannel_   = 1;
    int hoveredNote_   = -1;
    VirtualMidiKeyboardCore* core_ = nullptr;

    std::unordered_map<int, int> heldNotes_;      // midi note → source index
    std::unordered_set<int>      kbdHeld_;         // computer-kbd held notes
    std::unordered_map<int, int> fingerNote_;      // sourceIndex → midi note

    //==========================================================================
    // Geometry helpers

    float whiteKeyWidth() const noexcept
    {
        return (float) getWidth() / (float) kNumWhiteKeys;
    }

    juce::Rectangle<float> whiteKeyRect (int whiteIndex) const noexcept
    {
        float kw = whiteKeyWidth();
        return { (float) whiteIndex * kw + 1.f, 1.f,
                 kw - 2.f, (float) getHeight() - 2.f };
    }

    juce::Rectangle<float> blackKeyRect (int whiteIndex) const noexcept
    {
        float kw = whiteKeyWidth();
        float bw = kw * kBlackWidthRatio;
        float bh = (float) getHeight() * kBlackHeightRatio;
        float x  = (float) whiteIndex * kw + kw - bw * 0.5f;
        return { x, 1.f, bw, bh };
    }

    // C=0 within octave: white key positions (0=C,1=D,2=E,3=F,4=G,5=A,6=B)
    int whiteKeyMidi (int whiteIndex) const noexcept
    {
        static const int semitone[] = { 0,2,4,5,7,9,11 };
        int octave = (whiteIndex / 7) + 2 + octaveOffset_;
        return juce::jlimit (0, 127, octave * 12 + semitone[whiteIndex % 7]);
    }

    // Returns -1 if this white key has no black key to its right
    int blackKeyMidiForWhiteIndex (int whiteIndex) const noexcept
    {
        static const int hasBlack[] = { 1,1,0,1,1,1,0 };   // C D _ F G A _
        int pos = whiteIndex % 7;
        if (!hasBlack[pos]) return -1;
        return juce::jlimit (0, 127, whiteKeyMidi (whiteIndex) + 1);
    }

    int noteFromPoint (juce::Point<float> p) const noexcept
    {
        // Check black keys first (on top)
        for (int i = 0; i < kNumWhiteKeys; ++i)
        {
            int midi = blackKeyMidiForWhiteIndex (i);
            if (midi < 0) continue;
            if (blackKeyRect (i).contains (p)) return midi;
        }
        // Then white keys
        for (int i = 0; i < kNumWhiteKeys; ++i)
            if (whiteKeyRect (i).contains (p)) return whiteKeyMidi (i);
        return -1;
    }

    int velocityFromPoint (juce::Point<float> p, int midi) const noexcept
    {
        // Velocity is proportional to Y-position within the key
        // (top = soft, bottom = loud) — same convention as all pro DAWs
        bool isBlack = (midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6
                     || midi % 12 == 8 || midi % 12 == 10);
        float keyH = isBlack ? (float) getHeight() * kBlackHeightRatio
                              : (float) getHeight();
        float ratio = juce::jlimit (0.0f, 1.0f, p.y / keyH);
        return juce::jlimit (1, 127, (int) (ratio * 97.0f + 30.0f));
    }

    //==========================================================================
    void fingerDown (const juce::MouseEvent& e, int sourceIdx)
    {
        int midi = noteFromPoint (e.position);
        if (midi < 0) return;
        int vel  = velocityFromPoint (e.position, midi);

        // Release any previous note on this finger
        auto it = fingerNote_.find (sourceIdx);
        if (it != fingerNote_.end() && it->second != midi)
            noteOff (it->second);

        fingerNote_[sourceIdx] = midi;
        noteOn (midi, vel);
    }

    void fingerMoved (int sourceIdx, juce::Point<float> pos)
    {
        int newMidi = noteFromPoint (pos);
        auto it = fingerNote_.find (sourceIdx);

        if (it != fingerNote_.end())
        {
            if (newMidi != it->second)
            {
                noteOff (it->second);
                if (newMidi >= 0)
                {
                    int vel = velocityFromPoint (pos, newMidi);
                    noteOn (newMidi, vel);
                    fingerNote_[sourceIdx] = newMidi;
                }
                else
                {
                    fingerNote_.erase (sourceIdx);
                }
            }
        }
        hoveredNote_ = newMidi;
        repaint();
    }

    void fingerUp (int sourceIdx)
    {
        auto it = fingerNote_.find (sourceIdx);
        if (it != fingerNote_.end())
        {
            noteOff (it->second);
            fingerNote_.erase (it);
        }
        hoveredNote_ = -1;
        repaint();
    }

    void noteOn (int midi, int vel)
    {
        if (midi < 0 || midi > 127) return;
        heldNotes_[midi] = vel;
        if (core_) core_->getState().noteOn (midiChannel_, midi, vel / 127.0f);
        repaint();
    }

    void noteOff (int midi)
    {
        if (midi < 0 || midi > 127) return;
        heldNotes_.erase (midi);
        if (core_) core_->getState().noteOff (midiChannel_, midi, 0.5f);
        repaint();
    }

    //==========================================================================
    // Computer keyboard mapping: two-octave span (Z row = lower, Q row = upper)
    // Z X C V B N M   → C D E F G A B  (lower octave relative to base)
    // Q W E R T Y U   → C D E F G A B  (upper octave)
    // S D   G H J     → C# D#  F# G# A# (lower)
    // 2 3   5 6 7     → C# D#  F# G# A# (upper)

    int computerKeyToMidi (int kc) const noexcept
    {
        int base = (2 + octaveOffset_) * 12;  // base C
        switch (kc)
        {
            // Lower octave white keys
            case 'z': return base + 0;
            case 'x': return base + 2;
            case 'c': return base + 4;
            case 'v': return base + 5;
            case 'b': return base + 7;
            case 'n': return base + 9;
            case 'm': return base + 11;
            // Lower octave black keys
            case 's': return base + 1;
            case 'd': return base + 3;
            case 'g': return base + 6;
            case 'h': return base + 8;
            case 'j': return base + 10;
            // Upper octave white keys
            case 'q': return base + 12;
            case 'w': return base + 14;
            case 'e': return base + 16;
            case 'r': return base + 17;
            case 't': return base + 19;
            case 'y': return base + 21;
            case 'u': return base + 23;
            // Upper octave black keys
            case '2': return base + 13;
            case '3': return base + 15;
            case '5': return base + 18;
            case '6': return base + 20;
            case '7': return base + 22;
            default: return -1;
        }
    }

    int midiToComputerKey (int midi) const noexcept
    {
        int base = (2 + octaveOffset_) * 12;
        static const int keys[] = { 'z','s','x','d','c','v','g','b','h','n','j','m',
                                     'q','2','w','3','e','r','5','t','6','y','7','u' };
        int rel = midi - base;
        if (rel < 0 || rel >= 24) return -1;
        return keys[rel];
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualMidiKeyboardComponent)
};

} // namespace DAW
