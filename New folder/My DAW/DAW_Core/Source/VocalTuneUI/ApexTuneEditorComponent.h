// =============================================================================
//  ApexTuneEditorComponent.h
//  Top-level vocal tune editor. Owns toolbar + piano grid + pitch trace,
//  wires their callbacks together, and mutates the ApexTuneClipState.
//
//  Drop-in: Source/VocalTuneUI/ApexTuneEditorComponent.h
//  Depends on: JUCE, ApexTuneTypes.h, the three child component headers.
//  Touches:    nothing else in the APEX codebase.
//
//  Lifecycle: the editor does NOT own ApexTuneClipState. The caller (Phase 7
//  glue code: right-click menu handler) constructs the state, hands a pointer
//  to the editor, hosts the editor in a window, and listens for callbacks
//  to know when to bump renderVersion + trigger a render.
// =============================================================================

#pragma once

#include <JuceHeader.h>
#include "../VocalTuneCore/ApexTuneTypes.h"
#include "ApexTuneToolBarComponent.h"
#include "ApexTunePianoGridComponent.h"
#include "ApexTunePitchTraceComponent.h"

namespace apex { namespace vocaltune {

class ApexTuneEditorComponent : public juce::Component,
                                private juce::Timer
{
public:
    ApexTuneEditorComponent();
    ~ApexTuneEditorComponent() override;

    // ---- State / data injection (editor does NOT own these) ----------------
    void setClipState   (ApexTuneClipState* state);
    void setAnalysis    (const ApexTuneAnalysis* analysis);
    void setSourceAudio (const float* mono, int numSamples, double sampleRate);
    void setTransportPositionProvider (std::function<int64_t()> provider);
    void setRenderInProgress (bool inProgress);

    // Set/get bypass state without firing onBypassChanged.
    void setBypassed (bool b);
    bool isBypassed() const;

    // ---- Callbacks (caller binds these) ------------------------------------
    // Fired any time the user changes something that requires a re-render.
    // Caller should: clipState->renderVersion++; project marked dirty; render.
    std::function<void ()> onStateModified;

    // Fired when the user explicitly clicks Render/Apply.
    std::function<void ()> onRenderRequested;

    // Fired when the user toggles Bypass.
    std::function<void (bool)> onBypassChanged;

    // ---- juce::Component ---------------------------------------------------
    void paint   (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    ApexTuneClipState* state_ = nullptr;

    std::unique_ptr<ApexTuneToolBarComponent>    toolbar_;
    std::unique_ptr<ApexTunePianoGridComponent>  pianoGrid_;
    std::unique_ptr<ApexTunePitchTraceComponent> pitchTrace_;
    std::function<int64_t()> transportPositionProvider_;
    std::vector<ApexTuneClipState> undoStack_;
    std::vector<ApexTuneClipState> redoStack_;
    bool applyingHistory_ = false;
    bool toolbarGestureActive_ = false;

    void wireToolbar();
    void applyToAllNotes (std::function<void (ApexTuneNote&)> fn);
    void applyToTargetNotes (std::function<void (ApexTuneNote&)> fn);
    void beginUndoableChange();
    bool undo();
    bool redo();
    void syncToolbarFromState();
    void syncChildStateViews();
    void markGestureUndoStarted();
    void markGestureUndoEnded();
    void notifyModified();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexTuneEditorComponent)
};

}} // namespace apex::vocaltune
