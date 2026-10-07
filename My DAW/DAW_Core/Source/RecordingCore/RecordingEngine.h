#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../TransportCore/TransportController.h"
#include "../AudioEngineCore/AudioFileManager.h"
#include "RecordingDiskWriterCore.h"
#include "RecordingClipFinalizerCore.h"
#include "RecordingInputValidityCore.h"
#include "RecordingLifecycleStateCore.h"
#include "RecordingWriterStateCore.h"
#include "../CommandCore/CommandManager.h"
#include "../CommandCore/RecordCommands.h"
#include <cmath>
#include <cstring>

namespace DAW {

#ifndef APEX_RECORDING_DIAG_LOGS
#define APEX_RECORDING_DIAG_LOGS 0
#endif

#ifndef APEX_RECORDING_DIAG_DBG
#define APEX_RECORDING_DIAG_DBG 0
#endif

/**
 * RecordingEngine — unlimited-length, crash-safe multitrack audio recording.
 */
class RecordingEngine : public TransportController::Listener,
                        private juce::Timer
{
public:
    struct DiagnosticsSnapshot
    {
        int64_t recBlocks = 0;
        int64_t recSamplesOffered = 0;
        int64_t recShortVsCb = 0;
        int64_t recZeroOffered = 0;

        struct TrackSnapshot
        {
            TrackID trackId;
            int64_t dupInputBlocks = 0;
            RecordingDiskWriterCore::DiagnosticsSnapshot writer;
        };

        juce::Array<TrackSnapshot> tracks;
    };

    /** Holds take writers alive for the complete device callback, including
        the post-fader tap that runs inside AudioEngine. */
    class AudioCallbackScope
    {
    public:
        AudioCallbackScope(RecordingEngine& owner, int numSamples) noexcept
            : owner_(owner), numSamples_(juce::jmax(0, numSamples))
        {
            owner_.activeAudioCallbacks_.fetch_add(1, std::memory_order_acq_rel);
            capture_ = owner_.recording_.load(std::memory_order_acquire)
                && !owner_.stopping_.load(std::memory_order_acquire);
            if (capture_)
                owner_.beginPostFaderCallbackBlock();
        }

        ~AudioCallbackScope() noexcept
        {
            if (capture_)
                owner_.finishPostFaderCallbackBlock(numSamples_);
            owner_.activeAudioCallbacks_.fetch_sub(1, std::memory_order_acq_rel);
        }

        bool shouldCapture() const noexcept { return capture_; }

        AudioCallbackScope(const AudioCallbackScope&) = delete;
        AudioCallbackScope& operator=(const AudioCallbackScope&) = delete;

    private:
        RecordingEngine& owner_;
        int numSamples_ = 0;
        bool capture_ = false;
    };

    RecordingEngine()
        : diskThread_("DAW_RecordingDiskThread")
    {}

    ~RecordingEngine() override
    {
        shutdown();
    }

    void shutdown()
    {
        lifecycleState_.beginShutdown();
        stopFlushTimer();
        recording_.store(false, std::memory_order_release);
        if (transport_) transport_->removeListener(this);
        clearActiveTakeStates();
        jassert(activeAudioCallbacks_.load(std::memory_order_acquire) == 0);
        for (auto& rec : activeRecordings_)
            if (rec) rec->writer.stop();
        activeRecordings_.clear();
        writerStopRequested_.store(false, std::memory_order_release);
        stopping_.store(false, std::memory_order_release);
        finalizing_.store(false, std::memory_order_release);
        recordingFinalizedCallback_ = {};
        diskThread_.stopThread(3000);
        tracks_ = nullptr;
        clips_ = nullptr;
        transport_ = nullptr;
        audioFiles_ = nullptr;
    }

    void setSubsystems(TrackManager* tracks, ClipManager* clips,
                       TransportController* transport,
                       AudioFileManager* audioFiles)
    {
        if (transport_) transport_->removeListener(this);
        tracks_     = tracks;
        clips_      = clips;
        transport_  = transport;
        audioFiles_ = audioFiles;
        if (transport_) transport_->addListener(this);
    }

    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
        // Pre-allocate the print scratch buffer so the audio thread never
        // allocates when printing the track's plugin effects into a recording.
        const int safeBlockSize = juce::jmax(1, blockSize);
        const int realtimeScratchSamples = juce::jmax(safeBlockSize, 8192);
        printBuffer_.setSize(2, realtimeScratchSamples, false, false, true);
        if (!diskThread_.isThreadRunning())
            diskThread_.startThread(juce::Thread::Priority::high);
    }

    /** Set the folder where recorded WAV files are written. */
    void setProjectDirectory(const juce::File& dir)
    {
        projectDir_ = dir;
    }

    /** Assign which hardware input channel pair a track records from.
     *  Default is 0 (channels 0-1).  Set to 2 for channels 2-3, etc. */
    void setTrackInputChannels(const TrackID& trackId, int firstChannel)
    {
        inputChannelMap_[trackId] = juce::jmax(0, firstChannel);
    }

    int getTrackInputChannel(const TrackID& trackId) const
    {
        auto it = inputChannelMap_.find(trackId);
        return (it != inputChannelMap_.end()) ? it->second : 0;
    }

    void setRecordingBitDepth(int bits)
    {
        if (bits == 16 || bits == 24 || bits == 32)
            recordingBitDepth_ = bits;
    }

    int getRecordingBitDepth() const noexcept { return recordingBitDepth_; }

    void setRecordingFinalizedCallback(std::function<void()> callback)
    {
        recordingFinalizedCallback_ = std::move(callback);
    }

    // ── Recording lifecycle (message thread) ─────────────────────────────

    void beginRecording()
    {
        if (!tracks_ || !transport_) return;
        if (!lifecycleState_.getSnapshot().restartAllowed
            || stopping_.load(std::memory_order_acquire)
            || finalizing_.load(std::memory_order_acquire))
            return;

        jassert(sampleRate_ > 0.0 && "sampleRate_ read before prepareToPlay");
        jassert(blockSize_  > 0   && "blockSize_ read before prepareToPlay");
        if (sampleRate_ <= 0.0 || blockSize_ <= 0) return;

        stopFlushTimer();
        recording_.store(false, std::memory_order_seq_cst);
        // [APEX-DIAG-RECPATH] fresh counters for this take
        diagWetOk_.store(0, std::memory_order_relaxed);
        diagWetMissSilence_.store(0, std::memory_order_relaxed);
        diagDry_.store(0, std::memory_order_relaxed);
        diagPrintedButNoReader_.store(0, std::memory_order_relaxed);
        recBlocks_.store(0, std::memory_order_relaxed);
        recSamplesOffered_.store(0, std::memory_order_relaxed);
        recShortVsCb_.store(0, std::memory_order_relaxed);
        recZeroOffered_.store(0, std::memory_order_relaxed);
        writerStopRequested_.store(false, std::memory_order_relaxed);
        lastDiagnosticsSnapshot_ = DiagnosticsSnapshot{};
        clearActiveTakeStates();
        for (auto& rec : activeRecordings_)
            if (rec) rec->writer.stop();
        activeRecordings_.clear();

        bool anyArmed = false;
        for (int i = 0; i < tracks_->getNumTracks(); ++i)
            if (auto* t = tracks_->getTrack(i); t && t->isArmed())
                { anyArmed = true; break; }

        if (!anyArmed && tracks_->getNumTracks() > 0)
        {
            // Auto-arm the first track that has NO existing clips.
            // Previously this always armed track 0, which would record
            // over an imported beat or any other content already on that
            // track — the root cause of Bug #1.
            for (int i = 0; i < tracks_->getNumTracks(); ++i)
            {
                if (auto* t = tracks_->getTrack(i))
                {
                    if (clips_ != nullptr
                        && !clips_->getClipsOnTrack(t->getID()).isEmpty())
                        continue;  // skip tracks that already have clips
                    t->setArmed(true);
                    juce::Logger::writeToLog("[REC] auto-armed track=" + t->getID()
                        + " (index " + juce::String(i) + ")");
                    break;
                }
            }

            // Fallback: if ALL tracks have clips, arm the first one anyway
            // so recording can still proceed.
            bool anyNowArmed = false;
            for (int i = 0; i < tracks_->getNumTracks(); ++i)
                if (auto* t = tracks_->getTrack(i); t && t->isArmed())
                    { anyNowArmed = true; break; }
            if (!anyNowArmed)
                if (auto* first = tracks_->getTrack(0))
                {
                    first->setArmed(true);
                    juce::Logger::writeToLog("[REC] fallback auto-armed track=" + first->getID()
                        + " (all tracks have clips)");
                }
        }

        recordStartPos_ = transport_->getPosition();

        auto recordDir = projectDir_.exists()
            ? projectDir_.getChildFile("Recordings")
            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                  .getChildFile("DAW_Core_Projects").getChildFile("Recordings");
        recordDir.createDirectory();

        const auto ts = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        int fileIndex = 0;

        for (int i = 0; i < tracks_->getNumTracks(); ++i)
        {
            auto* track = tracks_->getTrack(i);
            if (!track || !track->isArmed()) continue;

            auto rec = std::make_unique<TrackRecording>();
            rec->trackID       = track->getID();
            rec->startPosition = recordStartPos_;
            // Track is the source of truth for input routing (per-track input
            // selector). The legacy channel map is a fallback.
            rec->firstInputCh  = track->getInputFirstChannel() > 0
                ? track->getInputFirstChannel()
                : getTrackInputChannel(track->getID());
            rec->monoInput     = track->isInputMono();
            rec->recordWet     = track->getRecordMode() == TrackRecordMode::PostFader;
            rec->outputFile    = recordDir.getChildFile(
                "rec_" + rec->trackID + "_" + ts
                + "_" + juce::String(fileIndex++) + ".wav");

            auto& liveWaveform = track->getLiveRecordWaveform();
            liveWaveform.reset(blockSize_);
            // C11: bind the take to the resolved track objects now (message
            // thread) instead of resolving per block on the audio thread.
            rec->resolvedTrack = track;
            rec->liveWaveform  = &liveWaveform;

            if (!rec->writer.start(rec->outputFile, sampleRate_,
                                   2, recordingBitDepth_, diskThread_, rec->recordWet))
            {
                juce::Logger::writeToLog("[REC] ERROR: could not create take file for track=" + rec->trackID
                    + " file=" + rec->outputFile.getFullPathName()
                    + " — check disk space/permissions. Track will NOT be recorded.");
                jassertfalse;
                continue;
            }

            liveWaveform.setTakeActive(true);

            juce::Logger::writeToLog("[REC] take started track=" + rec->trackID
                + (rec->monoInput
                    ? " input=ch" + juce::String(rec->firstInputCh) + " (mono)"
                    : " input=ch" + juce::String(rec->firstInputCh) + "/" + juce::String(rec->firstInputCh + 1))
                + (rec->recordWet ? " mode=POST_FADER" : " mode=DRY")
                + " file=" + rec->outputFile.getFullPathName());

            activeRecordings_.push_back(std::move(rec));
        }

        if (!activeRecordings_.empty())
        {
            juce::Logger::writeToLog("[REC] session rolling: " + juce::String((int) activeRecordings_.size())
                + " track(s), sampleRate=" + juce::String(sampleRate_)
                + " blockSize=" + juce::String(blockSize_)
                + " bitDepth=" + juce::String(recordingBitDepth_)
                + " startPos=" + juce::String((juce::int64) recordStartPos_));
            recording_.store(true, std::memory_order_release);
            startTimerHz(1);
        }
        else
        {
            juce::Logger::writeToLog("[REC] ERROR: transport is in record mode but NO takes could be started "
                                     "(no armed tracks or all take files failed) — nothing will be captured.");
        }
    }

    void stopRecording(const juce::File& /*projectDir*/)
    {
        const bool wasRecording = recording_.exchange(false, std::memory_order_seq_cst);
        if (!wasRecording)
            return;

        clearActiveTakeStates();

        bool expectedStopping = false;
        if (!stopping_.compare_exchange_strong(expectedStopping, true, std::memory_order_acq_rel))
            return;

        logDiagnostic("[APEX-DIAG-CRASH] stopRecording ENTER activeRecordings=" + juce::String((int) activeRecordings_.size()));
        finalizing_.store(true, std::memory_order_release);

        auto waitStart = juce::Time::getMillisecondCounter();
        while (activeAudioCallbacks_.load(std::memory_order_acquire) > 0)
        {
            if (juce::Time::getMillisecondCounter() - waitStart > 2000)
            {
                jassertfalse;
                finalizing_.store(false, std::memory_order_release);
                lifecycleState_.beginDeferredStop();
                deferredStopDeadlineMs_ = juce::Time::getMillisecondCounter()
                    + deferredStopTimeoutMs_;
                startTimerHz(20);
                if (activeAudioCallbacks_.load(std::memory_order_acquire) == 0)
                    finalizeStoppedRecordings();
                return;
            }

            juce::Thread::yield();
        }

        finalizeStoppedRecordings();
    }

    // ── Audio thread ─────────────────────────────────────────────────────

    /**
     * Capture input samples for all armed tracks.  Lock-free, no allocation.
     *
     * @param inputBuffer  Full hardware input buffer (may have 2+ channels)
     * @param numSamples        Block size
     * @param validInputChannels Number of callback-valid hardware input channels
     */
    void processBlock(const juce::AudioBuffer<float>& inputBuffer,
                      int numSamples,
                      int validInputChannels,
                      bool captureThisBlock)
    {
        if (!captureThisBlock)
            return;

        if (numSamples <= 0)
            return;

        const int validChannels = juce::jmin(
            inputBuffer.getNumChannels(),
            juce::jmax(0, validInputChannels));

        for (auto& rec : activeRecordings_)
        {
            if (!rec || !rec->writer.isActive()
                || rec->writerFailed.load(std::memory_order_acquire))
                continue;

            // C11: use the take's resolved pointers — no live TrackManager
            // iteration on the audio thread (data race / potential UAF).
            Track* track = rec->resolvedTrack;
            const bool recordDryBecauseMonitorIsOff = rec->recordWet
                && track != nullptr
                && track->getMonitoringState().getMode() == InputMonitorMode::Off;

            // Printed takes are written from the post-fader tap inside the
            // mixer. When monitoring is explicitly Off, the mic is deliberately
            // excluded from that processing path; keep the vocal take instead
            // of silently recording nothing. This fallback is dry and does not
            // turn monitoring on or send the mic to the speakers.
            if (rec->recordWet && !recordDryBecauseMonitorIsOff)
                continue;

            auto* liveWaveform = rec->liveWaveform != nullptr
                ? rec->liveWaveform
                : (track != nullptr ? &track->getLiveRecordWaveform() : nullptr);

            // ── DRY capture (pristine) ────────────────────────────────────────
            const int writableSamples = juce::jmin(numSamples, printBuffer_.getNumSamples());
            if (writableSamples <= 0)
                continue;

            const float* chans[2] = { nullptr, nullptr };
            const bool inputAvailable = RecordingInputValidityCore::prepareRouteSources(
                inputBuffer, rec->firstInputCh, rec->monoInput, validChannels,
                printBuffer_, writableSamples, chans);

            // Both a post-fader tap and the monitor-off dry fallback fulfill
            // this callback's take. Mark before enqueueing so a failed push is
            // not followed by a second block that would shift the sample clock.
            if (recordDryBecauseMonitorIsOff)
                rec->blockWritten = true;

            if (liveWaveform != nullptr)
                liveWaveform->setInputAvailable(inputAvailable);

            const bool writeAccepted = rec->writer.pushSamples(chans, writableSamples);
            if (liveWaveform != nullptr)
                RecordingWriterStateCore::publishWriteResult(
                    writeAccepted, *liveWaveform,
                    rec->writerFailed, writerStopRequested_);
            else if (!writeAccepted)
            {
                rec->writerFailed.store(true, std::memory_order_release);
                writerStopRequested_.store(true, std::memory_order_release);
            }
            diagDry_.fetch_add(1, std::memory_order_relaxed);                        // [APEX-DIAG-RECPATH]

            if (writeAccepted && inputAvailable && liveWaveform != nullptr)
            {
                float mn = 0.f, mx = 0.f;
                bool hasPeak = false;
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < writableSamples; ++s)
                    {
                        accumulateLivePeakSample(chans[ch][s], mn, mx, hasPeak);
                    }
                liveWaveform->pushPeak(mn, mx);
            }
        }
    }

    /** Called by AudioEngine after inserts, fader/pan and mute ramp. The tap
        only copies into the writer; it never changes the audible mix buffer. */
    void processPostFaderBlock(const TrackID& trackId,
                               const juce::AudioBuffer<float>& postFaderBuffer,
                               int numSamples,
                               bool captureThisBlock) noexcept
    {
        if (!captureThisBlock || numSamples <= 0
            || postFaderBuffer.getNumChannels() < 2
            || numSamples > postFaderBuffer.getNumSamples()
            || numSamples > printBuffer_.getNumSamples())
            return;

        for (auto& rec : activeRecordings_)
        {
            if (!rec || !rec->recordWet || rec->trackID != trackId
                || !rec->writer.isActive()
                || rec->writerFailed.load(std::memory_order_acquire))
                continue;

            // Mark before enqueueing: a rejected block must not be replaced by
            // a second silent block, which would skew the take's sample clock.
            rec->blockWritten = true;
            printBuffer_.copyFrom(0, 0, postFaderBuffer, 0, 0, numSamples);
            printBuffer_.copyFrom(1, 0, postFaderBuffer, 1, 0, numSamples);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* samples = printBuffer_.getWritePointer(ch);
                for (int sample = 0; sample < numSamples; ++sample)
                    if (!std::isfinite(samples[sample]) || std::abs(samples[sample]) < 1.0e-30f)
                        samples[sample] = 0.0f;
            }
            const float* channels[2] = {
                printBuffer_.getReadPointer(0),
                printBuffer_.getReadPointer(1)
            };
            const bool writeAccepted = rec->writer.pushSamples(channels, numSamples);
            auto* liveWaveform = rec->liveWaveform;
            if (liveWaveform != nullptr)
            {
                liveWaveform->setInputAvailable(true);
                RecordingWriterStateCore::publishWriteResult(
                    writeAccepted, *liveWaveform,
                    rec->writerFailed, writerStopRequested_);
            }
            else if (!writeAccepted)
            {
                rec->writerFailed.store(true, std::memory_order_release);
                writerStopRequested_.store(true, std::memory_order_release);
            }

            diagWetOk_.fetch_add(1, std::memory_order_relaxed);
            if (writeAccepted && liveWaveform != nullptr)
            {
                float mn = 0.0f, mx = 0.0f;
                bool hasPeak = false;
                for (int ch = 0; ch < 2; ++ch)
                    for (int sample = 0; sample < numSamples; ++sample)
                        accumulateLivePeakSample(channels[ch][sample], mn, mx, hasPeak);
                liveWaveform->pushPeak(mn, mx);
            }
        }
    }

    // ── Queries ──────────────────────────────────────────────────────────

    bool isRecording() const
    {
        return recording_.load(std::memory_order_acquire)
            || finalizing_.load(std::memory_order_acquire)
            || stopping_.load(std::memory_order_acquire);
    }

    bool isActivelyRecording() const noexcept
    {
        return recording_.load(std::memory_order_acquire);
    }

    /** C11: true while a take for this track is actively recording.
     *  Consulted by the TrackManager deletion gate (message thread) to
     *  refuse deletion of a track whose resolved take pointers are in use. */
    bool isTrackBeingRecorded(const TrackID& trackId) const
    {
        if (! recording_.load(std::memory_order_acquire))
            return false;
        for (const auto& rec : activeRecordings_)
            if (rec && rec->trackID == trackId && rec->writer.isActive())
                return true;
        return false;
    }

    RecordingLifecycleStateCore::Snapshot getLifecycleSnapshot() const noexcept
    {
        return lifecycleState_.getSnapshot();
    }

    int getNumActiveRecordings() const { return (int) activeRecordings_.size(); }

    int getSamplesRecorded() const
    {
        if (activeRecordings_.empty() || !activeRecordings_[0]) return 0;
        return activeRecordings_[0]->writer.getSamplesWritten();
    }

    void addRecorderInputDiagnosticBlock(int callbackNumSamples, int recordedSamples) noexcept
    {
        recBlocks_.fetch_add(1, std::memory_order_relaxed);
        recSamplesOffered_.fetch_add(recordedSamples, std::memory_order_relaxed);

        if (recordedSamples < callbackNumSamples)
            recShortVsCb_.fetch_add(1, std::memory_order_relaxed);

        if (recordedSamples == 0)
            recZeroOffered_.fetch_add(1, std::memory_order_relaxed);
    }

    DiagnosticsSnapshot getDiagnosticsSnapshot() const
    {
        if (activeRecordings_.empty())
            return lastDiagnosticsSnapshot_;

        DiagnosticsSnapshot snapshot;
        snapshot.recBlocks = recBlocks_.load(std::memory_order_relaxed);
        snapshot.recSamplesOffered = recSamplesOffered_.load(std::memory_order_relaxed);
        snapshot.recShortVsCb = recShortVsCb_.load(std::memory_order_relaxed);
        snapshot.recZeroOffered = recZeroOffered_.load(std::memory_order_relaxed);

        for (const auto& rec : activeRecordings_)
        {
            if (rec == nullptr)
                continue;

            DiagnosticsSnapshot::TrackSnapshot trackSnapshot;
            trackSnapshot.trackId = rec->trackID;
            trackSnapshot.dupInputBlocks = rec->dupInputBlocks.load(std::memory_order_relaxed);
            trackSnapshot.writer = rec->writer.getDiagnosticsSnapshot();
            snapshot.tracks.add(trackSnapshot);
        }

        return snapshot;
    }

    double getSecondsRecorded() const
    {
        return (sampleRate_ > 0.0) ? getSamplesRecorded() / sampleRate_ : 0.0;
    }

    bool hasAnyOverrun() const
    {
        for (auto& rec : activeRecordings_)
            if (rec && rec->writer.hasOverrun()) return true;
        return false;
    }

    void clearOverrunFlags()
    {
        for (auto& rec : activeRecordings_)
            if (rec) rec->writer.clearOverrun();
    }

    // ── TransportController::Listener ────────────────────────────────────

    void transportStateChanged() override
    {
        if (!transport_) return;

        if (transport_->isRecording())
        {
            if (finalizing_.load(std::memory_order_acquire))
            {
                lifecycleState_.requestRestartAfterFinalization();
                return;
            }

            if (!isRecording() && lifecycleState_.getSnapshot().restartAllowed)
                beginRecording();
        }
        else
        {
            lifecycleState_.cancelPendingRestart();

            if (recording_.load(std::memory_order_acquire))
            {
                // Transport just left record mode → finalize WAVs + clips
                auto dir = projectDir_.exists() ? projectDir_
                    : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                          .getChildFile("DAW_Core_Projects");
                stopRecording(dir);
            }
        }
    }

private:
    void finalizeStoppedRecordings()
    {
        jassert(activeAudioCallbacks_.load(std::memory_order_acquire) == 0);
        finalizing_.store(true, std::memory_order_release);
        stopFlushTimer();

        if (!clips_ || !audioFiles_)
        {
            for (auto& rec : activeRecordings_)
                if (rec) rec->writer.stop();
            activeRecordings_.clear();
            writerStopRequested_.store(false, std::memory_order_release);
            lifecycleState_.markSafeCleanupComplete();
            stopping_.store(false, std::memory_order_release);
            finalizing_.store(false, std::memory_order_release);
            return;
        }

        std::vector<RecordingClipFinalizerCore::FinalizeRequest> finalizeRequests;
        finalizeRequests.reserve(activeRecordings_.size());

        for (auto& rec : activeRecordings_)
        {
            if (!rec) continue;

            const int samplesWritten = rec->writer.getSamplesWritten();
            rec->writer.stop();

            juce::Logger::writeToLog("[REC] take stopped track=" + rec->trackID
                + " samplesWritten=" + juce::String(samplesWritten)
                + " (" + juce::String(sampleRate_ > 0.0 ? samplesWritten / sampleRate_ : 0.0, 2) + "s)"
                + " file=" + rec->outputFile.getFullPathName());

            logDiagnostic("[APEX-DIAG-CRASH] stopRecording pre-finalize track=" + rec->trackID
                + " samplesWritten=" + juce::String(samplesWritten)
                + " sampleRate=" + juce::String(sampleRate_)
                + " outputFile=" + rec->outputFile.getFullPathName());

#if APEX_RECORDING_DIAG_DBG
            if (samplesWritten <= 0)
                DBG("[APEX-DIAG-RECPATH] stopRecording track=" << rec->trackID
                    << " wrote 0 samples (will not finalize clip). firstInputCh=" << rec->firstInputCh);
#endif

            RecordingClipFinalizerCore::FinalizeRequest req;
            req.trackId        = rec->trackID;
            req.startPosition  = rec->startPosition;
            req.samplesWritten = samplesWritten;
            req.outputFile     = rec->outputFile;
            req.clipManager    = clips_;
            req.trackManager   = tracks_;
            req.audioFiles     = audioFiles_;
            req.sampleRate     = sampleRate_;
            finalizeRequests.push_back(req);
        }

        logDiagnostic("[APEX-DIAG-RECPATH] end totals"
            + juce::String(" wetOk=") + juce::String(diagWetOk_.load(std::memory_order_relaxed))
            + juce::String(" wetMissSilence=") + juce::String(diagWetMissSilence_.load(std::memory_order_relaxed))
            + juce::String(" dry=") + juce::String(diagDry_.load(std::memory_order_relaxed))
            + juce::String(" printedButNoReader=") + juce::String(diagPrintedButNoReader_.load(std::memory_order_relaxed)));

        const double durationSeconds = sampleRate_ > 0.0
            ? (double) recSamplesOffered_.load(std::memory_order_relaxed) / sampleRate_
            : 0.0;
        juce::String summary = "[APEX-DIAG-SUMMARY] dur=" + juce::String(durationSeconds, 3)
            + "s cb=" + juce::String(recBlocks_.load(std::memory_order_relaxed))
            + " | REC offered=" + juce::String(recSamplesOffered_.load(std::memory_order_relaxed))
            + " shortVsCb=" + juce::String(recShortVsCb_.load(std::memory_order_relaxed))
            + " zeroOffered=" + juce::String(recZeroOffered_.load(std::memory_order_relaxed));

        lastDiagnosticsSnapshot_.recBlocks = recBlocks_.load(std::memory_order_relaxed);
        lastDiagnosticsSnapshot_.recSamplesOffered = recSamplesOffered_.load(std::memory_order_relaxed);
        lastDiagnosticsSnapshot_.recShortVsCb = recShortVsCb_.load(std::memory_order_relaxed);
        lastDiagnosticsSnapshot_.recZeroOffered = recZeroOffered_.load(std::memory_order_relaxed);
        lastDiagnosticsSnapshot_.tracks.clear();

        for (const auto& rec : activeRecordings_)
        {
            if (rec == nullptr)
                continue;

            const auto writerDiag = rec->writer.getDiagnosticsSnapshot();
            DiagnosticsSnapshot::TrackSnapshot trackSnapshot;
            trackSnapshot.trackId = rec->trackID;
            trackSnapshot.dupInputBlocks = rec->dupInputBlocks.load(std::memory_order_relaxed);
            trackSnapshot.writer = writerDiag;
            lastDiagnosticsSnapshot_.tracks.add(trackSnapshot);

            summary << " | track=" << rec->trackID
                    << " WR attempt=" << writerDiag.pushAttempts
                    << " acc=" << writerDiag.pushAccepted
                    << " rej=" << writerDiag.pushRejected
                    << " sAcc=" << writerDiag.pushSamplesAccepted
                    << " sRej=" << writerDiag.pushSamplesRejected
                    << " DUP dupInBlk=" << rec->dupInputBlocks.load(std::memory_order_relaxed);
        }
        juce::Logger::writeToLog(summary);

        activeRecordings_.clear();
        writerStopRequested_.store(false, std::memory_order_release);
        lifecycleState_.markSafeCleanupComplete();
        stopping_.store(false, std::memory_order_release);

        juce::Component::SafePointer<DummySafeComponent> safeFinalizer(&safeComponent_);
        auto callback = recordingFinalizedCallback_;
        auto requestsForMessageThread = std::make_shared<
            std::vector<RecordingClipFinalizerCore::FinalizeRequest>>(std::move(finalizeRequests));
        const bool posted = juce::MessageManager::callAsync(
            [requestsForMessageThread, callback, safeFinalizer, this]() mutable
        {
            if (safeFinalizer == nullptr)
                return;

            if (lifecycleState_.getSnapshot().shutdownStarted)
            {
                finalizing_.store(false, std::memory_order_release);
                return;
            }

            // Collect every clip this record pass produced so the whole pass
            // lands on the unified undo history as ONE step.
            std::vector<RecordAudioTakeCommand::Take> takes;
            takes.reserve(requestsForMessageThread->size());

            for (const auto& req : *requestsForMessageThread)
            {
                if (auto* clip = RecordingClipFinalizerCore::finalize(req))
                {
                    RecordAudioTakeCommand::Take take;
                    take.state      = clip->getState();
                    take.sourceFile = req.outputFile;
                    take.currentId  = clip->getID();
                    takes.push_back(std::move(take));
                }
            }

            if (!takes.empty() && clips_ != nullptr)
                CommandManager::getInstance().execute(
                    std::make_unique<RecordAudioTakeCommand>(*clips_, audioFiles_, std::move(takes)));

            finalizing_.store(false, std::memory_order_release);

            if (callback)
                callback();

            const bool transportStillRecording = transport_ != nullptr
                && transport_->isRecording();
            if (lifecycleState_.consumeRestartAfterFinalization(transportStillRecording))
                beginRecording();
        });

        if (!posted)
        {
            for (const auto& request : *requestsForMessageThread)
                juce::Logger::writeToLog(
                    "[REC] finalizer dispatch failed; preserving recoverable WAV: "
                    + request.outputFile.getFullPathName());
            lifecycleState_.markFinalizationDispatchFailed();
            finalizing_.store(false, std::memory_order_release);
        }
    }

    void enterTerminalStopFailure()
    {
        clearActiveTakeStates();
        writerStopRequested_.store(false, std::memory_order_release);
        stopping_.store(false, std::memory_order_release);
        finalizing_.store(false, std::memory_order_release);
        stopFlushTimer();
        logDiagnostic("[REC] terminal stop failure: callback did not drain before second deadline; writers retained until shutdown");
    }

    void clearActiveTakeStates() noexcept
    {
        if (tracks_ == nullptr)
            return;

        for (const auto& rec : activeRecordings_)
            if (rec != nullptr)
                if (auto* track = tracks_->getTrack(rec->trackID))
                {
                    auto& waveform = track->getLiveRecordWaveform();
                    waveform.setInputAvailable(false);
                    waveform.setTakeActive(false);
                }
    }

    static uint64_t hashFirstInputSamples(const float* samples, int numSamples) noexcept
    {
        if (samples == nullptr || numSamples <= 0)
            return 0;

        const int hashSamples = juce::jmin(8, numSamples);
        uint64_t hash = 1469598103934665603ull;

        for (int i = 0; i < hashSamples; ++i)
        {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(samples[i]), "float size mismatch");
            std::memcpy(&bits, samples + i, sizeof(bits));
            hash ^= (uint64_t) bits;
            hash *= 1099511628211ull;
        }

        return hash;
    }

    static void accumulateLivePeakSample(float sample, float& mn, float& mx, bool& hasPeak) noexcept
    {
        if (! std::isfinite(sample))
            return;

        sample = juce::jlimit(-1.0f, 1.0f, sample);

        if (! hasPeak)
        {
            mn = sample;
            mx = sample;
            hasPeak = true;
            return;
        }

        mn = juce::jmin(mn, sample);
        mx = juce::jmax(mx, sample);
    }

    static void logDiagnostic(const juce::String& message)
    {
#if APEX_RECORDING_DIAG_LOGS
        juce::Logger::writeToLog(message);
#else
        juce::ignoreUnused(message);
#endif
    }

    void timerCallback() override
    {
        if (lifecycleState_.getSnapshot().stopState
            == RecordingLifecycleStateCore::StopState::pendingDrain)
        {
            const bool callbacksDrained =
                activeAudioCallbacks_.load(std::memory_order_acquire) == 0;
            const bool deadlineExpired = hasReachedDeadline(
                juce::Time::getMillisecondCounter(), deferredStopDeadlineMs_);
            const auto action = lifecycleState_.pollDeferredStop(
                callbacksDrained, deadlineExpired);

            if (action == RecordingLifecycleStateCore::StopPollAction::finalize)
                finalizeStoppedRecordings();
            else if (action == RecordingLifecycleStateCore::StopPollAction::terminalFailure)
                enterTerminalStopFailure();
            return;
        }

        if (RecordingWriterStateCore::consumeStopRequest(writerStopRequested_))
        {
            if (transport_ != nullptr && transport_->isRecording())
                transport_->stop();
            else if (recording_.load(std::memory_order_acquire))
                stopRecording(projectDir_);
            return;
        }

        for (auto& rec : activeRecordings_)
            if (rec) rec->writer.flushToDisk();
    }

    void stopFlushTimer()
    {
        if (isTimerRunning()) stopTimer();
    }

    static bool hasReachedDeadline(uint32_t now, uint32_t deadline) noexcept
    {
        return static_cast<int32_t>(now - deadline) >= 0;
    }

    struct TrackRecording
    {
        TrackID                 trackID;
        SamplePosition          startPosition { 0 };
        int                     firstInputCh  { 0 };
        bool                    monoInput     { false };
        bool                    recordWet     { false };
        bool                    blockWritten   { false }; // audio callback only
        juce::File              outputFile;
        RecordingDiskWriterCore writer;
        uint64_t                lastInputHash { 0 };
        int                     lastInputNumSamples { 0 };
        bool                    hasLastInputHash { false };
        std::atomic<int64_t>    dupInputBlocks { 0 };
        std::atomic<bool>       writerFailed { false };
        // C11: resolved once at beginRecording (message thread) — the audio
        // thread never iterates the live TrackManager array. Deletion of the
        // track while this recording is active is refused by the
        // TrackManager deletion gate installed by ApplicationCore.
        Track*                  resolvedTrack { nullptr };
        LiveRecordWaveformCore* liveWaveform  { nullptr };
    };

    void beginPostFaderCallbackBlock() noexcept
    {
        for (auto& rec : activeRecordings_)
            if (rec && rec->recordWet)
                rec->blockWritten = false;
    }

    void finishPostFaderCallbackBlock(int numSamples) noexcept
    {
        if (numSamples <= 0 || printBuffer_.getNumChannels() < 2
            || printBuffer_.getNumSamples() <= 0)
            return;

        const int scratchCapacity = printBuffer_.getNumSamples();
        for (auto& rec : activeRecordings_)
        {
            if (!rec || !rec->recordWet || rec->blockWritten
                || !rec->writer.isActive()
                || rec->writerFailed.load(std::memory_order_acquire))
                continue;

            // If Auto/On monitoring was active but routing/plugin processing
            // was suspended or the track node was unavailable, preserve the
            // take's clock with silence. Monitor Off has its explicit dry-input
            // fallback in processBlock().
            rec->blockWritten = true;
            if (rec->liveWaveform != nullptr)
                rec->liveWaveform->setInputAvailable(false);

            int remaining = numSamples;
            while (remaining > 0)
            {
                const int chunk = juce::jmin(remaining, scratchCapacity);
                printBuffer_.clear(0, chunk);
                const float* silence[2] = {
                    printBuffer_.getReadPointer(0), printBuffer_.getReadPointer(1)
                };
                const bool writeAccepted = rec->writer.pushSamples(silence, chunk);
                if (rec->liveWaveform != nullptr)
                    RecordingWriterStateCore::publishWriteResult(
                        writeAccepted, *rec->liveWaveform,
                        rec->writerFailed, writerStopRequested_);
                else if (!writeAccepted)
                {
                    rec->writerFailed.store(true, std::memory_order_release);
                    writerStopRequested_.store(true, std::memory_order_release);
                }

                if (!writeAccepted)
                    break;
                remaining -= chunk;
            }
            diagWetMissSilence_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    TrackManager*        tracks_     = nullptr;
    ClipManager*         clips_      = nullptr;
    TransportController* transport_  = nullptr;
    AudioFileManager*    audioFiles_ = nullptr;

    double sampleRate_ = 0.0;
    int    blockSize_  = 0;
    int    recordingBitDepth_ = 24;
    juce::File projectDir_;

    // Per-track input channel assignment (trackID → first channel index)
    std::map<TrackID, int> inputChannelMap_;

    std::vector<std::unique_ptr<TrackRecording>> activeRecordings_;
    SamplePosition recordStartPos_ = 0;
    juce::AudioBuffer<float> printBuffer_;   // scratch for printing track FX (audio thread)
    std::atomic<bool> recording_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> finalizing_{false};
    std::atomic<bool> writerStopRequested_{false};
    std::atomic<int> activeAudioCallbacks_{0};
    RecordingLifecycleStateCore lifecycleState_;
    uint32_t deferredStopDeadlineMs_ = 0;
    static constexpr uint32_t deferredStopTimeoutMs_ = 2000;
    std::atomic<int64_t> recBlocks_ { 0 };
    std::atomic<int64_t> recSamplesOffered_ { 0 };
    std::atomic<int64_t> recShortVsCb_ { 0 };
    std::atomic<int64_t> recZeroOffered_ { 0 };
    DiagnosticsSnapshot lastDiagnosticsSnapshot_;
    // [APEX-DIAG-RECPATH] per-take path counters (audio-thread incremented,
    // message-thread read). Reset at beginRecording(), logged at stopRecording().
    std::atomic<int> diagWetOk_              { 0 };
    std::atomic<int> diagWetMissSilence_     { 0 };
    std::atomic<int> diagDry_                { 0 };
    std::atomic<int> diagPrintedButNoReader_ { 0 };
    juce::TimeSliceThread diskThread_;
    std::function<void()> recordingFinalizedCallback_;

    struct DummySafeComponent final : public juce::Component {} safeComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingEngine)
};

} // namespace DAW
