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
#include "../PDCCore/LiveMonitoringBypassCore.h"
#include "../MeteringCore/MeteringFacadeCore.h"
#include "../MeteringCore/TrackPeakMeterManagerCore.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../PluginHostCore/PluginPlayheadInfoCore.h"
#include "../PluginHostCore/ClipRegionPluginCore.h"
#include "../FolderBusCore/FolderBusStateModel.h"
#include "../MidiCore/MidiInputCore.h"
#include "../InputMonitorCore/TrackInputProcessorCore.h"
#include "../RecordingCore/RecordingInputValidityCore.h"
#include "../RenderCore/StemRouteMaskCore.h"
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
#include "../StepSequencerCore/StepSequencerModel.h"
#include "../StepSequencerCore/StepSequencerPlaybackCore.h"
#include "../ClipCore/PatternClip.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/VoiceTransformMapperCore.h"
#include "../UICore/ForensicAuditWindow.h"
#include <cstdint>
#include <map>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <unordered_set>
#include <limits>

// Audio-thread logging must be OFF in Release builds.
// Set to 1 only for targeted debug sessions, then revert to 0.
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
    using PostFaderRecordTap = void (*)(void* context,
                                        const TrackID& trackId,
                                        const juce::AudioBuffer<float>& postFaderBuffer,
                                        int numSamples,
                                        bool captureThisBlock) noexcept;

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

    /** C3: publish the immutable plugin-chain map snapshot (MESSAGE THREAD,
     *  ApplicationCore). The audio thread reads ONLY this snapshot — never
     *  the live std::map (RB-tree rotation during a concurrent find is UB). */
    using PluginChainSnapshotMap = std::map<TrackID, std::shared_ptr<PluginChainCore>>;
    void publishPluginChainsSnapshot(std::shared_ptr<const PluginChainSnapshotMap> snap) noexcept
    {
        std::atomic_store_explicit(&pluginChainsSnapshot_, std::move(snap), std::memory_order_release);
    }
    std::shared_ptr<const PluginChainSnapshotMap> getPluginChainsSnapshotRT() const noexcept
    {
        return std::atomic_load_explicit(&pluginChainsSnapshot_, std::memory_order_acquire);
    }

    /**
     * Release the previous block's chain-map snapshot at a control-plane
     * lifecycle boundary.  The caller must have stopped/gated device callbacks
     * and waited for admitted callbacks to drain before invoking this method;
     * it exists so chain destruction cannot be deferred until AudioEngine's
     * member teardown after the application's final retirement drain.
     */
    void releasePluginChainsSnapshotForControlPlane() noexcept
    {
        pluginChainsBlockSnap_.reset();
    }

    /** Supply MIDI playback core for MIDI tracks. */
    void setMidiPlayback(DAW::PianoRollPlaybackCore* pb) noexcept { midiPlayback_ = pb; }
    void setMidiInput(DAW::MidiInputCore* input) noexcept { midiInput_ = input; }
    void setVirtualKeyboard(DAW::VirtualMidiKeyboardCore* kb) noexcept { virtualKeyboard_ = kb; }
    void setStepSequencerModel(DAW::StepSequencerModel* model) noexcept { stepSequencerModel_ = model; }
    void setPluginPlayheadInfoCore(PluginPlayheadInfoCore* playheadInfo) noexcept { pluginPlayheadInfo_ = playheadInfo; }
    void setClipRegionPluginCore(ClipRegionPluginCore* clipRegionPlugins) noexcept { clipRegionPlugins_ = clipRegionPlugins; }
    void setAutomationManager(AutomationManagerCore* automationManager) noexcept { automationManager_ = automationManager; }
    void setTrackPeakMeterManager(TrackPeakMeterManagerCore* mgr) noexcept { trackPeakMeterManager_ = mgr; }
    void setVocalTuneIntegration(apex::vocaltune::ApexTuneIntegrationCore* integration) noexcept { vocalTuneIntegration_ = integration; }
    void setLiveInputBuffer(const juce::AudioBuffer<float>* inputBuffer,
                            int numSamples,
                            int validInputChannels) noexcept
    {
        liveInputBuffer_ = inputBuffer;
        liveInputNumSamples_ = numSamples;
        liveInputValidChannels_ = inputBuffer != nullptr
            ? juce::jmin(inputBuffer->getNumChannels(), juce::jmax(0, validInputChannels))
            : 0;
    }
    void clearLiveInputBuffer() noexcept
    {
        liveInputBuffer_ = nullptr;
        liveInputNumSamples_ = 0;
        liveInputValidChannels_ = 0;
    }

    void setPostFaderRecordTap(void* context, PostFaderRecordTap tap) noexcept
    {
        postFaderRecordContext_ = context;
        postFaderRecordTap_ = tap;
    }

    void setPostFaderRecordCaptureEnabled(bool enabled) noexcept
    {
        postFaderRecordCaptureEnabled_ = enabled;
    }
    /** Publish per-track plugin-chain latencies (MESSAGE THREAD only).
     *  Called by ApplicationCore whenever a chain is created, structurally
     *  changed, cleared, or prepared. Lets the audio thread recompute PDC
     *  entirely from published state — it never iterates the live
     *  RoutingGraph or the pluginChains_ map for PDC. */
    using ChainLatencyMap = std::map<TrackID, int>;
    void publishChainLatencies(std::shared_ptr<const ChainLatencyMap> latencies) noexcept
    {
        std::atomic_store_explicit(&chainLatencies_, std::move(latencies), std::memory_order_release);
    }
    /** Acquire the current latency map (audio-thread safe, lock-free). */
    std::shared_ptr<const ChainLatencyMap> getChainLatenciesRT() const noexcept
    {
        return std::atomic_load_explicit(&chainLatencies_, std::memory_order_acquire);
    }

    // Release so the latency-map release-store above is visible to any thread
    // that observes this dirty flag (publication happens before dirty-marking
    // on the message thread).
    void markMasterPdcDirty() noexcept { masterPdcDirty_.store(true, std::memory_order_release); }

    /** C6: monitoring-PDC mode for record-armed tracks with live monitoring
     *  enabled. Bypass (product default) skips master-edge compensation for
     *  those tracks so the performer hears only their own chain's latency;
     *  Reduced caps the compensation at max(2*blockSize, 1024) samples;
     *  Full preserves the previous behaviour. Effective-delay transitions
     *  crossfade via the C7 delay-line machinery. */
    void setMonitoringPdcMode(DAW::LiveMonitoringBypassCore::Mode m) noexcept
    {
        monitoringPdcMode_.store((int) m, std::memory_order_release);
    }
    DAW::LiveMonitoringBypassCore::Mode getMonitoringPdcMode() const noexcept
    {
        return (DAW::LiveMonitoringBypassCore::Mode) monitoringPdcMode_.load(std::memory_order_acquire);
    }

    /** C9: per-sample transport fade ramp applied to the engine output in
     *  the last processed block — nullptr when no fade was applied. The
     *  click/metronome sum uses this to join the exact same fade curve.
     *  Audio-thread read only (written in process()). */
    const float* getTransportFadeRamp(int numSamples) const noexcept
    {
        return (transportFadeRampActive_ && numSamples <= (int) transportFadeRamp_.size())
            ? transportFadeRamp_.data() : nullptr;
    }

    double getSampleRate() const noexcept { return sampleRate_; }
    int getBlockSize() const noexcept { return blockSize_; }

#if APEX_ENABLE_TEST_HOOKS
    struct DeviceLifecycleSnapshotForTesting
    {
        int blockSize = 0;
        uint64_t processCounter = 0;
        bool engineWasPlaying = false;
        SamplePosition stopFadePosition = 0;
        size_t clipAudioCacheCount = 0;
        size_t clipDspCount = 0;
        size_t clipPitchCount = 0;
        size_t clipTapeStopCount = 0;
        size_t clipRenderCursorCount = 0;
        size_t trackVolumeRampCount = 0;
        size_t trackMuteFadeCount = 0;
        size_t connectionRampCount = 0;
        size_t pdcLineCount = 0;
        size_t pluginChainCount = 0;
        uint64_t routingSnapshotVersion = 0;
    };

    DeviceLifecycleSnapshotForTesting getDeviceLifecycleSnapshotForTesting() const
    {
        DeviceLifecycleSnapshotForTesting s;
        s.blockSize = blockSize_;
        s.processCounter = processCounter_;
        s.engineWasPlaying = engineWasPlaying_;
        s.stopFadePosition = engineStopFadePosition_;
        s.clipAudioCacheCount = clipAudioCacheMap_.size();
        s.clipDspCount = clipDspMap_.size();
        s.clipPitchCount = clipPitchCoreMap_.size();
        s.clipTapeStopCount = clipTapeStopProcessorMap_.size();
        s.clipRenderCursorCount = std::max(lastClipRenderEndOffset_.size(),
                                           lastClipRenderPath_.size());
        s.trackVolumeRampCount = trackVolumeRampMap_.size();
        s.trackMuteFadeCount = muteFadeMap_.size();
        s.connectionRampCount = connectionGainRamps_.size();
        s.pdcLineCount = pdcLines_.size();
        if (const auto chains = getPluginChainsSnapshotRT())
            s.pluginChainCount = chains->size();
        s.routingSnapshotVersion = lastRoutingSnapshotVersion_;
        return s;
    }
#endif

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
        if ((int) tapeStopRamp_.size() < capacity)
            tapeStopRamp_.resize((size_t) capacity, 0.0f);

        mixBuffer_.clear();
        trackBuffer_.clear();
        preFxBuffer_.clear();
        clipRegionPluginBuffer_.clear();
        routingBuffers_.clearAllAudio();
        resetAllClipDSPState();
        // Offline blocks may be larger than the live device block.  Reprepare
        // the already-owned clip tape heads on this control/render thread so
        // their prepared capacity and channel contract match the buffer that
        // renderClip() will use; no callback-side growth is permitted.
        for (auto& [id, core] : clipTapeStopProcessorMap_)
            if (core)
                core->prepare(sampleRate_, capacity, 2);
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
        if ((int) resampleRateRamp_.size() < blockSize * 2)
            resampleRateRamp_.assign((size_t)(blockSize * 2), 0.0);
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
        const int inputPrimeCapacity =
            ArrangementEditor::ClipIndependentPitchCore::maxInputPreRollSamplesForSampleRate(sampleRate_);
        if (pitchInputBuffer_.getNumSamples() < blockSize * 10 + inputPrimeCapacity)
            pitchInputBuffer_.setSize(2, blockSize * 10 + inputPrimeCapacity);
        if (pitchOutputBuffer_.getNumSamples() < blockSize)
            pitchOutputBuffer_.setSize(2, blockSize);
    }

    void resetOfflineDebugLogging(int blocks) noexcept
    {
        offlineRouteDebugBlocksRemaining_ = juce::jmax(0, blocks);
    }

    /** Immutable per-pass stem route policy. It is installed only after the
        realtime callback barrier is owned and cleared before realtime resumes. */
    using OfflineStemRenderMask = DAW::OfflineStemRenderMask;

    void setOfflineStemRenderMask(std::shared_ptr<const OfflineStemRenderMask> mask)
    {
        offlineStemRenderMask_ = std::move(mask);
    }

    void clearOfflineStemRenderMask() { offlineStemRenderMask_.reset(); }

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
        // prepare() is entered only after the device/project callback gate has
        // drained admitted realtime work. Retire the previous block's chain
        // snapshot here so replacing it in processWithSnapshot() can never
        // become the last-owner release of an old project on the audio thread.
        pluginChainsBlockSnap_.reset();

        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
        // Pre-allocate all buffers for the worst-case block size (8192).
        // This prevents crashes when ASIO4ALL or other drivers reconfigure
        // to a larger buffer mid-session — the allocated capacity never shrinks.
        const int worstCaseBlock = juce::jmax(blockSize, 8192);

        apexSoundEngineCore_.prepare(sampleRate, blockSize);
        clipRenderCore_.prepare(sampleRate, worstCaseBlock);
        mixBuffer_.setSize(2, worstCaseBlock);
        trackBuffer_.setSize(2, worstCaseBlock);
        preFxBuffer_.setSize(2, worstCaseBlock);
        routingBuffers_.prepare(worstCaseBlock, routing_ != nullptr ? routing_->getNodeCount() + 4 : 128);
        pdcLines_.reserve(64);
        scratchBuf_.assign(worstCaseBlock * 8, 0.f); // 8x for stretch overrun
        pitchRamp_.assign(worstCaseBlock * 2, 0.f);
        resampleRateRamp_.assign(worstCaseBlock * 2, 0.0);  // per-sample Resample-mode read-rate trajectory
        clipGainRamp_.assign(worstCaseBlock, 0.f);   // per-sample clip gain ramp  // 2x headroom for sub-block partial renders
        clipPanLeftRamp_.assign(worstCaseBlock, 1.0f);
        clipPanRightRamp_.assign(worstCaseBlock, 1.0f);
        tapeStopRamp_.assign(worstCaseBlock, 0.0f);

        // Reserve map capacity to prevent rehash during audio callback.
        // New inserts after this point must never trigger a rehash.
        constexpr size_t kMaxTracks = 256;
        constexpr size_t kMaxClips  = 1024;
        trackVolumeRampMap_.reserve(kMaxTracks);
        muteFadeMap_.reserve(kMaxTracks);
        clipTapeStopProcessorMap_.reserve(kMaxClips);
        clipTapeStopParameterIdMap_.reserve(kMaxClips);
        clipDspMap_.reserve(kMaxClips);
        clipPitchCoreMap_.reserve(kMaxClips);
        clipPitchSmootherMap_.reserve(kMaxClips);
        clipPanRampMap_.reserve(kMaxClips);
        clipTapeStopStateMap_.reserve(kMaxClips);
        resampleReadStateMap_.reserve(kMaxClips);
        liveInputMonitorGainByTrack_.reserve(kMaxTracks);
        // Routing edges: reserve buckets so first-use inserts never rehash
        // inside the audio callback. (Per-node first-use allocation is still
        // possible and documented; rehash is the unbounded operation.)
        constexpr size_t kMaxEdges = 4096;
        connectionGainRamps_.reserve(kMaxEdges);
        clipAudioCacheMap_.reserve(kMaxClips);
        retiredClipAudio_.reserve((size_t) kMaxRetiredClipAudio);
        retiredClipAudio_.clear();

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
        // C2-device-lifecycle: releaseResources() destroys every per-clip DSP
        // core on device stop (Bug 16 stale-state semantics). Re-create the
        // full set for every loaded audio clip HERE on the message thread so
        // the first realtime block after a device reconfiguration (or project
        // restore, which also funnels through prepare()) never allocates
        // inside getOrCreateClipDSP()/getOrCreateClipPitchCore()/
        // getOrCreateClipPitchSmoother(). Cores are freshly constructed (no
        // stale ring/filter state) and receive their render state on the
        // audio thread exactly as before.
        if (clips_ != nullptr)
        {
            for (auto* clip : clips_->getAllClips())
                if (clip != nullptr && dynamic_cast<AudioClip*>(clip) != nullptr)
                    prewarmClipDSP(clip->getID());
        }
        for (auto& [id, core] : trackVolumeRampMap_)
            if (core) core->prepare(sampleRate, worstCaseBlock);
        stepSequencerPlayback_.prepareToPlay(sampleRate, blockSize);
        if (routing_)
        {
            pdcLines_.reserve((size_t) juce::jmax(8, routing_->getConnectionCount() + 4));
        }
        // Re-prepare any engine-owned connection gain ramps at the new sample rate.
        // Capacity uses the worst-case block so a mid-session driver reconfigure
        // to a larger buffer never triggers a ramp re-allocation on the audio thread.
        for (auto& [id, ramp] : connectionGainRamps_)
            ramp.prepare(sampleRate, worstCaseBlock);
        for (auto& [id, core] : muteFadeMap_)
            if (core) core->prepare(sampleRate, worstCaseBlock);
        for (auto& [id, core] : clipTapeStopProcessorMap_)
            if (core) core->prepare(sampleRate, worstCaseBlock, 2);
        // Per-clip independent pitch cores are created on demand in getOrCreateClipPitchCore()
        pitchInputBuffer_.setSize(2, worstCaseBlock * 10
            + ArrangementEditor::ClipIndependentPitchCore::maxInputPreRollSamplesForSampleRate(sampleRate));
        pitchOutputBuffer_.setSize(2, worstCaseBlock);
        clipRegionPluginBuffer_.setSize(2, worstCaseBlock);
        pdcScratchL_.assign(worstCaseBlock, 0.f);
        pdcScratchR_.assign(worstCaseBlock, 0.f);
        masterPdcScratchL_.assign(worstCaseBlock, 0.f);
        masterPdcScratchR_.assign(worstCaseBlock, 0.f);
        transportFadeRamp_.assign(worstCaseBlock, 1.0f);
        monitoringPdcReducedCap_ = juce::jmax(blockSize_ * 2, 1024);   // C6 Reduced-mode cap
        // Sidechain PDC lines from the published snapshot + latency map
        // (message thread here, so the publisher read is safe).
        if (routing_)
        {
            if (auto prepSnap = routing_->getSnapshotPublisher().get())
            {
                auto latencies = getChainLatenciesRT();
                rebuildPdcLines(*prepSnap, latencies.get(), worstCaseBlock);
                // C2-device-lifecycle: pre-size every per-node routing buffer
                // on THIS (message) thread. syncNodeBuffers() runs on the
                // audio thread for the first block after reconfiguration
                // (lastRoutingSnapshotVersion_ is reset to 0 below), and
                // ApexRoutingBufferCore::syncFromSnapshot grows any buffer
                // whose capacity is below jmax(numSamples, blockSize_). With
                // worstCaseBlock (>= 8192) capacity prepared here, that first
                // audio-thread sync is a capacity check only — never an
                // allocation. Previously every device reconfiguration
                // (e.g. 512 -> 2048) cleared all node buffers in
                // releaseResources() and reallocated them inside the first
                // audio callback, stalling the callback and producing audible
                // cracking at large block sizes.
                routingBuffers_.syncFromSnapshot(*prepSnap, worstCaseBlock);
            }
        }
        lastRoutingSnapshotVersion_ = 0;
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
        // A device stop destroys this execution lifetime. If transport.stop()
        // happened after the last callback, engineWasPlaying_ still describes
        // the previous session and would otherwise synthesize its stop-fade on
        // the first callback after reprepare. That callback can touch freshly
        // rebuilt clip/plugin/routing state while the real transport is stopped.
        engineWasPlaying_ = false;
        engineStopFadePosition_ = 0;
        transportFadeGain_ = 1.0f;
        transportFadeRampActive_ = false;

        mixBuffer_.setSize(0, 0);
        trackBuffer_.setSize(0, 0);
        clipRegionPluginBuffer_.setSize(0, 0);
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
        clipTapeStopParameterIdMap_.clear();
        clipTapeStopStateMap_.clear();
        resampleReadStateMap_.clear();
        clipPanRampMap_.clear();
        trackVolumeRampMap_.clear();
        muteFadeMap_.clear();
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

        // ── Transport fade state machine (click-free start/stop/seek) ────
        // Professional engines never hard-cut the master mix on transport
        // changes. Two mechanisms:
        //   1. Play start → master fade-in from silence (~5 ms soft start).
        //   2. Stop       → render ONE extra block from the engine's
        //      continuation position with a linear fade-out to exact zero,
        //      so the waveform lands at 0.0 instead of truncating mid-cycle.
        // Correct at ANY buffer size and sample rate: the fade-in step is
        // derived from the sample rate; the stop fade always completes
        // within the block regardless of block length.
        bool stopFadeThisBlock = false;
        bool effectivePlaying  = isPlaying;
        SamplePosition effectivePosition = position;

        if (!isPlaying && engineWasPlaying_)
        {
            // Stop pressed between callbacks: play one final faded block.
            stopFadeThisBlock = true;
            effectivePlaying  = true;
            effectivePosition = engineStopFadePosition_;
        }
        else if (isPlaying && !engineWasPlaying_)
        {
            transportFadeGain_ = 0.0f; // soft start on play
        }

        auto renderContext = apexSoundEngineCore_.makeContext(numSamples, effectivePosition, effectivePlaying, SoundEngine::RenderMode::Live);
        if (!apexSoundEngineCore_.validateContext(renderContext))
            return;

        ++processCounter_;

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

#if APEX_AUDIO_DEBUG_LOGS
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
#endif

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

        // Use the EFFECTIVE transport view so the stop-fade block renders as a
        // seamless continuation (no DSP reset mid-fade). The real play→stop
        // discontinuity fires on the NEXT block, when output is already silent.
        const bool transportDiscontinuous = clipRenderCore_.consumeTransportDiscontinuity(effectivePosition, effectivePlaying, numSamples);

        if (transportDiscontinuous)
            clipRenderCore_.noteDspResetRequest();
        if (transportDiscontinuous)
            resetAllClipDSPState();

        // Seek while playing (position jump): the waveform is about to jump
        // mid-cycle. Re-arm the soft start so the jump is masked by a ~5 ms
        // ramp instead of producing a hard discontinuity (click/zipper).
        if (transportDiscontinuous && effectivePlaying && !stopFadeThisBlock)
            transportFadeGain_ = 0.0f;

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
                processWithSnapshot(snap.get(), output, numSamples, effectivePlaying, effectivePosition, anySolo, fbSnap.get(), automationSnap.get());
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

        // ── Transport fade application (click-free start/stop) ──────────
        // Applied at the FINAL master output so every source (clips, live
        // monitoring, plugins, metronome) is covered by one gate.
        {
            const int  numCh = juce::jmin(2, output.buffer->getNumChannels());
            float* outL = output.buffer->getWritePointer(0, output.startSample);
            float* outR = numCh > 1 ? output.buffer->getWritePointer(1, output.startSample) : nullptr;

            if (stopFadeThisBlock)
            {
                // Linear fade from the current gain down to exactly 0.0 across
                // this block — works for any block length. Ends at true zero so
                // the next (silent) block cannot click.
                const float g0 = transportFadeGain_;
                const float step = numSamples > 0 ? g0 / (float) numSamples : 0.0f;
                const bool storeRamp = numSamples <= (int) transportFadeRamp_.size();
                float g = g0;
                for (int s = 0; s < numSamples; ++s)
                {
                    g -= step;
                    const float gain = juce::jmax(0.0f, g);
                    outL[s] *= gain;
                    if (outR) outR[s] *= gain;
                    if (storeRamp) transportFadeRamp_[(size_t) s] = gain;   // C9
                }
                transportFadeRampActive_ = storeRamp;
                transportFadeGain_ = 0.0f;
            }
            else if (isPlaying && transportFadeGain_ < 0.9999f)
            {
                // One-pole soft start (~5 ms). Sample-rate aware: coefficient
                // derives from sampleRate_, so fade time is constant at 44.1k,
                // 48k, 96k, 192k and any buffer size.
                const float coeff = (float)(1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * 0.005)));
                const bool storeRamp = numSamples <= (int) transportFadeRamp_.size();
                float g = transportFadeGain_;
                for (int s = 0; s < numSamples; ++s)
                {
                    g += (1.0f - g) * coeff;
                    outL[s] *= g;
                    if (outR) outR[s] *= g;
                    if (storeRamp) transportFadeRamp_[(size_t) s] = g;      // C9
                }
                transportFadeRampActive_ = storeRamp;
                transportFadeGain_ = (g > 0.9999f) ? 1.0f : g;
            }
            else
            {
                transportFadeRampActive_ = false;
                if (isPlaying)
                    transportFadeGain_ = 1.0f;
            }
        }

        // Persist transport-edge state for next callback.
        engineWasPlaying_ = isPlaying;
        if (isPlaying)
            engineStopFadePosition_ = position + numSamples; // continuation point if stop comes next

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
            master->setPeakLevels(masterPeakL, masterPeakR);
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
    // Published plugin-chain map (C3). pluginChainsBlockSnap_ is refreshed
    // once per block in processWithSnapshot() and used by all chain lookups.
    std::shared_ptr<const PluginChainSnapshotMap> pluginChainsSnapshot_;
    std::shared_ptr<const PluginChainSnapshotMap> pluginChainsBlockSnap_;
    std::shared_ptr<const OfflineStemRenderMask> offlineStemRenderMask_;
    FolderBusStateModel* folderBusStateModel_ = nullptr;
    DAW::PianoRollPlaybackCore* midiPlayback_ = nullptr;
    DAW::MidiInputCore* midiInput_ = nullptr;
    DAW::VirtualMidiKeyboardCore* virtualKeyboard_ = nullptr;
    DAW::StepSequencerModel* stepSequencerModel_ = nullptr;
    DAW::StepSequencerPlaybackCore stepSequencerPlayback_;
    PluginPlayheadInfoCore* pluginPlayheadInfo_ = nullptr;
    TrackPeakMeterManagerCore* trackPeakMeterManager_ = nullptr;
    ClipRegionPluginCore* clipRegionPlugins_ = nullptr;
    AutomationManagerCore* automationManager_ = nullptr;
    apex::vocaltune::ApexTuneIntegrationCore* vocalTuneIntegration_ = nullptr;
    const juce::AudioBuffer<float>* liveInputBuffer_ = nullptr;
    int liveInputNumSamples_ = 0;
    int liveInputValidChannels_ = 0;
    void* postFaderRecordContext_ = nullptr;
    PostFaderRecordTap postFaderRecordTap_ = nullptr;
    bool postFaderRecordCaptureEnabled_ = false; // set on the device callback thread

    // Hash functor for juce::String keys in unordered_map — O(1) average lookup.
    struct JuceStringHash
    {
        size_t operator()(const juce::String& s) const noexcept { return (size_t) s.hashCode64(); }
    };

    std::unordered_map<TrackID, float, JuceStringHash> liveInputMonitorGainByTrack_;

    // Generation-validated per-clip audio resolution (see resolveClipAudio).
    struct ClipAudioCacheRef
    {
        AudioFileManager::CachedAudioHandle handle;
        const juce::AudioBuffer<float>* buffer = nullptr;
        double sourceRate = 44100.0;
        std::uint64_t generation = ~0ull;
    };
    std::unordered_map<ClipID, ClipAudioCacheRef, JuceStringHash> clipAudioCacheMap_;
    std::vector<AudioFileManager::CachedAudioHandle> retiredClipAudio_;
    static constexpr int kMaxRetiredClipAudio = 256;
    int retireWritePos_ = 0;
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
    std::unordered_map<TrackID, MonitorJumpDiagState, JuceStringHash> monitorJumpDiagByTrack_;

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
    std::unordered_map<ClipID, std::unique_ptr<ArrangementEditor::TimePitchDSPCore>, JuceStringHash> clipDspMap_;
    std::unordered_map<ClipID, SamplePosition, JuceStringHash> lastClipRenderEndOffset_;
    std::unordered_map<ClipID, uint64_t, JuceStringHash>       lastClipRenderBlock_;
    std::unordered_map<ClipID, int, JuceStringHash>            lastClipRenderMode_;
    std::unordered_map<ClipID, bool, JuceStringHash>           lastClipUsingTunedAudio_;
    std::unordered_map<ClipID, double, JuceStringHash>         lastClipSourceSampleRate_;
    // Tracks which RenderPath last used the per-clip dspCore so we can reset
    // it when switching between StretchThenPitch and Normal/StretchOnly.
    // Without this, the dspCore retains Stretch-only WSOLA state when the path
    // switches back to Normal, causing a reset glitch on the first Normal block.
    std::unordered_map<ClipID, int, JuceStringHash>            lastClipRenderPath_;
    uint64_t                         processCounter_ = 0;
    int64_t                          pluginPlayheadDebugSampleCounter_ = 0;
    bool                             masterWasProcessed_ = false;
    int                              offlineRouteDebugBlocksRemaining_ = 0;

    // ── Transport fade state (audio-thread only) ─────────────────────────
    // Click-free play start / stop / seek. transportFadeGain_ is the master
    // output gate: 0 → silence, 1 → unity. engineWasPlaying_ detects the
    // play→stop edge so we can render one final fade-out block from
    // engineStopFadePosition_ (the engine's continuation point).
    float          transportFadeGain_       = 1.0f;
    bool           engineWasPlaying_        = false;
    SamplePosition engineStopFadePosition_  = 0;
    // C9: exact per-sample fade curve applied this block (unity capacity
    // worstCaseBlock); lets the click/metronome join the same fade.
    std::vector<float> transportFadeRamp_;
    bool           transportFadeRampActive_ = false;

    // Scratch buffer for renderClipSegment output before gain/fade mix
    std::vector<float> scratchBuf_;

    // Per-block pitch ramp buffer — preallocated in prepare(), reused every block.
    // Filled by PitchSmootherCore::getNextSemitones() for every sample in the block.
    // Eliminates the old "get one sample, discard the rest" zipper pattern.
    std::vector<float> pitchRamp_;

    // Per-block Resample-mode read-rate trajectory — pow(2, st/12) * scale for
    // every sample of the block, computed once per block and reused by all
    // channels. Consumed by the inline Resample path so pitch automation is
    // applied per sample instead of once per block.
    std::vector<double> resampleRateRamp_;

    // Per-block clip gain ramp buffer — prevents zipper when clip gain automation
    // or the gain slider changes. Filled once per clip before the per-sample loop.
    std::vector<float> clipGainRamp_;
    std::unordered_map<ClipID, float, JuceStringHash> lastClipGain_;

    // Per-block Tape Stop trajectory, evaluated from the immutable clip
    // automation snapshot.  It is prepared at the device boundary so the
    // clip render path never allocates for automation or curve evaluation.
    std::vector<float> tapeStopRamp_;

    // Independent pitch cores — one per clip (preserves grain state per clip)
    std::unordered_map<ClipID, std::unique_ptr<ArrangementEditor::ClipIndependentPitchCore>, JuceStringHash> clipPitchCoreMap_;
    std::unordered_map<ClipID, std::unique_ptr<ArrangementEditor::PitchSmootherCore>, JuceStringHash> clipPitchSmootherMap_;

    struct ClipPanRampState
    {
        float smoothedPan = 0.0f;
        bool initialised = false;
    };
    std::unordered_map<ClipID, ClipPanRampState, JuceStringHash> clipPanRampMap_;
    std::vector<float> clipPanLeftRamp_;
    std::vector<float> clipPanRightRamp_;

    // Per-track volume/pan smoother — eliminates zipper from block-rate gain
    std::unordered_map<TrackID, std::unique_ptr<VolumeRampCore>, JuceStringHash> trackVolumeRampMap_;
    std::unordered_map<TrackID, std::unique_ptr<MuteFadeCore>, JuceStringHash> muteFadeMap_;
    std::unordered_map<ClipID, std::unique_ptr<APEX::TapeStop::ProcessorCore>, JuceStringHash> clipTapeStopProcessorMap_;
    // Stable parameter IDs are built on the control thread during prewarm so
    // the clip render path does not construct a juce::String per callback.
    std::unordered_map<ClipID, juce::String, JuceStringHash> clipTapeStopParameterIdMap_;

    struct ClipTapeStopState
    {
        SamplePosition lastSampleEnd = 0;
        int64_t regionStart = 0;
        int64_t regionEnd = 0;
        int direction = (int) APEX::TapeStop::TransitionDirection::Ending;
        bool active = false;
    };
    std::unordered_map<ClipID, ClipTapeStopState, JuceStringHash> clipTapeStopStateMap_;

    // Persistent fractional source position for the inline Resample path.
    // A block-constant read rate re-anchored from engineClipOffset jumps the
    // source position by (engineClipOffset + count) * deltaRate whenever the
    // pitch changes between blocks — audible zipper/glitch under pitch
    // automation or knob drags. Accumulating the per-sample rate keeps the
    // source position continuous. The state is only reused while the clip's
    // engine offset advances contiguously; seeks/loops re-anchor.
    struct ResampleReadState
    {
        double delta = 0.0;                    // source samples consumed since source start
        SamplePosition nextEngineOffset = -1;  // expected engineClipOffset for continuity
        bool valid = false;
    };
    std::unordered_map<ClipID, ResampleReadState, JuceStringHash> resampleReadStateMap_;

    juce::AudioBuffer<float> pitchInputBuffer_;
    juce::AudioBuffer<float> pitchOutputBuffer_;
    juce::AudioBuffer<float> clipRegionPluginBuffer_;

    uint64_t lastRoutingSnapshotVersion_ = 0;

    // Engine-owned gain ramp state for every live routing edge, keyed by RouteID.
    // The audio thread reads EdgeSnapshot.gain as target and drives these ramps —
    // no RoutingConnection* access on the audio thread.
    std::unordered_map<RouteID, ConnectionGainRampCore, JuceStringHash> connectionGainRamps_;

    // Pre-FX tap: snapshot of the track buffer captured BEFORE the plugin chain runs
    juce::AudioBuffer<float> preFxBuffer_;

    // PDC delay lines for sidechain edges:
    //   key   = destNodeId
    //   value = circular delay buffer [2 ch x maxDelaySamples]
    // Each sidechain destination may need compensation when its source track
    // has lookahead plugins that introduce latency.
    std::unordered_map<juce::String, SoundEngine::ApexPdcDelayLineCore, JuceStringHash> pdcLines_;
    MasterPdcCore masterPdc_;
    std::atomic<bool> masterPdcDirty_ { true };

    // Published per-track plugin-chain latency map (message thread publishes,
    // audio thread acquires). Read via getChainLatenciesRT() only.
    std::shared_ptr<const ChainLatencyMap> chainLatencies_;

    // C6: monitoring-PDC mode (default Bypass per product decision) and the
    // Reduced-mode compensation cap, computed in prepare().
    std::atomic<int> monitoringPdcMode_ { (int) DAW::LiveMonitoringBypassCore::Mode::Bypass };
    int monitoringPdcReducedCap_ = 1024;
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

public:
    /** C10: pre-create the heavy per-clip DSP cores (pitch smoother,
     *  independent pitch core, time-pitch DSP) on the MESSAGE THREAD from
     *  clip lifecycle hooks (create / recreate / project load), so the
     *  audio thread never allocates FFT-heavy DSP on first realtime
     *  playback. Small ramp/fade cores stay lazy by design (bounded
     *  one-node allocations, approved in the audit). No-ops when the
     *  device is not started; prepare() re-prepares every core on device
     *  start (Bug 17 fix path), so early-created cores are recalibrated. */
    void prewarmClipDSP(const ClipID& clipId)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        if (sampleRate_ <= 0.0 || blockSize_ <= 0)
            return;
        (void) getOrCreateClipPitchSmoother(clipId);
        (void) getOrCreateClipPitchCore(clipId);
        (void) getOrCreateClipDSP(clipId);
        (void) getOrCreateClipTapeStopProcessor(clipId);
        clipTapeStopParameterIdMap_.try_emplace(
            clipId, AutomationLaneCore::makeClipTapeStopParameterId(clipId));
        clipTapeStopStateMap_.try_emplace(clipId);
        resampleReadStateMap_.try_emplace(clipId);
    }

private:

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

    APEX::TapeStop::ProcessorCore& getOrCreateClipTapeStopProcessor(const ClipID& clipId)
    {
        auto it = clipTapeStopProcessorMap_.find(clipId);
        if (it != clipTapeStopProcessorMap_.end())
            return *it->second;

        auto core = std::make_unique<APEX::TapeStop::ProcessorCore>();
        core->prepare(sampleRate_, blockSize_, 2);
        auto* corePtr = core.get();
        clipTapeStopProcessorMap_[clipId] = std::move(core);
        return *corePtr;
    }

    void resetClipTapeStopState(const ClipID& clipId)
    {
        auto processorIt = clipTapeStopProcessorMap_.find(clipId);
        if (processorIt != clipTapeStopProcessorMap_.end() && processorIt->second)
            processorIt->second->reset();

        auto stateIt = clipTapeStopStateMap_.find(clipId);
        if (stateIt != clipTapeStopStateMap_.end())
            stateIt->second = {};
    }

    static APEX::TapeStop::TransitionDirection inferClipTapeStopDirection(
        const AutomationSnapshot::LaneSnapshot& lane,
        int64_t clipStart,
        int64_t clipEnd) noexcept
    {
        const float firstValue = juce::jlimit(0.0f, 1.0f, lane.points.front().value);
        const float lastValue = juce::jlimit(0.0f, 1.0f, lane.points.back().value);

        // The existing Tape Stop parameter is stopped amount: 0 = normal,
        // 1 = stopped.  A rising lane therefore brakes into an ending;
        // a falling lane starts stopped and ramps into normal playback.
        if (firstValue > lastValue + 0.0001f)
            return APEX::TapeStop::TransitionDirection::Beginning;
        if (lastValue > firstValue + 0.0001f)
            return APEX::TapeStop::TransitionDirection::Ending;

        // Degenerate/held automation is still deterministic: placement at a
        // clip boundary chooses the state-machine entry direction.
        if (lane.points.front().timeSamples <= clipStart)
            return APEX::TapeStop::TransitionDirection::Beginning;
        if (lane.points.back().timeSamples >= clipEnd)
            return APEX::TapeStop::TransitionDirection::Ending;
        return APEX::TapeStop::TransitionDirection::Ending;
    }

    void applyClipTapeStop(Clip* clip,
                           SamplePosition playPos,
                           int numSamples,
                           const AutomationSnapshot* automationSnapshot,
                           juce::AudioBuffer<float>& isolatedBuffer)
    {
        auto* audioClip = dynamic_cast<AudioClip*>(clip);
        if (audioClip == nullptr)
            return;

        std::shared_ptr<const AutomationSnapshot> ownedSnapshot;
        if (automationSnapshot == nullptr && automationManager_ != nullptr)
        {
            ownedSnapshot = automationManager_->getSnapshotPublisher().get();
            automationSnapshot = ownedSnapshot.get();
        }

        const auto clipStart = audioClip->getStartPosition();
        const auto clipLength = audioClip->getProcessedTimelineLength();
        const auto readPlan = SoundEngine::ApexSourceReadContractCore::makeReadPlan(
            playPos, numSamples, clipStart, clipLength);

        if (!readPlan.intersects || readPlan.count <= 0)
        {
            resetClipTapeStopState(audioClip->getID());
            return;
        }

        if (automationSnapshot == nullptr)
        {
            resetClipTapeStopState(audioClip->getID());
            return;
        }

        // RT path: findLaneRT performs a bounded immutable scan and never
        // lazily builds the UI snapshot's hash index.
        const auto parameterIdIt = clipTapeStopParameterIdMap_.find(audioClip->getID());
        if (parameterIdIt == clipTapeStopParameterIdMap_.end())
            return;

        const auto* lane = automationSnapshot->findLaneRT(
            audioClip->getTrackID(), parameterIdIt->second);

        // Backward compatibility: projects saved before the clip-local tape
        // stop lane existed carry a TRACK-level "track.tape_stop" lane. If the
        // clip lane is absent, fall back to the legacy lane — the region logic
        // below already clamps it to this clip's range, so old projects keep
        // working with no data migration at all.
        if (lane == nullptr || !lane->enabled || lane->points.size() < 2)
        {
            if (auto* legacyLane = automationSnapshot->findLaneRT(
                    audioClip->getTrackID(), AutomationLaneCore::trackTapeStopParameterId))
            {
                if (legacyLane->enabled && legacyLane->points.size() >= 2)
                    lane = legacyLane;
            }
        }

        if (lane == nullptr || !lane->enabled || lane->points.size() < 2)
        {
            resetClipTapeStopState(audioClip->getID());
            return;
        }

        auto processorIt = clipTapeStopProcessorMap_.find(audioClip->getID());
        auto stateIt = clipTapeStopStateMap_.find(audioClip->getID());
        if (processorIt == clipTapeStopProcessorMap_.end()
            || processorIt->second == nullptr
            || stateIt == clipTapeStopStateMap_.end())
        {
            // A missing prewarmed core is a safe silent/no-op failure.  Never
            // allocate a Tape Stop processor from the realtime render path.
            return;
        }

        const int64_t clipEnd = clipStart + clipLength;
        const int64_t regionStart = lane->points.front().timeSamples;
        const int64_t regionEnd = lane->points.back().timeSamples;
        if (regionEnd <= regionStart)
        {
            resetClipTapeStopState(audioClip->getID());
            return;
        }

        // The lane point interval is the actual Tape Stop duration.  Clip
        // boundaries clamp only the usable portion; the clip's stored length
        // is never rewritten.
        const int64_t usableStart = juce::jmax<int64_t>(clipStart, regionStart);
        const int64_t usableEnd = juce::jmin<int64_t>(clipEnd, regionEnd);
        const int64_t renderStart = (int64_t) playPos + readPlan.bufferStart;
        const int64_t renderEnd = renderStart + readPlan.count;
        const int64_t activeStart = juce::jmax<int64_t>(renderStart, usableStart);
        const int64_t activeEnd = juce::jmin<int64_t>(renderEnd, usableEnd);
        if (activeEnd <= activeStart)
        {
            // Past the end of the region: the tape stop effect is over and the
            // clip resumes normal playback for the remainder of the clip.
            resetClipTapeStopState(audioClip->getID());
            return;
        }

        const auto direction = inferClipTapeStopDirection(*lane, clipStart, clipEnd);
        const int directionInt = (int) direction;
        auto& state = stateIt->second;
        const bool continuous = state.active
            && state.regionStart == regionStart
            && state.regionEnd == regionEnd
            && state.direction == directionInt
            && state.lastSampleEnd == activeStart;

        const int activeOffset = (int) (activeStart - renderStart);
        const int activeCount = (int) (activeEnd - activeStart);
        if (activeOffset < 0 || activeCount <= 0
            || activeOffset + activeCount > readPlan.count
            || activeCount > (int) tapeStopRamp_.size())
        {
            resetClipTapeStopState(audioClip->getID());
            return;
        }

        if (! continuous)
        {
            const float initialValue = lane->getValueAtSample(activeStart, 0.0f);
            processorIt->second->beginTransition(direction, initialValue);
        }

        for (int i = 0; i < activeCount; ++i)
            tapeStopRamp_[(size_t) i] = lane->getValueAtSample(activeStart + i, 0.0f);

        const int channels = isolatedBuffer.getNumChannels();
        if (channels < 1 || isolatedBuffer.getNumSamples() < readPlan.bufferStart + readPlan.count)
            return;

        auto& processor = *processorIt->second;
        auto* left = isolatedBuffer.getWritePointer(0, readPlan.bufferStart + activeOffset);
        auto* right = isolatedBuffer.getWritePointer(
            juce::jmin(1, channels - 1), readPlan.bufferStart + activeOffset);
        processor.processWithValues(left, right, activeCount, 0.0f, tapeStopRamp_.data());

        state.regionStart = regionStart;
        state.regionEnd = regionEnd;
        state.direction = directionInt;
        state.lastSampleEnd = activeEnd;
        state.active = activeEnd < usableEnd;
        if (! state.active)
        {
            // The curve ended: drop the tape-stop read head so the clip
            // resumes normal playback (no effect) for the rest of the clip.
            processor.reset();
        }
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
        // Use find + emplace instead of operator[] to avoid implicit insertion + heap allocation.
        auto it = connectionGainRamps_.find(edgeId);
        if (it == connectionGainRamps_.end())
        {
            // Defensive first-use path: the map was reserved in prepare() so this
            // emplace cannot rehash; the ramp is sized to the worst-case block so
            // no later callback can trigger a ramp re-allocation.
            auto [ins, _] = connectionGainRamps_.emplace(edgeId, ConnectionGainRampCore());
            it = ins;
            it->second.prepare(sampleRate_, juce::jmax(juce::jmax(blockSize_, 8192), numSamples));
        }
        else if (it->second.getCapacity() < numSamples)
        {
            it->second.prepare(sampleRate_, juce::jmax(juce::jmax(blockSize_, 8192), numSamples));
        }
        return it->second.generate(targetGain, numSamples);
    }

    juce::AudioBuffer<float>* findNodeBuffer(const juce::String& nodeId) noexcept
    {
        return routingBuffers_.findNodeBuffer(nodeId);
    }

    /** Per-clip audio resolution validated by AudioFileManager's change
     *  generation. Steady-state blocks do one map find + one atomic load —
     *  no ReadWriteLock (previously taken twice per clip per block).
     *  Re-resolution happens only on the first block after a cache mutation
     *  (load, normalize, share, unload, clear). Replaced handles are moved
     *  into a bounded retire ring reclaimed in prepare() (message thread),
     *  so large CachedAudio destruction never happens on the audio thread. */
    const ClipAudioCacheRef& resolveClipAudio(const ClipID& clipId)
    {
        auto& ref = clipAudioCacheMap_[clipId];   // map reserved in prepare(); first-use node insert only
        const std::uint64_t gen = audioFiles_ != nullptr ? audioFiles_->getChangeGeneration() : 0;
        if (ref.generation != gen)
        {
            if (ref.handle != nullptr)
            {
                if ((int) retiredClipAudio_.size() < kMaxRetiredClipAudio)
                    retiredClipAudio_.push_back(std::move(ref.handle));
                else
                    retiredClipAudio_[(size_t)(retireWritePos_++ % kMaxRetiredClipAudio)] = std::move(ref.handle);
            }
            ref.handle     = audioFiles_ != nullptr ? audioFiles_->getCachedAudioSnapshot(clipId) : nullptr;
            ref.buffer     = ref.handle ? &ref.handle->getBufferRef() : nullptr;
            ref.sourceRate = ref.handle ? ref.handle->sampleRate : 44100.0;
            ref.generation = gen;
        }
        return ref;
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
        pdcLines_.reserve(snapshot.edges.size() + 4);

        auto latencies = getChainLatenciesRT();
        rebuildPdcLines(snapshot, latencies.get(), numSamples);
    }

    void resetAllClipDSPState()
    {
        for (auto& [id, dsp] : clipDspMap_)
            if (dsp)
                dsp->reset();

        for (auto& [id, core] : clipPitchCoreMap_)
            if (core)
                core->reset();

        // C8: reset pitch smoothers to each clip's CURRENT pitch target
        // (AudioClip::getPitch is an audio-thread-safe atomic read), not
        // 0 st — pitched clips no longer swoop from zero semitones after
        // play/seek/loop-wrap. The clip list needs the clip lock; if the
        // message thread holds it this block, keep the legacy 0 st reset.
        bool pitchSmoothersReset = false;
        if (clips_ != nullptr)
        {
            juce::ScopedTryLock clipTryLock(clips_->getLock());
            if (clipTryLock.isLocked())
            {
                pitchSmoothersReset = true;
                for (auto& [id, smoother] : clipPitchSmootherMap_)
                {
                    if (!smoother) continue;
                    float targetSemitones = 0.0f;
                    if (auto* clip = clips_->getClip(id))
                        if (auto* ac = dynamic_cast<AudioClip*>(clip))
                            targetSemitones = ac->getPitch();
                    smoother->reset(targetSemitones);
                }
            }
        }
        if (!pitchSmoothersReset)
        {
            for (auto& [id, smoother] : clipPitchSmootherMap_)
                if (smoother)
                    smoother->reset(0.0f);
        }

        // ZIPPER FIX (Bug: transport-start snap):
        // Do NOT reset clipPanRampMap_, trackVolumeRampMap_, muteFadeMap_ or
        // any gain smoother here. Resetting them snapped smoothedValue to the
        // target instantly, so the first block after play/stop/seek jumped the
        // gain with zero ramp — audible zipper/click on every transport change.
        // Gain smoothers are continuous-state objects: they must carry their
        // smoothed value ACROSS transport discontinuities and glide to the new
        // target, exactly like REAPER/Cubase fader engines do. Only clip DSP
        // (pitch/stretch/tape) state is positional and needs a reset.

        for (auto& [id, core] : clipTapeStopProcessorMap_)
            if (core)
                core->reset();

        for (auto& [id, state] : clipTapeStopStateMap_)
            state = {};

        lastClipRenderEndOffset_.clear();
        lastClipRenderBlock_.clear();
        lastClipRenderMode_.clear();
        lastClipRenderPath_.clear();
        lastClipUsingTunedAudio_.clear();
        lastClipSourceSampleRate_.clear();
        resampleReadStateMap_.clear();
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
        // Buffers are pre-allocated to worstCaseBlock in prepare(). Never resize in audio callback.
        jassert((int)clipPanLeftRamp_.size() >= numSamples);

        // Use find + emplace instead of operator[] to avoid implicit insertion + heap allocation.
        auto it = clipPanRampMap_.find(clipId);
        if (it == clipPanRampMap_.end())
        {
            auto [ins, _] = clipPanRampMap_.emplace(clipId, ClipPanRampState{});
            it = ins;
        }
        auto& state = it->second;
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

    /** (Re)build sidechain PDC delay lines from the ADOPTED routing snapshot
     *  and the published chain-latency map — never from the live RoutingGraph
     *  or the pluginChains_ map. Called from prepare() (message thread) and
     *  from syncNodeBuffers() on graph-change blocks (audio thread); race-free
     *  in both contexts. Delay lines are updated in place: a line's buffer is
     *  only reallocated when a larger capacity is required, so existing
     *  history is preserved (no mass clear = no click storm on change).
     *  Capacity uses the engine worst-case block so a small block at
     *  creation time can never under-size a line for later larger blocks
     *  (previously a latent out-of-bounds read via negative ring offset). */
    void rebuildPdcLines(const RoutingSnapshot& snapshot,
                         const ChainLatencyMap* latencies,
                         int numSamples)
    {
        const int safeBlock = juce::jmax(juce::jmax(blockSize_, 8192), numSamples);

        auto latencyOfNode = [&](const juce::String& nodeId) noexcept -> int
        {
            if (latencies == nullptr)
                return 0;
            const auto it = snapshot.nodeIndexById.find(nodeId);
            if (it == snapshot.nodeIndexById.end() || it->second >= snapshot.nodes.size())
                return 0;
            const auto latIt = latencies->find(snapshot.nodes[it->second].trackId);
            return latIt != latencies->end() ? juce::jmax(0, latIt->second) : 0;
        };

        for (const auto& edge : snapshot.edges)
        {
            if (edge.type != ConnectionType::Sidechain)
                continue;

            const int delay = SoundEngine::ApexPluginPdcContractCore::computeSidechainDelaySamples(
                latencyOfNode(edge.sourceNodeId), latencyOfNode(edge.destNodeId));

            auto lineIt = pdcLines_.find(edge.destNodeId);
            if (lineIt == pdcLines_.end())
                lineIt = pdcLines_.emplace(edge.destNodeId, SoundEngine::ApexPdcDelayLineCore{}).first;
            auto& line = lineIt->second;
            const int capacity = SoundEngine::ApexPluginPdcContractCore::computeDelayLineCapacity(delay, safeBlock);
            if (line.getCapacity() < capacity)
                line.prepare(capacity);
            line.setCrossfadeSamples((int) (0.005 * sampleRate_));   // C7: crossfaded delay changes
            line.setDelaySamples(delay);
        }
    }

    void logExportRouteIfNeeded(bool shouldLog, const RoutingSnapshot::EdgeSnapshot& edge) const
    {
#if APEX_AUDIO_DEBUG_LOGS
        if (! shouldLog)
            return;

        juce::Logger::writeToLog("[EXPORT ROUTE] src=" + edge.sourceNodeId
            + " dst=" + edge.destNodeId
            + " type=" + juce::String((int) edge.type)
            + " gain=" + juce::String(edge.gain)
            + " active=" + juce::String(edge.active ? 1 : 0)
            + " bypassed=" + juce::String(edge.bypassed ? 1 : 0));
#else
        (void) shouldLog;
        (void) edge;
#endif
    }

    bool isStemProgrammeEdgeAllowed(const RoutingSnapshot::EdgeSnapshot& edge) const noexcept
    {
        return offlineStemRenderMask_ == nullptr
            || offlineStemRenderMask_->programmeEdges.count(edge.id) > 0;
    }

    bool isStemSidechainEdgeAllowed(const RoutingSnapshot::EdgeSnapshot& edge) const noexcept
    {
        return offlineStemRenderMask_ == nullptr
            || offlineStemRenderMask_->sidechainEdges.count(edge.id) > 0;
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

    bool usesLiveInputOnlyForTrimMeter(const Track& track, int numSamples) const noexcept
    {
        if (!track.isArmed() || liveInputBuffer_ == nullptr
            || liveInputNumSamples_ != numSamples || transport_ == nullptr
            || shouldMonitorLiveInputForTrack(track))
            return false;

        // An armed track with monitoring disabled does not receive hardware
        // input in trackBuffer_. LiveInputMonitorEngine meters that input on a
        // scratch copy instead, so avoid overwriting its VU with the unrelated
        // playback buffer later in this callback.
        return RecordingInputValidityCore::isRouteAvailable(
            track.getInputFirstChannel(), track.isInputMono(), liveInputValidChannels_);
    }

    bool addLiveInputToTrackBuffer(Track& track, int numSamples) noexcept
    {
        const bool monitorTargetOn = shouldMonitorLiveInputForTrack(track);
        // Use find + emplace instead of operator[] to avoid implicit insertion.
        auto it = liveInputMonitorGainByTrack_.find(track.getID());
        if (it == liveInputMonitorGainByTrack_.end())
        {
            auto [ins, _] = liveInputMonitorGainByTrack_.emplace(track.getID(), 0.0f);
            it = ins;
        }
        float& smoothedGain = it->second;
        const float targetGain = monitorTargetOn ? 1.0f : 0.0f;

        if (monitorTargetOn)
        {
            diagMonitorBlocks_.fetch_add(1, std::memory_order_relaxed);
            diagLastMonitorSmoothedGain_.store(smoothedGain, std::memory_order_relaxed);
            diagLastMonitorTrackHash_.store((int64_t) track.getID().hashCode64(), std::memory_order_relaxed);
        }

        const bool haveValidLiveInput = liveInputBuffer_ != nullptr
            && liveInputNumSamples_ == numSamples
            && RecordingInputValidityCore::isRouteAvailable(
                track.getInputFirstChannel(), track.isInputMono(), liveInputValidChannels_)
            && numSamples > 0;

        if (!haveValidLiveInput)
        {
            if (monitorTargetOn)
            {
                if (liveInputBuffer_ == nullptr || liveInputValidChannels_ <= 0 || numSamples <= 0)
                    diagMonitorZeroInputRejects_.fetch_add(1, std::memory_order_relaxed);
                else if (liveInputNumSamples_ != numSamples)
                    diagMonitorShortRejects_.fetch_add(1, std::memory_order_relaxed);
            }
            smoothedGain = targetGain;
            return false;
        }

        // Per-track input selection is valid only when every required channel
        // is present in this callback, never merely allocated in the buffer.
        const int wantFirst = track.getInputFirstChannel();
        const bool mono     = track.isInputMono();
        const int srcL = wantFirst;
        const int srcR = mono ? wantFirst : wantFirst + 1;
        const float* inL = liveInputBuffer_->getReadPointer(srcL);
        const float* inR = liveInputBuffer_->getReadPointer(srcR);

        auto& tb = activeTrackBuffer();
        auto* dstL = tb.getWritePointer(0);
        auto* dstR = tb.getWritePointer(juce::jmin(1, tb.getNumChannels() - 1));

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
        auto& tb = activeTrackBuffer();
        if (!liveInputWasAdded || numSamples <= 0 || tb.getNumChannels() <= 0)
            return;

        auto& state = monitorJumpDiagByTrack_[track.getID()];
        const float* in = tb.getReadPointer(0);
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

    // ── Snapshot-based routing processing (audio thread — zero locks) ────
    void processWithSnapshot(const RoutingSnapshot* snap,
                             const juce::AudioSourceChannelInfo& output,
                             int numSamples, bool isPlaying,
                             SamplePosition position, bool anySolo,
                             const FolderBusSnapshot* fbSnap,
                             const AutomationSnapshot* automationSnap)
    {
        // Refresh the per-block plugin-chain view from the published
        // snapshot (C3) — one atomic load per block, then lock-free finds.
        pluginChainsBlockSnap_ = getPluginChainsSnapshotRT();

        // The adopted immutable snapshot is the only synchronization authority.
        // Reading live graphVersion here created a torn old-snapshot/new-version
        // race that could leave buffers/PDC prepared for the wrong generation.
        if (snap->version != lastRoutingSnapshotVersion_)
        {
            syncNodeBuffers(*snap, numSamples);
            lastRoutingSnapshotVersion_ = snap->version;
            masterPdcDirty_.store(true, std::memory_order_relaxed);
        }

        if (masterPdcDirty_.exchange(false, std::memory_order_acquire))
        {
            // PDC recompute consumes ONLY published state: the adopted routing
            // snapshot plus the atomically published chain-latency map. The
            // audio thread never iterates the live RoutingGraph or the
            // pluginChains_ map here (previously a data race), and
            // MasterPdcCore::sync updates delay lines in place (no mass
            // free/clear of delay history).
            auto latencies = getChainLatenciesRT();
            masterPdc_.sync(*snap, latencies.get(), numSamples);
        }

        // Ensure per-node buffers are cleared by the routing buffer nucleus.
        routingBuffers_.clearSnapshotAudio(*snap, numSamples);

        // Process nodes in dependency order — independent track nodes
        // run in parallel across worker threads (REAPER Anticipative FX).
        const bool logRoutesThisBlock = offlineRouteDebugBlocksRemaining_ > 0;
        if (logRoutesThisBlock)
            --offlineRouteDebugBlocksRemaining_;

        // Sequential processing — parallel dispatch disabled for now
        // (parallel tracks race on shared node buffers).
        for (const auto& nodeId : snap->processingOrder)
        {
            const RoutingSnapshot::NodeSnapshot* nodeMeta = nullptr;
            const auto indexed = snap->nodeIndexById.find(nodeId);
            if (indexed != snap->nodeIndexById.end() && indexed->second < snap->nodes.size())
                nodeMeta = &snap->nodes[indexed->second];
            else
                for (const auto& node : snap->nodes) // compatibility for hand-built test snapshots
                    if (node.id == nodeId) { nodeMeta = &node; break; }
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

    /** Process a Track node: render clips, apply plugins/vol/pan, route to outputs.
     *  Uses the internal trackBuffer_. For parallel processing (Anticipative FX),
     *  call processTrackNodeExt() with an external buffer. */
    void processTrackNode(const RoutingSnapshot::NodeSnapshot* nodeMeta,
                          const RoutingSnapshot* snap,
                          int numSamples,
                          bool isPlaying, SamplePosition position, bool anySolo,
                          const FolderBusSnapshot* fbSnap,
                          const AutomationSnapshot* automationSnap,
                          bool logRoutesThisBlock)
    {
        // Thread-local preFx buffer for parallel safety
        thread_local juce::AudioBuffer<float> localPreFx;
        processTrackNodeExt(nodeMeta, snap, numSamples, isPlaying, position,
                            anySolo, fbSnap, automationSnap, logRoutesThisBlock,
                            trackBuffer_, localPreFx);
    }

    /** Thread-local track buffer redirect for parallel processing.
     *  Each worker thread has its own instance — zero contention. */
    static juce::AudioBuffer<float>*& getThreadLocalTrackBuf() noexcept
    {
        static thread_local juce::AudioBuffer<float>* tlBuf = nullptr;
        return tlBuf;
    }

    /** Redirect trackBuffer_ to an external buffer for the current thread.
     *  Set to nullptr (default) to use the internal trackBuffer_. */
    void setParallelTrackBuffer(juce::AudioBuffer<float>* buf) noexcept
    {
        getThreadLocalTrackBuf() = buf;
    }

    /** Returns the active track buffer for the current thread. */
    juce::AudioBuffer<float>& activeTrackBuffer() noexcept
    {
        auto* tlBuf = getThreadLocalTrackBuf();
        return tlBuf ? *tlBuf : trackBuffer_;
    }

    /** External-buffer version of processTrackNode — used by ParallelTrackEngine
     *  so each worker thread writes to its own buffer (zero contention). */
    void processTrackNodeExt(const RoutingSnapshot::NodeSnapshot* nodeMeta,
                             const RoutingSnapshot* snap,
                             int numSamples,
                             bool isPlaying, SamplePosition position, bool anySolo,
                             const FolderBusSnapshot* fbSnap,
                             const AutomationSnapshot* automationSnap,
                             bool logRoutesThisBlock,
                             juce::AudioBuffer<float>& trackBuf,
                             juce::AudioBuffer<float>& preFxBuf)
    {
        auto* track = tracks_->getTrack(nodeMeta->trackId);
        if (!track) return;

        auto* nodeBufPtr = findNodeBuffer(nodeMeta->id);
        if (nodeBufPtr == nullptr) return;

        trackBuf.clear();
        bool liveInputWasAdded = false;
        if (clips_)
        {
            juce::ScopedTryLock cl(clips_->getLock());
            if (cl.isLocked())
            {
                // thread_local: parallel track processing must not share one
                // scratch array across worker threads (data race → corrupted
                // clip lists → dropouts/crackles).
                static thread_local juce::Array<Clip*> clipsOnTrackScratch;
                clips_->getClipsOnTrack(track->getID(), clipsOnTrackScratch);
                for (auto* clip : clipsOnTrackScratch)
                {
                    if (clip->isMuted()) continue;
                    if (isPlaying)
                        renderClip(clip, position, numSamples, automationSnap);
                }
            }
            else
            {
                DBG("[AudioEngine] Skipped clip render for track " + track->getID()
                    + ": clip manager locked by message thread");
            }
        }

        liveInputWasAdded = addLiveInputToTrackBuffer(*track, numSamples);

        {
            auto* incomingBufPtr = findNodeBuffer(nodeMeta->id);
            if (incomingBufPtr == nullptr) return;
            auto& incomingBuf = *incomingBufPtr;
            auto* srcL = incomingBuf.getReadPointer(0);
            auto* srcR = incomingBuf.getReadPointer(juce::jmin(1, incomingBuf.getNumChannels() - 1));
            auto* dstL = trackBuf.getWritePointer(0);
            auto* dstR = trackBuf.getWritePointer(juce::jmin(1, trackBuf.getNumChannels() - 1));
            SoundEngine::ApexMixFanoutCore::addStereo(srcL, srcR, dstL, dstR, numSamples);
        }

        {
            auto* tbL = trackBuf.getWritePointer(0);
            auto* tbR = trackBuf.getWritePointer(juce::jmin(1, trackBuf.getNumChannels() - 1));
            const bool publishTrackInputMeter = !usesLiveInputOnlyForTrimMeter(*track, numSamples);
            TrackInputProcessorCore::processTrack(*track, tbL, tbR, numSamples,
                                                  publishTrackInputMeter);
        }

        // Capture pre-FX snapshot before the plugin chain runs (for PreFX tap)
        // thread_local buffer: grows once on first encounter, then stays grown.
        // Should never need to grow after initial warm-up.
        if (preFxBuf.getNumSamples() < numSamples || preFxBuf.getNumChannels() < 2)
            preFxBuf.setSize(2, juce::jmax(numSamples, blockSize_), false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            preFxBuf.copyFrom(ch, 0, trackBuf, ch, 0, numSamples);

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

        // PatternClip MIDI playback — Step Sequencer patterns placed in arrangement
        if (stepSequencerModel_ && clips_ && isPlaying
            && (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument))
        {
            const double tempo = transport_ ? transport_->getTempo() : 120.0;
            if (tempo > 0.0)
            {
                juce::ScopedTryLock cl(clips_->getLock());
                if (cl.isLocked())
                {
                    static thread_local juce::Array<Clip*> patternClipScratch;
                    clips_->getClipsOnTrack(track->getID(), patternClipScratch);
                    for (auto* clip : patternClipScratch)
                    {
                        if (clip->isMuted()) continue;
                        if (clip->getType() != ClipType::Pattern) continue;

                        auto* patternClip = static_cast<DAW::PatternClip*>(clip);

                        // Find the pattern in the StepSequencerModel
                        const auto& snap = stepSequencerModel_->getSnapshotRT();
                        if (!snap) continue;

                        // For now, use the first matching pattern (single-pattern mode)
                        // In multi-pattern mode, we'd match patternClip->getPatternId()
                        // against snap->patternId.
                        // The pattern snapshot is used directly by processBlock.
                        const auto clipStart = clip->getStartPosition();
                        const auto clipLen   = clip->getLength();
                        if (clipLen <= 0) continue;

                        // Compute offset within the pattern for the current play position
                        const auto relativePos = position - clipStart;
                        if (relativePos < 0 || relativePos >= clipLen) continue;

                        // Apply instance transpose. The immutable snapshot is only
                        // deep-copied when a transpose is actually set — the common
                        // case (transpose == 0) processes the published snapshot
                        // directly with zero allocation on the audio thread.
                        const int instanceTranspose = (int) patternClip->getTranspose();

                        juce::MidiBuffer patternMidi;
                        if (instanceTranspose == 0)
                        {
                            stepSequencerPlayback_.processBlock(
                                patternMidi, numSamples, (int64_t) position, tempo, *snap);
                        }
                        else
                        {
                            DAW::StepSequencerModel::Snapshot overriddenSnap = *snap;
                            for (auto& ls : overriddenSnap.lanes)
                                ls.midiNote = juce::jlimit(0, 127, ls.midiNote + instanceTranspose);

                            stepSequencerPlayback_.processBlock(
                                patternMidi, numSamples, (int64_t) position, tempo, overriddenSnap);
                        }

                        // Apply instance gain by scaling velocities
                        const float instGain = patternClip->getGain();
                        if (instGain < 0.999f || instGain > 1.001f)
                        {
                            for (const auto metadata : patternMidi)
                            {
                                auto msg = metadata.getMessage();
                                if (msg.isNoteOn() && msg.getVelocity() > 0)
                                {
                                    const uint8_t newVel = (uint8_t) juce::jlimit(
                                        0, 127, (int)(msg.getVelocity() * instGain));
                                    patternMidi.addEvent(
                                        juce::MidiMessage::noteOn(
                                            msg.getChannel(), msg.getNoteNumber(), newVel),
                                        metadata.samplePosition);
                                }
                            }
                        }

                        // Merge into track MIDI
                        trackMidi.addEvents(patternMidi, 0, numSamples, 0);
                    }
                }
            }
        }

        // Plugin chain — check for sidechain input using snapshot edges (no RoutingGraph access)
        if (pluginChainsBlockSnap_)
        {
            auto it = pluginChainsBlockSnap_->find(track->getID());
            if (it != pluginChainsBlockSnap_->end() && it->second)
            {
                auto& chain = *it->second;
                const bool hasMidiInput = (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument) && !trackMidi.isEmpty();
                bool hasSidechainInput = false;
                for (const auto& e : snap->edges)
                {
                    if (e.destNodeId == nodeMeta->id
                        && e.type == ConnectionType::Sidechain
                        && e.active && !e.bypassed
                        && isStemSidechainEdgeAllowed(e))
                    { hasSidechainInput = true; break; }
                }

#if APEX_AUDIO_DEBUG_LOGS
                emitMonitorJumpDiagIfNeeded(*track, liveInputWasAdded, numSamples);
#endif

                auto* sc = hasSidechainInput ? findSidechainBuffer(nodeMeta->id) : nullptr;
                chain.processBlockWithAutomation(
                    trackBuf,
                    hasMidiInput ? &trackMidi : nullptr,
                    hasMidiInput ? nullptr : sc,
                    track->getID(), automationSnap, (int64_t)position,
                    sampleRate_, transport_ ? transport_->getTempo() : 120.0,
                    numSamples);

#if APEX_AUDIO_DEBUG_LOGS
                emitMonitorFxDiagIfNeeded(*track, liveInputWasAdded, &chain);
#endif
            }
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

        volumeRamp.setTargetVolume(trackVolume);
        volumeRamp.setTargetPan(trackPan);

#if APEX_AUDIO_DEBUG_LOGS
        {
            static int buzzRampBlocks = 0;
            if (buzzRampBlocks < 20)
            {
                ++buzzRampBlocks;
                juce::Logger::writeToLog(
                    juce::String("[BUZZ-HB-PERBLOCK] track=") + track->getID()
                    + " block=" + juce::String(buzzRampBlocks)
                    + " targetVol=" + juce::String(trackVolume, 6)
                    + " targetPan=" + juce::String(trackPan, 6)
                    + " currentLeftGain=" + juce::String(volumeRamp.getCurrentLeftGain(), 6)
                    + " currentRightGain=" + juce::String(volumeRamp.getCurrentRightGain(), 6));
            }
        }
#endif

        const float leftGain  = volumeRamp.getCurrentLeftGain();
        const float rightGain = volumeRamp.getCurrentRightGain();

        float leftPeak  = trackBuf.getMagnitude(0, 0, numSamples) * leftGain;
        float rightPeak = trackBuf.getMagnitude(
            juce::jmin(1, trackBuf.getNumChannels() - 1), 0, numSamples) * rightGain;
        track->setPeakLevels(leftPeak, rightPeak);

        // ── TrackLens: post-plugin, post-fader metering ───────────────────
        if (trackPeakMeterManager_ != nullptr)
        {
            const float* mL = trackBuf.getReadPointer(0);
            const float* mR = trackBuf.getReadPointer(
                juce::jmin(1, trackBuf.getNumChannels() - 1));
            // Apply fader gain inline — reuse pdcScratchL_/R_, no allocation
            const int ns = numSamples;
            jassert((int)pdcScratchL_.size() >= ns);
            jassert((int)pdcScratchR_.size() >= ns);
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
            if (offlineStemRenderMask_ == nullptr && !fbSnap->effectiveSoloSet.empty())
                audible = audible && fbSnap->effectiveSoloSet.count(track->getID()) > 0;
        }
        else
        {
            audible = !track->isMuted();
            if (offlineStemRenderMask_ == nullptr && anySolo) audible = track->isSoloed();
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
            if (!isStemProgrammeEdgeAllowed(edge)) continue;

            auto* destBufPtr = findNodeBuffer(edge.destNodeId);
            if (destBufPtr == nullptr) continue;

            const float* sendGain = getConnectionGainRamp(edge.id, edge.gain, numSamples);
            auto& destBuf = *destBufPtr;
            auto* srcL = trackBuf.getReadPointer(0);
            auto* srcR = trackBuf.getReadPointer(juce::jmin(1, trackBuf.getNumChannels() - 1));
            auto* dstL = destBuf.getWritePointer(0);
            auto* dstR = destBuf.getWritePointer(juce::jmin(1, destBuf.getNumChannels() - 1));
            SoundEngine::ApexMixFanoutCore::addStereoWithGainAndMute(srcL, srcR, sendGain, muteRamp, dstL, dstR, numSamples);
        }

        // ── Apply gain to trackBuffer for post-fader routing ─────────────
        auto* tbL = trackBuf.getWritePointer(0);
        auto* tbR = trackBuf.getWritePointer(juce::jmin(1, trackBuf.getNumChannels() - 1));
        volumeRamp.applyToStereoBuffer(tbL, tbR, numSamples);
        SoundEngine::ApexMixFanoutCore::applyStereoMuteRamp(tbL, tbR, muteRamp, numSamples);

        // Record mode taps the already-processed channel output. This is a
        // read-only copy point: no second plugin pass and no audible summing.
        if (postFaderRecordCaptureEnabled_ && liveInputWasAdded && track->isArmed()
            && postFaderRecordTap_ != nullptr)
            postFaderRecordTap_(postFaderRecordContext_, track->getID(), trackBuf,
                                numSamples, true);

        // ── Route to outputs (Direct + Post-fader sends) ────────────────
        for (const auto& edge : snap->edges)
        {
            if (edge.sourceNodeId != nodeMeta->id) continue;
            logExportRouteIfNeeded(logRoutesThisBlock, edge);
            if (!edge.active || edge.bypassed) continue;
            if (edge.type == ConnectionType::Sidechain)
                continue;
            if (!isStemProgrammeEdgeAllowed(edge)) continue;

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

                // C6: monitoring-PDC mode for record-armed live-monitored
                // tracks — Bypass skips compensation (performer latency),
                // Reduced caps it, Full keeps previous behaviour.
                int pdcDelayOverride = -1;
                if (liveInputWasAdded && track->isArmed())
                {
                    using MonMode = DAW::LiveMonitoringBypassCore::Mode;
                    const auto monMode = (MonMode) monitoringPdcMode_.load(std::memory_order_acquire);
                    if (monMode == MonMode::Bypass)
                        pdcDelayOverride = 0;
                    else if (monMode == MonMode::Reduced)
                        pdcDelayOverride = juce::jmin(masterPdc_.getEdgeDelay(edge.id), monitoringPdcReducedCap_);
                }

                masterPdc_.processEdge(edge.id, masterPdcScratchL_.data(), masterPdcScratchR_.data(), numSamples, pdcDelayOverride);
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
            if (!isStemSidechainEdgeAllowed(edge)) continue;

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
                tapL = preFxBuf.getReadPointer(0);
                tapR = preFxBuf.getReadPointer(juce::jmin(1, preFxBuf.getNumChannels() - 1));
            }
            else // PostFX
            {
                tapL = trackBuf.getReadPointer(0);
                tapR = trackBuf.getReadPointer(juce::jmin(1, trackBuf.getNumChannels() - 1));
            }

            auto pdcIt = pdcLines_.find(edge.destNodeId);
            if (pdcIt != pdcLines_.end() && pdcIt->second.getDelaySamples() > 0)
            {
                auto& line = pdcIt->second;
                line.push(tapL, tapR, numSamples);
                jassert((int)pdcScratchL_.size() >= numSamples);
                jassert((int)pdcScratchR_.size() >= numSamples);
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

        // Bus input trim + per-bus input meter — mirrors the Track-node tap in
        // processTrackNodeExt (TrackInputProcessorCore::processTrack). Trim is
        // applied pre-FX and the InputMeterCore is fed post-trim/pre-FX, so the
        // trim panel on a folder bus behaves exactly like on a regular track.
        // RT-safe: no allocation, no locks (same cores already run per Track node).
        if (busTrack != nullptr)
        {
            auto* btL = nodeBuf.getWritePointer(0);
            auto* btR = nodeBuf.getWritePointer(juce::jmin(1, nodeBuf.getNumChannels() - 1));
            TrackInputProcessorCore::processTrack(*busTrack, btL, btR, numSamples);
        }

        // Bus plugin chain — apply automation smoothing before processing
        if (busTrack && pluginChainsBlockSnap_)
        {
            auto it = pluginChainsBlockSnap_->find(busTrack->getID());
            if (it != pluginChainsBlockSnap_->end() && it->second)
            {
                // Same sidechain contract as the track path: an active
                // sidechain edge into this node must feed the chain's
                // auxiliary bus instead of a plain processBlock. Bus
                // destinations (e.g. a bus compressor ducked by a vocal bus)
                // previously fell through to processBlock and never saw the
                // sidechain signal.
                bool hasSidechainInput = false;
                for (const auto& e : snap->edges)
                {
                    if (e.destNodeId == nodeMeta->id
                        && e.type == ConnectionType::Sidechain
                        && e.active && !e.bypassed
                        && isStemSidechainEdgeAllowed(e))
                    { hasSidechainInput = true; break; }
                }

                auto* sc = hasSidechainInput ? findSidechainBuffer(nodeMeta->id) : nullptr;
                it->second->processBlockWithAutomation(
                    nodeBuf, nullptr, sc, busTrack->getID(), automationSnap,
                    (int64_t)position, sampleRate_,
                    transport_ ? transport_->getTempo() : 120.0, numSamples);
            }
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
            busTrack->setPeakLevels(peakL, peakR);
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
            if (!isStemProgrammeEdgeAllowed(edge)) continue;

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

        // ── Sidechain sends ──────────────────────────────────────────────
        // Bus nodes can be sidechain SOURCES (e.g. a "Vocal Bus" ducking the
        // compressor on a reverb track). The track-node path fills the
        // destination's sidechain buffer; without the same block here, a
        // sidechain sourced from a BUS stayed SILENT — the destination
        // received an active route and an enabled auxiliary bus, yet its
        // detector saw only silence: the "EXT does not recognise the
        // sidechain" defect for bus sources. Tap point: a bus node has no
        // separate pre-FX capture in this path, so the post-fader bus signal
        // (bufL/bufR) feeds every tap point — the detector still receives the
        // bus's programme material.
        for (const auto& edge : snap->edges)
        {
            if (edge.sourceNodeId != nodeMeta->id) continue;
            logExportRouteIfNeeded(logRoutesThisBlock, edge);
            if (!edge.active || edge.bypassed) continue;
            if (edge.type != ConnectionType::Sidechain) continue;
            if (!isStemSidechainEdgeAllowed(edge)) continue;

            auto* scBufPtr = findSidechainBuffer(edge.destNodeId);
            if (scBufPtr == nullptr) continue;

            const float* scGain = getConnectionGainRamp(edge.id, edge.gain, numSamples);
            auto& scBuf = *scBufPtr;
            auto* scL   = scBuf.getWritePointer(0);
            auto* scR   = scBuf.getWritePointer(juce::jmin(1, scBuf.getNumChannels() - 1));

            auto pdcIt = pdcLines_.find(edge.destNodeId);
            if (pdcIt != pdcLines_.end() && pdcIt->second.getDelaySamples() > 0)
            {
                auto& line = pdcIt->second;
                line.push(bufL, bufR, numSamples);
                jassert((int)pdcScratchL_.size() >= numSamples);
                jassert((int)pdcScratchR_.size() >= numSamples);
                line.read(pdcScratchL_.data(), pdcScratchR_.data(), numSamples);
                SoundEngine::ApexMixFanoutCore::addStereoWithGain(pdcScratchL_.data(), pdcScratchR_.data(), scGain, scL, scR, numSamples);
            }
            else
            {
                SoundEngine::ApexMixFanoutCore::addStereoWithGain(bufL, bufR, scGain, scL, scR, numSamples);
            }
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
                    static thread_local juce::Array<Clip*> clipsOnTrackScratch;
                    clips_->getClipsOnTrack(track->getID(), clipsOnTrackScratch);
                    for (auto* clip : clipsOnTrackScratch)
                    {
                        if (clip->isMuted()) continue;
                        if (isPlaying) renderClip(clip, position, numSamples);
                    }
                }
            }

            if (auto chainsSnap = getPluginChainsSnapshotRT())
            {
                auto it = chainsSnap->find(track->getID());
                if (it != chainsSnap->end() && it->second)
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
            track->setPeakLevels(leftPeak, rightPeak);

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

    /** Render a single clip into an isolated scratch buffer, then mix the
     *  completed clip into the active track buffer.  Clip-local processing
     *  must never see samples already contributed by another clip. */
    void renderClip(Clip* clip, SamplePosition playPos, int numSamples,
                    const AutomationSnapshot* automationSnapshot = nullptr)
    {
        if (clip == nullptr || numSamples <= 0)
            return;

        const auto clipContext = clipRenderCore_.makeContext(playPos, numSamples, transport_ != nullptr && transport_->isPlaying(), false);
        if (!clipRenderCore_.validateContext(clipContext))
            return;

        auto& trackBuffer = activeTrackBuffer();
        // This buffer is allocated during prepare() and is reused for one
        // clip at a time.  A failed capacity check is a safe silent failure;
        // resizing here would violate the realtime contract.
        jassert(clipRegionPluginBuffer_.getNumChannels() >= 2
                && clipRegionPluginBuffer_.getNumSamples() >= numSamples);
        if (clipRegionPluginBuffer_.getNumChannels() < 2
            || clipRegionPluginBuffer_.getNumSamples() < numSamples)
            return;

        clipRegionPluginBuffer_.clear(0, numSamples);
        auto* previousTarget = currentClipRenderTarget_;
        currentClipRenderTarget_ = &clipRegionPluginBuffer_;
        renderClipInternal(clip, playPos, numSamples);
        currentClipRenderTarget_ = previousTarget;

        // Clip-region plugins own the isolated signal.  Tape Stop is applied
        // after that plugin chain, still before the signal is mixed into the
        // track, so neither another clip nor the dry source can be affected.
        if (clipRegionPlugins_ != nullptr && clipRegionPlugins_->hasPluginsForClip(clip->getID()))
            clipRegionPlugins_->processClipBlock(clip->getID(), clipRegionPluginBuffer_, numSamples);

        applyClipTapeStop(clip, playPos, numSamples, automationSnapshot,
                          clipRegionPluginBuffer_);

        const auto crossfadeState = [&]() noexcept
        {
            if (auto* audioClip = dynamic_cast<AudioClip*>(clip))
                return audioClip->getAutoCrossfadeState();
            return SoundEngine::ClipCrossfadeState{};
        }();

        if (!crossfadeState.hasAnyRange())
        {
            // Preserve the existing zero-overhead path when this clip has no
            // prepared overlap relationship.
            for (int ch = 0; ch < trackBuffer.getNumChannels(); ++ch)
                trackBuffer.addFrom(ch, 0, clipRegionPluginBuffer_,
                                    juce::jmin(ch, clipRegionPluginBuffer_.getNumChannels() - 1),
                                    0, numSamples);
        }
        else
        {
            // Crossfade only the already-isolated, fully processed clip.  The
            // relationship gain is applied once at final clip composition;
            // ordinary clip fades, clip gain, pan, region plugins, and Tape
            // Stop remain upstream and therefore are not double-applied.
            for (int s = 0; s < numSamples; ++s)
            {
                const float crossfadeGain = SoundEngine::ApexClipCrossfadeCore::gainAt(
                    (SamplePosition)(playPos + s), crossfadeState);
                for (int ch = 0; ch < trackBuffer.getNumChannels(); ++ch)
                {
                    const int sourceChannel = juce::jmin(ch, clipRegionPluginBuffer_.getNumChannels() - 1);
                    trackBuffer.addSample(ch, s,
                                          clipRegionPluginBuffer_.getSample(sourceChannel, s)
                                          * crossfadeGain);
                }
            }
        }
    }

    juce::AudioBuffer<float>* currentClipRenderTarget_ = nullptr;

    juce::AudioBuffer<float>& getClipRenderTarget() noexcept
    {
        return currentClipRenderTarget_ != nullptr ? *currentClipRenderTarget_ : activeTrackBuffer();
    }

    void renderClipInternal(Clip* clip, SamplePosition playPos, int numSamples)
    {
        juce::ScopedNoDenormals noDenormals;
        jassert(numSamples <= (int) scratchBuf_.size() || scratchBuf_.empty());
        if (numSamples > (int) scratchBuf_.size() && !scratchBuf_.empty())
            numSamples = (int) scratchBuf_.size();

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
            // Generation-validated resolution — no ReadWriteLock acquisition
            // per clip per block (previously 2x per clip per block).
            const auto& clipAudio = resolveClipAudio(clip->getID());
            const juce::AudioBuffer<float>* srcBuf = clipAudio.buffer;
            std::shared_ptr<const std::vector<float>> tunedAudio;
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
                    : clipAudio.sourceRate;

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
                            // A clip pitch/stretch lane OWNS its parameter: the
                            // curve applies between its first and last dot and
                            // the clip is unaffected outside them (identity
                            // fallback, not the clip's static value). This is
                            // what makes "the effect runs dot to dot" true.
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipPitchParameterId(ac->getID())))
                                if (lane->enabled)
                                    clipPitch = getPitchValueInsideDrawnRegion(*lane, blockSamplePos, 0.0f);
                            if (auto* lane = automationSnap->findLane(ac->getTrackID(), AutomationLaneCore::makeClipStretchParameterId(ac->getID())))
                                if (lane->enabled)
                                    clipStretch = getStretchValueInsideDrawnRegion(*lane, blockSamplePos, 1.0f);
                        }
                    }
                    // ═══════════════════════════════════════════════════════════════
                    // FORENSIC AUDIT — Mode Value Trap
                    // ═══════════════════════════════════════════════════════════════
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
                // TEMPO-RELATIVE PLAYBACK: DISABLED
                // ═══════════════════════════════════════════════════════════════
                // This feature was automatically applying a tempo multiplier
                // (referenceBpm / currentBpm = 120 / currentBpm) to ALL audio
                // clips during rendering.  This caused two severe bugs:
                //
                // 1) BPM/Import bug: user changes project BPM to match an
                //    imported beat → every audio clip (including the new beat)
                //    gets time-stretched and pitch-shifted because the multiplier
                //    is ≠ 1.0.
                //
                // 2) Recording bug: vocals monitored clean during recording but
                //    played back with time/pitch shift because recorded clips
                //    go through the same render pipeline.
                //
                // The project BPM should define the musical grid (MIDI, metronome)
                // and NOT automatically stretch audio clips.  In professional DAWs,
                // tempo-follow is an opt-in per-clip feature.
                //
                //   const double currentBpm = getCurrentTempo();
                //   const double referenceBpm = 120.0;
                //   if (currentBpm > 0.0)
                //   {
                //       const double tempoMultiplier = referenceBpm / currentBpm;
                //       clipStretch = juce::jmax(0.01f, (float)(clipStretch * tempoMultiplier));
                //   }

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
                // Buffer is pre-allocated to worstCaseBlock*2 in prepare(). Never resize in audio callback.
                jassert((int)pitchRamp_.size() >= count);
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

                // Per-sample clip gain ramp — prevents zipper noise when clip
                // gain automation or the gain slider changes between blocks.
                // Always fills the ramp (constant fill when steady, smooth
                // transition when changing).
                {
                    // Use find + emplace instead of operator[] to avoid implicit insertion.
                    auto gainIt = lastClipGain_.find(clip->getID());
                    if (gainIt == lastClipGain_.end())
                    {
                        auto [ins, _] = lastClipGain_.emplace(clip->getID(), 0.0f);
                        gainIt = ins;
                    }
                    float& lastGain = gainIt->second;
                    const float gainTarget = clipGain;
                    // Buffer is pre-allocated to worstCaseBlock in prepare(). Never resize in audio callback.
                    jassert((int)clipGainRamp_.size() >= count);
                    if (std::abs(lastGain - gainTarget) > 0.0001f)
                    {
                        const float clipGainCoeff = (float)(1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * 0.010)));
                        for (int s = 0; s < count; ++s)
                        {
                            lastGain += (gainTarget - lastGain) * clipGainCoeff;
                            if (std::abs(lastGain - gainTarget) < 0.0001f)
                            {   lastGain = gainTarget;
                                for (int r = s; r < count; ++r)
                                    clipGainRamp_[(size_t)r] = gainTarget;
                                break;
                            }
                            clipGainRamp_[(size_t)s] = lastGain;
                        }
                    }
                    else
                    {
                        lastGain = gainTarget;
                        std::fill(clipGainRamp_.begin(), clipGainRamp_.begin() + count, gainTarget);
                    }
                }

                // ═══════════════════════════════════════════════════════════════════
                // BUZZ DIAGNOSTIC — Hypothesis A: pitch/resample engagement
                // ═══════════════════════════════════════════════════════════════════
#if APEX_AUDIO_DEBUG_LOGS
                {
                    static bool buzzOneShotLogged = false;
                    if (!buzzOneShotLogged)
                    {
                        buzzOneShotLogged = true;
                        const double totalSt = (double)clipPitch + (double)fineTuneCents / 100.0;
                        const double pitchRatio = std::pow(2.0, totalSt / 12.0);
                        const double readRate = pitchRatio / (double)clipStretch * srcPerEngineSample;
                        juce::Logger::writeToLog(
                            juce::String("[BUZZ-HA-ONESHOT] clip=") + clip->getID()
                            + " tpMode=" + juce::String(tpMode)
                            + " defaultUserMode=" + juce::String(DAW::TimePitchModeIds::DefaultUserMode)
                            + " pitchSt=" + juce::String(clipPitch, 4)
                            + " fineTune=" + juce::String(fineTuneCents, 2)
                            + " stretch=" + juce::String(clipStretch, 6)
                            + " srcRate=" + juce::String(srcRate, 1)
                            + " engineSr=" + juce::String(sampleRate_, 1)
                            + " srcPerEngine=" + juce::String(srcPerEngineSample, 6)
                            + " pitchRatio=" + juce::String(pitchRatio, 6)
                            + " readRate=" + juce::String(readRate, 6));
                    }
                }
#endif

                if (tpMode == DAW::TimePitchModeIds::Resample)
                {
                    // ═══════════════════════════════════════════════════════════════════
                    // FORENSIC AUDIT — Resample/Tape Path Counter
                    // ═══════════════════════════════════════════════════════════════════
                    ++PitchAuditCore::resampleTapeHits;

                    // ── MODE 0: Resample (inline, fastest path) ────────────
                    // pitch and duration change together (tape/vinyl/DJ)
                    // srcPerEngineSample decouples source sample rate from engine
                    // sample rate (e.g. 44100 Hz file in 48000 Hz project).
                    //
                    // The read rate is consumed PER SAMPLE from the block's pitch
                    // ramp and the source position is accumulated across blocks.
                    // The previous block-constant rate re-anchored the position
                    // from engineClipOffset every block, so any pitch change
                    // between blocks jumped the source position by
                    // (engineClipOffset + count) * deltaRate — audible zipper /
                    // glitch under pitch automation and knob drags.
                    const double rateScale = srcPerEngineSample / juce::jmax(0.01, (double) clipStretch);

                    jassert((int) resampleRateRamp_.size() >= count);
                    for (int s = 0; s < count; ++s)
                        resampleRateRamp_[(size_t) s] = std::pow(2.0, (double) pitchRamp_[(size_t) s] / 12.0) * rateScale;

                    auto& readState = resampleReadStateMap_[clip->getID()];
                    const bool readContinuous = readState.valid
                        && readState.nextEngineOffset == engineClipOffset;
                    if (! readContinuous)
                        readState.delta = (double) engineClipOffset * resampleRateRamp_[0];

                    const double blockStartDelta = readState.delta;

#if APEX_AUDIO_DEBUG_LOGS
                    {
                        static int buzzResampleBlocks = 0;
                        if (buzzResampleBlocks < 20)
                        {
                            ++buzzResampleBlocks;
                            juce::Logger::writeToLog(
                                juce::String("[BUZZ-HA-PERBLOCK-RESAMPLE] clip=") + clip->getID()
                                + " block=" + juce::String(buzzResampleBlocks)
                                + " rate0=" + juce::String(resampleRateRamp_[0], 6)
                                + " rateEnd=" + juce::String(resampleRateRamp_[(size_t) juce::jmax(0, count - 1)], 6)
                                + " continuous=" + juce::String(readContinuous ? 1 : 0)
                                + " stretch=" + juce::String(clipStretch, 6)
                                + " srcPerEngine=" + juce::String(srcPerEngineSample, 6)
                                + " engineClipOffset=" + juce::String((int64_t)engineClipOffset));
                        }
                    }
#endif

                    const int renderChannels = getClipRenderTarget().getNumChannels();

                    for (int ch = 0; ch < renderChannels; ++ch)
                    {
                        auto* dst = getClipRenderTarget().getWritePointer(ch, bufferStart);
                        int   sch = juce::jmin(ch, srcChans - 1);
                        auto* src = useTunedAudio ? tunedSrc : srcBuf->getReadPointer(sch);

                        double sourceDelta = blockStartDelta;
                        for (int s = 0; s < count; ++s)
                        {
                            const double srcPos = clipReversed
                                ? (double)(sourceEndBound - 1) - sourceDelta
                                : (double)sourceStartBound + sourceDelta;
                            const int i1 = (int)std::floor(srcPos);
                            if (i1 >= sourceStartBound && i1 < sourceEndBound)
                            {
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
                                const float sample = (((c3 * frac + c2) * frac + c1) * frac + c0) * clipGainRamp_[(size_t)s] * panGain;

                                const int   posInClip = (int)(engineClipOffset + s);
                                const float fadeGain  = computeFadeGain(posInClip, (int)clipTimelineLength,
                                                                         fadeInLength, fadeOutLength,
                                                                         fadeInCurve, fadeOutCurve);
                                dst[s] += sample * fadeGain;
                            }

                            sourceDelta += resampleRateRamp_[(size_t) s];
                        }
                    }

                    // Persist the accumulated source position once per block (not
                    // per channel) so the next block continues exactly where this
                    // one stopped while the engine offset advances contiguously.
                    for (int s = 0; s < count; ++s)
                        readState.delta += resampleRateRamp_[(size_t) s];
                    readState.nextEngineOffset = engineClipOffset + count;
                    readState.valid = true;

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
                        auto& pitchCore = getOrCreateClipPitchCore(clip->getID());
                        const int startupPreRollSamples = pitchCore.requiredStartupPreRollSamples(clipStretch);
                        SoundEngine::PitchTimeInputPlan inputPlan;
                        {
                            // Defensive: clear the full count region BEFORE reading so
                            // any samples not written by the loop below are zero, not stale.
                            pitchInputBuffer_.clear(0, count);

#if APEX_AUDIO_DEBUG_LOGS
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
#endif

                            inputPlan = SoundEngine::ApexPitchTimeInputPlanCore::makeInputPlan(
                                count,
                                engineClipOffset,
                                clipStretch,
                                pitchInputBuffer_.getNumSamples(),
                                pitchCore.activeInputLeadSamples(),
                                startupPreRollSamples);
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
                            pitchParams.startupPreRollSamples = inputPlan.startupPreRollSamples;
                            if (pitchActive)
                                DBG("[PITCH CORE RAMP] first=" << pitchRamp_[0]
                                    << " last=" << smoothedClipPitch);
#if APEX_AUDIO_DEBUG_LOGS
                            {
                                static int buzzIndepBlocks = 0;
                                if (buzzIndepBlocks < 20)
                                {
                                    ++buzzIndepBlocks;
                                    juce::Logger::writeToLog(
                                        juce::String("[BUZZ-HA-PERBLOCK-INDEP] clip=") + clip->getID()
                                        + " block=" + juce::String(buzzIndepBlocks)
                                        + " pitchSemitones=" + juce::String(pitchParams.pitchSemitones, 4)
                                        + " stretchRatio=" + juce::String(pitchParams.stretchRatio, 6)
                                        + " sampleRate=" + juce::String(pitchParams.sampleRate)
                                        + " pitchRampLen=" + juce::String(pitchParams.pitchRampLength)
                                        + " rampFirst=" + (pitchParams.pitchRampData ? juce::String(pitchParams.pitchRampData[0], 6) : juce::String("null"))
                                        + " rampLast=" + (pitchParams.pitchRampData && pitchParams.pitchRampLength > 0 ? juce::String(pitchParams.pitchRampData[pitchParams.pitchRampLength - 1], 6) : juce::String("null"))
                                        + " count=" + juce::String(count));
                                }
                            }
#endif
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
                                float sample = pitchSrc[s] * clipGainRamp_[(size_t)s] * panGain;
                                int   posInClip = (int) (engineClipOffset + s);
                                float fadeGain = computeFadeGain(posInClip, (int) clipTimelineLength,
                                                                 fadeInLength, fadeOutLength,
                                                                 fadeInCurve, fadeOutCurve);
                                trackDst[s] += sample * fadeGain;
                            }
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
                                float sample = src[(int) readPlan.sourceIndex] * clipGainRamp_[(size_t)s] * panGain;
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
                                clipGainRamp_[(size_t)s],
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
        auto& renderTarget = getClipRenderTarget();
        for (int ch = 0; ch < renderTarget.getNumChannels(); ++ch)
        {
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
