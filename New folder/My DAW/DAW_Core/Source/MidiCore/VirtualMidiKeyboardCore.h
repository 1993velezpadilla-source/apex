#pragma once
#include <JuceHeader.h>
#include <unordered_map>

namespace DAW {

class VirtualMidiKeyboardCore
{
public:
    enum class Layout
    {
        PianoStandard,
        WickiHayden,
        Janko,
        GuitarFretboard,
        DrumPads
    };

    enum class VelocityMode
    {
        YPosition,
        Pressure,
        Fixed
    };

    struct Pad
    {
        int midiNote = 60;
        int column = 0;
        int row = 0;
        bool isAccidental = false;
        juce::String label;
        juce::String secondaryLabel;
    };

    VirtualMidiKeyboardCore()
    {
        rebuildPads();
    }

    juce::MidiKeyboardState& getState() noexcept { return state_; }
    const juce::Array<Pad>& getPads() const noexcept { return pads_; }

    void setLayout(Layout newLayout)
    {
        if (layout_ == newLayout)
            return;

        layout_ = newLayout;
        rebuildPads();
    }

    Layout getLayout() const noexcept { return layout_; }

    void setBaseOctave(int octave)
    {
        const int clamped = juce::jlimit(-2, 8, octave);
        if (baseOctave_ == clamped)
            return;

        baseOctave_ = clamped;
        rebuildPads();
    }

    int getBaseOctave() const noexcept { return baseOctave_; }

    void setIsomorphicGridSize(int columns, int rows)
    {
        cols_ = juce::jmax(1, columns);
        rows_ = juce::jmax(1, rows);
        rebuildPads();
    }

    void setDrumPadCount(int count)
    {
        drumPadCount_ = juce::jmax(1, count);
        rebuildPads();
    }

    int getColumnCount() const noexcept { return cols_; }
    int getRowCount() const noexcept { return rows_; }

    void setVelocityMode(VelocityMode mode) noexcept { velocityMode_ = mode; }
    VelocityMode getVelocityMode() const noexcept { return velocityMode_; }

    void setFixedVelocity(float v01) noexcept { fixedVelocity_ = juce::jlimit(0.0f, 1.0f, v01); }
    float getFixedVelocity() const noexcept { return fixedVelocity_; }

    void setMidiChannel(int channel1based) noexcept { channel1based_ = juce::jlimit(1, 16, channel1based); }
    int getMidiChannel() const noexcept { return channel1based_; }

    void setScale(int rootPitchClass, juce::Array<int> scaleSemitones)
    {
        rootPitchClass_ = juce::jlimit(0, 11, rootPitchClass);
        scaleSemitones_ = std::move(scaleSemitones);
    }

    void clearScale()
    {
        rootPitchClass_ = -1;
        scaleSemitones_.clear();
    }

    bool isPadInScale(int padIndex) const
    {
        if (!juce::isPositiveAndBelow(padIndex, pads_.size()) || rootPitchClass_ < 0 || scaleSemitones_.isEmpty())
            return false;

        const int pitchClass = ((pads_.getReference(padIndex).midiNote % 12) + 12) % 12;
        const int relative = (pitchClass - rootPitchClass_ + 12) % 12;
        return scaleSemitones_.contains(relative);
    }

    void touchPad(int padIndex, float yWithinPad, float pressure, juce::int64 touchId)
    {
        const juce::ScopedLock sl(lock_);
        if (!juce::isPositiveAndBelow(padIndex, pads_.size()))
            return;

        if (const auto existing = activeTouches_.find(touchId); existing != activeTouches_.end())
            state_.noteOff(channel1based_, pads_.getReference(existing->second).midiNote, 0.0f);

        const auto& pad = pads_.getReference(padIndex);
        state_.noteOn(channel1based_, pad.midiNote, computeVelocity(yWithinPad, pressure));
        activeTouches_[touchId] = padIndex;
        focusedPadIndex_ = padIndex;
    }

    void releasePad(juce::int64 touchId)
    {
        const juce::ScopedLock sl(lock_);
        const auto it = activeTouches_.find(touchId);
        if (it == activeTouches_.end())
            return;

        state_.noteOff(channel1based_, pads_.getReference(it->second).midiNote, 0.0f);
        activeTouches_.erase(it);
    }

    int moveFocus(int columnDelta, int rowDelta)
    {
        if (pads_.isEmpty())
            return focusedPadIndex_ = 0;

        const auto& current = pads_.getReference(juce::jlimit(0, pads_.size() - 1, focusedPadIndex_));
        const int targetColumn = current.column + columnDelta;
        const int targetRow = current.row + rowDelta;

        for (int i = 0; i < pads_.size(); ++i)
        {
            const auto& pad = pads_.getReference(i);
            if (pad.column == targetColumn && pad.row == targetRow)
                return focusedPadIndex_ = i;
        }

        return focusedPadIndex_;
    }

    void setFocus(int padIndex)
    {
        focusedPadIndex_ = juce::jlimit(0, juce::jmax(0, pads_.size() - 1), padIndex);
    }

    int getFocus() const noexcept { return focusedPadIndex_; }

    void gamepadTriggerFocused(float velocity01)
    {
        const int touchId = std::numeric_limits<int>::min();
        touchPad(focusedPadIndex_, 1.0f - juce::jlimit(0.0f, 1.0f, velocity01), velocity01, touchId);
    }

    void gamepadReleaseFocused()
    {
        releasePad(std::numeric_limits<int>::min());
    }

    void setActiveTrackId(const juce::String& id) noexcept
    {
        const juce::ScopedLock sl(lock_);
        activeTrackId_ = id;
    }

    juce::String getActiveTrackId() const noexcept
    {
        const juce::ScopedLock sl(lock_);
        return activeTrackId_;
    }

    void processBlock(const juce::String& trackId,
                      juce::MidiBuffer& midiOut,
                      int numSamples) noexcept
    {
        {
            const juce::ScopedLock sl(lock_);
            if (trackId != activeTrackId_)
                return;
        }

        state_.processNextMidiBuffer(midiOut, 0, numSamples, true);
    }

    void allNotesOff() noexcept
    {
        const juce::ScopedLock sl(lock_);
        for (int ch = 1; ch <= 16; ++ch)
            state_.allNotesOff(ch);
        activeTouches_.clear();
    }

private:
    void rebuildPads()
    {
        pads_.clearQuick();

        switch (layout_)
        {
            case Layout::PianoStandard:   buildPianoLayout(); break;
            case Layout::WickiHayden:     buildWickiHaydenLayout(); break;
            case Layout::Janko:           buildJankoLayout(); break;
            case Layout::GuitarFretboard: buildGuitarFretboardLayout(); break;
            case Layout::DrumPads:        buildDrumPadsLayout(); break;
        }

        focusedPadIndex_ = juce::jlimit(0, juce::jmax(0, pads_.size() - 1), focusedPadIndex_);
    }

    void buildPianoLayout()
    {
        const int baseMidi = (baseOctave_ + 1) * 12;
        cols_ = 24;
        rows_ = 1;

        for (int i = 0; i < cols_; ++i)
        {
            Pad pad;
            pad.midiNote = juce::jlimit(0, 127, baseMidi + i);
            pad.column = i;
            pad.row = 0;
            pad.isAccidental = isAccidental(pad.midiNote);
            pad.label = pitchToNoteName(pad.midiNote);
            pad.secondaryLabel = juce::String((pad.midiNote / 12) - 1);
            pads_.add(std::move(pad));
        }
    }

    void buildWickiHaydenLayout()
    {
        for (int row = 0; row < rows_; ++row)
        {
            for (int col = 0; col < cols_; ++col)
            {
                Pad pad;
                pad.midiNote = juce::jlimit(0, 127, (baseOctave_ + 1) * 12 + col * 2 + row * 7);
                pad.column = col;
                pad.row = row;
                pad.isAccidental = isAccidental(pad.midiNote);
                pad.label = pitchToNoteName(pad.midiNote);
                pads_.add(std::move(pad));
            }
        }
    }

    void buildJankoLayout()
    {
        rows_ = juce::jmax(2, rows_);
        for (int row = 0; row < rows_; ++row)
        {
            for (int col = 0; col < cols_; ++col)
            {
                Pad pad;
                pad.midiNote = juce::jlimit(0, 127, (baseOctave_ + 1) * 12 + col + (row % 2) * 2 + (row / 2) * 12);
                pad.column = col;
                pad.row = row;
                pad.isAccidental = isAccidental(pad.midiNote);
                pad.label = pitchToNoteName(pad.midiNote);
                pads_.add(std::move(pad));
            }
        }
    }

    void buildGuitarFretboardLayout()
    {
        static constexpr int openStrings[] = { 40, 45, 50, 55, 59, 64 };
        rows_ = (int) std::size(openStrings);
        cols_ = juce::jmax(cols_, 12);

        for (int row = 0; row < rows_; ++row)
        {
            for (int col = 0; col < cols_; ++col)
            {
                Pad pad;
                pad.midiNote = juce::jlimit(0, 127, openStrings[row] + col + (baseOctave_ - 3) * 12);
                pad.column = col;
                pad.row = row;
                pad.isAccidental = isAccidental(pad.midiNote);
                pad.label = pitchToNoteName(pad.midiNote);
                pad.secondaryLabel = juce::String(col);
                pads_.add(std::move(pad));
            }
        }
    }

    void buildDrumPadsLayout()
    {
        cols_ = juce::jmax(1, (int) std::ceil(std::sqrt((double) drumPadCount_)));
        rows_ = juce::jmax(1, (int) std::ceil((double) drumPadCount_ / (double) cols_));
        const int baseMidi = 36;

        for (int i = 0; i < drumPadCount_; ++i)
        {
            Pad pad;
            pad.midiNote = juce::jlimit(0, 127, baseMidi + i);
            pad.column = i % cols_;
            pad.row = i / cols_;
            pad.isAccidental = false;
            pad.label = pitchToNoteName(pad.midiNote);
            pads_.add(std::move(pad));
        }
    }

    static bool isAccidental(int midiNote) noexcept
    {
        switch (midiNote % 12)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
        }
    }

    float computeVelocity(float yWithinPad, float pressure) const noexcept
    {
        switch (velocityMode_)
        {
            case VelocityMode::Pressure: return juce::jlimit(0.0f, 1.0f, pressure);
            case VelocityMode::Fixed:    return fixedVelocity_;
            case VelocityMode::YPosition:
            default:                     return juce::jlimit(0.08f, 1.0f, juce::jlimit(0.0f, 1.0f, yWithinPad));
        }
    }

    static juce::String pitchToNoteName(int pitch)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String(names[((pitch % 12) + 12) % 12]);
    }

    juce::MidiKeyboardState state_;
    juce::String activeTrackId_;
    mutable juce::CriticalSection lock_;
    std::unordered_map<juce::int64, int> activeTouches_;

    Layout layout_ = Layout::PianoStandard;
    int baseOctave_ = 3;
    int cols_ = 14;
    int rows_ = 1;
    int drumPadCount_ = 16;
    VelocityMode velocityMode_ = VelocityMode::YPosition;
    float fixedVelocity_ = 0.78f;
    int channel1based_ = 1;
    juce::Array<Pad> pads_;
    int focusedPadIndex_ = 0;
    int rootPitchClass_ = -1;
    juce::Array<int> scaleSemitones_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirtualMidiKeyboardCore)
};

} // namespace DAW
