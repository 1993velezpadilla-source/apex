// =============================================================================
//  ApexTuneSessionCore.cpp
//  See header.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneSessionCore.cpp
// =============================================================================

#include "ApexTuneSessionCore.h"

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
ApexTuneSessionCore::ApexTuneSessionCore (juce::String                    clipId,
                                          juce::File                      sourceFile,
                                          std::vector<float>              monoAudio,
                                          double                          sampleRate,
                                          ApexTuneAnalysis                analysis,
                                          std::vector<ApexSibilantRegion> sibilants,
                                          ApexTuneClipState               initialState)
    : clipId_      (std::move (clipId))
    , sourceFile_  (std::move (sourceFile))
    , mono_        (std::make_shared<std::vector<float>>             (std::move (monoAudio)))
    , analysis_    (std::make_shared<ApexTuneAnalysis>               (std::move (analysis)))
    , sibilants_   (std::make_shared<std::vector<ApexSibilantRegion>> (std::move (sibilants)))
    , sampleRate_  (sampleRate)
    , state_       (std::move (initialState))
{
    // Make sure state's identity fields are correct.
    state_.clipId     = clipId_;
    state_.sourceFile = sourceFile_;
    state_.analyzed   = true;

    editor_ = std::make_unique<ApexTuneEditorComponent>();
    editor_->setClipState   (&state_);
    editor_->setAnalysis    (analysis_.get());
    editor_->setSourceAudio (mono_->data(), (int) mono_->size(), sampleRate_);

    editor_->onStateModified = [this]
    {
        if (onStateModified) onStateModified (*this);
    };

    editor_->onRenderRequested = [this]
    {
        if (onRenderRequested) onRenderRequested (*this);
    };

    editor_->onBypassChanged = [this] (bool b)
    {
        if (onBypassChanged) onBypassChanged (*this, b);
    };
}

// -----------------------------------------------------------------------------
ApexTuneSessionCore::~ApexTuneSessionCore()
{
    stopTimer();

    // Detach the editor from our buffers before destroying anything else.
    if (editor_ != nullptr)
    {
        editor_->setClipState   (nullptr);
        editor_->setAnalysis    (nullptr);
        editor_->setSourceAudio (nullptr, 0, 0.0);
    }

    if (onSessionClosed) onSessionClosed (*this);
}

void ApexTuneSessionCore::requestDeferredRender()
{
    startTimer (250);
}

void ApexTuneSessionCore::timerCallback()
{
    stopTimer();
    if (onRenderRequested) onRenderRequested (*this);
}

}} // namespace apex::vocaltune
