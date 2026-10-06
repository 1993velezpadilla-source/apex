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
    toolbar_    = std::make_unique<ApexTuneToolBarComponent>();
    pianoGrid_  = std::make_unique<ApexTunePianoGridComponent>();
    pitchTrace_ = std::make_unique<ApexTunePitchTraceComponent>();

    addAndMakeVisible (*toolbar_);
    addAndMakeVisible (*pianoGrid_);
    addAndMakeVisible (*pitchTrace_);

    wireToolbar();

    pitchTrace_->onNoteEdited = [this]
    {
        notifyModified();
    };

    pitchTrace_->onNoteSelected = [this] (int /*idx*/)
    {
        // Reserved for Phase 6b: show selected-note details in toolbar.
    };

    setSize (960, 480);
}

// -----------------------------------------------------------------------------
ApexTuneEditorComponent::~ApexTuneEditorComponent() = default;

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::setClipState (ApexTuneClipState* state)
{
    state_ = state;
    if (pitchTrace_ != nullptr) pitchTrace_->setClipState (state);

    if (state_ != nullptr && pitchTrace_ != nullptr)
        pitchTrace_->setScaleInfo (state_->scaleRoot, state_->scaleType);

    // Sync toolbar to the "average" state of the notes -- if all notes have
    // the same correctionAmount we display it; otherwise we leave defaults.
    if (state_ != nullptr && toolbar_ != nullptr && ! state_->notes.empty())
    {
        const auto& first = state_->notes.front();
        bool sameCorr   = true, sameDrift = true, sameMod = true, sameForm = true;

        for (const auto& n : state_->notes)
        {
            if (n.correctionAmount  != first.correctionAmount)  sameCorr  = false;
            if (n.driftAmount       != first.driftAmount)       sameDrift = false;
            if (n.modulationAmount  != first.modulationAmount)  sameMod   = false;
            if (n.formantShift      != first.formantShift)      sameForm  = false;
        }

        if (sameCorr)  toolbar_->setCorrectionValue (first.correctionAmount);
        if (sameDrift) toolbar_->setDriftValue      (first.driftAmount);
        if (sameMod)   toolbar_->setModulationValue (first.modulationAmount);
        if (sameForm)  toolbar_->setFormantValue    (first.formantShift);
    }

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
void ApexTuneEditorComponent::wireToolbar()
{
    if (toolbar_ == nullptr) return;

    toolbar_->onCorrectionChanged = [this] (float v)
    {
        applyToAllNotes ([v] (ApexTuneNote& n) { n.correctionAmount = v; });
        notifyModified();
    };

    toolbar_->onDriftChanged = [this] (float v)
    {
        applyToAllNotes ([v] (ApexTuneNote& n) { n.driftAmount = v; });
        notifyModified();
    };

    toolbar_->onModulationChanged = [this] (float v)
    {
        applyToAllNotes ([v] (ApexTuneNote& n) { n.modulationAmount = v; });
        notifyModified();
    };

    toolbar_->onFormantChanged = [this] (float v)
    {
        applyToAllNotes ([v] (ApexTuneNote& n) { n.formantShift = v; });
        notifyModified();
    };

    toolbar_->onSnapToScaleClicked = [this]
    {
        if (state_ == nullptr) return;
        ApexScaleSnapCore::snapAll (*state_);
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

// -----------------------------------------------------------------------------
void ApexTuneEditorComponent::notifyModified()
{
    if (state_ != nullptr) state_->renderVersion++;
    if (pitchTrace_ != nullptr) pitchTrace_->repaint();
    if (onStateModified) onStateModified();
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
