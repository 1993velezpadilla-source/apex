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

void ApexTuneIntegrationCore::logAvailabilityTransition (ClipRecord& rec,
                                                         const juce::String& clipId,
                                                         const juce::String& reason)
{
    if (rec.lastAvailabilityReason == reason)
        return;

    FORENSIC_LOG("[VOCAL TUNE AVAILABILITY] clipId=" << clipId << " state=" << reason);
    rec.lastAvailabilityReason = reason;
}

void ApexTuneIntegrationCore::logMissingAvailabilityTransition (std::unordered_map<juce::String, juce::String>& reasons,
                                                                const juce::String& clipId,
                                                                const juce::String& reason)
{
    auto it = reasons.find (clipId);
    if (it != reasons.end() && it->second == reason)
        return;

    FORENSIC_LOG("[VOCAL TUNE AVAILABILITY] clipId=" << clipId << " state=" << reason);
    reasons[clipId] = reason;
}

// -----------------------------------------------------------------------------
ApexTuneIntegrationCore::ClipRecord&
ApexTuneIntegrationCore::getOrCreateRecord (const juce::String& clipId)
{
    // Caller holds recordsMutex_.
    missingAvailabilityReasons_.erase (clipId);
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

        const bool sourceMatches = rec.state.sourceFile == juce::File()
            || rec.state.sourceFile.getFullPathName() == payload.sourceFile.getFullPathName();

        if (rec.state.analyzed && ! rec.state.notes.empty() && sourceMatches)
        {
            // Existing edits -- preserve them.
            initialState = rec.state;
        }
        else
        {
            // First analysis, or source audio changed -- seed with fresh notes.
            initialState.clipId      = clipId;
            initialState.sourceFile  = payload.sourceFile;
            initialState.notes       = std::move (freshNotes);
            initialState.analyzed    = true;
            initialState.analysisVersion = rec.state.analysisVersion + 1;
            initialState.renderVersion   = 0;
            initialState.bypassed        = rec.bypassed;
        }
    }

    auto session = std::make_unique<ApexTuneSessionCore> (
        clipId, payload.sourceFile,
        std::move (payload.samples), payload.sampleRate,
        std::move (analysis), std::move (sibilants),
        std::move (initialState));

    session->getEditor().setTransportPositionProvider ([this]
    {
        return adapter_.getTransportSamplePosition();
    });

    attachSessionCallbacks (*session);

    // Push the persisted bypass state into the editor so the toolbar reflects it.
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto* rec = findRecord (clipId);
        if (rec != nullptr)
            session->getEditor().setBypassed (rec->bypassed);
    }

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
    session.getEditor().setRenderInProgress (true);

    adapter_.scheduleBackgroundJob (
        [this, clipId, sr, ver, notes, monoPtr, analysisPtr, sibilantsPtr]
        {
            auto res = ApexTuneRenderCore::renderClipMono (
                monoPtr->data(), (int) monoPtr->size(), sr,
                *analysisPtr, notes, *sibilantsPtr);

            if (! res.success)
            {
                adapter_.postToMessageThread ([this, clipId]
                {
                    std::lock_guard<std::mutex> lock (recordsMutex_);
                    if (auto* rec = findRecord (clipId))
                        if (rec->activeSession != nullptr)
                            rec->activeSession->getEditor().setRenderInProgress (false);
                });
                return;
            }

            // Publish to the record. Lock briefly to swap buffers atomically.
            {
                std::lock_guard<std::mutex> lock (recordsMutex_);
                auto& rec = getOrCreateRecord (clipId);
                rec.tunedAudio          = std::make_shared<const std::vector<float>> (std::move (res.audio));
                rec.tunedSampleRate     = res.sampleRate;
                rec.cachedRenderVersion = ver;
            }
            // Publish snapshot outside recordsMutex_ to avoid deadlock with snapshotMutex_.
            publishAudioSnapshot();

            adapter_.postToMessageThread ([this, clipId]
            {
                std::lock_guard<std::mutex> lock (recordsMutex_);
                if (auto* rec = findRecord (clipId))
                    if (rec->activeSession != nullptr)
                        rec->activeSession->getEditor().setRenderInProgress (false);
            });
        });
}

// -----------------------------------------------------------------------------
// RT-safe audio-thread accessors — read from prebuilt snapshot, no recordsMutex_
// -----------------------------------------------------------------------------

std::shared_ptr<const std::vector<float>>
ApexTuneIntegrationCore::getTunedAudioForClip (const juce::String& clipId) const noexcept
{
    auto published = std::atomic_load_explicit (&publishedAudioSnapshot_, std::memory_order_acquire);
    if (!published) return nullptr;
    auto it = published->clips.find (clipId);
    if (it == published->clips.end()) return nullptr;
    const auto& snap = it->second;
    if (snap.bypassed || snap.tunedAudio == nullptr || snap.tunedSampleRate <= 0.0)
        return nullptr;
    return snap.tunedAudio;
}

double ApexTuneIntegrationCore::getTunedAudioSampleRate (const juce::String& clipId) const
{
    auto published = std::atomic_load_explicit (&publishedAudioSnapshot_, std::memory_order_acquire);
    if (!published) return 0.0;
    auto it = published->clips.find (clipId);
    if (it == published->clips.end()) return 0.0;
    return it->second.tunedSampleRate;
}

// -----------------------------------------------------------------------------
// Message-thread snapshot publisher
// -----------------------------------------------------------------------------

void ApexTuneIntegrationCore::publishAudioSnapshot()
{
    auto snap = std::make_shared<AudioSnapshot>();
    std::unique_lock<std::mutex> lock (recordsMutex_);
    snap->clips.reserve (records_.size());
    for (const auto& [id, rec] : records_)
    {
        ClipAudioSnapshot cs;
        cs.tunedAudio       = (rec.tunedAudio == nullptr || rec.tunedAudio->empty()) ? nullptr : rec.tunedAudio;
        cs.tunedSampleRate  = rec.tunedSampleRate;
        cs.bypassed         = rec.bypassed;
        snap->clips[id] = cs;
    }
    // Publish with a release store — the audio thread acquires lock-free.
    // (lock releases before the store so the snapshot never publishes under recordsMutex_)
    lock.unlock();
    const std::shared_ptr<const AudioSnapshot> immutableSnap = std::move (snap);
    std::atomic_store_explicit (&publishedAudioSnapshot_, std::move (immutableSnap), std::memory_order_release);
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
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto& rec = getOrCreateRecord (clipId);
        rec.state               = std::move (state);
        rec.state.clipId        = clipId;
        rec.bypassed            = rec.state.bypassed;  // restore persisted bypass
        rec.tunedAudio.reset();
        rec.tunedSampleRate     = 0.0;
        rec.cachedRenderVersion = -1;
        rec.lastAvailabilityReason = {};
    }
    publishAudioSnapshot();
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::scheduleRerenderAfterLoad (const juce::String& clipId)
{
    // Only bother if the restored state already has notes (was previously tuned).
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto* rec = findRecord (clipId);
        if (rec == nullptr || rec->state.notes.empty()) return;
    }

    adapter_.scheduleBackgroundJob ([this, clipId]
    {
        auto payload = adapter_.loadClipMonoAudio (clipId);
        if (payload.samples.empty() || payload.sampleRate <= 0.0) return;

        // Re-use the restored notes (not fresh analysis) so the render exactly
        // matches the user's last edit session.
        std::vector<ApexTuneNote>     notes;
        std::vector<ApexSibilantRegion> sibilants;
        int renderVer = 0;
        {
            std::lock_guard<std::mutex> lock (recordsMutex_);
            auto* rec = findRecord (clipId);
            if (rec == nullptr) return;
            notes     = rec->state.notes;
            renderVer = rec->state.renderVersion;
        }

        auto analysis  = ApexPitchDetectionCore::analyzeOffline (
                             payload.samples.data(), (int) payload.samples.size(),
                             payload.sampleRate);
        sibilants = ApexSibilantDetectorCore::detect (analysis);

        auto res = ApexTuneRenderCore::renderClipMono (
                       payload.samples.data(), (int) payload.samples.size(),
                       payload.sampleRate, analysis, notes, sibilants);

        if (! res.success) return;

        {
            std::lock_guard<std::mutex> lock (recordsMutex_);
            auto& rec = getOrCreateRecord (clipId);
            rec.tunedAudio          = std::make_shared<const std::vector<float>> (std::move (res.audio));
            rec.tunedSampleRate     = res.sampleRate;
            rec.cachedRenderVersion = renderVer;
            DBG ("[VOCAL TUNE] scheduleRerenderAfterLoad: clip=" + clipId
                 + " samples=" + juce::String ((int) (rec.tunedAudio != nullptr ? rec.tunedAudio->size() : 0))
                 + " version=" + juce::String (renderVer));
        }
        publishAudioSnapshot();
    });
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::onClipDeleted (const juce::String& clipId)
{
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        missingAvailabilityReasons_.erase (clipId);
        auto it = records_.find (clipId);
        if (it != records_.end())
        {
            // Note: if activeSession != nullptr the window is still open. The
            // session callbacks check the record on close; safe to erase here.
            records_.erase (it);
        }
    }
    publishAudioSnapshot();
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::setBypassedForClip (const juce::String& clipId, bool b)
{
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto& rec = getOrCreateRecord (clipId);
        rec.bypassed       = b;
        rec.state.bypassed = b;  // keep persisted copy in sync
        logAvailabilityTransition (rec, clipId, b ? "bypassed" : "bypass_disabled");
    }
    publishAudioSnapshot();
}

bool ApexTuneIntegrationCore::isBypassedForClip (const juce::String& clipId) const
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    const auto* rec = findRecord (clipId);
    return (rec != nullptr) && rec->bypassed;
}

// -----------------------------------------------------------------------------
void ApexTuneIntegrationCore::handleSessionStateModified (ApexTuneSessionCore& session)
{
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto& rec = getOrCreateRecord (session.getClipId());
        rec.state = session.getState();
        rec.lastAvailabilityReason = {};
    }

    session.requestDeferredRender();
    adapter_.markProjectDirty();
}

void ApexTuneIntegrationCore::attachSessionCallbacks (ApexTuneSessionCore& session)
{
    session.onStateModified = [this] (ApexTuneSessionCore& s)
    {
        handleSessionStateModified (s);
    };

    session.onRenderRequested = [this] (ApexTuneSessionCore& s)
    {
        handleSessionRenderRequested (s);
    };

    session.onBypassChanged = [this] (ApexTuneSessionCore& s, bool bypassed)
    {
        handleSessionBypassChanged (s, bypassed);
    };

    session.onSessionClosed = [this] (ApexTuneSessionCore& s)
    {
        handleSessionClosed (s);
    };
}

void ApexTuneIntegrationCore::handleSessionRenderRequested (ApexTuneSessionCore& session)
{
    renderForSession (session);
}

void ApexTuneIntegrationCore::handleSessionBypassChanged (ApexTuneSessionCore& session, bool bypassed)
{
    session.getMutableState().bypassed = bypassed;   // keep session copy in sync for handleSessionClosed
    {
        std::lock_guard<std::mutex> lock (recordsMutex_);
        auto& rec = getOrCreateRecord (session.getClipId());
        rec.bypassed       = bypassed;
        rec.state.bypassed = bypassed;
        adapter_.markProjectDirty();
    }
    publishAudioSnapshot();
}

void ApexTuneIntegrationCore::handleSessionClosed (ApexTuneSessionCore& session)
{
    std::lock_guard<std::mutex> lock (recordsMutex_);
    auto* rec = findRecord (session.getClipId());
    if (rec != nullptr)
    {
        rec->state         = session.getState();
        rec->activeSession = nullptr;
    }
}

}} // namespace apex::vocaltune
