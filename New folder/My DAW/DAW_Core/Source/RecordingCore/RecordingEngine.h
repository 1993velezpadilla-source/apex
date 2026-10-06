#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../TransportCore/TransportController.h"
#include "../AudioEngineCore/AudioFileManager.h"
#include "RecordingDiskWriterCore.h"
#include "RecordingClipFinalizerCore.h"
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

    using WetBlockReader = bool (*)(void* context, const TrackID& trackId, float* destL, float* destR, int numSamples);

    RecordingEngine()
        : diskThread_("DAW_RecordingDiskThread")
    {}

    ~RecordingEngine() override
    {
        stopFlushTimer();
        if (transport_) transport_->removeListener(this);
        for (auto& rec : activeRecordings_)
            if (rec) rec->writer.stop();
        activeRecordings_.clear();
        diskThread_.stopThread(3000);
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

    /** Set the total number of hardware input channels available. */
    void setInputChannelCount(int numInputChannels)
    {
        numHardwareInputs_ = juce::jmax(0, numInputChannels);
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

    void setLivePrintedCachePrepareCallback(std::function<void(const juce::Array<TrackID>&, int)> callback)
    {
        livePrintedCachePrepareCallback_ = std::move(callback);
    }

    juce::Array<TrackID> getActiveWetRecordingTrackIds() const
    {
        juce::Array<TrackID> result;
        result.ensureStorageAllocated((int) activeRecordings_.size());

        for (const auto& rec : activeRecordings_)
            if (rec != nullptr && rec->recordWet)
                result.add(rec->trackID);

        return result;
    }

    // ── Recording lifecycle (message thread) ─────────────────────────────

    void beginRecording()
    {
        if (!tracks_ || !transport_) return;

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
        lastDiagnosticsSnapshot_ = DiagnosticsSnapshot{};
        for (auto& rec : activeRecordings_)
            if (rec) rec->writer.stop();
        activeRecordings_.clear();

        bool anyArmed = false;
        for (int i = 0; i < tracks_->getNumTracks(); ++i)
            if (auto* t = tracks_->getTrack(i); t && t->isArmed())
                { anyArmed = true; break; }

        if (!anyArmed && tracks_->getNumTracks() > 0)
            if (auto* first = tracks_->getTrack(0))
                first->setArmed(true);

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
            // "Record With Effects (printed)" — only print when the track has a
            // plugin chain and the user selected the wet-record mode.
            rec->recordWet     = (track->getMonitoringState().getRecordingMode()
                                      == RecordInputRouter::Mode::MonitorWetRecordWet)
                                 && track->getPluginChain() != nullptr;
#if APEX_RECORDING_DIAG_DBG
            DBG("[APEX-DIAG-RECPATH] begin track=" << rec->trackID
                << " recordWet=" << (int) rec->recordWet
                << " hasChain=" << (int) (track->getPluginChain() != nullptr)
                << " sampleRate=" << sampleRate_
                << " blockSize=" << blockSize_);
#endif
            rec->outputFile    = recordDir.getChildFile(
                "rec_" + rec->trackID + "_" + ts
                + "_" + juce::String(fileIndex++) + ".wav");

            if (!rec->writer.start(rec->outputFile, sampleRate_,
                                   2, recordingBitDepth_, diskThread_))
            {
                juce::Logger::writeToLog("[REC] ERROR: could not create take file for track=" + rec->trackID
                    + " file=" + rec->outputFile.getFullPathName()
                    + " — check disk space/permissions. Track will NOT be recorded.");
                jassertfalse;
                continue;
            }

            juce::Logger::writeToLog("[REC] take started track=" + rec->trackID
                + (rec->monoInput
                    ? " input=ch" + juce::String(rec->firstInputCh) + " (mono)"
                    : " input=ch" + juce::String(rec->firstInputCh) + "/" + juce::String(rec->firstInputCh + 1))
                + (rec->recordWet ? " mode=WET(printed)" : " mode=DRY")
                + " file=" + rec->outputFile.getFullPathName());

            activeRecordings_.push_back(std::move(rec));
        }

        if (livePrintedCachePrepareCallback_)
            livePrintedCachePrepareCallback_(getActiveWetRecordingTrackIds(), blockSize_);

        if (!activeRecordings_.empty())
        {
            juce::Logger::writeToLog("[REC] session rolling: " + juce::String((int) activeRecordings_.size())
                + " track(s), sampleRate=" + juce::String(sampleRate_)
                + " blockSize=" + juce::String(blockSize_)
                + " bitDepth=" + juce::String(recordingBitDepth_)
                + " startPos=" + juce::String((juce::int64) recordStartPos_));
            recording_.store(true, std::memory_order_release);
            // Reset live waveform buffers for all armed tracks
            for (auto& rec : activeRecordings_)
                if (rec)
                    if (auto* track = tracks_->getTrack(rec->trackID))
                        track->getLiveRecordWaveform().reset(blockSize_);
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
                stopping_.store(false, std::memory_order_release);
                finalizing_.store(false, std::memory_order_release);
                return;
            }

            juce::Thread::yield();
        }

        stopFlushTimer();

        if (!clips_ || !audioFiles_)
        {
            for (auto& rec : activeRecordings_)
                if (rec) rec->writer.stop();
            activeRecordings_.clear();
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
        stopping_.store(false, std::memory_order_release);

        juce::Component::SafePointer<DummySafeComponent> safeFinalizer(&safeComponent_);
        auto callback = recordingFinalizedCallback_;
        juce::MessageManager::callAsync([requests = std::move(finalizeRequests), callback, safeFinalizer, this]() mutable
        {
            if (safeFinalizer == nullptr)
                return;

            // Collect every clip this record pass produced so the whole pass
            // lands on the unified undo history as ONE step (pro-DAW model:
            // Ctrl+Z after recording removes the take(s); redo restores them
            // with audio, WAV files stay on disk).
            std::vector<RecordAudioTakeCommand::Take> takes;
            takes.reserve(requests.size());

            for (const auto& req : requests)
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
        });
    }

    // ── Audio thread ─────────────────────────────────────────────────────

    /**
     * Capture input samples for all armed tracks.  Lock-free, no allocation.
     *
     * @param inputBuffer  Full hardware input buffer (may have 2+ channels)
     * @param numSamples   Block size
     */
    void processBlock(const juce::AudioBuffer<float>& inputBuffer, int numSamples,
                      void* wetBlockContext = nullptr,
                      WetBlockReader wetBlockReader = nullptr)
    {
        struct AudioCallbackGuard
        {
            explicit AudioCallbackGuard(std::atomic<int>& count) noexcept : count_(count)
            {
                count_.fetch_add(1, std::memory_order_acq_rel);
            }

            ~AudioCallbackGuard() noexcept
            {
                count_.fetch_sub(1, std::memory_order_acq_rel);
            }

            std::atomic<int>& count_;
        } guard(activeAudioCallbacks_);

        if (!recording_.load(std::memory_order_acquire)
            || stopping_.load(std::memory_order_acquire))
            return;

        const int inputChans = inputBuffer.getNumChannels();
        if (inputChans <= 0 || numSamples <= 0)
            return;

        for (auto& rec : activeRecordings_)
        {
            if (!rec || !rec->writer.isActive()) continue;

            const int srcL = rec->firstInputCh;
            const int srcR = rec->monoInput ? rec->firstInputCh : rec->firstInputCh + 1;

            if (srcL >= inputChans) continue;

            const float* chans[2];
            chans[0] = inputBuffer.getReadPointer(srcL);
            chans[1] = (srcR < inputChans) ? inputBuffer.getReadPointer(srcR) : chans[0];

            // ── PRINTED ("Record With Effects") ───────────────────────────────
            // The wet signal MUST come from the main audio engine's already-
            // processed track output, delivered via wetBlockReader. The recorder
            // must NEVER run the plugin chain itself: track->getPluginChain() is
            // the same instance the engine processes every block, so processing
            // it here would advance stateful plugins twice (double processing).
            if (rec->recordWet
                && wetBlockContext != nullptr
                && wetBlockReader  != nullptr
                && printBuffer_.getNumChannels() >= 2)
            {
                if (auto* track = tracks_ ? tracks_->getTrack(rec->trackID) : nullptr)
                {
                    const int writableSamples = juce::jmin(numSamples, printBuffer_.getNumSamples());
                    if (writableSamples <= 0)
                        continue;

                    float* pL = printBuffer_.getWritePointer(0);
                    float* pR = printBuffer_.getWritePointer(1);

                    const bool gotWet =
                        wetBlockReader(wetBlockContext, rec->trackID, pL, pR, writableSamples);

                    if (!gotWet)
                    {
                        // Cache miss (track not processed this block). Write
                        // silence to keep the file sample-aligned. Never fall
                        // back to the live chain (R1) or to dry (R3).
                        juce::FloatVectorOperations::clear(pL, writableSamples);
                        juce::FloatVectorOperations::clear(pR, writableSamples);
                        diagWetMissSilence_.fetch_add(1, std::memory_order_relaxed); // [APEX-DIAG-RECPATH]
                    }
                    else
                    {
                        diagWetOk_.fetch_add(1, std::memory_order_relaxed);          // [APEX-DIAG-RECPATH]
                    }

                    const float* wetChans[2] = { pL, pR };
                    rec->writer.pushSamples(wetChans, writableSamples);

                    if (auto* lw = &track->getLiveRecordWaveform())
                    {
                        float mn = 0.f, mx = 0.f;
                        bool hasPeak = false;
                        for (int ch = 0; ch < 2; ++ch)
                            for (int s = 0; s < writableSamples; ++s)
                            {
                                accumulateLivePeakSample(wetChans[ch][s], mn, mx, hasPeak);
                            }
                        lw->pushPeak(mn, mx);
                    }
                    continue;
                }
            }

            // If printed mode was requested but no wet reader is wired (only
            // possible for non-app callers — the live app always wires it), we
            // cannot safely print wet without risking double-processing, so we
            // record a pristine DRY capture instead.
            if (rec->recordWet && (wetBlockContext == nullptr || wetBlockReader == nullptr))
                diagPrintedButNoReader_.fetch_add(1, std::memory_order_relaxed);     // [APEX-DIAG-RECPATH]

            // ── DRY capture (pristine) ────────────────────────────────────────
            const int writableSamples = juce::jmin(numSamples, printBuffer_.getNumSamples());
            if (writableSamples <= 0)
                continue;

            rec->writer.pushSamples(chans, writableSamples);
            diagDry_.fetch_add(1, std::memory_order_relaxed);                        // [APEX-DIAG-RECPATH]

            if (tracks_)
                if (auto* track = tracks_->getTrack(rec->trackID))
                {
                    float mn = 0.f, mx = 0.f;
                    bool hasPeak = false;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int s = 0; s < writableSamples; ++s)
                        {
                            accumulateLivePeakSample(chans[ch][s], mn, mx, hasPeak);
                        }
                    track->getLiveRecordWaveform().pushPeak(mn, mx);
                }
        }
    }

    // ── Queries ──────────────────────────────────────────────────────────

    bool isRecording() const
    {
        return recording_.load(std::memory_order_acquire)
            || finalizing_.load(std::memory_order_acquire);
    }

    bool isActivelyRecording() const noexcept
    {
        return recording_.load(std::memory_order_acquire);
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

        if (transport_->isRecording() && !isRecording())
        {
            // Transport just entered record mode → start capture
            beginRecording();
        }
        else if (!transport_->isRecording() && recording_.load(std::memory_order_acquire))
        {
            // Transport just left record mode → finalize WAVs + clips
            auto dir = projectDir_.exists() ? projectDir_
                : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                      .getChildFile("DAW_Core_Projects");
            stopRecording(dir);
        }
    }

private:
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
        for (auto& rec : activeRecordings_)
            if (rec) rec->writer.flushToDisk();
    }

    void stopFlushTimer()
    {
        if (isTimerRunning()) stopTimer();
    }

    struct TrackRecording
    {
        TrackID                 trackID;
        SamplePosition          startPosition { 0 };
        int                     firstInputCh  { 0 };
        bool                    monoInput     { false };
        bool                    recordWet     { false };
        juce::File              outputFile;
        RecordingDiskWriterCore writer;
        uint64_t                lastInputHash { 0 };
        int                     lastInputNumSamples { 0 };
        bool                    hasLastInputHash { false };
        std::atomic<int64_t>    dupInputBlocks { 0 };
    };

    TrackManager*        tracks_     = nullptr;
    ClipManager*         clips_      = nullptr;
    TransportController* transport_  = nullptr;
    AudioFileManager*    audioFiles_ = nullptr;

    double sampleRate_ = 0.0;
    int    blockSize_  = 0;
    int    numHardwareInputs_ = 2;
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
    std::atomic<int> activeAudioCallbacks_{0};
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
    std::function<void(const juce::Array<TrackID>&, int)> livePrintedCachePrepareCallback_;

    struct DummySafeComponent final : public juce::Component {} safeComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingEngine)
};

} // namespace DAW
