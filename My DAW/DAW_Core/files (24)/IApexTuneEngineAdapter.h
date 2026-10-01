// =============================================================================
//  IApexTuneEngineAdapter.h
//  Pure-virtual interface APEX implements so the VocalTune system can run
//  without knowing anything about APEX's clip system, threading model,
//  window hosting, or project model.
//
//  Drop-in: Source/VocalTuneCore/IApexTuneEngineAdapter.h
//  Depends on: JUCE.
//  Touches:    nothing else in the APEX codebase.
//
//  APEX-side responsibility:
//   Implement ONE class that derives from this interface and forwards each
//   call into the appropriate existing APEX subsystem (ClipCore, worker
//   pool, window manager, project model). Hand that instance to
//   ApexTuneIntegrationCore on construction.
//
//  Threading contract:
//   - loadClipMonoAudio() called on the worker thread that runs analysis.
//   - scheduleBackgroundJob() must NOT block; it queues a job.
//   - postToMessageThread() must arrange the lambda to run on the message
//     thread (juce::MessageManager::callAsync is the canonical impl).
//   - showEditorWindow(), markProjectDirty(), showError() called on the
//     message thread.
// =============================================================================

#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
//  Mono audio buffer handed from APEX to the VocalTune pipeline.
//  Empty `samples` indicates a load failure.
// -----------------------------------------------------------------------------
struct ApexTuneAudioPayload
{
    std::vector<float> samples;       // mono, -1..+1
    double             sampleRate = 0.0;
    juce::File         sourceFile;    // for cache fingerprinting + UI display
};

// -----------------------------------------------------------------------------
class IApexTuneEngineAdapter
{
public:
    virtual ~IApexTuneEngineAdapter() = default;

    // -------------------------------------------------------------------------
    //  Resolve a clip ID to its mono audio + sample rate + source file.
    //  Called from the analysis worker thread. May do blocking disk I/O.
    //  Return an ApexTuneAudioPayload with empty samples on failure.
    // -------------------------------------------------------------------------
    virtual ApexTuneAudioPayload loadClipMonoAudio (const juce::String& clipId) = 0;

    // -------------------------------------------------------------------------
    //  Queue `job` to run on a background worker. Must not block. The job
    //  may take seconds (full clip analysis + render).
    //
    //  Canonical implementation:
    //      myThreadPool.addJob (new LambdaJob (std::move (job)), true);
    // -------------------------------------------------------------------------
    virtual void scheduleBackgroundJob (std::function<void()> job) = 0;

    // -------------------------------------------------------------------------
    //  Arrange `task` to run on the message thread. Used by analysis workers
    //  to deliver results / show editor windows.
    //
    //  Canonical implementation:
    //      juce::MessageManager::callAsync (std::move (task));
    // -------------------------------------------------------------------------
    virtual void postToMessageThread (std::function<void()> task) = 0;

    // -------------------------------------------------------------------------
    //  Host the supplied editor component. APEX decides the window strategy
    //  (DialogWindow, dockable panel, modal, etc.) and takes ownership of
    //  the component for the duration of its visibility.
    //
    //  The component must be deleted when the user closes the window.
    //  Returning ownership via setOwned() on a DialogWindow content holder
    //  is the simplest implementation.
    //
    //  `title` is a suggested window title -- APEX may override.
    // -------------------------------------------------------------------------
    virtual void showEditorWindow (std::unique_ptr<juce::Component> editor,
                                   const juce::String& title) = 0;

    // -------------------------------------------------------------------------
    //  Mark APEX's current project as dirty (has unsaved changes).
    //  Called after any user edit that mutates clip state.
    // -------------------------------------------------------------------------
    virtual void markProjectDirty() = 0;

    // -------------------------------------------------------------------------
    //  Show a non-blocking error to the user. Used when analysis or load
    //  fails. APEX decides the UI (AlertWindow, toast, status bar).
    // -------------------------------------------------------------------------
    virtual void showError (const juce::String& title,
                            const juce::String& message) = 0;
};

}} // namespace apex::vocaltune
