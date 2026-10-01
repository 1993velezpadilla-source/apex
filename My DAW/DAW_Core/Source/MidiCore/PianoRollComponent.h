#pragma once

#include <JuceHeader.h>
#include "PianoRollGridRenderer.h"
#include "PianoRollKeyboardComponent.h"
#include "PianoRollNoteRenderer.h"
#include "PianoRollSelectionRenderer.h"
#include "PianoRollRulerComponent.h"
#include "PianoRollClipModel.h"
#include "../CommandCore/GeneralCommands.h"

namespace DAW {

class PianoRollComponent : public juce::Component,
                           private juce::ScrollBar::Listener,
                           private juce::Timer
{
public:
    enum class NoteColorMode
    {
        Velocity,
        Pitch,
        Clip,
        Channel
    };

    PianoRollComponent();
    ~PianoRollComponent() override;

    void setClipModel(PianoRollClipModel* model);
    void setTicksPerGridDivision(juce::int64 ticks);
    juce::int64 getTicksPerGridDivision() const noexcept { return ticksPerGridDivision_; }
    void setSnapEnabled(bool enabled);
    bool isSnapEnabled() const noexcept { return snapEnabled_; }
    void setScaleSnapEnabled(bool enabled);
    bool isScaleSnapEnabled() const noexcept { return scaleSnapEnabled_; }
    void setNoteColorMode(NoteColorMode mode);
    NoteColorMode getNoteColorMode() const noexcept { return noteColorMode_; }
    void setScaleRoot(const juce::String& root);
    const juce::String& getScaleRoot() const noexcept { return scaleRoot_; }
    void setScaleName(const juce::String& name);
    const juce::String& getScaleName() const noexcept { return scaleName_; }
    /** Set the current playback position in ticks (called by PianoRollWindow timer). */
    void setPlayheadTick(juce::int64 tick);

    /** Zoom: pixels per tick. Range [0.02, 4.0]. */
    void setPixelsPerTick(float ppt);
    float getPixelsPerTick() const noexcept { return pixelsPerTick_; }

    /** Row height in pixels. Range [8, 40]. */
    void setKeyRowHeight(float h);
    float getKeyRowHeight() const noexcept { return keyRowHeight_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class GridViewportComponent : public juce::Component
    {
    public:
        GridViewportComponent(PianoRollComponent& owner, PianoRollGridRenderer& renderer)
            : owner_(owner), renderer_(renderer) {}

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;

    private:
        PianoRollComponent& owner_;
        PianoRollGridRenderer& renderer_;
    };

    enum class ActiveEditMode
    {
        None,
        Move,
        Velocity,
        ResizeStart,
        ResizeEnd,
        MarqueeSelect
    };

    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
    void updateLayout();
    void updateRenderState();
    void handleGridMouseDown(const juce::MouseEvent& e);
    void handleGridMouseDrag(const juce::MouseEvent& e);
    void handleGridMouseUp(const juce::MouseEvent& e);
    void handleGridMouseMove(const juce::MouseEvent& e);
    juce::int64 snapTickFromX(float x) const;
    int midiNoteFromY(float y) const;
    void selectSingleNote(juce::int64 noteId);
    void toggleNoteSelection(juce::int64 noteId);
    void clearSelection();
    void setNotesSelected(const juce::Array<juce::int64>& noteIds, bool selected, bool clearExisting);
    void createNoteAt(juce::Point<float> position);
    bool handleGridKeyPress(const juce::KeyPress& key);
    void beginUndoGesture();
    void commitUndoGesture(const juce::String& description);
    juce::Array<juce::int64> getSelectedNoteIds() const;
    juce::Array<PianoRollClipModel::NoteEvent> getSelectedNotes() const;
    int snapPitchToScale(int midiNote) const;
    void applyMoveToNotes(const juce::Array<PianoRollClipModel::NoteEvent>& originalNotes,
                          juce::int64 deltaTicks,
                          int deltaPitch);
    void applyResizeToNotes(const juce::Array<PianoRollClipModel::NoteEvent>& originalNotes,
                            juce::int64 deltaTicks,
                            bool resizeStart);
    void applyVelocityToNotes(const juce::Array<PianoRollClipModel::NoteEvent>& originalNotes,
                              int deltaVelocity,
                              bool absolute);
    void deleteSelectedNotes();
    void copySelectedNotes();
    void pasteNotes();
    void duplicateSelectedNotes();
    void quantizeSelectedNotes();
    void paintPlayhead(juce::Graphics& g);

    PianoRollClipModel* clipModel_ = nullptr;

    PianoRollGridRenderer gridRenderer_;
    PianoRollNoteRenderer noteRenderer_;
    PianoRollSelectionRenderer selectionRenderer_;
    PianoRollKeyboardComponent keyboard_;
    PianoRollRulerComponent ruler_;
    GridViewportComponent gridViewport_;
    juce::ScrollBar horizontalScrollBar_ { false };
    juce::ScrollBar verticalScrollBar_ { true };

    float pixelsPerTick_ = 0.1f;
    float keyRowHeight_ = 20.0f;
    float scrollX_ = 0.0f;
    int firstVisibleMidiNote_ = 96;
    juce::int64 ticksPerGridDivision_ = 240;
    bool snapEnabled_ = true;
    bool scaleSnapEnabled_ = false;
    NoteColorMode noteColorMode_ = NoteColorMode::Velocity;
    juce::String scaleRoot_ = "C";
    juce::String scaleName_ = "Major";
    ActiveEditMode activeEditMode_ = ActiveEditMode::None;
    juce::int64 activeNoteId_ = 0;
    juce::Point<float> dragStartPosition_;
    PianoRollClipModel::NoteEvent dragOriginalNote_;
    juce::Array<PianoRollClipModel::NoteEvent> dragOriginalNotes_;
    juce::Rectangle<float> marqueeRect_;
    bool pendingCreateNote_ = false;
    bool marqueeAdditive_ = false;
    bool undoGestureActive_ = false;
    juce::ValueTree undoGestureBeforeState_;

    // Playhead
    juce::int64 playheadTick_ = 0;

    // Clipboard (copy/paste)
    juce::Array<PianoRollClipModel::NoteEvent> clipboard_;

    static constexpr int keyboardWidth_ = 72;
    static constexpr int rulerHeight_ = 28;
    static constexpr int scrollBarThickness_ = 14;
    static constexpr float dragThresholdPx_ = 4.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace DAW

