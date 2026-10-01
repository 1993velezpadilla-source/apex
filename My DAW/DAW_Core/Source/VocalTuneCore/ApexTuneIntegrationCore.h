// =============================================================================
//  ApexTuneIntegrationCore.h
//  The single entry point APEX calls to integrate vocal tune.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneIntegrationCore.h
//  Depends on: JUCE, ApexTuneTypes.h, IApexTuneEngineAdapter.h.
//  Touches:    nothing else in the APEX codebase.
//
//  Lifecycle:
//   APEX creates ONE instance at app startup, hands it an IEngineAdapter,
//   and calls into it from various subsystems. Destruction happens at app
//   shutdown.
//
//  APEX integration checklist (each is a SINGLE call site in APEX code):
//
//   [1] App startup:
//         integrationCore_ = std::make_unique<ApexTuneIntegrationCore>(adapter);
//
//   [2] Right-click clip menu builder:
//         menu.addItem ("Vocal Tune", [this, clipId]
//             { integrationCore_->openEditorForClip (clipId); });
//
//   [3] Playback audio fetch (per clip, per processBlock):
//         if (auto* tuned = integrationCore_->getTunedAudioForClip (clipId))
//             playFromBuffer (*tuned);     // use tuned audio
//         else
//             playFromOriginal();          // dry source -- bypass, missing,
//                                          // or no edits yet
//
//   [4] Offline export -- if the export path uses the same clip-fetch as
//       playback, no extra call is needed (export gets tuned audio for free).
//
//   [5] Project save (in your project serializer, for each audio clip):
//         if (auto state = integrationCore_->getClipState (clipId))
//             clipTree.appendChild (
//                 ApexTuneProjectStateCore::toValueTree (*state), nullptr);
//
//   [6] Project load (in your project deserializer, for each audio clip):
//         if (auto stateTree = clipTree.getChildWithName (
//                 ApexTuneProjectStateCore::idRoot); stateTree.isValid())
//         {
//             ApexTuneClipState s;
//             if (ApexTuneProjectStateCore::fromValueTree (stateTree, s))
//                 integrationCore_->restoreClipState (clipId, std::move (s));
//         }
//
//   [7] Clip deletion (if your clip model has explicit delete):
//         integrationCore_->onClipDeleted (clipId);
//
//  All other behavior -- analysis worker scheduling, editor window display,
//  render caching, dirty-marking -- is handled inside IntegrationCore via
//  the adapter.
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"
#include "ApexSibilantDetectorCore.h"
#include "IApexTuneEngineAdapter.h"
#include "../UICore/ForensicAuditWindow.h"

#include <unordered_map>
#include <mutex>

namespace apex { namespace vocaltune {

class ApexTuneSessionCore;

class ApexTuneIntegrationCore
{
public:
    explicit ApexTuneIntegrationCore (IApexTuneEngineAdapter& adapter);
    ~ApexTuneIntegrationCore();

    // -------------------------------------------------------------------------
    //  Entry point [2]: user picked "Vocal Tune" on a clip.
    //  Idempotent -- safe to call multiple times for the same clip; only
    //  one editor opens.
    // -------------------------------------------------------------------------
    void openEditorForClip (const juce::String& clipId);

    // -------------------------------------------------------------------------
    //  Entry point [3]: playback / export wants the clip's audio.
    //  Returns:
    //   - pointer to a tuned mono buffer if (a) the clip has been analyzed,
    //     (b) the bypass flag is OFF, and (c) a render cache hit exists for
    //     the current renderVersion.
    //   - nullptr in every other case (caller uses the original clip audio).
    //
    //  Thread-safe: callable from the audio thread. Internally takes a
    //  short lock; never allocates. The returned pointer is valid until the
    //  next call to render or until the clip state is removed.
    // -------------------------------------------------------------------------
    //  Audio thread: returns the tuned render for a clip, or nullptr when
    //  unavailable/bypassed. Lock-free (published snapshot) and the returned
    //  shared_ptr keeps the audio alive for the duration of the block.
    std::shared_ptr<const std::vector<float>> getTunedAudioForClip (const juce::String& clipId) const noexcept;

    // Sample rate of the cached tuned audio for `clipId`.
    // Returns 0.0 if no tuned audio is available.
    double getTunedAudioSampleRate (const juce::String& clipId) const;

    // -------------------------------------------------------------------------
    //  Entry point [5]: project save asks for serializable state.
    //  Returns the stored state or nullopt if this clip has none.
    // -------------------------------------------------------------------------
    const ApexTuneClipState* getClipState (const juce::String& clipId) const;

    // -------------------------------------------------------------------------
    //  Entry point [6]: project load restores per-clip state.
    // -------------------------------------------------------------------------
    void restoreClipState (const juce::String& clipId, ApexTuneClipState state);

    // -------------------------------------------------------------------------
    //  Called by ProjectManager after restoring state for a clip that already
    //  has notes (was previously analyzed + rendered). Schedules a background
    //  load → analyze → render so tuned audio is warm without opening the editor.
    // -------------------------------------------------------------------------
    void scheduleRerenderAfterLoad (const juce::String& clipId);

    // -------------------------------------------------------------------------
    //  Entry point [7]: clip deleted in APEX.
    // -------------------------------------------------------------------------
    void onClipDeleted (const juce::String& clipId);

    // -------------------------------------------------------------------------
    //  Bypass control. When true, getTunedAudioForClip() always returns
    //  nullptr regardless of renders cached, so APEX plays the dry source.
    //  Per-clip.
    // -------------------------------------------------------------------------
    void setBypassedForClip (const juce::String& clipId, bool bypassed);
    bool isBypassedForClip  (const juce::String& clipId) const;

private:
    // Per-clip persistent record kept across editor sessions.
    struct ClipRecord
    {
        ApexTuneClipState   state;
        std::shared_ptr<const std::vector<float>> tunedAudio;   // last render output (shared ownership)
        double              tunedSampleRate = 0.0;
        int                 cachedRenderVersion = -1;
        bool                bypassed = false;
        juce::String         lastAvailabilityReason;

        // Set while an editor session is active for this clip; null otherwise.
        ApexTuneSessionCore* activeSession = nullptr;
    };

    IApexTuneEngineAdapter& adapter_;

    mutable std::mutex                            recordsMutex_;
    std::unordered_map<juce::String, ClipRecord>  records_;
    mutable std::unordered_map<juce::String, juce::String> missingAvailabilityReasons_;

    // ── RT-safe snapshot for audio thread (avoids recordsMutex_ on audio thread) ──
    struct ClipAudioSnapshot
    {
        std::shared_ptr<const std::vector<float>> tunedAudio;   // shared ownership — never dangles
        double tunedSampleRate = 0.0;
        bool bypassed = false;
    };
    struct AudioSnapshot
    {
        std::unordered_map<juce::String, ClipAudioSnapshot> clips;
    };
    // Published with a release store, acquired lock-free on the audio thread
    // (previously guarded by snapshotMutex_ — a std::mutex taken up to 3x per
    // clip per audio block).
    std::shared_ptr<const AudioSnapshot> publishedAudioSnapshot_;

    /** Build and publish an immutable audio snapshot from the records map.
     *  MUST be called from the message thread after any render/bypass/state change. */
    void publishAudioSnapshot();

    // --- Internal helpers ---------------------------------------------------
    ClipRecord& getOrCreateRecord  (const juce::String& clipId);
    ClipRecord* findRecord         (const juce::String& clipId);
    const ClipRecord* findRecord   (const juce::String& clipId) const;

    void runAnalysisJob   (const juce::String& clipId);
    void presentEditor    (const juce::String& clipId,
                           ApexTuneAudioPayload payload,
                           ApexTuneAnalysis     analysis,
                           std::vector<ApexTuneNote>        notes,
                           std::vector<ApexSibilantRegion>  sibilants);
    void attachSessionCallbacks (ApexTuneSessionCore& session);
    void renderForSession (ApexTuneSessionCore& session);
    void handleSessionStateModified (ApexTuneSessionCore& session);
    void handleSessionRenderRequested (ApexTuneSessionCore& session);
    void handleSessionBypassChanged (ApexTuneSessionCore& session, bool bypassed);
    void handleSessionClosed (ApexTuneSessionCore& session);
    static void logAvailabilityTransition (ClipRecord& rec,
                                           const juce::String& clipId,
                                           const juce::String& reason);
    static void logMissingAvailabilityTransition (std::unordered_map<juce::String, juce::String>& reasons,
                                                  const juce::String& clipId,
                                                  const juce::String& reason);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexTuneIntegrationCore)
};

}} // namespace apex::vocaltune
