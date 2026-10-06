// =============================================================================
//  ApexTuneEditorComponent.cpp
//  Lays out the three child components, wires toolbar callbacks to clipState
//  mutations, and forwards modifications upward.
//
//  Drop-in: Source/VocalTuneUI/ApexTuneEditorComponent.cpp
// =============================================================================

#include "ApexTuneEditorComponent.h"
#include "ApexTuneColors.h"
#include "../VocalTuneCore/ApexScaleSnapCore.h"

namespace apex { namespace vocaltune {

namespace
{
    constexpr int kToolbarHeight = 56;
    constexpr int kPianoWidth    = 80;
}

// -----------------------------------------------------------------------------
ApexTuneEditorComponent::ApexTuneEditorComponent()
{
    setWantsKeyboardFocus (true);
    toolbar_    = std::make_unique<ApexTuneToolBarComponent>();
    pianoGrid_  = std::make_unique<ApexTunePianoGridComponent>();
    pitchTrace_ = std::make_unique<ApexTunePitchTraceComponent>();

    addAndMakeVisible (*toolbar_);
    addAndMakeVisible (*pianoGrid_);
    addAndMakeVisible (*pitchTrace_);

    wireToolbar();

    pitchTrace_->onNoteEditStarted = [this]
    {
        beginUndoableChange();
    };

    pitchTrace_->onNoteEdited = [this]
    {
        notifyModified();
    };

    pitchTrace_->onNoteSelected = [this] (int /*idx*/)
    {
        grabKeyboardFocus();
        syncToolbarFromState();
    };

    setSize (960, 480);
    startTimerHz (30);
}

// -----------------------------------------------------------------------------
ApexTuneEditorComponent::~ApexTuneEditorComponent() = default;

void ApexTuneEditorComponent::setTransportPositionProvider (std::function<int64_t()> provider)
{
    transportPositionProvider_ = std::move (provider);
}

void ApexTuneEditorComponent::setRenderInProgress (bool inProgress)
{
    if (pitchTrace_ != nullptr)
        pitchTrace_->setRenderInProgress (inProgress);
}

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::setClipState (ApexTuneClipState* state)
{
    state_ = state;
    if (pitchTrace_ != nullptr) pitchTrace_->setClipState (state);

    if (state_ != nullptr && pitchTrace_ != nullptr)
        pitchTrace_->setScaleInfo (state_->scaleRoot, state_->scaleType);
    syncToolbarFromState();

    repaint();
}

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::setAnalysis (const ApexTuneAnalysis* analysis)
{
    if (pitchTrace_ != nullptr) pitchTrace_->setAnalysis (analysis);
}

void ApexTuneEditorComponent::setSourceAudio (const float* mono, int numSamples, double sampleRate)
{
    if (pitchTrace_ != nullptr) pitchTrace_->setSourceAudio (mono, numSamples, sampleRate);
}

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::setBypassed (bool b)
{
    if (toolbar_ != nullptr) toolbar_->setBypassed (b);
}

bool ApexTuneEditorComponent::isBypassed() const
{
    return (toolbar_ != nullptr) && toolbar_->isBypassed();
}

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::wireToolbar()
{
    if (toolbar_ == nullptr) return;

    toolbar_->onGestureStarted = [this]
    {
        markGestureUndoStarted();
    };
    toolbar_->onGestureEnded = [this]
    {
        markGestureUndoEnded();
    };

    toolbar_->onCorrectionChanged = [this] (float v)
    {
        if (! toolbarGestureActive_)
            beginUndoableChange();
        applyToTargetNotes ([v] (ApexTuneNote& n) { n.correctionAmount = v; });
        notifyModified();
    };

    toolbar_->onScaleChanged = [this] (juce::String root, juce::String type)
    {
        if (state_ == nullptr) return;
        if (state_->scaleRoot == root && state_->scaleType == type) return;
        beginUndoableChange();
        state_->scaleRoot = root;
        state_->scaleType = type;
        if (pitchTrace_ != nullptr)
            pitchTrace_->setScaleInfo (state_->scaleRoot, state_->scaleType);
        notifyModified();
    };

    toolbar_->onDriftChanged = [this] (float v)
    {
        if (! toolbarGestureActive_)
            beginUndoableChange();
        applyToTargetNotes ([v] (ApexTuneNote& n) { n.driftAmount = v; });
        notifyModified();
    };

    toolbar_->onModulationChanged = [this] (float v)
    {
        if (! toolbarGestureActive_)
            beginUndoableChange();
        applyToTargetNotes ([v] (ApexTuneNote& n) { n.modulationAmount = v; });
        notifyModified();
    };

    toolbar_->onFormantChanged = [this] (float v)
    {
        if (! toolbarGestureActive_)
            beginUndoableChange();
        applyToTargetNotes ([v] (ApexTuneNote& n) { n.formantShift = v; });
        notifyModified();
    };

    toolbar_->onGainChanged = [this] (float v)
    {
        if (! toolbarGestureActive_)
            beginUndoableChange();
        applyToTargetNotes ([v] (ApexTuneNote& n) { n.gainDb = v; });
        notifyModified();
    };

    toolbar_->onSnapToScaleClicked = [this]
    {
        if (state_ == nullptr) return;
        beginUndoableChange();
        if (pitchTrace_ != nullptr && pitchTrace_->hasSelectedNotes())
        {
            for (int idx : pitchTrace_->getSelectedNoteIndices())
                if (idx >= 0 && idx < (int) state_->notes.size())
                    ApexScaleSnapCore::snapNote (state_->notes[(size_t) idx], state_->scaleRoot, state_->scaleType);
        }
        else
        {
            ApexScaleSnapCore::snapAll (*state_);
        }
        notifyModified();
    };

    toolbar_->onRenderClicked = [this]
    {
        if (onRenderRequested) onRenderRequested();
    };

    toolbar_->onBypassToggled = [this] (bool b)
    {
        if (onBypassChanged) onBypassChanged (b);
    };
}

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::applyToAllNotes (std::function<void (ApexTuneNote&)> fn)
{
    if (state_ == nullptr || ! fn) return;
    for (auto& n : state_->notes) fn (n);
}

void ApexTuneEditorComponent::applyToTargetNotes (std::function<void (ApexTuneNote&)> fn)
{
    if (state_ == nullptr || ! fn) return;
    if (pitchTrace_ != nullptr && pitchTrace_->hasSelectedNotes())
    {
        for (int idx : pitchTrace_->getSelectedNoteIndices())
            if (idx >= 0 && idx < (int) state_->notes.size())
                fn (state_->notes[(size_t) idx]);
        return;
    }
    applyToAllNotes (std::move (fn));
}

void ApexTuneEditorComponent::beginUndoableChange()
{
    if (state_ == nullptr || applyingHistory_) return;
    if (! undoStack_.empty() && undoStack_.back().renderVersion == state_->renderVersion)
        return;
    undoStack_.push_back (*state_);
    if (undoStack_.size() > 64)
        undoStack_.erase (undoStack_.begin());
    redoStack_.clear();
}

bool ApexTuneEditorComponent::undo()
{
    if (state_ == nullptr || undoStack_.empty()) return false;
    applyingHistory_ = true;
    redoStack_.push_back (*state_);
    *state_ = undoStack_.back();
    undoStack_.pop_back();
    state_->renderVersion++;
    syncChildStateViews();
    applyingHistory_ = false;
    if (onStateModified) onStateModified();
    return true;
}

bool ApexTuneEditorComponent::redo()
{
    if (state_ == nullptr || redoStack_.empty()) return false;
    applyingHistory_ = true;
    undoStack_.push_back (*state_);
    *state_ = redoStack_.back();
    redoStack_.pop_back();
    state_->renderVersion++;
    syncChildStateViews();
    applyingHistory_ = false;
    if (onStateModified) onStateModified();
    return true;
}

void ApexTuneEditorComponent::syncToolbarFromState()
{
    if (state_ == nullptr || toolbar_ == nullptr)
        return;

    toolbar_->setScale (state_->scaleRoot, state_->scaleType);

    std::vector<const ApexTuneNote*> notesToInspect;
    if (pitchTrace_ != nullptr && pitchTrace_->hasSelectedNotes())
    {
        for (int idx : pitchTrace_->getSelectedNoteIndices())
            if (idx >= 0 && idx < (int) state_->notes.size())
                notesToInspect.push_back (&state_->notes[(size_t) idx]);
    }

    if (notesToInspect.empty())
    {
        for (const auto& n : state_->notes)
            notesToInspect.push_back (&n);
    }

    if (notesToInspect.empty())
        return;

    const auto& first = *notesToInspect.front();
    bool sameCorr = true, sameDrift = true, sameMod = true, sameForm = true;

    for (const auto* note : notesToInspect)
    {
        const auto& n = *note;
        if (n.correctionAmount != first.correctionAmount) sameCorr = false;
        if (n.driftAmount != first.driftAmount) sameDrift = false;
        if (n.modulationAmount != first.modulationAmount) sameMod = false;
        if (n.formantShift != first.formantShift) sameForm = false;
    }

    if (sameCorr)  toolbar_->setCorrectionValue (first.correctionAmount);
    if (sameDrift) toolbar_->setDriftValue      (first.driftAmount);
    if (sameMod)   toolbar_->setModulationValue (first.modulationAmount);
    if (sameForm)  toolbar_->setFormantValue    (first.formantShift);

    bool sameGain = true;
    for (const auto* note : notesToInspect)
        if (note->gainDb != first.gainDb) sameGain = false;
    if (sameGain)  toolbar_->setGainValue (first.gainDb);
}

void ApexTuneEditorComponent::syncChildStateViews()
{
    if (pitchTrace_ != nullptr)
    {
        pitchTrace_->setClipState (state_);
        if (state_ != nullptr)
            pitchTrace_->setScaleInfo (state_->scaleRoot, state_->scaleType);
        pitchTrace_->repaint();
    }
    syncToolbarFromState();
    repaint();
}

void ApexTuneEditorComponent::markGestureUndoStarted()
{
    toolbarGestureActive_ = true;
    beginUndoableChange();
}

void ApexTuneEditorComponent::markGestureUndoEnded()
{
    toolbarGestureActive_ = false;
}

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::notifyModified()
{
    if (state_ != nullptr && ! applyingHistory_) state_->renderVersion++;
    syncToolbarFromState();
    if (pitchTrace_ != nullptr) pitchTrace_->repaint();
    if (onStateModified) onStateModified();
}

bool ApexTuneEditorComponent::keyPressed (const juce::KeyPress& key)
{
    const bool command = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    if (key == juce::KeyPress::escapeKey)
    {
        if (pitchTrace_ != nullptr) pitchTrace_->clearSelectedNotes();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::upKey)
        return pitchTrace_ != nullptr && pitchTrace_->nudgeSelectedNotes (1.0f, key.getModifiers().isShiftDown());
    if (key.getKeyCode() == juce::KeyPress::downKey)
        return pitchTrace_ != nullptr && pitchTrace_->nudgeSelectedNotes (-1.0f, key.getModifiers().isShiftDown());

    if (! command) return false;

    const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getTextCharacter());
    if (c == 'z') return undo();
    if (c == 'y') return redo();
    if (c == 'a')
    {
        if (pitchTrace_ != nullptr) pitchTrace_->selectAllNotes();
        return true;
    }
    if (c == 't')
        return pitchTrace_ != nullptr && pitchTrace_->splitSelectedNotes();
    if (c == 'j')
        return pitchTrace_ != nullptr && pitchTrace_->mergeSelectedNotes();
    return false;
}

void ApexTuneEditorComponent::timerCallback()
{
    if (pitchTrace_ == nullptr) return;
    const int64_t pos = transportPositionProvider_ ? transportPositionProvider_() : -1;
    pitchTrace_->setPlayheadSample (pos);
}

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::paint (juce::Graphics& g)
{
    g.fillAll (Col::VocalTune::background());

    if (state_ == nullptr)
    {
        g.setColour (Col::VocalTune::textDim());
        g.setFont (juce::Font (14.0f, juce::Font::plain));
        g.drawText ("No clip loaded",
                    getLocalBounds(),
                    juce::Justification::centred);
    }
}

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;

    auto area = getLocalBounds();

    if (toolbar_ != nullptr)
        toolbar_->setBounds (area.removeFromTop (kToolbarHeight));

    if (pianoGrid_ != nullptr)
        pianoGrid_->setBounds (area.removeFromLeft (kPianoWidth));

    if (pitchTrace_ != nullptr)
        pitchTrace_->setBounds (area);
}

}} // namespace apex::vocaltune
