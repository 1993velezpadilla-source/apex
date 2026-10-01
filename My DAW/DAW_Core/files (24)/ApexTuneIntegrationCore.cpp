// =============================================================================
//  ApexTuneIntegrationCore.cpp
//  See header. Orchestrates analyze -> session -> render -> cache flow.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneIntegrationCore.cpp
// =============================================================================

#include "ApexTuneIntegrationCore.h"
#include "ApexTuneSessionCore.h"

#include "ApexPitchDetectionCore.h"
#include "ApexNoteSegmentationCore.h"
#include "ApexSibilantDetectorCore.h"
#include "ApexTuneRenderCore.h"

namespace apex { namespace vocaltune {

// =============================================================================
//  SessionHost: a tiny juce::Component that owns the ApexTuneSessionCore and
//  lays out the editor to fill itself. Handed to the adapter for window
//  display; destruction of the host destructs the session.
// =============================================================================
namespace
{
    class SessionHost : public juce::Component
    {
    public:
        explicit SessionHost (std::unique_ptr<ApexTuneSessionCore> session)
            : session_ (std::move (session))
        {
            addAndMakeVisible (session_->getEditor());
            setSize (960, 480);
        }

        void resized() override
        {
            if (getWidth() <= 0 || getHeight() <= 0) return;
            session_->getEditor().setBounds (getLocalBounds());
        }

        ApexTuneSessionCore& session() { return *session_; }

    private:
        std::unique_ptr<ApexTuneSessionCore> session_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionHost)
    };
}

// =============================================================================
//  ApexTuneIntegrationCore
// =============================================================================
ApexTuneIntegrationCore::ApexTuneIntegrationCore (IApexTuneEngineAdapter& adapter)
    : adapter_ (adapter)
{
}

ApexTuneIntegrationCore::~ApexTuneIntegrationCore() = default;

// -----------------------------------------------------------------------------
ApexTuneIntegrationCore::ClipRecord&
ApexTuneIntegrationCore::getOrCreateRecord (const juce::String& clipId)
{
    // Caller holds recordsMutex_.
    auto it = records_.find (clipId);
    if (it == records_.end())
        it = records_.emplace (clipId, ClipRecord{}).first;
    return it->second;
}

ApexTuneIntegrationCore::ClipRecord*
ApexTuneIntegrationCore::findRecord (const juce::String& clipId)
{
    auto it = records_.find (clipId);
    return (it == records_.end()) ? nullptr : &it->second;
}

const ApexTuneIntegrationCore::ClipRecord*
ApexTuneIntegrationCore::findRecord (const juce::String& clipId) const
{
    auto it = records_.find (clipId);
    return (it == records_.end()) ? nullptr : &it->second;
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::openEditorForClip (const juce::String& clipId)
{
    // If a session is already open for this clip, ignore (idempotent).
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto* rec = findRecord (clipId);
        if (rec != nullptr && rec->activeSession != nullptr) return;
    }

    // Schedule analysis on a worker; presentEditor() runs on the message
    // thread after the worker finishes.
    runAnalysisJob (clipId);
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::runAnalysisJob (const juce::String& clipId)
{
    adapter_.scheduleBackgroundJob ([this, clipId]
    {
        // ---- Worker thread -----------------------------------------------
        auto payload = adapter_.loadClipMonoAudio (clipId);
        if (payload.samples.empty() || payload.sampleRate <= 0.0)
        {
            adapter_.postToMessageThread ([this, clipId]
            {
                adapter_.showError ("Vocal Tune",
                                    "Could not load audio for clip " + clipId);
            });
            return;
        }

        auto analysis  = ApexPitchDetectionCore::analyzeOffline
                            (payload.samples.data(), (int) payload.samples.size(),
                             payload.sampleRate);
        auto notes     = ApexNoteSegmentationCore::segment (analysis);
        auto sibilants = ApexSibilantDetectorCore::detect (analysis);
        ApexSibilantDetectorCore::markOverlappingNotes (notes, sibilants);

        // Hand results back to message thread by-value (moved into the lambda).
        adapter_.postToMessageThread (
            [this, clipId,
             payload  = std::move (payload),
             analysis = std::move (analysis),
             notes    = std::move (notes),
             sibilants = std::move (sibilants)] () mutable
            {
                presentEditor (clipId, std::move (payload),
                               std::move (analysis),
                               std::move (notes),
                               std::move (sibilants));
            });
    });
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::presentEditor (const juce::String& clipId,
                                             ApexTuneAudioPayload payload,
                                             ApexTuneAnalysis analysis,
                                             std::vector<ApexTuneNote> freshNotes,
                                             std::vector<ApexSibilantRegion> sibilants)
{
    // Pull (or create) the persistent record. If we already have edits saved
    // for this clip from a previous session or a project load, keep them;
    // otherwise seed from the fresh analysis-derived notes.
    ApexTuneClipState initialState;
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto& rec = getOrCreateRecord (clipId);

        if (rec.state.analyzed && ! rec.state.notes.empty())
        {
            // Existing edits -- preserve them.
            initialState = rec.state;
        }
        else
        {
            // First time analyzing -- seed with detected notes.
            initialState.clipId      = clipId;
            initialState.sourceFile  = payload.sourceFile;
            initialState.notes       = std::move (freshNotes);
            initialState.analyzed    = true;
            initialState.analysisVersion = 1;
            initialState.renderVersion   = 0;
        }
    }

    auto session = std::make_unique<ApexTuneSessionCore> (
        clipId, payload.sourceFile,
        std::move (payload.samples), payload.sampleRate,
        std::move (analysis), std::move (sibilants),
        std::move (initialState));

    // Wire callbacks.
    session->onStateModified = [this] (ApexTuneSessionCore& s)
    {
        // Copy live state into the persistent record so save/load sees it.
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto& rec = getOrCreateRecord (s.getClipId());
        rec.state = s.getState();
        // No render kicked off until user clicks Render.
        adapter_.markProjectDirty();
    };

    session->onRenderRequested = [this] (ApexTuneSessionCore& s)
    {
        renderForSession (s);
    };

    session->onBypassChanged = [this] (ApexTuneSessionCore& s, bool b)
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto& rec = getOrCreateRecord (s.getClipId());
        rec.bypassed = b;
        adapter_.markProjectDirty();
    };

    session->onSessionClosed = [this] (ApexTuneSessionCore& s)
    {
        // Final state sync + clear the activeSession pointer.
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto* rec = findRecord (s.getClipId());
        if (rec != nullptr)
        {
            rec->state         = s.getState();
            rec->activeSession = nullptr;
        }
    };

    // Register the active session so we don't open a second editor.
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto& rec = getOrCreateRecord (clipId);
        rec.activeSession = session.get();
        rec.state         = session->getState();   // initial sync
    }

    auto host = std::make_unique<SessionHost> (std::move (session));
    adapter_.showEditorWindow (std::move (host),
                               "Vocal Tune - " + payload.sourceFile.getFileName());
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::renderForSession (ApexTuneSessionCore& session)
{
    // Snapshot data on the message thread. Heavy buffers are shared_ptr so
    // the worker can safely outlive the editor window if the user closes it
    // mid-render. Notes are copied by value (small). Sample rate + version
    // are primitives.
    const juce::String clipId       = session.getClipId();
    const double       sr           = session.getSampleRate();
    const int          ver          = session.getState().renderVersion;
    const auto         notes        = session.getState().notes;
    auto               monoPtr      = session.getMonoPtr();
    auto               analysisPtr  = session.getAnalysisPtr();
    auto               sibilantsPtr = session.getSibilantsPtr();

    adapter_.scheduleBackgroundJob (
        [this, clipId, sr, ver, notes, monoPtr, analysisPtr, sibilantsPtr]
        {
            auto res = ApexTuneRenderCore::renderClipMono (
                monoPtr->data(), (int) monoPtr->size(), sr,
                *analysisPtr, notes, *sibilantsPtr);

            if (! res.success) return;

            // Publish to the record. Lock briefly to swap buffers atomically.
            std::lock_guard<std::mutex> lock (recordsMutex_);
            auto& rec = getOrCreateRecord (clipId);
            rec.tunedAudio          = std::move (res.audio);
            rec.tunedSampleRate     = res.sampleRate;
            rec.cachedRenderVersion = ver;
        });
}

// -----------------------------------------------------------------------------
const std::vector<float>*
ApexTuneIntegrationCore::getTunedAudioForClip (const juce::String& clipId) const
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    const auto* rec = findRecord (clipId);
    if (rec == nullptr) return nullptr;
    if (rec->bypassed) return nullptr;
    if (rec->tunedAudio.empty()) return nullptr;
    if (rec->cachedRenderVersion != rec->state.renderVersion) return nullptr;
    return &rec->tunedAudio;
}

double ApexTuneIntegrationCore::getTunedAudioSampleRate (const juce::String& clipId) const
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    const auto* rec = findRecord (clipId);
    return (rec != nullptr) ? rec->tunedSampleRate : 0.0;
}

// -----------------------------------------------------------------------------
const ApexTuneClipState*
ApexTuneIntegrationCore::getClipState (const juce::String& clipId) const
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    const auto* rec = findRecord (clipId);
    if (rec == nullptr || ! rec->state.analyzed) return nullptr;
    return &rec->state;
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::restoreClipState (const juce::String& clipId,
                                                ApexTuneClipState state)
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    auto& rec = getOrCreateRecord (clipId);
    rec.state               = std::move (state);
    rec.state.clipId        = clipId;
    rec.tunedAudio.clear();
    rec.cachedRenderVersion = -1;
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::onClipDeleted (const juce::String& clipId)
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    auto it = records_.find (clipId);
    if (it != records_.end())
    {
        // Note: if activeSession != nullptr the window is still open. The
        // session callbacks check the record on close; safe to erase here.
        records_.erase (it);
    }
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::setBypassedForClip (const juce::String& clipId, bool b)
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    auto& rec = getOrCreateRecord (clipId);
    rec.bypassed = b;
}

bool ApexTuneIntegrationCore::isBypassedForClip (const juce::String& clipId) const
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    const auto* rec = findRecord (clipId);
    return (rec != nullptr) && rec->bypassed;
}

}} // namespace apex::vocaltune
