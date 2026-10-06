#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../TransportCore/TransportController.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../RoutingCore/RoutingSnapshot.h"
#include "../RoutingCore/RoutingSnapshotPublisher.h"
#include "../SoundEngineCore/ApexSoundEngineNucleus.h"
#include "AudioFileManager.h"
#include "VolumeRampCore.h"
#include "ConnectionGainRampCore.h"
#include "MuteFadeCore.h"
#include "../MasterCore/MasterPdcCore.h"
#include "../MeteringCore/MeteringFacadeCore.h"
#include "../MeteringCore/TrackPeakMeterManagerCore.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../PluginHostCore/PluginPlayheadInfoCore.h"
#include "../PluginHostCore/ClipRegionPluginCore.h"
#include "../FolderBusCore/FolderBusStateModel.h"
#include "../MidiCore/MidiInputCore.h"
#include "../InputMonitorCore/TrackInputProcessorCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../Automation/AutomationSystemCore.h"
#include "../TapeStop/TapeStopProcessorCore.h"
#include "../VocalTuneCore/ApexTuneIntegrationCore.h"
// TimePitch DSP nuclei
#include "../../Builds/VisualStudio2026/ArrangementEditor/TimePitchDSPCore.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/ClipIndependentPitchCore.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/PitchSmootherCore.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/VoiceTransformDSPCore.h"
#include "../MidiCore/PianoRollPlaybackCore.h"
#include "../MidiCore/VirtualMidiKeyboardCore.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/VoiceTransformMapperCore.h"
#include "../UICore/ForensicAuditWindow.h"
#include <cstdint>
#include <map>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <limits>

#ifndef APEX_AUDIO_DEBUG_LOGS
#define APEX_AUDIO_DEBUG_LOGS 0
#endif

#if ! APEX_AUDIO_DEBUG_LOGS
#undef DBG
#define DBG(x) do {} while (false)
#endif

// ═══════════════════════════════════════════════════════════════════════════
// FORENSIC AUDIT — Path Counters
// Track which audio processing path is actually being used at runtime.
// ═══════════════════════════════════════════════════════════════════════════
namespace PitchAuditCore {
    inline std::atomic<int> normalPathHits{0};
    inline std::atomic<int> independentPitchPathHits{0};
    inline std::atomic<int> stretchOnlyPathHits{0};
    inline std::atomic<int> oldTimePitchDSPHits{0};
    inline std::atomic<int> resampleTapeHits{0};
}

namespace DAW {

static inline float applyClipFadeCurve(float x, int curve = 0) noexcept
{
    x = juce::jlimit(0.0f, 1.0f, x);
    switch (juce::jlimit(0, 3, curve))
    {
        case 1:  return x * x;
        case 2:  return 1.0f - (1.0f - x) * (1.0f - x);
        case 3:  return x * x * (3.0f - 2.0f * x);
        default: return x;
    }
}

static inline void sanitizeStereoBuffer(juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    const int channels = juce::jmin(2, buffer.getNumChannels());
    constexpr float subnormalFloor = 1.0e-30f;

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            float v = data[s];
            if (!std::isfinite(v) || std::abs(v) < subnormalFloor)
                data[s] = 0.0f;
        }
    }
}

/**
 * AudioEngine — real-time audio processor.
 *
 * Processes nodes in routing-graph topological order:
 *   Track nodes → Bus nodes → Master node → hardware output.
 *
 * Per-node: source rendering, plugin inserts, volume, pan, mute/solo,
 *           send taps (pre/post), peak metering.
 *
 * All audio MUST pass through the Master node before reaching hardware.
 * NO track or bus may bypass Master in default routing.
 *
 * Thread safety: process() runs on the audio thread.
 * All shared state uses atomics or lock-free reads.
 */
class AudioEngine : public juce::Timer
{
public:
    AudioEngine() = default;

    void setSubsystems(TrackManager* tracks, ClipManager* clips,
                       TransportController* transport, RoutingGraph* routing,
                       AudioFileManager* audioFiles = nullptr)
    {
        tracks_     = tracks;
        clips_      = clips;
        transport_  = transport;
        routing_    = routing;
        audioFiles_ = audioFiles;
    }

    /** Supply the per-track plugin chains (owned by ApplicationCore). */
    void setPluginChains(std::map<TrackID, std::unique_ptr<PluginChainCore>>* chains)
    {
        pluginChains_ = chains;
    }

    /** Supply MIDI playback core for MIDI tracks. */
    void setMidiPlayback(DAW::PianoRollPlaybackCore* pb) noexcept { midiPlayback_ = pb; }
    void setMidiInput(DAW::MidiInputCore* input) noexcept { midiInput_ = input; }
    void setVirtualKeyboard(DAW::VirtualMidiKeyboardCore* kb) noexcept { virtualKeyboard_ = kb; }
    void setPluginPlayheadInfoCore(PluginPlayheadInfoCore* playheadInfo) noexcept { pluginPlayheadInfo_ = playheadInfo; }
    void setClipRegionPluginCore(ClipRegionPluginCore* clipRegionPlugins) noexcept { clipRegionPlugins_ = clipRegionPlugins; }
    void setAutomationManager(AutomationManagerCore* automationManager) noexcept { automationManager_ = automationManager; }
    void setTrackPeakMeterManager(TrackPeakMeterManagerCore* mgr) noexcept { trackPeakMeterManager_ = mgr; }
    void setVocalTuneIntegration(apex::vocaltune::ApexTuneIntegrationCore* integration) noexcept { vocalTuneIntegration_ = integration; }
    void setLiveInputBuffer(const juce::AudioBuffer<float>* inputBuffer, int numSamples) noexcept
    {
        liveInputBuffer_ = inputBuffer;
        liveInputNumSamples_ = numSamples;
    }
    void clearLiveInputBuffer() noexcept
    {
        liveInputBuffer_ = nullptr;
        liveInputNumSamples_ = 0;
    }
    void prepareLivePrintedInputCache(const juce::Array<TrackID>& trackIds, int numSamples)
    {
        const juce::SpinLock::ScopedLockType sl(livePrintedInputCacheLock_);
        livePrintedInputCache_.clear();

        const int safeSamples = juce::jmax(1, numSamples);
        for (const auto& trackId : trackIds)
        {
            auto& cache = livePrintedInputCache_[trackId];
            cache.buffer.setSize(2, safeSamples, false, false, true);
            cache.numSamples = 0;
            cache.valid = false;
        }
    }
    bool copyLivePrintedInputBlock(const TrackID& trackId, float* destL, float* destR, int numSamples) const noexcept
    {
        const juce::SpinLock::ScopedLockType sl(livePrintedInputCacheLock_);
        auto it = livePrintedInputCache_.find(trackId);
        if (it == livePrintedInputCache_.end())
            return false;

        const auto& cache = it->second;
        if (!cache.valid || cache.numSamples != numSamples || cache.buffer.getNumChannels() < 2)
            return false;

        if (destL)
            juce::FloatVectorOperations::copy(destL, cache.buffer.getReadPointer(0), numSamples);
        if (destR)
            juce::FloatVectorOperations::copy(destR, cache.buffer.getReadPointer(1), numSamples);
        return true;
    }
    void markMasterPdcDirty() noexcept { masterPdcDirty_.store(true, std::memory_order_relaxed); }

    double getSampleRate() const noexcept { return sampleRate_; }
    int getBlockSize() const noexcept { return blockSize_; }

    /** Set the current project BPM (called from transport tempo changes). */
    void setCurrentTempo(double bpm) noexcept
    {
        currentBpm_.store(juce::jlimit(20.0, 999.0, bpm), std::memory_order_relaxed);
    }

    /** Get the current project BPM for tempo-relative playback. */
    double getCurrentTempo() const noexcept
    {
        return currentBpm_.load(std::memory_order_relaxed);
    }

    void resetOfflineRenderBuffers(int blockSize)
    {
        const int capacity = juce::jmax(blockSize, blockSize_);
        if (mixBuffer_.getNumSamples() < capacity || mixBuffer_.getNumChannels() < 2)
            mixBuffer_.setSize(2, capacity);
        if (trackBuffer_.getNumSamples() < capacity || trackBuffer_.getNumChannels() < 2)
            trackBuffer_.setSize(2, capacity);
        if (preFxBuffer_.getNumSamples() < capacity || preFxBuffer_.getNumChannels() < 2)
            preFxBuffer_.setSize(2, capacity);

        mixBuffer_.clear();
        trackBuffer_.clear();
        preFxBuffer_.clear();
        routingBuffers_.clearAllAudio();
        resetAllClipDSPState();
        masterPdc_.reset();
        for (auto& [id, line] : pdcLines_)
            line.prepare(line.getCapacity() > 0 ? line.getCapacity() : 1);
        masterWasProcessed_ = false;

        // Grow per-block scratch buffers if the export block size exceeds the
        // live device block size so renderClip() never indexes past their end.
        if ((int) scratchBuf_.size() < blockSize * 8)
            scratchBuf_.assign((size_t)(blockSize * 8), 0.f);
        if ((int) pitchRamp_.size() < blockSize * 2)
            pitchRamp_.assign((size_t)(blockSize * 2), 0.f);
        if ((int) pdcScratchL_.size() < blockSize)
        {
            pdcScratchL_.assign((size_t) blockSize, 0.f);
            pdcScratchR_.assign((size_t) blockSize, 0.f);
        }
        if ((int) masterPdcScratchL_.size() < blockSize)
        {
            masterPdcScratchL_.assign((size_t) blockSize, 0.f);
            masterPdcScratchR_.assign((size_t) blockSize, 0.f);
        }
        if (pitchInputBuffer_.getNumSamples() < blockSize * 10)
            pitchInputBuffer_.setSize(2, blockSize * 10);
        if (pitchOutputBuffer_.getNumSamples() < blockSize)
            pitchOutputBuffer_.setSize(2, blockSize);
    }

    void resetOfflineDebugLogging(int blocks) noexcept
    {
        offlineRouteDebugBlocksRemaining_ = juce::jmax(0, blocks);
    }

    bool renderOfflineBlock(juce::AudioBuffer<float>& output,
                            int numSamples,
                            int64_t timelineSample)
    {
        juce::ScopedNoDenormals noDenormals;
        if (!tracks_ || !routing_ || output.getNumChannels() < 1 || numSamples <= 0)
            return false;

        const int safeSamples = juce::jmin(numSamples, output.getNumSamples());
        auto renderContext = apexSoundEngineCore_.makeContext(safeSamples, (SamplePosition) timelineSample, true, SoundEngine::RenderMode::Offline);
        if (!apexSoundEngineCore_.validateContext(renderContext))
            return false;

        output.clear(0, safeSamples);

        PluginTransportSnapshot previousPlayheadSnapshot;
        if (pluginPlayheadInfo_ != nullptr)
        {
            previousPlayheadSnapshot = buildPluginTransportSnapshot((SamplePosition)timelineSample, false);
            auto offlineSnapshot = buildPluginTransportSnapshot((SamplePosition)timelineSample, true);
            offlineSnapshot.isRecording = false;
            offlineSnapshot.isLooping = false;
            offlineSnapshot.loopStartSamples = 0;
            offlineSnapshot.loopEndSamples = 0;
            pluginPlayheadInfo_->updateSnapshot(offlineSnapshot);
        }

        bool anySolo = false;
        std::shared_ptr<const FolderBusSnapshot> fbSnap;
        if (folderBusStateModel_)
            fbSnap = folderBusStateModel_->get();
        if (!fbSnap || fbSnap->effectiveSoloSet.empty())
            for (int i = 0; i < tracks_->getNumTracks(); ++i)
                if (tracks_->getTrack(i)->isSoloed()) { anySolo = true; break; }

        auto snap = routing_->getSnapshotPublisher().get();
        auto automationSnap = automationManager_ != nullptr
            ? automationManager_->getSnapshotPublisher().get()
            : std::shared_ptr<const AutomationSnapshot>();
        if (!apexSoundEngineCore_.validateSnapshot(snap.get()))
            return false;

        juce::AudioSourceChannelInfo info(&output, 0, safeSamples);
        masterWasProcessed_ = false;
        processWithSnapshot(snap.get(), info, safeSamples, true, (SamplePosition)timelineSample, anySolo, fbSnap.get(), automationSnap.get());
        const bool masterContractOk = apexSoundEngineCore_.finalizeMasterContract(masterWasProcessed_, renderContext, info);
        juce::ignoreUnused(masterContractOk);
        apexSoundEngineCore_.sanitizeOutput(output, 0, safeSamples);

        // If the master node was not processed, bus contributions are still valid —
        // do NOT clear. Assert in debug so a future snapshot-ordering regression
        // is caught immediately rather than shipping silently-corrupted exports.
        jassert(masterWasProcessed_); // offline render: publishSnapshotOnly() must precede renderOfflineBlock()

        if (pluginPlayheadInfo_ != nullptr && transport_ != nullptr)
            pluginPlayheadInfo_->updateSnapshot(buildPluginTransportSnapshot(transport_->getPosition(), transport_->isPlaying()));

        return true;
    }

    /** Supply the FolderBus state model so the audio thread reads pre-baked mute/solo. */
    void setFolderBusStateModel(FolderBusStateModel* model) { folderBusStateModel_ = model; }

    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
        apexSoundEngineCore_.prepare(sampleRate, blockSize);
        clipRenderCore_.prepare(sampleRate, blockSize);
        mixBuffer_.setSize(2, blockSize);
        trackBuffer_.setSize(2, blockSize);
        preFxBuffer_.setSize(2, blockSize);
        routingBuffers_.prepare(blockSize, routing_ != nullptr ? routing_->getNodeCount() + 4 : 128);
        pdcLines_.reserve(64);
        scratchBuf_.assign(blockSize * 8, 0.f); // 8x for stretch overrun
        pitchRamp_.assign(blockSize * 2, 0.f);  // 2x headroom for sub-block partial renders

        constexpr double liveInputTauSeconds = 0.005;
        liveInputMonitorSmoothingCoeff_ = (float) (1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * liveInputTauSeconds)));

        // Bug 17 fix: re-prepare ALL existing per-clip DSP cores at the new
        // sample rate and block size. prepare() is called on every device start
        // and restart (including sample rate changes). Without this, cores created
        // at 44100 Hz remain calibrated to that rate when the device reopens at
        // 48000 Hz, causing pitch offsets, filter frequency errors, and smoother
        // time-constant drift.
        for (auto& [id, dsp] : clipDspMap_)
            if (dsp) dsp->prepare(sampleRate, blockSize);
        for (auto& [id, core] : clipPitchCoreMap_)
            if (core) core->prepare(sampleRate, blockSize, 2);
        for (auto& [id, smoother] : clipPitchSmootherMap_)
            if (smoother) smoother->prepare(sampleRate, 0.050);
        for (auto& [id, core] : trackVolumeRampMap_)
            if (core) core->prepare(sampleRate, blockSize);
        if (routing_)
        {
            pdcLines_.reserve((size_t) juce::jmax(8, routing_->getConnectionCount() + 4));
        }
        // Re-prepare any engine-owned connection gain ramps at the new sample rate
        for (auto& [id, ramp] : connectionGainRamps_)
            ramp.prepare(sampleRate, blockSize);
        for (auto& [id, core] : muteFadeMap_)
            if (core) core->prepare(sampleRate, blockSize);
        for (auto& [id, core] : tapeStopProcessorMap_)
            if (core) core->prepare(sampleRate, blockSize);
        // Per-clip independent pitch cores are created on demand in getOrCreateClipPitchCore()
        pitchInputBuffer_.setSize(2, blockSize * 10);
        pitchOutputBuffer_.setSize(2, blockSize);
        pdcScratchL_.assign(blockSize, 0.f);
        pdcScratchR_.assign(blockSize, 0.f);
        masterPdcScratchL_.assign(blockSize, 0.f);
        masterPdcScratchR_.assign(blockSize, 0.f);
        rebuildPdcLines(blockSize);
        lastGraphVersion_ = 0;
        masterPdc_.prepare(sampleRate, blockSize);
        masterMeter_.prepare(sampleRate, blockSize);
        prepareTrackInputProcessors(sampleRate, blockSize);

        // ═══════════════════════════════════════════════════════════════
        // FORENSIC AUDIT — Start Counter Report Timer
        // ═══════════════════════════════════════════════════════════════
        startTimer(2000); // Report every 2 seconds
    }

    void releaseResources()
    {
        mixBuffer_.setSize(0, 0);
        trackBuffer_.setSize(0, 0);
        apexSoundEngineCore_.releaseResources();
        routingBuffers_.releaseResources();
        clipRenderCore_.releaseResources();
        liveInputMonitorGainByTrack_.clear();
        // Bug 16 fix: clear ALL per-clip DSP state on device close.
        // Without this, cores survive the release/prepare cycle and carry stale
        // ring-buffer and filter state into the next device session.
        clipDspMap_.clear();
        clipPitchCoreMap_.clear();
        clipPitchSmootherMap_.clear();
        clipTapeStopProcessorMap_.clear();
        clipPanRampMap_.clear();
        trackVolumeRampMap_.clear();
        muteFadeMap_.clear();
        tapeStopProcessorMap_.clear();
        lastClipRenderEndOffset_.clear();
        lastClipRenderBlock_.clear();
        lastClipRenderMode_.clear();
        lastClipRenderPath_.clear();
    }

    /** Called on the audio thread. Fills the output buffer. */
    void process(const juce::AudioSourceChannelInfo& output)
    {
        juce::ScopedNoDenormals noDenormals;

        struct PlaybackPitchWriteScope
        {
            PlaybackPitchWriteScope() noexcept
            {
                DAW::PitchWriteAudit::playbackActive.store(true, std::memory_order_relaxed);
            }

            ~PlaybackPitchWriteScope() noexcept
            {
                DAW::PitchWriteAudit::playbackActive.store(false, std::memory_order_relaxed);
            }
        } playbackPitchWriteScope;

        output.clearActiveBufferRegion();

        if (!tracks_ || !transport_) return;

        if (pluginPlayheadInfo_ != nullptr)
        {
            int64_t requestedPosition = 0;
            if (pluginPlayheadInfo_->consumePendingSeek(requestedPosition))
            {
                transport_->setPositionFromAudioThread((SamplePosition)requestedPosition);
                DBG("[PluginPlayhead] plugin requested playhead seek samples=" << requestedPosition);
            }
        }

        bool isPlaying = transport_->isPlaying();
        auto position  = transport_->getPosition();
        int  numSamples = output.numSamples;
        auto renderContext = apexSoundEngineCore_.makeContext(numSamples, position, isPlaying, SoundEngine::RenderMode::Live);
        if (!apexSoundEngineCore_.validateContext(renderContext))
            return;

        ++processCounter_;

        {
            const juce::SpinLock::ScopedLockType sl(livePrintedInputCacheLock_);
            for (auto& [trackId, cache] : livePrintedInputCache_)
            {
                juce::ignoreUnused(trackId);
                cache.valid = false;
                cache.numSamples = 0;
            }
        }

        // ===== APEX Automation Phase 1B integration ================================
        // Drive the automation clock and evaluator before any audio processing reads
        // parameter values for this block. The evaluator may update parameter values
        // for any track currently in Read/Touch/Latch mode based on lane breakpoints;
        // downstream code must see those updated values, NOT pre-block stale ones.
        //
        // This call is the ONLY thing that publishes transport-rolling state into
        // the automation system. Without it the recorder's rolling check fails on
        // every event and no writes ever land in lanes.
        //
        // Cost: one lock-free clock publish + a registry forEach that early-outs on
        // parameters with no lane (zero-cost for unautomated params).
        {
            const double bpm_        = transport_->getTempo();
            const double timeInSec_  = (sampleRate_ > 0.0)
                                           ? (double) position / sampleRate_
                                           : 0.0;
            const double ppqPos_     = (timeInSec_ / 60.0) * bpm_;
            const double ppqPerSamp_ = (sampleRate_ > 0.0)
                                           ? (bpm_ / 60.0) / sampleRate_
                                           : 0.0;

            // ===== TEMP DIAGNOSTIC =====
            {
                static int blockCount = 0;
                ++blockCount;
                if (blockCount % 90 == 0)
                {
                    juce::Logger::writeToLog (
                        juce::String ("[AUTO-CLOCK] block#") + juce::String (blockCount)
                        + " ppq=" + juce::String (ppqPos_)
                        + " bpm=" + juce::String (bpm_)
                        + " sr="  + juce::String (sampleRate_)
                        + " rolling=" + juce::String ((int) isPlaying));
                }
            }
            // ===== END DIAGNOSTIC =====

            apex::automation::AutomationSystem::getInstance()
                .onAudioBlockStart (ppqPos_, ppqPerSamp_, isPlaying);
        }
        // ===== End Phase 1B automation integration =================================

        const auto playheadSnapshot = buildPluginTransportSnapshot(position, isPlaying);
        if (pluginPlayheadInfo_ != nullptr)
            pluginPlayheadInfo_->updateSnapshot(playheadSnapshot);

        pluginPlayheadDebugSampleCounter_ += numSamples;
        const int64_t debugIntervalSamples = (int64_t)juce::jmax(1.0, sampleRate_);
        if (pluginPlayheadDebugSampleCounter_ >= debugIntervalSamples)
        {
            pluginPlayheadDebugSampleCounter_ %= debugIntervalSamples;
            DBG("[PluginPlayhead] samples=" + juce::String(playheadSnapshot.timeInSamples)
                + " seconds=" + juce::String(playheadSnapshot.timeInSeconds)
                + " bpm=" + juce::String(playheadSnapshot.bpm)
                + " playing=" + juce::String(playheadSnapshot.isPlaying ? "true" : "false")
                + " recording=" + juce::String(playheadSnapshot.isRecording ? "true" : "false")
                + " ppq=" + juce::String(playheadSnapshot.ppqPosition));
        }

        const bool transportDiscontinuous = clipRenderCore_.consumeTransportDiscontinuity(position, isPlaying, numSamples);

        if (transportDiscontinuous)
            clipRenderCore_.noteDspResetRequest();
        if (transportDiscontinuous)
            resetAllClipDSPState();

        // ── Read FolderBus snapshot (mute/solo, built on message thread) ────
        std::shared_ptr<const FolderBusSnapshot> fbSnap;
        if (folderBusStateModel_)
            fbSnap = folderBusStateModel_->get();

        // Legacy flat solo detection (used when no FolderBus snapshot available)
        bool anySolo = false;
        if (!fbSnap || fbSnap->effectiveSoloSet.empty())
            for (int i = 0; i < tracks_->getNumTracks(); ++i)
                if (tracks_->getTrack(i)->isSoloed()) { anySolo = true; break; }

        // ── Route-aware processing ───────────────────────────────────────
        // Read the pre-built atomic snapshot published by the message thread.
        // The audio thread NEVER touches the live RoutingGraph directly.
        // Zero locks in this path.
        if (routing_)
        {
            auto snap = routing_->getSnapshotPublisher().get();
            auto automationSnap = automationManager_ != nullptr
                ? automationManager_->getSnapshotPublisher().get()
                : std::shared_ptr<const AutomationSnapshot>();
            if (snap && apexSoundEngineCore_.validateSnapshot(snap.get()))
            {
                masterWasProcessed_ = false;
                processWithSnapshot(snap.get(), output, numSamples, isPlaying, position, anySolo, fbSnap.get(), automationSnap.get());
                apexSoundEngineCore_.finalizeMasterContract(masterWasProcessed_, renderContext, output);
                apexSoundEngineCore_.sanitizeOutput(*output.buffer, output.startSample, numSamples);
            }
            else
            {
                output.clearActiveBufferRegion();
            }
        }
        else
        {
            // No routing graph wired — output silence.
            output.clearActiveBufferRegion();
        }

        // ── Advance transport ────────────────────────────────────────────
        if (isPlaying)
        {
            auto newPos = position + numSamples;

            if (transport_->isLooping())
            {
                auto [ls, le] = transport_->getLoopRange();
                if (le > ls && newPos >= le)
                    newPos = ls + (newPos - le) % juce::jmax((SamplePosition)1, le - ls);
            }

            transport_->setPositionFromAudioThread(newPos);
        }

        // ── Master peak metering (for legacy compatibility) ─────────────
        float masterPeakL = output.buffer->getMagnitude(0, output.startSample, numSamples);
        float masterPeakR = output.buffer->getMagnitude(
            juce::jmin(1, output.buffer->getNumChannels() - 1), output.startSample, numSamples);
        masterPeakLevel_.store(juce::jmax(masterPeakL, masterPeakR), std::memory_order_relaxed);

        // Feed master track meter so the MixerStrip sees real stereo levels
        if (tracks_->hasMasterTrack())
        {
            auto* master = tracks_->getMasterTrack();
            float dispL = juce::jmax(masterPeakL, master->getPeakLevelLeft()  * 0.92f);
            float dispR = juce::jmax(masterPeakR, master->getPeakLevelRight() * 0.92f);
            master->setPeakLevels(dispL, dispR);
        }

    }

    float getMasterPeakLevel() const { return masterPeakLevel_.load(std::memory_order_relaxed); }

    // ═══════════════════════════════════════════════════════════════════════
    // FORENSIC AUDIT — Timer Callback for Path Counter Report
    // ═══════════════════════════════════════════════════════════════════════
    void timerCallback() override
    {
        FORENSIC_LOG("===============================================================");
        FORENSIC_LOG("[FORENSIC AUDIT] Path Counter Report (every 2 seconds)");
        FORENSIC_LOG("===============================================================");
        FORENSIC_LOG("  normalPathHits:           " << juce::String(PitchAuditCore::normalPathHits.load()));
        FORENSIC_LOG("  independentPitchPathHits: " << juce::String(PitchAuditCore::independentPitchPathHits.load()));
        FORENSIC_LOG("  stretchOnlyPathHits:      " << juce::String(PitchAuditCore::stretchOnlyPathHits.load()));
        FORENSIC_LOG("  oldTimePitchDSPHits:      " << juce::String(PitchAuditCore::oldTimePitchDSPHits.load()));
        FORENSIC_LOG("  resampleTapeHits:         " << juce::String(PitchAuditCore::resampleTapeHits.load()));
        FORENSIC_LOG("===============================================================");

        // If independentPitchPathHits == 0 while moving pitch knob → SUSPECT 1 CONFIRMED
        if (PitchAuditCore::independentPitchPathHits.load() == 0 &&
            PitchAuditCore::oldTimePitchDSPHits.load() > 0)
        {
            FORENSIC_LOG("!!! SUSPECT 1 CONFIRMED !!!");
            FORENSIC_LOG("Independent pitch path is NEVER executed!");
            FORENSIC_LOG("Old TimePitchDSPCore is being used instead.");
        }
    }

private:
    TrackManager*        tracks_     = nullptr;
    ClipManager*         clips_      = nullptr;
    TransportController* transport_  = nullptr;
    RoutingGraph*        routing_    = nullptr;
    AudioFileManager*    audioFiles_ = nullptr;
    std::map<TrackID, std::unique_ptr<PluginChainCore>>* pluginChains_ = nullptr;
    FolderBusStateModel* folderBusStateModel_ = nullptr;
    DAW::PianoRollPlaybackCore* midiPlayback_ = nullptr;
    DAW::MidiInputCore* midiInput_ = nullptr;
    DAW::VirtualMidiKeyboardCore* virtualKeyboard_ = nullptr;
    PluginPlayheadInfoCore* pluginPlayheadInfo_ = nullptr;
    TrackPeakMeterManagerCore* trackPeakMeterManager_ = nullptr;
    ClipRegionPluginCore* clipRegionPlugins_ = nullptr;
    AutomationManagerCore* automationManager_ = nullptr;
    apex::vocaltune::ApexTuneIntegrationCore* vocalTuneIntegration_ = nullptr;
    const juce::AudioBuffer<float>* liveInputBuffer_ = nullptr;
    int liveInputNumSamples_ = 0;
    std::map<TrackID, float> liveInputMonitorGainByTrack_;
    float liveInputMonitorSmoothingCoeff_ = 1.0f;
    static constexpr float liveInputMonitorEpsilon_ = 1.0e-5f;
    struct MonitorJumpDiagState
    {
        float prevLast = 0.0f;
        float maxJumpThisSec = 0.0f;
        double jumpSumThisSec = 0.0;
        int blocksThisSec = 0;
        bool hasPrevLast = false;
        double lastLogMs = 0.0;
    };
    std::atomic<int64_t> diagMonitorBlocks_ { 0 };
    std::atomic<int64_t> diagMonitorMixedBlocks_ { 0 };
    std::atomic<int64_t> diagMonitorShortRejects_ { 0 };
    std::atomic<int64_t> diagMonitorZeroInputRejects_ { 0 };
    std::atomic<float> diagLastMonitorSmoothedGain_ { 0.0f };
    std::atomic<int64_t> diagLastMonitorTrackHash_ { 0 };
    int64_t diagMonitorSamplesAccum_ = 0;
    double diagLastMonitorFxLogMs_ = 0.0;
    int diagLastMonitorFxPrepareResetTotal_ = 0;
    std::map<TrackID, MonitorJumpDiagState> monitorJumpDiagByTrack_;

    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;
    SoundEngine::ApexSoundEngineCore apexSoundEngineCore_;
    SoundEngine::ApexRoutingBufferCore routingBuffers_;
    SoundEngine::ApexClipRenderCore clipRenderCore_;

    juce::AudioBuffer<float> mixBuffer_;
    juce::AudioBuffer<float> trackBuffer_;
    std::atomic<float> masterPeakLevel_{0.f};
    std::atomic<double> currentBpm_{120.0}; // Project BPM for tempo-relative clip playback

    // Per-clip DSP cores for Mode 1-6 processing (one per unique ClipID)
    std::map<ClipID, std::unique_ptr<ArrangementEditor::TimePitchDSPCore>> clipDspMap_;
    std::map<ClipID, SamplePosition> lastClipRenderEndOffset_;
    std::map<ClipID, uint64_t>       lastClipRenderBlock_;
    std::map<ClipID, int>            lastClipRenderMode_;
    std::map<ClipID, bool>           lastClipUsingTunedAudio_;
    std::map<ClipID, double>         lastClipSourceSampleRate_;
    // Tracks which RenderPath last used the per-clip dspCore so we can reset
    // it when switching between StretchThenPitch and Normal/StretchOnly.
    // Without this, the dspCore retains Stretch-only WSOLA state when the path
    // switches back to Normal, causing a reset glitch on the first Normal block.
    std::map<ClipID, int>            lastClipRenderPath_;
    uint64_t                         processCounter_ = 0;
    int64_t                          pluginPlayheadDebugSampleCounter_ = 0;
    bool                             masterWasProcessed_ = false;
    int                              offlineRouteDebugBlocksRemaining_ = 0;

    // Scratch buffer for renderClipSegment output before gain/fade mix
    std::vector<float> scratchBuf_;

    // Per-block pitch ramp buffer — preallocated in prepare(), reused every block.
    // Filled by PitchSmootherCore::getNextSemitones() for every sample in the block.
    // Eliminates the old "get one sample, discard the rest" zipper pattern.
    std::vector<float> pitchRamp_;

    // Independent pitch cores — one per clip (preserves grain state per clip)
    std::map<ClipID, std::unique_ptr<ArrangementEditor::ClipIndependentPitchCore>> clipPitchCoreMap_;
    std::map<ClipID, std::unique_ptr<ArrangementEditor::PitchSmootherCore>> clipPitchSmootherMap_;

    struct ClipPanRampState
    {
        float smoothedPan = 0.0f;
        bool initialised = false;
    };
    std::map<ClipID, ClipPanRampState> clipPanRampMap_;
    std::vector<float> clipPanLeftRamp_;
    std::vector<float> clipPanRightRamp_;

    // Per-track volume/pan smoother — eliminates zipper from block-rate gain
    std::map<TrackID, std::unique_ptr<VolumeRampCore>> trackVolumeRampMap_;
    std::map<TrackID, std::unique_ptr<MuteFadeCore>> muteFadeMap_;
    std::map<TrackID, std::unique_ptr<APEX::TapeStop::ProcessorCore>> tapeStopProcessorMap_;
    std::map<ClipID, std::unique_ptr<APEX::TapeStop::ProcessorCore>> clipTapeStopProcessorMap_;

    juce::AudioBuffer<float> pitchInputBuffer_;
    juce::AudioBuffer<float> pitchOutputBuffer_;
    juce::AudioBuffer<float> clipRegionPluginBuffer_;

    // Per-node audio buffers for routing-graph-aware processing
    struct JuceStringHash
    {
        size_t operator()(const juce::String& s) const noexcept { return (size_t) s.hashCode64(); }
    };

    uint64_t lastGraphVersion_ = 0;

    // Engine-owned gain ramp state for every live routing edge, keyed by RouteID.
    // The audio thread reads EdgeSnapshot.gain as target and drives these ramps —
    // no RoutingConnection* access on the audio thread.
    std::unordered_map<RouteID, ConnectionGainRampCore, JuceStringHash> connectionGainRamps_;

    // Pre-FX tap: snapshot of the track buffer captured BEFORE the plugin chain runs
    juce::AudioBuffer<float> preFxBuffer_;

    struct LivePrintedInputCacheEntry
    {
        juce::AudioBuffer<float> buffer;
        int numSamples = 0;
        bool valid = false;
    };
    mutable juce::SpinLock livePrintedInputCacheLock_;
    std::map<TrackID, LivePrintedInputCacheEntry> livePrintedInputCache_;

    // PDC delay lines for sidechain edges:
    //   key   = destNodeId
    //   value = circular delay buffer [2 ch x maxDelaySamples]
    // Each sidechain destination may need compensation when its source track
    // has lookahead plugins that introduce latency.
    std::unordered_map<juce::String, SoundEngine::ApexPdcDelayLineCore, JuceStringHash> pdcLines_;
    MasterPdcCore masterPdc_;
    std::atomic<bool> masterPdcDirty_ { true };
    MeteringFacadeCore masterMeter_;
    // Pre-allocated scratch buffers for PDC sidechain reads — avoids
    // heap allocation inside the audio callback (Bug 46).
    std::vector<float> pdcScratchL_;
    std::vector<float> pdcScratchR_;
    std::vector<float> masterPdcScratchL_;
    std::vector<float> masterPdcScratchR_;

    /** Returns (creating if necessary) the per-clip ClipIndependentPitchCore. */
    ArrangementEditor::ClipIndependentPitchCore& getOrCreateClipPitchCore(const ClipID& id)
    {
        auto it = clipPitchCoreMap_.find(id);
        if (it == clipPitchCoreMap_.end())
        {
            auto core = std::make_unique<ArrangementEditor::ClipIndependentPitchCore>();
            core->prepare(sampleRate_, blockSize_, 2);
            it = clipPitchCoreMap_.emplace(id, std::move(core)).first;
        }
        return *it->second;
    }

    /** Returns (creating if necessary) the per-clip pitch smoother used by the connected audio path. */
    ArrangementEditor::PitchSmootherCore& getOrCreateClipPitchSmoother(const ClipID& id)
    {
        auto it = clipPitchSmootherMap_.find(id);
        if (it == clipPitchSmootherMap_.end())
        {
            auto smoother = std::make_unique<ArrangementEditor::PitchSmootherCore>();
            smoother->prepare(sampleRate_, 0.050);
            it = clipPitchSmootherMap_.emplace(id, std::move(smoother)).first;
        }
        return *it->second;
    }

    /** Returns (creating if necessary) the per-clip TimePitchDSPCore. */
    ArrangementEditor::TimePitchDSPCore& getOrCreateClipDSP(const ClipID& id)
    {
        auto it = clipDspMap_.find(id);
        if (it == clipDspMap_.end())
        {
            auto core = std::make_unique<ArrangementEditor::TimePitchDSPCore>();
            core->prepare(sampleRate_, blockSize_);
            it = clipDspMap_.emplace(id, std::move(core)).first;
        }
        return *it->second;
    }

    VolumeRampCore& getOrCreateVolumeRamp(const TrackID& trackId)
    {
        auto it = trackVolumeRampMap_.find(trackId);
        if (it != trackVolumeRampMap_.end())
            return *it->second;

        auto core = std::make_unique<VolumeRampCore>();
        core->prepare(sampleRate_, blockSize_);
        auto* corePtr = core.get();
        trackVolumeRampMap_[trackId] = std::move(core);
        return *corePtr;
    }

    MuteFadeCore& getOrCreateMuteFade(const TrackID& trackId)
    {
        auto it = muteFadeMap_.find(trackId);
        if (it != muteFadeMap_.end())
            return *it->second;

        auto core = std::make_unique<MuteFadeCore>();
        core->prepare(sampleRate_, blockSize_);
        auto* corePtr = core.get();
        muteFadeMap_[trackId] = std::move(core);
        return *corePtr;
    }

    APEX::TapeStop::ProcessorCore& getOrCreateTapeStopProcessor(const TrackID& trackId)
    {
        auto it = tapeStopProcessorMap_.find(trackId);
        if (it != tapeStopProcessorMap_.end())
            return *it->second;

        auto core = std::make_unique<APEX::TapeStop::ProcessorCore>();
        core->prepare(sampleRate_, blockSize_);
        auto* corePtr = core.get();
        tapeStopProcessorMap_[trackId] = std::move(core);
        return *corePtr;
    }

    APEX::TapeStop::ProcessorCore& getOrCreateClipTapeStopProcessor(const ClipID& clipId)
    {
        auto it = clipTapeStopProcessorMap_.find(clipId);
        if (it != clipTapeStopProcessorMap_.end())
            return *it->second;

        auto core = std::make_unique<APEX::TapeStop::ProcessorCore>();
        core->prepare(sampleRate_, blockSize_);
        auto* corePtr = core.get();
        clipTapeStopProcessorMap_[clipId] = std::move(core);
        return *corePtr;
    }

    void resetClipTapeStopProcessorIfBypassed(const ClipID& clipId, float tapeStopValue)
    {
        if (tapeStopValue > 0.0001f)
            return;

        auto it = clipTapeStopProcessorMap_.find(clipId);
        if (it != clipTapeStopProcessorMap_.end() && it->second)
            it->second->reset();
    }

    static float getTapeStopValueInsideDrawnRegion(const AutomationSnapshot::LaneSnapshot& lane,
                                                   int64_t samplePosition) noexcept
    {
        if (!isAutomationRegionActive(lane, samplePosition))
            return 0.0f;

        return juce::jlimit(0.0f, 1.0f, lane.getValueAtSample(samplePosition, 0.0f));
    }

    static bool isAutomationRegionActive(const AutomationSnapshot::LaneSnapshot& lane,
                                         int64_t samplePosition) noexcept
    {
        return lane.enabled
            && lane.points.size() >= 2
            && samplePosition >= lane.points.front().timeSamples
            && samplePosition <= lane.points.back().timeSamples;
    }

    static float getPitchValueInsideDrawnRegion(const AutomationSnapshot::LaneSnapshot& lane,
                                                int64_t samplePosition,
                                                float naturalPitch) noexcept
    {
        if (!isAutomationRegionActive(lane, samplePosition))
            return naturalPitch;

        return juce::jlimit(-36.0f, 36.0f, lane.getValueAtSample(samplePosition, naturalPitch));
    }

    static float getStretchValueInsideDrawnRegion(const AutomationSnapshot::LaneSnapshot& lane,
                                                  int64_t samplePosition,
                                                  float naturalStretch) noexcept
    {
        if (!isAutomationRegionActive(lane, samplePosition))
            return naturalStretch;

        return juce::jmax(0.01f, lane.getValueAtSample(samplePosition, naturalStretch));
    }

    static float getClipGainValueInsideDrawnRegion(const AutomationSnapshot::LaneSnapshot& lane,
                                                   int64_t samplePosition,
                                                   float naturalGain) noexcept
    {
        if (!isAutomationRegionActive(lane, samplePosition))
            return naturalGain;

        return juce::jlimit(0.0f, 4.0f, lane.getValueAtSample(samplePosition, naturalGain));
    }

    static float getClipPanValueInsideDrawnRegion(const AutomationSnapshot::LaneSnapshot& lane,
                                                  int64_t samplePosition,
                                                  float naturalPan) noexcept
    {
        if (!isAutomationRegionActive(lane, samplePosition))
            return naturalPan;

        return juce::jlimit(-1.0f, 1.0f, lane.getValueAtSample(samplePosition, naturalPan));
    }

    static int getClipFadeLengthInsideDrawnRegion(const AutomationSnapshot::LaneSnapshot& lane,
                                                  int64_t samplePosition,
                                                  int naturalFadeLength,
                                                  int clipTimelineLength) noexcept
    {
        if (!isAutomationRegionActive(lane, samplePosition))
            return naturalFadeLength;

        const float value = lane.getValueAtSample(samplePosition, (float) naturalFadeLength);
        return juce::jlimit(0, juce::jmax(0, clipTimelineLength), (int) std::lround(value));
    }

    void prepareTrackInputProcessors(double sampleRate, int blockSize) noexcept
    {
        if (tracks_ == nullptr)
            return;

        for (int i = 0; i < tracks_->getNumTracks(); ++i)
            if (auto* track = tracks_->getTrack(i))
                TrackInputProcessorCore::prepareTrack(*track, sampleRate, blockSize);

        if (auto* master = tracks_->getMasterTrack())
            TrackInputProcessorCore::prepareTrack(*master, sampleRate, blockSize);
    }

    const float* getConnectionGainRamp(const RouteID& edgeId, float targetGain, int numSamples)
    {
        auto& ramp = connectionGainRamps_[edgeId];
        if (ramp.getCapacity() < numSamples)
            ramp.prepare(sampleRate_, juce::jmax(blockSize_, numSamples));
        return ramp.generate(targetGain, numSamples);
    }

    juce::AudioBuffer<float>* findNodeBuffer(const juce::String& nodeId) noexcept
    {
        return routingBuffers_.findNodeBuffer(nodeId);
    }

    juce::AudioBuffer<float>* findSidechainBuffer(const juce::String& nodeId) noexcept
    {
        return routingBuffers_.findSidechainBuffer(nodeId);
    }

    bool isOfflineRoutingSnapshotSafe(const RoutingSnapshot* snap) const noexcept
    {
        if (snap == nullptr || snap->processingOrder.empty() || snap->nodes.empty())
            return false;

        for (const auto& nodeId : snap->processingOrder)
        {
            if (nodeId.isEmpty())
                continue;

            bool foundNode = false;
            for (const auto& node : snap->nodes)
            {
                if (node.id == nodeId)
                {
                    foundNode = true;
                    break;
                }
            }

            if (!foundNode)
                return false;
        }

        return true;
    }

    void syncNodeBuffers(const RoutingSnapshot& snapshot, int numSamples)
    {
        if (!routing_) return;

        routingBuffers_.syncFromSnapshot(snapshot, numSamples);
        pdcLines_.reserve((size_t) juce::jmax(8, routing_->getConnectionCount() + 4));

        for (const auto& edge : snapshot.edges)
        {
            if (edge.type == ConnectionType::Sidechain)
            {
                auto pdcIt = pdcLines_.find(edge.destNodeId);
                if (pdcIt == pdcLines_.end())
                    pdcLines_.emplace(edge.destNodeId, SoundEngine::ApexPdcDelayLineCore());
            }
        }

        rebuildPdcLines(numSamples);
    }

    void resetAllClipDSPState()
    {
        for (auto& [id, dsp] : clipDspMap_)
            if (dsp)
                dsp->reset();

        for (auto& [id, core] : clipPitchCoreMap_)
            if (core)
                core->reset();

        for (auto& [id, smoother] : clipPitchSmootherMap_)
            if (smoother)
                smoother->reset(0.0f);

        for (auto& [id, panState] : clipPanRampMap_)
            panState.initialised = false;

        for (auto& [id, core] : trackVolumeRampMap_)
            if (core)
                core->reset();

        for (auto& [id, core] : muteFadeMap_)
            if (core)
                core->reset(false);

        for (auto& [id, core] : tapeStopProcessorMap_)
            if (core)
                core->reset();

        for (auto& [id, core] : clipTapeStopProcessorMap_)
            if (core)
                core->reset();

        lastClipRenderEndOffset_.clear();
        lastClipRenderBlock_.clear();
        lastClipRenderMode_.clear();
        lastClipRenderPath_.clear();
        lastClipUsingTunedAudio_.clear();
        lastClipSourceSampleRate_.clear();
    }

    PluginTransportSnapshot buildPluginTransportSnapshot(SamplePosition position, bool isPlaying) const
    {
        jassert(sampleRate_ > 0.0 && "buildPluginTransportSnapshot called before prepareToPlay");
        if (sampleRate_ <= 0.0)
        {
            PluginTransportSnapshot safe;
            safe.sampleRate    = 0.0;
            safe.timeInSeconds = 0.0;
            safe.ppqPosition   = 0.0;
            safe.isPlaying     = false;
            return safe;
        }

        PluginTransportSnapshot snap;
        snap.sampleRate    = sampleRate_;
        snap.timeInSamples = (int64_t)position;
        snap.timeInSeconds = (double)position / snap.sampleRate;

        if (transport_ != nullptr)
        {
            snap.bpm = juce::jlimit(20.0, 999.0, transport_->getTempo());
            snap.isPlaying = isPlaying;
            snap.isRecording = transport_->isRecording();
            snap.isLooping = transport_->isLooping();

            auto [loopStart, loopEnd] = transport_->getLoopRange();
            snap.loopStartSamples = (int64_t)loopStart;
            snap.loopEndSamples = (int64_t)loopEnd;
        }

        snap.ppqPosition = (snap.timeInSeconds / 60.0) * snap.bpm;
        snap.ppqLastBarStart = std::floor(snap.ppqPosition / (double)juce::jmax(1, snap.timeSigNumerator))
            * (double)juce::jmax(1, snap.timeSigNumerator);

        return snap;
    }

    float computeFadeGain(int posInClip, int clipLength,
                          int fadeInLen, int fadeOutLen,
                          int fadeInCurve = 0, int fadeOutCurve = 0) const noexcept
    {
        float g = 1.0f;
        if (fadeInLen > 0 && posInClip < fadeInLen)
            g *= applyClipFadeCurve((float)posInClip / (float)juce::jmax(1, fadeInLen), fadeInCurve);
        const int fadeOutStart = juce::jmax(0, clipLength - fadeOutLen);
        if (fadeOutLen > 0 && posInClip >= fadeOutStart)
        {
            const float x = (float)(posInClip - fadeOutStart) / (float)juce::jmax(1, fadeOutLen);
            g *= applyClipFadeCurve(juce::jlimit(0.f, 1.f, 1.f - x), fadeOutCurve);
        }
        return g;
    }

    std::pair<const float*, const float*> generateClipPanGainRamps(const ClipID& clipId, float targetPan, int numSamples)
    {
        if ((int)clipPanLeftRamp_.size() < numSamples)
        {
            clipPanLeftRamp_.assign((size_t)numSamples, 1.0f);
            clipPanRightRamp_.assign((size_t)numSamples, 1.0f);
        }

        auto& state = clipPanRampMap_[clipId];
        targetPan = juce::jlimit(-1.0f, 1.0f, targetPan);

        if (!state.initialised)
        {
            state.smoothedPan = targetPan;
            state.initialised = true;
        }

        const double tau = 0.005;
        const float coeff = (float)(1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * tau)));
        for (int s = 0; s < numSamples; ++s)
        {
            state.smoothedPan += (targetPan - state.smoothedPan) * coeff;
            const float angle = (state.smoothedPan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            clipPanLeftRamp_[(size_t)s] = std::cos(angle);
            clipPanRightRamp_[(size_t)s] = std::sin(angle);
        }

        return { clipPanLeftRamp_.data(), clipPanRightRamp_.data() };
    }

    /** (Re)build PDC delay lines based on current plugin chain latencies.
     *  Call after prepare() and whenever a plugin is added/removed.
     *  For each sidechain dest node, computes how many samples the source track
     *  chain is ahead of the dest track's main path and sets up a matching delay. */
    void rebuildPdcLines(int numSamples)
    {
        if (!routing_ || !pluginChains_) return;
        for (auto* conn : routing_->getAllConnections())
        {
            if (!conn || conn->type != ConnectionType::Sidechain) continue;
            auto* srcNode  = routing_->getNode(conn->sourceNodeId);
            auto* destNode = routing_->getNode(conn->destNodeId);
            if (!srcNode || !destNode) continue;

            const auto latency = SoundEngine::ApexPluginPdcContractCore::makeSidechainLatencyPair(
                pluginChains_, srcNode->trackId, destNode->trackId);

            auto lineIt = pdcLines_.find(conn->destNodeId);
            if (lineIt == pdcLines_.end())
                lineIt = pdcLines_.emplace(conn->destNodeId, SoundEngine::ApexPdcDelayLineCore{}).first;
            auto& line = lineIt->second;
            const int capacity = SoundEngine::ApexPluginPdcContractCore::computeDelayLineCapacity(
                latency.delaySamples, numSamples);
            if (line.getCapacity() < capacity)
                line.prepare(capacity);
            line.setDelaySamples(latency.delaySamples);
        }
    }

    void logExportRouteIfNeeded(bool shouldLog, const RoutingSnapshot::EdgeSnapshot& edge) const
    {
        if (! shouldLog)
            return;

        juce::Logger::writeToLog("[EXPORT ROUTE] src=" + edge.sourceNodeId
            + " dst=" + edge.destNodeId
            + " type=" + juce::String((int) edge.type)
            + " gain=" + juce::String(edge.gain)
            + " active=" + juce::String(edge.active ? 1 : 0)
            + " bypassed=" + juce::String(edge.bypassed ? 1 : 0));
    }

    bool shouldMonitorLiveInputForTrack(const Track& track) const noexcept
    {
        if (liveInputBuffer_ == nullptr || liveInputNumSamples_ <= 0 || transport_ == nullptr)
            return false;

        switch (track.getMonitoringState().getMode())
        {
            case InputMonitorMode::Off:
                return false;
            case InputMonitorMode::On:
                return true;
            case InputMonitorMode::Auto:
                return track.isArmed() && (!transport_->isPlaying() || transport_->isRecording());
        }

        return false;
    }

    bool addLiveInputToTrackBuffer(Track& track, int numSamples) noexcept
    {
        const bool monitorTargetOn = shouldMonitorLiveInputForTrack(track);
        float& smoothedGain = liveInputMonitorGainByTrack_[track.getID()];
        const float targetGain = monitorTargetOn ? 1.0f : 0.0f;

        if (monitorTargetOn)
        {
            diagMonitorBlocks_.fetch_add(1, std::memory_order_relaxed);
            diagLastMonitorSmoothedGain_.store(smoothedGain, std::memory_order_relaxed);
            diagLastMonitorTrackHash_.store((int64_t) track.getID().hashCode64(), std::memory_order_relaxed);
        }

        const bool haveValidLiveInput = liveInputBuffer_ != nullptr
            && liveInputNumSamples_ == numSamples
            && liveInputBuffer_->getNumChannels() > 0
            && numSamples > 0;

        if (!haveValidLiveInput)
        {
            if (monitorTargetOn)
            {
                if (liveInputBuffer_ == nullptr || liveInputBuffer_->getNumChannels() <= 0 || numSamples <= 0)
                    diagMonitorZeroInputRejects_.fetch_add(1, std::memory_order_relaxed);
                else if (liveInputNumSamples_ != numSamples)
                    diagMonitorShortRejects_.fetch_add(1, std::memory_order_relaxed);
            }
            smoothedGain = targetGain;
            return false;
        }

        // Per-track input selection: Track carries the chosen hardware input
        // (first channel + mono flag). Clamp to the available channel count so
        // a stale selection never reads out of bounds.
        const int inChans   = liveInputBuffer_->getNumChannels();
        const int wantFirst = juce::jlimit(0, inChans - 1, track.getInputFirstChannel());
        const bool mono     = track.isInputMono();
        const int srcL = wantFirst;
        const int srcR = mono ? wantFirst : juce::jmin(wantFirst + 1, inChans - 1);
        const float* inL = liveInputBuffer_->getReadPointer(srcL);
        const float* inR = liveInputBuffer_->getReadPointer(srcR);

        auto* dstL = trackBuffer_.getWritePointer(0);
        auto* dstR = trackBuffer_.getWritePointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));

        if (std::abs(targetGain - 1.0f) < liveInputMonitorEpsilon_
            && std::abs(smoothedGain - 1.0f) < liveInputMonitorEpsilon_)
        {
            juce::FloatVectorOperations::add(dstL, inL, numSamples);
            juce::FloatVectorOperations::add(dstR, inR, numSamples);
            smoothedGain = 1.0f;
            if (monitorTargetOn)
            {
                diagMonitorMixedBlocks_.fetch_add(1, std::memory_order_relaxed);
                diagLastMonitorSmoothedGain_.store(smoothedGain, std::memory_order_relaxed);
            }
            return true;
        }

        if (targetGain == 0.0f && smoothedGain < liveInputMonitorEpsilon_)
        {
            smoothedGain = 0.0f;
            return false;
        }

        for (int s = 0; s < numSamples; ++s)
        {
            smoothedGain += (targetGain - smoothedGain) * liveInputMonitorSmoothingCoeff_;
            const float g = smoothedGain;
            dstL[s] += inL[s] * g;
            dstR[s] += inR[s] * g;
        }

        if (targetGain == 0.0f && smoothedGain < liveInputMonitorEpsilon_)
            smoothedGain = 0.0f;

        if (monitorTargetOn)
        {
            diagMonitorMixedBlocks_.fetch_add(1, std::memory_order_relaxed);
            diagLastMonitorSmoothedGain_.store(smoothedGain, std::memory_order_relaxed);
        }

        return monitorTargetOn;
    }

    void emitMonitorJumpDiagIfNeeded(const Track& track,
                                     bool liveInputWasAdded,
                                     int numSamples) noexcept
    {
        if (!liveInputWasAdded || numSamples <= 0 || trackBuffer_.getNumChannels() <= 0)
            return;

        auto& state = monitorJumpDiagByTrack_[track.getID()];
        const float* in = trackBuffer_.getReadPointer(0);
        const float curFirst = in[0];
        const float curLast = in[numSamples - 1];
        const float prevLastForLog = state.prevLast;

        double avgAbsSample = 0.0;
        for (int s = 0; s < numSamples; ++s)
            avgAbsSample += std::abs(in[s]);
        avgAbsSample /= (double) numSamples;

        float jump = 0.0f;
        if (state.hasPrevLast)
        {
            jump = std::abs(curFirst - state.prevLast);
            state.maxJumpThisSec = juce::jmax(state.maxJumpThisSec, jump);
            state.jumpSumThisSec += jump;
            ++state.blocksThisSec;
        }

        state.prevLast = curLast;
        state.hasPrevLast = true;

        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (state.lastLogMs <= 0.0)
        {
            state.lastLogMs = nowMs;
            return;
        }

        if (nowMs - state.lastLogMs < 1000.0)
            return;

        const double avgJumpThisSec = state.blocksThisSec > 0
            ? (state.jumpSumThisSec / (double) state.blocksThisSec)
            : 0.0;

        juce::Logger::writeToLog("[APEX-DIAG-MONJUMP] trk=" + track.getID()
            + " prevLast=" + juce::String(prevLastForLog, 6)
            + " curFirst=" + juce::String(curFirst, 6)
            + " jump=" + juce::String(jump, 6)
            + " maxJumpThisSec=" + juce::String(state.maxJumpThisSec, 6)
            + " avgJumpThisSec=" + juce::String(avgJumpThisSec, 6)
            + " avgAbsSample=" + juce::String(avgAbsSample, 6));

        state.maxJumpThisSec = 0.0f;
        state.jumpSumThisSec = 0.0;
        state.blocksThisSec = 0;
        state.lastLogMs = nowMs;
    }

    void emitMonitorFxDiagIfNeeded(const Track& track,
                                   bool liveInputWasAdded,
                                   PluginChainCore* chain) noexcept
    {
        if (!liveInputWasAdded || chain == nullptr)
            return;

        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (nowMs - diagLastMonitorFxLogMs_ < 1000.0)
            return;

        diagLastMonitorFxLogMs_ = nowMs;
        const auto snapshot = chain->getAndResetDiagSnapshot();
        const int fxPrepared = snapshot.totalPrepareCalls + snapshot.totalResetCalls
            - diagLastMonitorFxPrepareResetTotal_;
        diagLastMonitorFxPrepareResetTotal_ = snapshot.totalPrepareCalls + snapshot.totalResetCalls;

        juce::String msg = juce::String("[APEX-DIAG-MONFX] trk=") + track.getID()
            + " monAdded=" + juce::String(liveInputWasAdded ? 1 : 0)
            + " fxBlocks=" + juce::String(snapshot.processedBlocks)
            + " fxPrepared=" + juce::String(fxPrepared)
            + " inSamples=" + juce::String(snapshot.firstPluginSamples)
            + " bypass=" + juce::String(snapshot.firstPluginBypassed ? 1 : 0);

        if (snapshot.firstPluginName.isNotEmpty())
        {
            msg += " plugin=\"";
            msg += snapshot.firstPluginName;
            msg += "\"";
        }

        juce::Logger::writeToLog(msg);
    }

    void cacheLivePrintedInputBlock(const TrackID& trackId, int numSamples)
    {
        const juce::SpinLock::ScopedLockType sl(livePrintedInputCacheLock_);
        auto it = livePrintedInputCache_.find(trackId);
        if (it == livePrintedInputCache_.end())
            return;

        auto& cache = it->second;
        if (cache.buffer.getNumChannels() < 2 || cache.buffer.getNumSamples() < numSamples)
        {
            cache.valid = false;
            cache.numSamples = 0;
            return;
        }

        cache.buffer.copyFrom(0, 0, trackBuffer_, 0, 0, numSamples);
        cache.buffer.copyFrom(1, 0, trackBuffer_, juce::jmin(1, trackBuffer_.getNumChannels() - 1), 0, numSamples);
        cache.numSamples = numSamples;
        cache.valid = true;
    }

    // ── Snapshot-based routing processing (audio thread — zero locks) ────
    void processWithSnapshot(const RoutingSnapshot* snap,
                             const juce::AudioSourceChannelInfo& output,
                             int numSamples, bool isPlaying,
                             SamplePosition position, bool anySolo,
                             const FolderBusSnapshot* fbSnap,
                             const AutomationSnapshot* automationSnap)
    {
        // Sync node buffers and PDC lines whenever the graph topology changes.
        // graphVersion is bumped by rebuildAdjacency() on every topology change.
        const uint64_t graphVersion = routing_->getGraphVersion();
        if (graphVersion != lastGraphVersion_)
        {
            syncNodeBuffers(*snap, numSamples);
            lastGraphVersion_ = graphVersion;
            masterPdcDirty_.store(true, std::memory_order_relaxed);
        }

        if (masterPdcDirty_.exchange(false, std::memory_order_relaxed))
            masterPdc_.sync(*routing_, pluginChains_);

        // ── Retire stale gain ramps (outside sample loop, no allocation) ──
        // Build a set of active edge IDs from this snapshot, then erase any
        // ramp whose ID is no longer present.
        {
            static thread_local std::vector<RouteID> toErase;
            toErase.clear();
            for (auto& [id, ramp] : connectionGainRamps_)
            {
                bool found = false;
                for (const auto& e : snap->edges)
                    if (e.id == id) { found = true; break; }
                if (!found) toErase.push_back(id);
            }
            for (const auto& id : toErase)
                connectionGainRamps_.erase(id);
        }

        // Ensure per-node buffers are cleared by the routing buffer nucleus.
        routingBuffers_.clearSnapshotAudio(*snap, numSamples);

        // Process each node in topological order using snapshot node metadata
        const bool logRoutesThisBlock = offlineRouteDebugBlocksRemaining_ > 0;
        if (logRoutesThisBlock)
            --offlineRouteDebugBlocksRemaining_;

        for (const auto& nodeId : snap->processingOrder)
        {
            const RoutingSnapshot::NodeSnapshot* nodeMeta = nullptr;
            for (const auto& n : snap->nodes)
                if (n.id == nodeId) { nodeMeta = &n; break; }
            if (!nodeMeta || !nodeMeta->active) continue;

            switch (nodeMeta->type)
            {
                case RoutingNodeType::Track:
                    processTrackNode(nodeMeta, snap, numSamples, isPlaying, position, anySolo, fbSnap, automationSnap, logRoutesThisBlock);
                    break;

                case RoutingNodeType::Bus:
                case RoutingNodeType::FolderBus:
                    processBusNode(nodeMeta, snap, numSamples, position, anySolo, fbSnap, automationSnap, logRoutesThisBlock);
                    break;

                case RoutingNodeType::Master:
                    processMasterNode(nodeId, numSamples, output);
                    break;

                case RoutingNodeType::Hardware:
                    break;
            }
        }
    }

    // ── Legacy entry kept for non-snapshot code paths (internal only) ──────
    void processWithRouting(const juce::AudioSourceChannelInfo& output,
                            int numSamples, bool isPlaying,
                            SamplePosition position, bool anySolo,
                            const FolderBusSnapshot* fbSnap)
    {
        auto snap = routing_->getSnapshotPublisher().get();
        if (snap)
            processWithSnapshot(snap.get(), output, numSamples, isPlaying, position, anySolo, fbSnap, nullptr);
    }

    /** Process a Track node: render clips, apply plugins/vol/pan, route to outputs. */
    void processTrackNode(const RoutingSnapshot::NodeSnapshot* nodeMeta,
                          const RoutingSnapshot* snap,
                          int numSamples,
                          bool isPlaying, SamplePosition position, bool anySolo,
                          const FolderBusSnapshot* fbSnap,
                          const AutomationSnapshot* automationSnap,
                          bool logRoutesThisBlock)
    {
        auto* track = tracks_->getTrack(nodeMeta->trackId);
        if (!track) return;

        auto* nodeBufPtr = findNodeBuffer(nodeMeta->id);
        if (nodeBufPtr == nullptr) return;

        // Render clips into node buffer
        // THREAD SAFETY: clips_ OwnedArray is protected by ClipManager::clipLock_.
        // Hold a ScopedTryLock for the entire getClipsOnTrack + renderClip loop so
        // no Clip* can be deleted mid-iteration on the message thread.
        // If the lock can't be obtained this block produces silence for this track (rare, acceptable).
        trackBuffer_.clear();
        bool liveInputWasAdded = false;
        if (clips_)
        {
            juce::ScopedTryLock cl(clips_->getLock());
            if (cl.isLocked())
            {
                auto clipsOnTrack = clips_->getClipsOnTrack(track->getID());
                for (auto* clip : clipsOnTrack)
                {
                    if (clip->isMuted()) continue;
                    if (isPlaying)
                        renderClip(clip, position, numSamples);
                }
            }
            else
            {
                DBG("[AudioEngine] Skipped clip render for track " + track->getID()
                    + ": clip manager locked by message thread");
            }
        }

        liveInputWasAdded = addLiveInputToTrackBuffer(*track, numSamples);

        // Mix in any incoming send contributions accumulated into this node's buffer.
        {
            auto* incomingBufPtr = findNodeBuffer(nodeMeta->id);
            if (incomingBufPtr == nullptr) return;
            auto& incomingBuf = *incomingBufPtr;
            auto* srcL = incomingBuf.getReadPointer(0);
            auto* srcR = incomingBuf.getReadPointer(juce::jmin(1, incomingBuf.getNumChannels() - 1));
            auto* dstL = trackBuffer_.getWritePointer(0);
            auto* dstR = trackBuffer_.getWritePointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
            SoundEngine::ApexMixFanoutCore::addStereo(srcL, srcR, dstL, dstR, numSamples);
        }

        {
            auto* tbL = trackBuffer_.getWritePointer(0);
            auto* tbR = trackBuffer_.getWritePointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
            TrackInputProcessorCore::processTrack(*track, tbL, tbR, numSamples);
        }

        // Capture pre-FX snapshot before the plugin chain runs (for PreFX tap)
        if (preFxBuffer_.getNumSamples() < numSamples || preFxBuffer_.getNumChannels() < 2)
            preFxBuffer_.setSize(2, juce::jmax(numSamples, blockSize_));
        for (int ch = 0; ch < 2; ++ch)
            preFxBuffer_.copyFrom(ch, 0, trackBuffer_, ch, 0, numSamples);

        // MIDI playback
        juce::MidiBuffer trackMidi;
        if (midiPlayback_ && (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument))
        {
            PianoRollPlaybackCore::TransportInfo ti;
            ti.isPlaying          = isPlaying;
            ti.timelinePosSamples = (juce::int64) position;
            ti.numSamples         = numSamples;
            midiPlayback_->processBlock(track->getID(), ti, trackMidi);
        }
        if (midiInput_ && (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument))
            midiInput_->processBlock(track->getID(), trackMidi, numSamples);
        if (virtualKeyboard_ && track->getRole() == TrackRole::MIDI)
            virtualKeyboard_->processBlock(track->getID(), trackMidi, numSamples);

        // Plugin chain — check for sidechain input using snapshot edges (no RoutingGraph access)
        if (pluginChains_)
        {
            auto it = pluginChains_->find(track->getID());
            if (it != pluginChains_->end() && it->second)
            {
                auto& chain = *it->second;
                const bool hasMidiInput = (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument) && !trackMidi.isEmpty();
                bool hasSidechainInput = false;
                for (const auto& e : snap->edges)
                {
                    if (e.destNodeId == nodeMeta->id
                        && e.type == ConnectionType::Sidechain
                        && e.active && !e.bypassed)
                    { hasSidechainInput = true; break; }
                }

                chain.applyAutomationAtSample(track->getID(), automationSnap, (int64_t)position,
                                              sampleRate_, transport_ ? transport_->getTempo() : 120.0);

                // Build per-slot wetdry array from automation snapshot
                const int numSlots = chain.getNumSlots();
                static thread_local std::vector<float> slotWetDry;
                slotWetDry.assign((size_t)numSlots, 1.0f);
                bool anyWetDryActive = false;
                if (automationSnap != nullptr)
                {
                    for (int si = 0; si < numSlots; ++si)
                    {
                        auto paramId = AutomationManagerCore::makePluginSlotMixId(si);
                        if (auto* lane = automationSnap->findLane(track->getID(), paramId))
                        {
                            if (lane->enabled && !lane->points.empty())
                            {
                                slotWetDry[(size_t)si] = lane->getValueAtSample((int64_t)position, 1.0f);
                                anyWetDryActive = true;
                            }
                        }
                    }
                }

                emitMonitorJumpDiagIfNeeded(*track, liveInputWasAdded, numSamples);

                if (hasMidiInput)
                    chain.processBlockWithMidi(trackBuffer_, trackMidi, numSamples);
                else if (hasSidechainInput)
                {
                    if (auto* sc = findSidechainBuffer(nodeMeta->id))
                        chain.processBlockWithSidechain(trackBuffer_, *sc, numSamples);
                    else
                        chain.processBlock(trackBuffer_, numSamples);
                }
                else if (anyWetDryActive)
                    chain.processBlockPerSlotWetDry(trackBuffer_, numSamples, slotWetDry);
                else
                    chain.processBlock(trackBuffer_, numSamples);

                emitMonitorFxDiagIfNeeded(*track, liveInputWasAdded, &chain);
            }
        }

        if (liveInputWasAdded
            && track->getMonitoringState().getRecordingMode() == RecordInputRouter::Mode::MonitorWetRecordWet)
        {
            cacheLivePrintedInputBlock(track->getID(), numSamples);
        }

        // Volume and pan
        auto& volumeRamp = getOrCreateVolumeRamp(track->getID());
        float trackVolume = track->getVolume();
        float trackPan = track->getPan();
        if (automationSnap != nullptr)
        {
            if (auto* lane = automationSnap->findLane(track->getID(), AutomationLaneCore::trackVolumeParameterId))
                if (lane->enabled)
                    trackVolume = lane->getValueAtSample((int64_t)position, trackVolume);

            if (auto* lane = automationSnap->findLane(track->getID(), AutomationLaneCore::trackPanParameterId))
                if (lane->enabled)
                    trackPan = lane->getValueAtSample((int64_t)position, trackPan);
        }

        if (automationSnap != nullptr)
        {
            if (auto* lane = automationSnap->findLane(track->getID(), AutomationLaneCore::trackTapeStopParameterId))
            {
                if (lane->enabled && !lane->points.empty())
                {
                    const float tapeStopValue = getTapeStopValueInsideDrawnRegion(*lane, (int64_t)position);
                    auto& tapeStop = getOrCreateTapeStopProcessor(track->getID());
                    tapeStop.process(trackBuffer_.getWritePointer(0),
                                     trackBuffer_.getWritePointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1)),
                                     numSamples,
                                     tapeStopValue);
                }
            }
        }

        volumeRamp.setTargetVolume(trackVolume);
        volumeRamp.setTargetPan(trackPan);

        const float leftGain  = volumeRamp.getCurrentLeftGain();
        const float rightGain = volumeRamp.getCurrentRightGain();

        float leftPeak  = trackBuffer_.getMagnitude(0, 0, numSamples) * leftGain;
        float rightPeak = trackBuffer_.getMagnitude(
            juce::jmin(1, trackBuffer_.getNumChannels() - 1), 0, numSamples) * rightGain;
        float displayLeft  = juce::jmax(leftPeak,  track->getPeakLevelLeft()  * 0.92f);
        float displayRight = juce::jmax(rightPeak, track->getPeakLevelRight() * 0.92f);
        track->setPeakLevels(displayLeft, displayRight);

        // ── TrackLens: post-plugin, post-fader metering ───────────────────
        if (trackPeakMeterManager_ != nullptr)
        {
            const float* mL = trackBuffer_.getReadPointer(0);
            const float* mR = trackBuffer_.getReadPointer(
                juce::jmin(1, trackBuffer_.getNumChannels() - 1));
            // Apply fader gain inline — reuse pdcScratchL_/R_, no allocation
            const int ns = numSamples;
            if ((int)pdcScratchL_.size() < ns) pdcScratchL_.resize((size_t)ns);
            if ((int)pdcScratchR_.size() < ns) pdcScratchR_.resize((size_t)ns);
            for (int s = 0; s < ns; ++s)
            {
                pdcScratchL_[(size_t)s] = mL[s] * leftGain;
                pdcScratchR_[(size_t)s] = mR[s] * rightGain;
            }
            trackPeakMeterManager_->processTrackBlock(
                track->getID(), pdcScratchL_.data(), pdcScratchR_.data(), ns);
        }

        // Mute/solo
        bool audible;
        if (fbSnap)
        {
            bool effMuted = fbSnap->effectivelyMutedTracks.count(track->getID()) > 0;
            audible = !effMuted;
            if (!fbSnap->effectiveSoloSet.empty())
                audible = audible && fbSnap->effectiveSoloSet.count(track->getID()) > 0;
        }
        else
        {
            audible = !track->isMuted();
            if (anySolo) audible = track->isSoloed();
        }

        auto& muteFade = getOrCreateMuteFade(track->getID());
        const float* muteRamp = muteFade.generate(!audible, numSamples);
        if (!audible && muteFade.isFullyMuted()) return;

        // ── Pre-fader sends (scan snapshot edges) ───────────────────────
        for (const auto& edge : snap->edges)
        {
            if (edge.sourceNodeId != nodeMeta->id) continue;
            logExportRouteIfNeeded(logRoutesThisBlock, edge);
            if (!edge.active || edge.bypassed) continue;
            if (edge.type != ConnectionType::PreSend) continue;

            auto* destBufPtr = findNodeBuffer(edge.destNodeId);
            if (destBufPtr == nullptr) continue;

            const float* sendGain = getConnectionGainRamp(edge.id, edge.gain, numSamples);
            auto& destBuf = *destBufPtr;
            auto* srcL = trackBuffer_.getReadPointer(0);
            auto* srcR = trackBuffer_.getReadPointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
            auto* dstL = destBuf.getWritePointer(0);
            auto* dstR = destBuf.getWritePointer(juce::jmin(1, destBuf.getNumChannels() - 1));
            SoundEngine::ApexMixFanoutCore::addStereoWithGainAndMute(srcL, srcR, sendGain, muteRamp, dstL, dstR, numSamples);
        }

        // ── Apply gain to trackBuffer for post-fader routing ─────────────
        auto* tbL = trackBuffer_.getWritePointer(0);
        auto* tbR = trackBuffer_.getWritePointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
        volumeRamp.applyToStereoBuffer(tbL, tbR, numSamples);
        SoundEngine::ApexMixFanoutCore::applyStereoMuteRamp(tbL, tbR, muteRamp, numSamples);

        // ── Route to outputs (Direct + Post-fader sends) ────────────────
        for (const auto& edge : snap->edges)
        {
            if (edge.sourceNodeId != nodeMeta->id) continue;
            logExportRouteIfNeeded(logRoutesThisBlock, edge);
            if (!edge.active || edge.bypassed) continue;
            if (edge.type == ConnectionType::Sidechain)
                continue;

            auto* destBufPtr = findNodeBuffer(edge.destNodeId);
            if (destBufPtr == nullptr) continue;

            const float* routeGain = getConnectionGainRamp(edge.id, edge.gain, numSamples);
            auto& destBuf = *destBufPtr;
            const float* routeSrcL = tbL;
            const float* routeSrcR = tbR;
            if (edge.destNodeId == "master")
            {
                if ((int)masterPdcScratchL_.size() < numSamples)
                {
                    masterPdcScratchL_.assign((size_t)numSamples, 0.0f);
                    masterPdcScratchR_.assign((size_t)numSamples, 0.0f);
                }
                std::memcpy(masterPdcScratchL_.data(), tbL, sizeof(float) * (size_t)numSamples);
                std::memcpy(masterPdcScratchR_.data(), tbR, sizeof(float) * (size_t)numSamples);
                masterPdc_.processEdge(edge.id, masterPdcScratchL_.data(), masterPdcScratchR_.data(), numSamples);
                routeSrcL = masterPdcScratchL_.data();
                routeSrcR = masterPdcScratchR_.data();
            }
            auto* dstL = destBuf.getWritePointer(0);
            auto* dstR = destBuf.getWritePointer(juce::jmin(1, destBuf.getNumChannels() - 1));
            SoundEngine::ApexMixFanoutCore::addStereoWithGain(routeSrcL, routeSrcR, routeGain, dstL, dstR, numSamples);
        }

        // ── Sidechain sends ──────────────────────────────────────────────
        for (const auto& edge : snap->edges)
        {
            if (edge.sourceNodeId != nodeMeta->id) continue;
            logExportRouteIfNeeded(logRoutesThisBlock, edge);
            if (!edge.active || edge.bypassed) continue;
            if (edge.type != ConnectionType::Sidechain) continue;

            auto* scBufPtr = findSidechainBuffer(edge.destNodeId);
            if (scBufPtr == nullptr) continue;

            const float* scGain = getConnectionGainRamp(edge.id, edge.gain, numSamples);
            auto& scBuf = *scBufPtr;
            auto* scL   = scBuf.getWritePointer(0);
            auto* scR   = scBuf.getWritePointer(juce::jmin(1, scBuf.getNumChannels() - 1));

            const float* tapL = nullptr;
            const float* tapR = nullptr;
            if (edge.tapPoint == TapPoint::PostMixer)
            {
                tapL = tbL;
                tapR = tbR;
            }
            else if (edge.tapPoint == TapPoint::PreFX)
            {
                tapL = preFxBuffer_.getReadPointer(0);
                tapR = preFxBuffer_.getReadPointer(juce::jmin(1, preFxBuffer_.getNumChannels() - 1));
            }
            else // PostFX
            {
                tapL = trackBuffer_.getReadPointer(0);
                tapR = trackBuffer_.getReadPointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
            }

            auto pdcIt = pdcLines_.find(edge.destNodeId);
            if (pdcIt != pdcLines_.end() && pdcIt->second.getDelaySamples() > 0)
            {
                auto& line = pdcIt->second;
                line.push(tapL, tapR, numSamples);
                if ((int)pdcScratchL_.size() < numSamples)
                {
                    pdcScratchL_.assign(numSamples, 0.f);
                    pdcScratchR_.assign(numSamples, 0.f);
                }
                line.read(pdcScratchL_.data(), pdcScratchR_.data(), numSamples);
                SoundEngine::ApexMixFanoutCore::addStereoWithGain(pdcScratchL_.data(), pdcScratchR_.data(), scGain, scL, scR, numSamples);
            }
            else
            {
                SoundEngine::ApexMixFanoutCore::addStereoWithGain(tapL, tapR, scGain, scL, scR, numSamples);
            }
        }
    }

    /** Process a Bus node: incoming signals already summed, apply bus FX/fader, route out. */
    void processBusNode(const RoutingSnapshot::NodeSnapshot* nodeMeta,
                        const RoutingSnapshot* snap,
                        int numSamples, SamplePosition position, bool anySolo,
                        const FolderBusSnapshot* fbSnap,
                        const AutomationSnapshot* automationSnap,
                        bool logRoutesThisBlock)
    {
        auto* nodeBufPtr = findNodeBuffer(nodeMeta->id);
        if (nodeBufPtr == nullptr) return;
        auto& nodeBuf = *nodeBufPtr;

        // Look up associated track for bus controls (volume, mute, solo-safe, plugins)
        Track* busTrack = nullptr;
        if (nodeMeta->trackId.isNotEmpty())
            busTrack = tracks_->getTrack(nodeMeta->trackId);

        // Bus plugin chain
        if (busTrack && pluginChains_)
        {
            auto it = pluginChains_->find(busTrack->getID());
            if (it != pluginChains_->end() && it->second)
                it->second->processBlock(nodeBuf, numSamples);
        }

        // Bus mute — snapshot-aware
        float busGain = busTrack ? busTrack->getVolume() : 1.0f;
        if (busTrack && automationSnap != nullptr)
            if (auto* lane = automationSnap->findLane(busTrack->getID(), AutomationLaneCore::trackVolumeParameterId))
                if (lane->enabled)
                    busGain = lane->getValueAtSample((int64_t)position, busGain);

        bool  busMuted;
        if (fbSnap && busTrack)
            busMuted = fbSnap->effectivelyMutedTracks.count(busTrack->getID()) > 0;
        else
            busMuted = busTrack ? busTrack->isMuted() : false;

        MuteFadeCore* busMuteFade = nullptr;
        const float* busMuteRamp = nullptr;
        if (busTrack)
        {
            busMuteFade = &getOrCreateMuteFade(busTrack->getID());
            busMuteRamp = busMuteFade->generate(busMuted, numSamples);
        }

        // Bus metering
        if (busTrack)
        {
            float peakL = nodeBuf.getMagnitude(0, 0, numSamples) * busGain;
            float peakR = nodeBuf.getMagnitude(juce::jmin(1, nodeBuf.getNumChannels() - 1), 0, numSamples) * busGain;
            float dispL = juce::jmax(peakL, busTrack->getPeakLevelLeft() * 0.92f);
            float dispR = juce::jmax(peakR, busTrack->getPeakLevelRight() * 0.92f);
            busTrack->setPeakLevels(dispL, dispR);
        }

        if (busMuted && busMuteFade != nullptr && busMuteFade->isFullyMuted()) return;

        // Apply bus gain and mute fade
        auto* bufL = nodeBuf.getWritePointer(0);
        auto* bufR = nodeBuf.getWritePointer(juce::jmin(1, nodeBuf.getNumChannels() - 1));
        if (busTrack)
        {
            auto& busRamp = getOrCreateVolumeRamp(busTrack->getID());
            busRamp.setTargetVolume(busGain);
            busRamp.setTargetPan(busTrack->getPan());
            busRamp.applyToStereoBuffer(bufL, bufR, numSamples);

            if (busMuteRamp != nullptr)
                SoundEngine::ApexMixFanoutCore::applyStereoMuteRamp(bufL, bufR, busMuteRamp, numSamples);
        }

        // Route bus output to connected destinations (typically master)
        for (const auto& edge : snap->edges)
        {
            if (edge.sourceNodeId != nodeMeta->id) continue;
            logExportRouteIfNeeded(logRoutesThisBlock, edge);
            if (!edge.active || edge.bypassed) continue;
            if (edge.type == ConnectionType::Sidechain) continue;

            auto* destBufPtr = findNodeBuffer(edge.destNodeId);
            if (destBufPtr == nullptr) continue;

            const float* routeGain = getConnectionGainRamp(edge.id, edge.gain, numSamples);
            auto& destBuf = *destBufPtr;
            const float* routeSrcL = bufL;
            const float* routeSrcR = bufR;
            if (edge.destNodeId == "master")
            {
                if ((int)masterPdcScratchL_.size() < numSamples)
                {
                    masterPdcScratchL_.assign((size_t)numSamples, 0.0f);
                    masterPdcScratchR_.assign((size_t)numSamples, 0.0f);
                }
                std::memcpy(masterPdcScratchL_.data(), bufL, sizeof(float) * (size_t)numSamples);
                std::memcpy(masterPdcScratchR_.data(), bufR, sizeof(float) * (size_t)numSamples);
                masterPdc_.processEdge(edge.id, masterPdcScratchL_.data(), masterPdcScratchR_.data(), numSamples);
                routeSrcL = masterPdcScratchL_.data();
                routeSrcR = masterPdcScratchR_.data();
            }
            auto* dstL = destBuf.getWritePointer(0);
            auto* dstR = destBuf.getWritePointer(juce::jmin(1, destBuf.getNumChannels() - 1));
            SoundEngine::ApexMixFanoutCore::addStereoWithGain(routeSrcL, routeSrcR, routeGain, dstL, dstR, numSamples);
        }
    }

    /** Process the Master node: copy summed mix to hardware output.
     *  All master processing (inserts → FX chain → fader → meter → render)
     *  is handled by MasterBusEngine in ApplicationCore::getNextAudioBlock(). */
    void processMasterNode(const juce::String& nodeId, int numSamples,
                           const juce::AudioSourceChannelInfo& output)
    {
        auto* masterBufPtr = findNodeBuffer(nodeId);
        if (masterBufPtr == nullptr) return;
        auto& masterBuf = *masterBufPtr;
        masterWasProcessed_ = true;

        const int safeSamples = juce::jmin(numSamples, masterBuf.getNumSamples());
        const int outChannels = output.buffer->getNumChannels();
        if (outChannels < 1) return;

        // Copy raw summed mix to hardware output — no plugin chain, no fader.
        // MasterBusEngine applies master inserts, FX chain, gain, mute,
        // metering, and render tap on this buffer next.
        auto* outL = output.buffer->getWritePointer(0, output.startSample);
        auto* outR = output.buffer->getWritePointer(
            juce::jmin(1, outChannels - 1), output.startSample);
        auto* srcL = masterBuf.getReadPointer(0);
        auto* srcR = masterBuf.getReadPointer(juce::jmin(1, masterBuf.getNumChannels() - 1));
        masterMeter_.processBlock(srcL, srcR, safeSamples);

        SoundEngine::ApexMixFanoutCore::copyStereo(srcL, srcR, outL, outR, safeSamples);

        SoundEngine::ApexMixFanoutCore::clearStereo(outL + safeSamples, outR + safeSamples, numSamples - safeSamples);
    }

    // ── Flat fallback (no routing graph) ─────────────────────────────────
    void processFlat(const juce::AudioSourceChannelInfo& output,
                     int numSamples, bool isPlaying,
                     SamplePosition position, bool anySolo)
    {
        for (int i = 0; i < tracks_->getNumTracks(); ++i)
        {
            auto* track = tracks_->getTrack(i);
            if (!track) continue;

            bool audible = !track->isMuted();
            if (anySolo) audible = track->isSoloed();

            trackBuffer_.clear();

            if (clips_)
            {
                juce::ScopedTryLock cl(clips_->getLock());
                if (cl.isLocked())
                {
                    auto clipsOnTrack = clips_->getClipsOnTrack(track->getID());
                    for (auto* clip : clipsOnTrack)
                    {
                        if (clip->isMuted()) continue;
                        if (isPlaying) renderClip(clip, position, numSamples);
                    }
                }
            }

            if (pluginChains_)
            {
                auto it = pluginChains_->find(track->getID());
                if (it != pluginChains_->end() && it->second)
                    it->second->processBlock(trackBuffer_, numSamples);
            }

            auto& volumeRamp = getOrCreateVolumeRamp(track->getID());
            volumeRamp.setTargetVolume(track->getVolume());
            volumeRamp.setTargetPan(track->getPan());
            volumeRamp.generateRamps(numSamples);

            const float leftGain  = volumeRamp.getCurrentLeftGain();
            const float rightGain = volumeRamp.getCurrentRightGain();

            float leftPeak  = trackBuffer_.getMagnitude(0, 0, numSamples) * leftGain;
            float rightPeak = trackBuffer_.getMagnitude(
                juce::jmin(1, trackBuffer_.getNumChannels() - 1), 0, numSamples) * rightGain;
            float displayLeft = juce::jmax(leftPeak, track->getPeakLevelLeft() * 0.92f);
            float displayRight = juce::jmax(rightPeak, track->getPeakLevelRight() * 0.92f);
            track->setPeakLevels(displayLeft, displayRight);

            auto& muteFade = getOrCreateMuteFade(track->getID());
            const float* muteRamp = muteFade.generate(!audible, numSamples);
            if (!audible && muteFade.isFullyMuted())
                continue;

            if (audible || !muteFade.isFullyMuted())
            {
                auto* outL = output.buffer->getWritePointer(0, output.startSample);
                auto* outR = output.buffer->getWritePointer(
                    juce::jmin(1, output.buffer->getNumChannels() - 1), output.startSample);
                auto* srcL = trackBuffer_.getReadPointer(0);
                auto* srcR = trackBuffer_.getReadPointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
                const float* leftRamp  = volumeRamp.getLeftRamp();
                const float* rightRamp = volumeRamp.getRightRamp();

                for (int s = 0; s < numSamples; ++s)
                {
                    outL[s] += srcL[s] * leftRamp[s] * muteRamp[s];
                    outR[s] += srcR[s] * rightRamp[s] * muteRamp[s];
                }
            }
        }

        // Master processing (inserts → FX chain → fader → meter → render)
        // is handled by MasterBusEngine in ApplicationCore::getNextAudioBlock().
        // No duplicate master processing here.
    }

    /** Render a single clip into trackBuffer_ at the current transport position. */
    void renderClip(Clip* clip, SamplePosition playPos, int numSamples)
    {
        const auto clipContext = clipRenderCore_.makeContext(playPos, numSamples, transport_ != nullptr && transport_->isPlaying(), false);
        if (!clipRenderCore_.validateContext(clipContext))
            return;

        if (clipRegionPlugins_ != nullptr && clipRegionPlugins_->hasPluginsForClip(clip->getID()))
        {
            if (clipRegionPluginBuffer_.getNumChannels() < trackBuffer_.getNumChannels()
                || clipRegionPluginBuffer_.getNumSamples() < numSamples)
            {
                clipRegionPluginBuffer_.setSize(trackBuffer_.getNumChannels(),
                                                juce::jmax(numSamples, blockSize_),
                                                false, false, true);
            }

            clipRegionPluginBuffer_.clear(0, numSamples);
            auto* previousTarget = currentClipRenderTarget_;
            currentClipRenderTarget_ = &clipRegionPluginBuffer_;
            renderClipInternal(clip, playPos, numSamples);
            currentClipRenderTarget_ = previousTarget;

            clipRegionPlugins_->processClipBlock(clip->getID(), clipRegionPluginBuffer_, numSamples);

            for (int ch = 0; ch < trackBuffer_.getNumChannels(); ++ch)
                trackBuffer_.addFrom(ch, 0, clipRegionPluginBuffer_, juce::jmin(ch, clipRegionPluginBuffer_.getNumChannels() - 1), 0, numSamples);
            return;
        }

        renderClipInternal(clip, playPos, numSamples);
    }

    juce::AudioBuffer<float>* currentClipRenderTarget_ = nullptr;

    juce::AudioBuffer<float>& getClipRenderTarget() noexcept
    {
        return currentClipRenderTarget_ != nullptr ? *currentClipRenderTarget_ : trackBuffer_;
    }

    void renderClipInternal(Clip* clip, SamplePosition playPos, int numSamples)
    {
        auto clipStart = clip->getStartPosition();
        auto clipTimelineLength = clip->getLength();
        if (auto* ac = dynamic_cast<AudioClip*>(clip))
            clipTimelineLength = ac->getProcessedTimelineLength();

        const auto sourceReadPlan = SoundEngine::ApexSourceReadContractCore::makeReadPlan(playPos, numSamples, clipStart, clipTimelineLength);
        if (!sourceReadPlan.intersects) return;

        const int bufferStart = sourceReadPlan.bufferStart;
        const int count = sourceReadPlan.count;
        const SamplePosition engineClipOffset = sourceReadPlan.engineClipOffset;

        if (audioFiles_)
        {
            const juce::AudioBuffer<float>* srcBuf = audioFiles_->getBuffer(clip->getID());
            const std::vector<float>* tunedAudio = nullptr;
            const float* tunedSrc = nullptr;
            if (vocalTuneIntegration_ != nullptr)
            {
                tunedAudio = vocalTuneIntegration_->getTunedAudioForClip(clip->getID());
                if (tunedAudio != nullptr && !tunedAudio->empty())
                    tunedSrc = tunedAudio->data();
            }

            if ((tunedSrc != nullptr) || (srcBuf != nullptr && srcBuf->getNumSamples() > 0))
            {
                const bool useTunedAudio = tunedSrc != nullptr
                    && vocalTuneIntegration_->getTunedAudioSampleRate(clip->getID()) > 0.0;
                int srcTotal = useTunedAudio ? (int)tunedAudio->size() : srcBuf->getNumSamples();
                int srcChans = useTunedAudio ? 1 : srcBuf->getNumChannels();
                double srcRate = useTunedAudio
                    ? vocalTuneIntegration_->getTunedAudioSampleRate(clip->getID())
                    : audioFiles_->getSourceSampleRate(clip->getID());

                const auto tunedStateIt = lastClipUsingTunedAudio_.find(clip->getID());
                const auto srcRateIt = lastClipSourceSampleRate_.find(clip->getID());
                const bool tunedStateChanged = tunedStateIt == lastClipUsingTunedAudio_.end()
                    || tunedStateIt->second != useTunedAudio;
                const bool srcRateChanged = srcRateIt == lastClipSourceSampleRate_.end()
                    || std::abs(srcRateIt->second - srcRate) > 0.001;
                if (tunedStateChanged || srcRateChanged)
                {
                    FORENSIC_LOG("[VOCAL TUNE SOURCE] clipId=" << clip->getID()
                        << " source=" << (useTunedAudio ? "tuned" : "dry")
                        << " sampleRate=" << juce::String(srcRate, 2)
                        << " samples=" << juce::String(srcTotal));
                    lastClipUsingTunedAudio_[clip->getID()] = useTunedAudio;
                    lastClipSourceSampleRate_[clip->getID()] = srcRate;
                }

                double srcPerEngineSample = srcRate / sampleRate_;
                float clipGain      = 1.0f;
                float clipPan       = 0.0f;
                float clipPitch     = 0.0f;    // semitones (legacy)
                float clipStretch   = 1.0f;
                float fineTuneCents = 0.0f;
                float clipTapeStop  = 0.0f;
                bool clipReversed   = false;
                int   tpMode        = DAW::TimePitchModeIds::DefaultUserMode;
                int fadeInLength    = 0;
                int fadeOutLength   = 0;
                int fadeInCurve     = 0;
                int fadeOutCurve    = 0;
                AudioClip* ac = dynamic_cast<AudioClip*>(clip);
                if (ac)
                {
                    clipGain      = ac->getGain();
                    clipPan       = ac->getClipPan();
                    clipPitch     = ac->getPitch();
                    clipStretch   = juce::jmax(0.01f, ac->getTimeStretch());
                    fineTuneCents = ac->getFineTuneCents();
                    clipReversed  = ac->isReversed();
                    tpMode        = ac->getTimePitchMode();
                    fadeInLength  = (int) ac->getFadeInLength();
                    fadeOutLength = (int) ac->getFadeOutLength();
                    fadeInCurve   = ac->getFadeInCurve();
                    fadeOutCurve  = ac->getFadeOutCurve();
                    if (automationManager_ != nullptr)
                    {
                        auto automationSnap = automationManager_->getSnapshotPublisher().get();
                        if (automationSnap != nullptr)
                        {
                            const auto blockSamplePos = (int64_t)(playPos + bufferStart);
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipGainParameterId(ac->getID())))
                                if (lane->enabled)
                                    clipGain = getClipGainValueInsideDrawnRegion(*lane, blockSamplePos, clipGain);
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipPanParameterId(ac->getID())))
                                if (lane->enabled)
                                    clipPan = getClipPanValueInsideDrawnRegion(*lane, blockSamplePos, clipPan);
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipFadeInParameterId(ac->getID())))
                                if (lane->enabled)
                                    fadeInLength = getClipFadeLengthInsideDrawnRegion(*lane, blockSamplePos, fadeInLength, (int) clipTimelineLength);
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipFadeOutParameterId(ac->getID())))
                                if (lane->enabled)
                                    fadeOutLength = getClipFadeLengthInsideDrawnRegion(*lane, blockSamplePos, fadeOutLength, (int) clipTimelineLength);
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipTapeStopParameterId(ac->getID())))
                                if (lane->enabled)
                                    clipTapeStop = getTapeStopValueInsideDrawnRegion(*lane, blockSamplePos);
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipPitchParameterId(ac->getID())))
                                if (lane->enabled)
                                    clipPitch = getPitchValueInsideDrawnRegion(*lane, blockSamplePos, clipPitch);
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipStretchParameterId(ac->getID())))
                                if (lane->enabled)
                                    clipStretch = getStretchValueInsideDrawnRegion(*lane, blockSamplePos, clipStretch);
                        }
                    }
                    // ═══════════════════════════════════════════════════════════════
                    // FORENSIC AUDIT — Mode Value Trap
                    // ═══════════════════════════════════════════════════════════════
                    resetClipTapeStopProcessorIfBypassed(clip->getID(), clipTapeStop);
                    static int auditLogCounter = 0;
                    if (++auditLogCounter % 50 == 0)
                    {
                        DBG("[FORENSIC AUDIT renderClip] clipId=" << ac->getID());
                        DBG("  modeRaw=" << tpMode);
                        DBG("  pitchSemi=" << clipPitch);
                        DBG("  fineTune=" << fineTuneCents);
                        DBG("  stretch=" << clipStretch);
                        DBG("  gain=" << clipGain);
                    }
                }

                // ═══════════════════════════════════════════════════════════════
                // TEMPO-RELATIVE PLAYBACK: Apply tempo multiplier to clip stretch
                // ═══════════════════════════════════════════════════════════════
                // If project BPM differs from reference BPM, adjust playback rate
                // to maintain musical time (bars/beats stay constant across tempo changes).
                // tempoMultiplier = referenceBpm / currentBpm (higher BPM = faster playback)
                const double currentBpm = getCurrentTempo();
                const double referenceBpm = ac ? 120.0 : 120.0; // TODO: retrieve from clip model when available
                if (currentBpm > 0.0 && referenceBpm > 0.0)
                {
                    const double tempoMultiplier = referenceBpm / currentBpm;
                    clipStretch = juce::jmax(0.01f, (float)(clipStretch * tempoMultiplier));
                }

                const auto sourceBoundsPlan = SoundEngine::ApexSourceReadContractCore::makeSourceBounds(
                    (int64_t)clip->getSourceOffset(),
                    ac ? ac->getSourceStartSample() : (int64_t)clip->getSourceOffset(),
                    ac ? ac->getSourceEndSample() : 0,
                    (int64_t)srcTotal);
                if (!sourceBoundsPlan.valid)
                    return;
                const auto sourceStartBound = sourceBoundsPlan.sourceStartBound;
                const auto sourceEndBound = sourceBoundsPlan.sourceEndBound;

                auto& clipPitchSmoother = getOrCreateClipPitchSmoother(clip->getID());
                // Set the target once per block (atomic read from AudioClip — no raw pitch_ race).
                const float pitchBlockTarget = clipPitch + fineTuneCents / 100.0f;
                clipPitchSmoother.setPitchTargetSemitones(pitchBlockTarget);

                // Fill per-sample ramp — consume ALL smoothed values for this block.
                // Previously only one sample was consumed and the rest discarded,
                // causing the smoother to lag by (count-1) samples every block and
                // producing stepped pitch jumps (zipper noise) under UI drag.
                if ((int)pitchRamp_.size() < count)
                    pitchRamp_.resize((size_t)count * 2, 0.f);
                for (int s = 0; s < count; ++s)
                    pitchRamp_[s] = clipPitchSmoother.getNextSemitones();

                // smoothedClipPitch: last value of the ramp = settled value for this block.
                // ClipIndependentPitchCore has its own per-sample internal smoother that
                // chases this value, providing additional sub-block interpolation.
                const float smoothedClipPitch = pitchRamp_[count - 1];

                DBG("[PITCH AUDIO TARGET] target=" << pitchBlockTarget
                    << " firstRamp=" << pitchRamp_[0]
                    << " lastRamp=" << smoothedClipPitch);
                // Log to ForensicAuditWindow when target changes meaningfully
                {
                    static float lastForensicPitchTarget = -9999.f;
                    if (std::fabs(pitchBlockTarget - lastForensicPitchTarget) > 0.005f)
                    {
                        FORENSIC_LOG("[FORENSIC TRAP setPitch] target=" << juce::String(pitchBlockTarget, 3)
                            << " smoothed=" << juce::String(smoothedClipPitch, 3)
                            << " clipId=" << clip->getID());
                        lastForensicPitchTarget = pitchBlockTarget;
                    }
                }

                // ── Pitch / time dispatch ───────────────────────────────
                //
                // Mode 0 (Resample): pitch and time coupled — inline resample.
                //   Fast path, tape/DJ behavior, backward compatible.
                //
                // Mode 1-6: delegate to TimePitchDSPCore::renderClipSegment().
                //   AudioEngine does NOT implement stretch math here.
                //   All algorithm details live in the DSP nuclei.
                //   Each clip maintains its own TimePitchDSPCore instance
                //   (accessed via per-clip DSP map, or fallback inline for Mode 0).

                const double totalSt    = (double)smoothedClipPitch;
                const double pitchRatio = std::pow(2.0, totalSt / 12.0);

                DBG("[AUDIO RENDER] clip=" << clip->getID()
                    << " mode=" << tpMode
                    << " pitch=" << clipPitch
                    << " fineTune=" << fineTuneCents
                    << " stretch=" << clipStretch << "x");
                DBG("[AUDIO PATH DETECTION] pitchActive=" << (pitchActive ? "true" : "false")
                    << " stretchActive=" << (stretchActive ? "true" : "false")
                    << " independentPitchMode=" << (independentPitchMode ? "true" : "false"));

                const auto clipPanGains = generateClipPanGainRamps(clip->getID(), clipPan, count);

                if (tpMode == DAW::TimePitchModeIds::Resample)
                {
                    // ═══════════════════════════════════════════════════════════════
                    // FORENSIC AUDIT — Resample/Tape Path Counter
                    // ═══════════════════════════════════════════════════════════════
                    ++PitchAuditCore::resampleTapeHits;

                    // ── MODE 0: Resample (inline, fastest path) ────────────
                    // pitch and duration change together (tape/vinyl/DJ)
                    const double readRate = pitchRatio / (double)clipStretch;

                    const int renderChannels = getClipRenderTarget().getNumChannels();

                    for (int ch = 0; ch < renderChannels; ++ch)
                    {
                        auto* dst = getClipRenderTarget().getWritePointer(ch, bufferStart);
                        int   sch = juce::jmin(ch, srcChans - 1);
                        auto* src = useTunedAudio ? tunedSrc : srcBuf->getReadPointer(sch);

                        for (int s = 0; s < count; ++s)
                        {
                            const double sourceDelta = ((double)(engineClipOffset + s) * readRate);
                            const double srcPos = clipReversed
                                ? (double)(sourceEndBound - 1) - sourceDelta
                                : (double)sourceStartBound + sourceDelta;
                            const int i1 = (int)std::floor(srcPos);
                            if (i1 < sourceStartBound || i1 >= sourceEndBound) continue;
                            const float frac = (float)(srcPos - i1);

                            // Bug 26 fix: cubic Hermite — linear interpolation aliases
                            // at pitch ratios > ~1.5x, producing imaging hiss.
                            // Bug 47 fix: clamp cubic neighbors against clip bounds, not
                            // just srcTotal.  Reading samples before sourceStartBound on a
                            // sliced/offset clip returns pre-clip audio or heap garbage.
                            const auto safe = [src, sourceStartBound, sourceEndBound](int idx) noexcept -> float
                            {
                                return (idx >= (int)sourceStartBound && idx < (int)sourceEndBound) ? src[idx] : 0.f;
                            };
                            const float y0 = safe(i1 - 1);
                            const float y1 = safe(i1);
                            const float y2 = safe(i1 + 1);
                            const float y3 = safe(i1 + 2);
                            const float c0 = y1;
                            const float c1 = 0.5f * (y2 - y0);
                            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                            float panGain = 1.0f;
                            if (renderChannels > 1)
                                panGain = ch == 0 ? clipPanGains.first[s] : clipPanGains.second[s];
                            const float sample = (((c3 * frac + c2) * frac + c1) * frac + c0) * clipGain * panGain;

                            const int   posInClip = (int)(engineClipOffset + s);
                            const float fadeGain  = computeFadeGain(posInClip, (int)clipTimelineLength,
                                                                     fadeInLength, fadeOutLength,
                                                                     fadeInCurve, fadeOutCurve);
                            dst[s] += sample * fadeGain;
                        }
                    }

                        if (clipTapeStop > 0.0001f)
                        {
                            auto* left = getClipRenderTarget().getWritePointer(0, bufferStart);
                            auto* right = getClipRenderTarget().getWritePointer(juce::jmin(1, renderChannels - 1), bufferStart);
                            getOrCreateClipTapeStopProcessor(clip->getID()).process(left, right, count, clipTapeStop);
                        }
                }
                else
                {
                    // ══════════════════════════════════════════════════════════════
                    // ── MODE 1-6: INDEPENDENT PITCH/TIME PROCESSING ───────────────
                    // ══════════════════════════════════════════════════════════════
                    //
                    // CRITICAL FIX: Detect if we need independent pitch shift.
                    // Independent pitch means:
                    //   - Pitch changes perceived frequency
                    //   - Clip duration stays EXACTLY the same
                    //   - Timeline length unchanged
                    //   - Output samples = Input samples ALWAYS
                    //
                    // This is different from Resample mode where:
                    //   readRate = pitchRatio / stretchRatio
                    //   causes duration change.
                    //
                    // ══════════════════════════════════════════════════════════════

                    // ══════════════════════════════════════════════════════════════
                    // DETECTION: Which audio path should we use?
                    // ══════════════════════════════════════════════════════════════

                    const auto renderPathPlan = SoundEngine::ApexClipRenderPathDecisionCore::makeDecision(
                        clipPitch,
                        smoothedClipPitch,
                        fineTuneCents,
                        clipStretch,
                        tpMode,
                        TimePitchModeIds::PitchOnly,
                        TimePitchModeIds::Vocal,
                        TimePitchModeIds::Stretch,
                        clipReversed);
                    const bool pitchActive = renderPathPlan.pitchActive;
                    const bool stretchActive = renderPathPlan.stretchActive;
                    const bool independentPitchMode = renderPathPlan.independentPitchMode;
                    const bool independentStretchMode = renderPathPlan.independentStretchMode;

                    // ══════════════════════════════════════════════════════════════
                    // ROUTER — Gate on which knob is active, NOT on mode alone.
                    // Precedence (order matters):
                    //   StretchThenPitch: both active in independent mode.
                    //     Step 1: stretch via DSP core (pitch=0) → pitchInputBuffer_.
                    //     Step 2: independent pitch core on that buffer (N→N samples).
                    //     The pitch core's input-N → output-N contract makes this safe.
                    //   StretchOnly: only stretch active (any mode) → DSP core, falls
                    //     through to the existing dspCore block below with stretchOnlyPathHits.
                    //   PitchOnly: only pitch active in independent mode → independentPitchCore_.
                    //   Normal: everything else → DSP core with full state.
                    // Vocal/PitchOnly mode controls WHICH pitch algorithm; it does NOT
                    // override stretch-only audio onto the pitch path.
                    // ══════════════════════════════════════════════════════════════
                    using RenderPath = SoundEngine::ApexClipRenderPath;
                    const auto renderPath = renderPathPlan.renderPath;

                    // FORENSIC_LOG every 100 blocks so the window stays readable
                    static int routerForensicCounter = 0;
                    if (++routerForensicCounter % 100 == 0)
                    {
                        FORENSIC_LOG("[ROUTER] path=" << (int)renderPath
                            << " pitchActive=" << (int)pitchActive
                            << " stretchActive=" << (int)stretchActive
                            << " mode=" << tpMode);
                    }
                    DBG("[ROUTER] path=" << (int)renderPath
                        << " pitchActive=" << (int)pitchActive
                        << " stretchActive=" << (int)stretchActive
                        << " mode=" << tpMode);

                    // useIndependentPitchPath: true for PitchOnly, StretchOnly, and StretchThenPitch.
                    // SignalSmith handles both pitch and stretch in the independent core.
                    const bool useIndependentPitchPath = renderPathPlan.useIndependentPitchPath;

                    DBG("[PitchRoute] " << (useIndependentPitchPath
                        ? "independent path ACTIVE - legacy path BYPASSED"
                        : "legacy path ACTIVE"));

                    static int renderLogCounter = 0;
                    if (++renderLogCounter % 50 == 0) // log every 50 blocks
                    {
                        DBG("[AUDIO PATH DETECTION] mode=" << tpMode
                            << " pitch=" << clipPitch << "st"
                            << " fineTune=" << fineTuneCents << "ct"
                            << " stretch=" << clipStretch << "x");
                        DBG("[AUDIO PATH DETECTION] pitchActive=" << (pitchActive ? "true" : "false")
                            << " stretchActive=" << (stretchActive ? "true" : "false")
                            << " independentPitchMode=" << (independentPitchMode ? "true" : "false"));
                        DBG("[AUDIO PATH DECISION] useIndependentPitchPath=" << (useIndependentPitchPath ? "true" : "false"));
                        if (useIndependentPitchPath)
                            DBG("[AUDIO PATH] -> INDEPENDENT PITCH CORE (duration preserved)");
                        else
                            DBG("[AUDIO PATH] -> TimePitchDSPCore fallback (may couple pitch/time)");
                    }

                    if (useIndependentPitchPath)
                    {
                        // ═══════════════════════════════════════════════════════════════
                        // FORENSIC AUDIT — Independent Pitch Path Counter
                        // ═══════════════════════════════════════════════════════════════
                        ++PitchAuditCore::independentPitchPathHits;
                        // StretchOnly and StretchThenPitch count stretch activity, but both now run through SignalSmith.
                        if (renderPath == RenderPath::StretchThenPitch || renderPath == RenderPath::StretchOnly)
                            ++PitchAuditCore::stretchOnlyPathHits;

                        // ══════════════════════════════════════════════════════════════
                        // ✅ INDEPENDENT PITCH PATH — HARD DISCONNECTED FROM STRETCH
                        // ══════════════════════════════════════════════════════════════
                        //
                        // ABSOLUTE RULES:
                        //   - NO readRate formula
                        //   - NO pitchRatio / stretchRatio coupling
                        //   - NO TimePitchDSPCore
                        //   - NO PhaseVocoderStretchCore
                        //   - Output samples = Input samples ALWAYS
                        //   - Clip duration NEVER changes
                        //   - Timeline length unchanged
                        //
                        // If both pitch AND stretch are active:
                        //   → First apply stretch (changes duration)
                        //   → Then apply pitch (preserves duration)
                        //   → Serial processing, no coupling formula
                        //
                        // PROOF: count variable never changes from input to output.
                        // ══════════════════════════════════════════════════════════════

                        const int maxChans = juce::jmin(srcChans, pitchInputBuffer_.getNumChannels());

                        // ──────────────────────────────────────────────────────────────
                        // CASE: Pitch and/or stretch active via SignalSmith
                        // Read source with stretch awareness and process in one pass.
                        // ──────────────────────────────────────────────────────────────

                        if (pitchActive)
                            DBG("[INDEPENDENT PITCH] Pitch active: raw=" << clipPitch << "st smoothed=" << smoothedClipPitch << "st");
                        if (stretchActive)
                            DBG("[INDEPENDENT PITCH] Stretch active: " << clipStretch << "x via SignalSmith");

                        // Step 1: Read source into pitch input buffer, with stretch awareness.
                        {
                            // Defensive: clear the full count region BEFORE reading so
                            // any samples not written by the loop below are zero, not stale.
                            pitchInputBuffer_.clear(0, count);

                            // One-shot diagnostic log per unique clip (debug only)
                            {
                                static juce::StringArray loggedClipIds;
                                static juce::CriticalSection logLock;
                                juce::ScopedLock lk(logLock);
                                if (!loggedClipIds.contains(clip->getID()))
                                {
                                    loggedClipIds.add(clip->getID());
                                    FORENSIC_LOG("[CLIP RENDER FIRST PASS]"
                                        << " id=" << clip->getID()
                                        << " sourceStartBound=" << (int64_t)sourceStartBound
                                        << " sourceEndBound=" << (int64_t)sourceEndBound
                                        << " srcTotal=" << srcTotal
                                        << " mode=" << tpMode);
                                }
                            }


                            // One-shot diagnostic log per unique clip (debug only)
                            {
                                static juce::StringArray loggedClipIds;
                                static juce::CriticalSection logLock;
                                juce::ScopedLock lk(logLock);
                                if (!loggedClipIds.contains(clip->getID()))
                                {
                                    loggedClipIds.add(clip->getID());
                                    FORENSIC_LOG("[CLIP RENDER FIRST PASS]"
                                        << " id=" << clip->getID()
                                        << " sourceOffset=" << (int64_t)clip->getSourceOffset()
                                        << " sourceStartBound=" << (int64_t)sourceStartBound
                                        << " sourceEndBound=" << (int64_t)sourceEndBound
                                        << " srcTotal=" << srcTotal
                                        << " mode=" << tpMode);
                                }
                            }

                            const auto inputPlan = SoundEngine::ApexPitchTimeInputPlanCore::makeInputPlan(
                                count,
                                engineClipOffset,
                                clipStretch,
                                pitchInputBuffer_.getNumSamples());
                            const int safeInputSamples = inputPlan.safeInputSamples;

                            for (int ch = 0; ch < maxChans; ++ch)
                            {
                                auto* src = srcBuf->getReadPointer(juce::jmin(ch, srcChans - 1));
                                auto* dst = pitchInputBuffer_.getWritePointer(ch);
                                const auto sourcePositionPlan = SoundEngine::ApexPitchTimeInputPlanCore::makeSourcePositionPlan(
                                    clipReversed,
                                    sourceStartBound,
                                    sourceEndBound,
                                    inputPlan.inputClipOffset);
                                const double srcPosBase = sourcePositionPlan.srcPosBase;
                                const double srcStep = sourcePositionPlan.srcStep;
                                const auto safe = [src, sourceStartBound, sourceEndBound](int idx) noexcept -> float
                                {
                                    return (idx >= (int)sourceStartBound && idx < (int)sourceEndBound) ? src[idx] : 0.f;
                                };

                                for (int s = 0; s < safeInputSamples; ++s)
                                {
                                    const double srcPos = srcPosBase + (double)s * srcStep;
                                    const int i1 = (int)std::floor(srcPos);
                                    const float frac = (float)(srcPos - i1);
                                    const float y0 = safe(i1 - 1);
                                    const float y1 = safe(i1);
                                    const float y2 = safe(i1 + 1);
                                    const float y3 = safe(i1 + 2);
                                    const float c0 = y1;
                                    const float c1 = 0.5f * (y2 - y0);
                                    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                                    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                                    dst[s] = ((c3 * frac + c2) * frac + c1) * frac + c0;
                                }
                            }
                        }

                        if (pitchActive)
                            DBG("[INDEPENDENT PITCH] Read complete: " << count << " samples in pitch input buffer");

                        // ──────────────────────────────────────────────────────────────
                        // Step 2: Independent pitch shift (PRESERVES sample count)
                        // ──────────────────────────────────────────────────────────────

                        if (pitchActive || stretchActive)
                        {
                            DBG("[INDEPENDENT PITCH] Calling ClipIndependentPitchCore.processBlock()");
                            DBG("[INDEPENDENT PITCH]   Output samples: " << count);
                            DBG("[INDEPENDENT PITCH]   Expected output: " << count << " samples (SAME)");

                            const float* inPtrs[2] = {
                                pitchInputBuffer_.getReadPointer(0),
                                pitchInputBuffer_.getReadPointer(juce::jmin(1, maxChans - 1))
                            };
                            // Clear output buffer before every pitch-shift call.
                            // Without this, channels not written (maxChans < numChannels)
                            // and the tail of the buffer retain stale samples from the
                            // previous block, which mix into the output as static noise.
                            pitchOutputBuffer_.clear(0, count);
                            float* outPtrs[2] = {
                                pitchOutputBuffer_.getWritePointer(0),
                                pitchOutputBuffer_.getWritePointer(juce::jmin(1, pitchOutputBuffer_.getNumChannels() - 1))
                            };

                            ArrangementEditor::ClipPitchProcessParams pitchParams;
                            pitchParams.pitchSemitones = pitchActive ? smoothedClipPitch : 0.0;
                            pitchParams.fineTuneCents = pitchActive ? 0.0 : 0.0f;
                            pitchParams.formantSemitones = ac ? ac->getFormantSemitones() : 0.0;
                            pitchParams.preserveFormants = ac ? ac->getPreserveFormants() : false;
                            pitchParams.sampleRate = (int) sampleRate_;
                            pitchParams.channels = maxChans;
                            // Bug 40 fix: forward the per-sample pitch ramp so
                            // ClipIndependentPitchCore updates its target each sample.
                            pitchParams.pitchRampData   = pitchActive ? pitchRamp_.data() : nullptr;
                            pitchParams.pitchRampLength = pitchActive ? count : 0;
                            pitchParams.stretchRatio = (double)clipStretch;
                            auto& pitchCore = getOrCreateClipPitchCore(clip->getID());
                            if (pitchActive)
                                DBG("[PITCH CORE RAMP] first=" << pitchRamp_[0]
                                    << " last=" << smoothedClipPitch);
                            pitchCore.processBlock(inPtrs, outPtrs, maxChans, count, pitchParams);

                            DBG("[INDEPENDENT PITCH] OK PROOF: processBlock received " << count
                                << " samples in, produced " << count << " samples out.");
                            DBG("[INDEPENDENT PITCH] OK Duration is UNCHANGED.");
                            DBG("[INDEPENDENT PITCH] OK NO coupling with stretch.");
                        }
                        else
                        {
                            // Identity: pitch = 0, just copy input to output
                            DBG("[INDEPENDENT PITCH] Identity path: pitch = 0, copying " << count << " samples");
                            for (int ch = 0; ch < maxChans; ++ch)
                            {
                                const float* in = pitchInputBuffer_.getReadPointer(ch);
                                float* out = pitchOutputBuffer_.getWritePointer(juce::jmin(ch, pitchOutputBuffer_.getNumChannels() - 1));
                                std::memcpy(out, in, sizeof(float) * (size_t)count);
                            }
                        }

                        // ──────────────────────────────────────────────────────────────
                        // Step 3: Apply gain/fades and mix to track buffer
                        // PROOF: Still mixing exactly count samples
                        // ──────────────────────────────────────────────────────────────

                        DBG("[PitchRoute] output write count = 1");
                        DBG("[INDEPENDENT PITCH] Mixing " << count << " samples to track buffer");

                        const int renderChannels = getClipRenderTarget().getNumChannels();
                        for (int ch = 0; ch < renderChannels; ++ch)
                        {
                            auto* trackDst = getClipRenderTarget().getWritePointer(ch, bufferStart);
                            const auto outputChannelPlan = SoundEngine::ApexClipOutputMixPlanCore::makeChannelPlan(ch, maxChans);
                            const auto* pitchSrc = pitchOutputBuffer_.getReadPointer(outputChannelPlan.sourceChannel);

                            for (int s = 0; s < count; ++s)
                            {
                                const float panGain = SoundEngine::ApexClipOutputMixPlanCore::getPanGainForChannel(
                                    ch,
                                    renderChannels,
                                    clipPanGains.first[s],
                                    clipPanGains.second[s]);
                                float sample = pitchSrc[s] * clipGain * panGain;
                                int   posInClip = (int) (engineClipOffset + s);
                                float fadeGain = computeFadeGain(posInClip, (int) clipTimelineLength,
                                                                 fadeInLength, fadeOutLength,
                                                                 fadeInCurve, fadeOutCurve);
                                trackDst[s] += sample * fadeGain;
                            }
                        }

                        if (clipTapeStop > 0.0001f)
                        {
                            auto* left = getClipRenderTarget().getWritePointer(0, bufferStart);
                            const auto tapeStopChannelPlan = SoundEngine::ApexClipOutputMixPlanCore::makeTapeStopChannelPlan(renderChannels);
                            auto* right = getClipRenderTarget().getWritePointer(tapeStopChannelPlan.rightChannel, bufferStart);
                            getOrCreateClipTapeStopProcessor(clip->getID()).process(left, right, count, clipTapeStop);
                        }

                        DBG("[INDEPENDENT PITCH] OK COMPLETE");
                        DBG("[INDEPENDENT PITCH] Input:  " << count << " samples");
                        DBG("[INDEPENDENT PITCH] Output: " << count << " samples");
                        DBG("[INDEPENDENT PITCH] Pitch: raw=" << clipPitch << "st smoothed=" << smoothedClipPitch << "st");
                        DBG("[INDEPENDENT PITCH] Stretch: " << clipStretch << "x (IGNORED in this mode)");
                        DBG("[INDEPENDENT PITCH] Duration: UNCHANGED");
                        DBG("[INDEPENDENT PITCH] Timeline length: UNCHANGED");
                        DBG("[INDEPENDENT PITCH] OK PROOF: Pitch is HARD DISCONNECTED from time stretch");
                        DBG("[INDEPENDENT PITCH] OK PROOF: NO TimePitchDSPCore used");
                        DBG("[INDEPENDENT PITCH] OK PROOF: NO PhaseVocoderStretchCore used");
                        DBG("[INDEPENDENT PITCH] OK PROOF: NO readRate formula used");
                        DBG("[INDEPENDENT PITCH] ===============================================");

                        // EXPLICIT RETURN: Bypass the old TimePitchDSPCore fallback path entirely
                        // Record which render path used the dspCore this block so the Normal
                        // fallback can detect a STP→Normal transition and reset accordingly.
                        lastClipRenderPath_[clip->getID()] = (int)renderPath;
                        return;
                    }

                    // ═══════════════════════════════════════════════════════════════
                    // FORENSIC AUDIT — DSP Core Path Counter
                    // StretchOnly falls here (pitch≈0, dspCore handles duration change).
                    // Normal falls here for all other non-independent-mode cases.
                    // ═══════════════════════════════════════════════════════════════
                    if (renderPath == RenderPath::StretchOnly)
                        ++PitchAuditCore::stretchOnlyPathHits;
                    else
                        ++PitchAuditCore::normalPathHits;

                    // ──────────────────────────────────────────────────────────────
                    // Fallback to existing TimePitchDSPCore for modes that need
                    // time-stretch or combined processing
                    // ──────────────────────────────────────────────────────────────

                    // Each clip needs its own DSP core instance for continuity.
                    // We use a per-clip map keyed by ClipID.
                    // If not found, a new one is created (first time per clip).
                    auto& dspCore = getOrCreateClipDSP(clip->getID());

                    const auto lastBlockIt = lastClipRenderBlock_.find(clip->getID());
                    const auto lastEndIt   = lastClipRenderEndOffset_.find(clip->getID());
                    const auto lastModeIt  = lastClipRenderMode_.find(clip->getID());
                    const bool renderedLastBlock = (lastBlockIt != lastClipRenderBlock_.end()
                                                    && lastBlockIt->second + 1 == processCounter_);
                    const bool continuousOffset = (lastEndIt != lastClipRenderEndOffset_.end()
                                                   && lastEndIt->second == engineClipOffset);
                    const bool sameMode = (lastModeIt != lastClipRenderMode_.end()
                                           && lastModeIt->second == tpMode);
                    // Bug 15 fix: also reset if last block used StretchThenPitch path —
                    // that path sets dspCore to Stretch-only state which is incompatible
                    // with Normal/StretchOnly full-state processing.
                    const auto lastPathIt = lastClipRenderPath_.find(clip->getID());
                    const bool samePath = (lastPathIt == lastClipRenderPath_.end())
                                       || (lastPathIt->second != (int)RenderPath::StretchThenPitch);
                    const auto continuityPlan = SoundEngine::ApexFallbackTimePitchContractCore::makeContinuityPlan(
                        renderedLastBlock,
                        continuousOffset,
                        sameMode,
                        samePath);

                    if (!sameMode)
                    {
                        DBG("[TimePitch] Clip " << clip->getID()
                            << " mode=" << tpMode
                            << " pitch=" << clipPitch
                            << " stretch=" << clipStretch);
                    }

                    if (continuityPlan.shouldReset)
                        dspCore.reset();

                    // Publish state if changed
                    if (ac)
                    {
                        // Bug 1 fix (setState side): StretchOnly must never
                        // publish a non-zero pitch into TimePitchDSPCore.
                        // If we do, ClipPitchRenderPathCore's internal smoother
                        // stays non-zero, isPitchTransitionActive() returns true,
                        // and renderClipSegment routes through the second pitch
                        // pipeline even though req.state.pitchSemitones was zeroed.
                        const auto contractPlan = SoundEngine::ApexFallbackTimePitchContractCore::makeContractPlan(
                            renderPath == RenderPath::StretchOnly,
                            smoothedClipPitch,
                            clipStretch,
                            tpMode);
                        // Bug 37 fix (setState side): clear voiceTransform when pitch is
                        // zeroed so TimePitchDSPCore::setState never marks the state as
                        // pitch-active due to a stale voiceTransform, avoiding an
                        // isPitchTransitionActive() false-positive on the next block.
                        const auto tpState = SoundEngine::ApexFallbackTimePitchStateCore::makeState(
                            contractPlan,
                            renderPath == RenderPath::StretchOnly,
                            ac->getPreserveFormants(),
                            ac->getFormantSemitones());
                        dspCore.setState(tpState);
                    }

                    const bool isIdentityModernMode = SoundEngine::ApexFallbackTimePitchContractCore::isIdentityModernMode(
                        clipPitch,
                        fineTuneCents,
                        clipStretch,
                        ac ? ac->getFormantSemitones() : 0.0,
                        ac ? ac->getPreserveFormants() : false);

                    const int renderChannels = getClipRenderTarget().getNumChannels();
                    for (int ch = 0; ch < renderChannels; ++ch)
                    {
                        auto* dst = getClipRenderTarget().getWritePointer(ch, bufferStart);
                        int   sch = SoundEngine::ApexFallbackTimePitchContractCore::makeSourceChannel(ch, srcChans);

                        if (isIdentityModernMode)
                        {
                            auto* src = useTunedAudio ? tunedSrc : srcBuf->getReadPointer(sch);
                            for (int s = 0; s < count; ++s)
                            {
                                const auto readPlan = SoundEngine::ApexIdentityFallbackReadPlanCore::makeReadPlan(
                                    engineClipOffset,
                                    s,
                                    srcPerEngineSample,
                                    clipReversed,
                                    sourceStartBound,
                                    sourceEndBound,
                                    srcTotal);
                                if (!readPlan.valid)
                                    continue;

                                const float panGain = SoundEngine::ApexClipOutputMixPlanCore::getPanGainForChannel(
                                    ch,
                                    renderChannels,
                                    clipPanGains.first[s],
                                    clipPanGains.second[s]);
                                float sample = src[(int) readPlan.sourceIndex] * clipGain * panGain;
                                float fadeGain = computeFadeGain(readPlan.posInClip, (int) clipTimelineLength,
                                                                 fadeInLength, fadeOutLength,
                                                                 fadeInCurve, fadeOutCurve);
                                dst[s] += sample * fadeGain;
                            }

                            const auto renderStatePlan = SoundEngine::ApexClipRenderStateBookkeepingCore::makeCompletedRenderPlan(
                                engineClipOffset,
                                count,
                                processCounter_,
                                tpMode,
                                (int)RenderPath::Normal);
                            lastClipRenderEndOffset_[clip->getID()] = renderStatePlan.endOffset;
                            lastClipRenderBlock_[clip->getID()]     = renderStatePlan.processBlock;
                            lastClipRenderMode_[clip->getID()]      = renderStatePlan.mode;
                            // Record path so a later switch to Normal resets the dspCore.
                            lastClipRenderPath_[clip->getID()]      = renderStatePlan.path;
                            continue;
                        }

                        ArrangementEditor::TimePitchRenderRequest req = SoundEngine::ApexFallbackTimePitchRequestCore::makeRequest(
                            useTunedAudio ? tunedSrc : srcBuf->getReadPointer(sch),
                            srcTotal,
                            sourceStartBound,
                            sourceEndBound,
                            scratchBuf_.data(),
                            count,
                            ch,
                            sampleRate_,
                            SoundEngine::ApexFallbackTimePitchContractCore::makeCallerTag(
                                renderPath == RenderPath::StretchOnly,
                                renderPath == RenderPath::Normal,
                                pitchActive,
                                stretchActive));
                        if (ac)
                        {
                            // BUG 1 FIX: StretchOnly and Normal (non-pitch) paths must
                            // NEVER carry a non-zero pitchSemitones into
                            // TimePitchDSPCore::renderClipSegment().
                            //
                            // If pitchSemitones != 0, the dispatcher enters
                            // ClipPitchRenderPathCore which runs a second, completely
                            // separate WSOLA+tape pitch pipeline. That doubles the
                            // pitch processing and produces static / flanging / double-
                            // voice artifacts.
                            //
                            // StretchOnly: pitch should be 0 by definition (only stretch is active).
                            // Normal: pitch is 0 or the mode does not use IndependentPitchCore,
                            //   so TimePitchDSPCore must also NOT apply pitch independently.
                            // In both cases, force pitch to 0 so renderClipSegment takes the
                            // identity / stretch-only code path and never enters
                            // ClipPitchRenderPathCore.
                            const auto contractPlan = SoundEngine::ApexFallbackTimePitchContractCore::makeContractPlan(
                                renderPath == RenderPath::StretchOnly,
                                smoothedClipPitch,
                                clipStretch,
                                tpMode);
                            SoundEngine::ApexFallbackTimePitchRequestCore::applyStateContract(
                                req,
                                contractPlan,
                                ac->getPreserveFormants(),
                                ac->getFormantSemitones());
                        }

                        dspCore.renderClipSegment(req);

                        // Apply clip gain + fades to scratch, then mix to dst
                        for (int s = 0; s < count; ++s)
                        {
                            const float panGain = SoundEngine::ApexClipOutputMixPlanCore::getPanGainForChannel(
                                ch,
                                renderChannels,
                                clipPanGains.first[s],
                                clipPanGains.second[s]);
                            const auto scratchMixPlan = SoundEngine::ApexFallbackScratchMixPlanCore::makeSamplePlan(
                                scratchBuf_[s],
                                clipGain,
                                panGain,
                                engineClipOffset,
                                s);
                            float fadeGain  = computeFadeGain(scratchMixPlan.posInClip, (int)clipTimelineLength,
                                                              fadeInLength, fadeOutLength,
                                                              fadeInCurve, fadeOutCurve);
                            dst[s] += scratchMixPlan.preFadeSample * fadeGain;
                        }

                        const auto renderStatePlan = SoundEngine::ApexClipRenderStateBookkeepingCore::makeCompletedRenderPlan(
                            engineClipOffset,
                            count,
                            processCounter_,
                            tpMode,
                            (int)renderPath);
                        lastClipRenderEndOffset_[clip->getID()] = renderStatePlan.endOffset;
                        lastClipRenderBlock_[clip->getID()]     = renderStatePlan.processBlock;
                        lastClipRenderMode_[clip->getID()]      = renderStatePlan.mode;
                        lastClipRenderPath_[clip->getID()]      = renderStatePlan.path;
                    }
                }
                return;
            }
        }

        // Fallback: generate a subtle test tone for clips without audio files
        float freq = 220.f + (float)(clip->getName().hashCode() % 440);
        for (int ch = 0; ch < trackBuffer_.getNumChannels(); ++ch)
        {
            auto& renderTarget = getClipRenderTarget();
            auto* dst = renderTarget.getWritePointer(ch, bufferStart);
            int fadeInLength = 0;
            int fadeOutLength = 0;
            int fadeInCurve = 0;
            int fadeOutCurve = 0;
            if (auto* ac = dynamic_cast<AudioClip*>(clip))
            {
                fadeInLength = (int) ac->getFadeInLength();
                fadeOutLength = (int) ac->getFadeOutLength();
                fadeInCurve = ac->getFadeInCurve();
                fadeOutCurve = ac->getFadeOutCurve();
            }
            for (int s = 0; s < count; ++s)
            {
                double t = (double)(engineClipOffset + s) / sampleRate_;
                float sample = 0.15f * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t);
                int posInClip = (int)(engineClipOffset + s);
                float fadeGain = 1.0f;

                if (fadeInLength > 0 && posInClip < fadeInLength)
                    fadeGain *= applyClipFadeCurve((float) posInClip / (float) juce::jmax(1, fadeInLength), fadeInCurve);

                const int fadeOutStart = juce::jmax(0, (int) clipTimelineLength - fadeOutLength);
                if (fadeOutLength > 0 && posInClip >= fadeOutStart)
                {
                    const float x = (float) (posInClip - fadeOutStart) / (float) juce::jmax(1, fadeOutLength);
                    fadeGain *= applyClipFadeCurve(juce::jlimit(0.0f, 1.0f, 1.0f - x), fadeOutCurve);
                }

                sample *= fadeGain;
                dst[s] += sample;
            }
        }
    }
};

} // namespace DAW
