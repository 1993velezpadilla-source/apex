// =============================================================================
//  ApexTunePitchTraceComponent.h
//  Main canvas: waveform background, detected pitch trace, note blocks
//  (color-coded by deviation), scale grid, drag-to-retune.
//
//  Drop-in: Source/VocalTuneUI/ApexTunePitchTraceComponent.h
//  Depends on: JUCE, ApexTuneTypes.h (for ApexTuneClipState / ApexTuneAnalysis).
//  Touches:    nothing else in the APEX codebase.
//
//  Phase 6a: header is full. cpp is a stub that paints a labeled placeholder.
//  Phase 6b: cpp gets full painting + mouse interaction.
//
//  Coordinate system:
//    x = time (left = clip start, right = clip end)
//    y = pitch (top = high MIDI, bottom = low MIDI)
//
//  The Y mapping must match ApexTunePianoGridComponent's range so that
//  detected notes sit on the correct horizontal piano-key row.
// =============================================================================

#pragma once

#include <JuceHeader.h>
#include "../VocalTuneCore/ApexTuneTypes.h"

namespace apex { namespace vocaltune {

class ApexTunePitchTraceComponent : public juce::Component
{
public:
    ApexTunePitchTraceComponent();
    ~ApexTunePitchTraceComponent() override;

    // ---- State injection (component does NOT own these) -------------------
    void setClipState   (ApexTuneClipState* state);
    void setAnalysis    (const ApexTuneAnalysis* analysis);
    void setSourceAudio (const float* mono, int numSamples, double sampleRate);

    // ---- View configuration ------------------------------------------------
    void setMidiRange   (int lowMidi, int highMidi);
    void setScaleInfo   (const juce::String& scaleRoot, const juce::String& scaleType);
    void setPlayheadSample (int64_t sample);
    void setRenderInProgress (bool inProgress);

    bool hasSelectedNotes() const;
    bool isNoteSelected (int noteIndex) const;
    std::vector<int> getSelectedNoteIndices() const;
    void selectAllNotes();
    void clearSelectedNotes();
    bool nudgeSelectedNotes (float deltaMidi, bool fine);
    bool splitSelectedNotes();
    bool mergeSelectedNotes();

    // ---- Callbacks ---------------------------------------------------------
    // Fired when the user changes any note (drag, double-click snap, etc.).
    // Caller should bump renderVersion and trigger a re-render.
    std::function<void ()> onNoteEditStarted;
    std::function<void ()> onNoteEdited;

    // Fired when the user selects a note (or deselects with -1).
    std::function<void (int /*noteIndex*/)> onNoteSelected;

    int getSelectedNoteIndex() const { return selectedNoteIndex_; }

    // ---- juce::Component ---------------------------------------------------
    void paint           (juce::Graphics&) override;
    void resized         () override;
    void mouseDown       (const juce::MouseEvent&) override;
    void mouseDrag       (const juce::MouseEvent&) override;
    void mouseUp         (const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove  (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove       (const juce::MouseEvent&) override;

private:
    ApexTuneClipState*       state_       = nullptr;
    const ApexTuneAnalysis*  analysis_    = nullptr;
    const float*             sourceAudio_ = nullptr;
    int                      sourceLen_   = 0;
    double                   sourceSr_    = 0.0;

    int lowMidi_   = 36;
    int highMidi_  = 84;

    juce::String scaleRoot_ = "C";
    juce::String scaleType_ = "Chromatic";

    int   selectedNoteIndex_     = -1;
    std::vector<int> selectedNoteIndices_;
    int   draggedNoteIndex_      = -1;
    float dragStartTargetMidi_   = 0.0f;
    float dragStartMouseMidi_    = 0.0f;
    bool  dragIsFine_            = false;   // set true on shift-drag
    bool  dragUndoStarted_       = false;
    std::vector<float> dragStartTargetMidis_;
    bool  marqueeActive_         = false;
    juce::Point<float> marqueeStart_;
    juce::Point<float> marqueeCurrent_;

    enum class DragMode { None, NotePitch, NoteTime, TrimLeft, TrimRight, Marquee, Scrollbar };
    DragMode  dragMode_      = DragMode::None;
    int       trimNoteIndex_ = -1;
    int64_t   trimOrigStart_ = 0;
    int64_t   trimOrigEnd_   = 0;
    float     trimStartX_    = 0.0f;
    std::vector<int64_t> dragStartNoteStarts_;
    std::vector<int64_t> dragStartNoteEnds_;
    float     timeDragStartX_ = 0.0f;

    int findEdgeAt (juce::Point<float> p, bool& isLeft) const;

    float viewStartFrac_ = 0.0f;
    float viewEndFrac_   = 1.0f;
    int64_t playheadSample_ = -1;
    bool renderInProgress_ = false;
    float scrollbarDragStartX_ = 0.0f;
    float scrollbarDragStartViewStart_ = 0.0f;
    float scrollbarDragStartViewEnd_ = 1.0f;

    // ---- Coordinate mapping (used in Phase 6b painting + hit testing) -----
    float midiToY    (float midi) const noexcept;
    float yToMidi    (float y)    const noexcept;
    float sampleToX  (int64_t s)  const noexcept;
    int64_t xToSample(float x)    const noexcept;
    juce::Rectangle<float> getScrollbarThumbBounds() const noexcept;

    // Returns index of note under (x,y), or -1.
    int findNoteAt (juce::Point<float> p) const;
    void selectSingleNote (int noteIndex);
    void toggleNoteSelection (int noteIndex);
    void clearSelection();
    bool noteIndexSelected (int noteIndex) const;
    void updateMarqueeSelection (bool additive);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexTunePitchTraceComponent)
};

}} // namespace apex::vocaltune
