#pragma once

#include "PluginSandboxFixedQuantumReblockerCore.h"
#include "PluginSandboxProcessCore.h"
#include "PluginSandboxPreparationCore.h"
#include "PluginSandboxAutomationTransportCore.h"   // Phase E2A (additive)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace DAW {

/** Parent-side owner for one worker-hosted plugin.

    The parent retains identity and negotiated facts only. Plugin module and
    AudioPluginInstance ownership remain exclusively in PluginWorkerHostedPluginCore.
*/
class SandboxedPluginProxyCore
{
public:
    enum class PrepareResult
    {
        Prepared,
        InvalidConfiguration,
        AlreadyActive,
        WorkerStartFailed
    };

    enum class LifecycleState
    {
        Constructed,
        Starting,
        Active,
        Releasing,
        Released,
        Failed
    };

    /** Phase D health/recovery state machine. The realtime path never reads
        this enum; it only observes the existing realtime gate atomics. */
    enum class HealthState
    {
        Constructed,
        Healthy,
        Recovering,
        Failed
    };

    // Phase D production policy constants. Conservative: the production tick
    // runs every 2 s and a healthy worker heartbeats far more often, so a
    // 6 s window cannot false-positive on scheduling jitter.
    static constexpr std::uint32_t kDefaultPollIntervalMs = 2000;
    static constexpr std::uint32_t kDefaultHangWindowMs = 6000;
    static constexpr std::uint32_t kDefaultRestartWindowMs = 60000;
    static constexpr std::uint32_t kDefaultMaxRestartsPerWindow = 3;
    static constexpr std::size_t kRestartHistoryCapacity = 8;
    static constexpr std::uint64_t kAutomationQuantumSamples = 512;

    using Preparation = PluginSandboxPreparation;

    struct LifecycleDiagnostics
    {
        LifecycleState lifecycle = LifecycleState::Constructed;
        bool prepared = false;
        bool active = false;
        bool bypassed = false;
        std::uint32_t activeRealtimeCalls = 0;
        int mainInputChannels = 0;
        int mainOutputChannels = 0;
        int workerPluginLatencySamples = 0;
        int phaseBQuantumLatencySamples = 0;
        int reblockTransportLatencySamples = 0;
        int effectiveLatencySamples = 0;
        int preparedBlockSamples = 0;
        PluginSandboxAudioTransportCore::Counters transport;
        PluginSandboxFixedQuantumReblockerCore::Diagnostics reblocker;
    };

    /** Phase D control-plane recovery diagnostics. Cumulative counters plus
        the current health snapshot. Message-thread reads; no RT use. */
    struct RecoveryDiagnostics
    {
        HealthState health = HealthState::Constructed;
        std::uint64_t detectedDeaths = 0;
        std::uint64_t detectedStalls = 0;
        std::uint64_t restartAttempts = 0;
        std::uint64_t restartSuccesses = 0;
        std::uint64_t restartFailures = 0;
        std::uint64_t forcedTerminations = 0;
        std::uint32_t currentWorkerPid = 0;
        std::uint64_t currentGeneration = 0;
        std::uint32_t stallStreakPolls = 0;
        std::uint32_t hangWindowMs = kDefaultHangWindowMs;
        std::uint32_t pollIntervalMs = kDefaultPollIntervalMs;
        std::uint32_t restartWindowMs = kDefaultRestartWindowMs;
        std::uint32_t maxRestartsPerWindow = kDefaultMaxRestartsPerWindow;
        std::uint32_t restartsInWindow = 0;
        bool remoteAvailable = false;
    };

    explicit SandboxedPluginProxyCore(const juce::PluginDescription& description,
                                      juce::String pluginInstanceId = juce::Uuid().toString())
        : description_(description),
          pluginInstanceId_(pluginInstanceId.isNotEmpty()
                                ? pluginInstanceId
                                : juce::Uuid().toString())
    {
    }

    ~SandboxedPluginProxyCore()
    {
        release(500);
    }

    SandboxedPluginProxyCore(const SandboxedPluginProxyCore&) = delete;
    SandboxedPluginProxyCore& operator=(const SandboxedPluginProxyCore&) = delete;

    PrepareResult prepare(const Preparation& preparation)
    {
        if (active_.load(std::memory_order_acquire) || process_.isRunning())
            return PrepareResult::AlreadyActive;

        stopEditorStatusCoordinator();
        resetEditorLifecycle();

        if (preparation.sampleRate <= 0.0
            || preparation.blockSamples == 0
            || preparation.blockSamples > PluginSandboxAudioShared::kPhysicalMaxSamples
            || preparation.mainInputChannels == 0
            || preparation.mainOutputChannels == 0
            || preparation.mainInputChannels > PluginSandboxAudioShared::kPhysicalMaxChannels
             || preparation.mainOutputChannels > PluginSandboxAudioShared::kPhysicalMaxChannels
             || (preparation.maximumHostBlockSamples != 0
                 && preparation.maximumHostBlockSamples
                        > PluginSandboxAudioShared::kPhysicalMaxSamples)
             || preparation.editorCreateTimeoutMs == 0
             || preparation.editorCreateTimeoutMs
                    > kPluginSandboxEditorCreateTimeoutMs)
        {
            lifecycle_.store(LifecycleState::Failed, std::memory_order_release);
            return PrepareResult::InvalidConfiguration;
        }

        lifecycle_.store(LifecycleState::Starting, std::memory_order_release);
        recoveryActivationPending_ = false;
        realtimeGateOpen_.store(false, std::memory_order_release);

        // d = ceil(maximumHostBlockSamples / Q): the prepared depth-aware
        // consume window. Phase B exchange k consumes k-d, so the worker gets
        // one full host-callback period even for back-to-back quanta inside a
        // single large callback.
        const std::uint32_t maximumHostEffective = juce::jmax(
            preparation.maximumHostBlockSamples > 0
                ? preparation.maximumHostBlockSamples
                : preparation.blockSamples,
            preparation.blockSamples);
        const std::uint32_t preparedDepth =
            (maximumHostEffective + preparation.blockSamples - 1)
                / preparation.blockSamples;

        PluginSandboxProcessCore::Options options;
        options.startupTimeoutMs = static_cast<DWORD>(preparation.startupTimeoutMs);
        options.pluginCreateTimeoutMs = static_cast<DWORD>(
            preparation.pluginCreateTimeoutMs);
        options.editorCreateTimeoutMs = static_cast<DWORD>(
            preparation.editorCreateTimeoutMs);
        options.enableAudioTransport = true;
        options.enableAutomationTransport = automationRequested_;   // Phase E2A
        options.createVst3Plugin = true;
        options.pluginDescription = description_;
        options.audioConfiguration.maxInputChannels = preparation.mainInputChannels;
        options.audioConfiguration.maxOutputChannels = preparation.mainOutputChannels;
        options.audioConfiguration.maxSamples = preparation.blockSamples;
        options.audioConfiguration.nominalBlockSamples = preparation.blockSamples;
        options.audioConfiguration.sampleRate = preparation.sampleRate;
        options.audioConfiguration.outstandingQuantumDepth = preparedDepth;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        // Preserve the test-provided worker executable across generations:
        // a reprepare rebuilds the preparation internally, and the process
        // core must never fall back to the parent's own module image.
        if (preparation.workerExecutablePathForTest.isNotEmpty())
            workerExecutablePathForTest_ = preparation.workerExecutablePathForTest;
        options.workerExecutablePathForTest = workerExecutablePathForTest_;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        options.editorCreateResponseDelayMilliseconds =
            preparation.editorCreateResponseDelayMilliseconds;
       #endif
        options.audioFaultPoint = preparation.audioFaultPoint;
         options.failStateRestoreForTest = preparation.failStateRestoreForTest
             || forceStateRestoreFailureForTest_.exchange(
                 false, std::memory_order_acq_rel);
       #endif

        if (process_.start(options) != PluginSandboxProcessCore::StartResult::Started)
        {
            lifecycle_.store(LifecycleState::Failed, std::memory_order_release);
            return PrepareResult::WorkerStartFailed;
        }

        // Automatic recovery keeps every transport gated while E1 replay and
        // the owning PluginInstanceCore's E2B rebind are completed. ProcessCore
        // opens the audio transport as part of start(); close it again before
        // any recovered worker can be exposed to normal processing.
        if (automaticRecoveryInProgress_)
        {
            process_.audioTransport().closeRealtimeGate();
            process_.automationTransport().closeRealtimeGate();
        }

        const auto& processDiagnostics = process_.diagnostics();
        mainInputChannels_ = processDiagnostics.hostedPluginInputChannels;
        mainOutputChannels_ = processDiagnostics.hostedPluginOutputChannels;
        workerPluginLatencySamples_ = processDiagnostics.hostedPluginLatencySamples;
        phaseBQuantumLatencySamples_ =
            process_.audioTransport().getTransportLatencySamples();
        preparedBlockSamples_ = static_cast<int>(preparation.blockSamples);
        preparedMaximumHostBlockSamples_ = static_cast<int>(maximumHostEffective);
        preparedOutstandingDepth_ = preparedDepth;
        preparedSampleRate_ = preparation.sampleRate;

        PluginSandboxFixedQuantumReblockerCore::Configuration reblockerConfiguration;
        reblockerConfiguration.quantumSamples = preparedBlockSamples_;
        reblockerConfiguration.maximumHostBlockSamples = preparedMaximumHostBlockSamples_;
        reblockerConfiguration.inputChannels = mainInputChannels_;
        reblockerConfiguration.outputChannels = mainOutputChannels_;
        reblockerConfiguration.pluginLatencySamples = workerPluginLatencySamples_;
        reblockerConfiguration.transportGeneration = process_.audioTransport().generation();
        reblockerConfiguration.initiallyBypassed = isBypassed();
        if (! reblocker_.prepare(reblockerConfiguration))
        {
            process_.shutdown(static_cast<DWORD>(preparation.startupTimeoutMs));
            lifecycle_.store(LifecycleState::Failed, std::memory_order_release);
            return PrepareResult::InvalidConfiguration;
        }
        backend_.setTransport(&process_.audioTransport());
        backend_.setAutomationOwner(this);                       // Phase E2A

        prepared_.store(true, std::memory_order_release);
        active_.store(true, std::memory_order_release);
        if (! automaticRecoveryInProgress_)
            realtimeGateOpen_.store(true, std::memory_order_release);
        lifecycle_.store(LifecycleState::Active, std::memory_order_release);

        preparedStartupTimeoutMs_ = preparation.startupTimeoutMs;
        preparedPluginCreateTimeoutMs_ = preparation.pluginCreateTimeoutMs;
        preparedEditorCreateTimeoutMs_ = preparation.editorCreateTimeoutMs;
        resetAutomationStagingState();                           // Phase E2A
        if (! automaticRecoveryInProgress_)
            process_.automationTransport().openRealtimeGate();    // Phase E2A (no-op when disabled)

        // Phase E1: a fresh worker is only published operational after the
        // host-known configuration is reapplied. With an empty shadow this is
        // a no-op with ZERO protocol traffic — frozen Phase C/D behavior is
        // unchanged. Replay failure tears the fresh worker down transactionally
        // rather than publishing a misconfigured replacement.
        juce::String replayError;
        if (! restoreHostConfigurationForReplay(replayError))
        {
            realtimeGateOpen_.store(false, std::memory_order_release);
            active_.store(false, std::memory_order_release);
            process_.shutdown(static_cast<DWORD>(preparation.startupTimeoutMs));
            prepared_.store(false, std::memory_order_release);
            lifecycle_.store(LifecycleState::Failed, std::memory_order_release);
            return PrepareResult::WorkerStartFailed;
        }

        if (! automaticRecoveryInProgress_)
            clearRestartHistory();
        stallStreakPolls_ = 0;
        progressSnapshot_ = currentProgressSnapshot();
        if (automaticRecoveryInProgress_)
        {
            // The worker has completed E1 replay, but the owner still has to
            // rebuild/reseed E2B before this generation may become usable.
            recoveryActivationPending_ = true;
        }
        else
        {
            health_.store(HealthState::Healthy, std::memory_order_release);
        }
        startEditorStatusCoordinator();
        return PrepareResult::Prepared;
    }

    bool release(std::uint32_t timeoutMs)
    {
        juce::String editorError;
        if (hasAsyncEditorIntent())
            requestEditorClose(editorError);
        else if (editorOpen_)
            closeEditor(timeoutMs, editorError);
        stopEditorStatusCoordinator();
        recoveryActivationPending_ = false;
        realtimeGateOpen_.store(false, std::memory_order_release);
        active_.store(false, std::memory_order_release);

        if (! process_.isRunning())
        {
            prepared_.store(false, std::memory_order_release);
            reblocker_.release();
            backend_.setTransport(nullptr);
            if (lifecycle_.load(std::memory_order_acquire) != LifecycleState::Failed)
                lifecycle_.store(LifecycleState::Released, std::memory_order_release);
            return true;
        }

        lifecycle_.store(LifecycleState::Releasing, std::memory_order_release);
        if (! waitForRealtimeDrain(timeoutMs))
        {
            lifecycle_.store(LifecycleState::Failed, std::memory_order_release);
            return false;
        }

        const bool released = process_.shutdown(static_cast<DWORD>(timeoutMs));
        prepared_.store(false, std::memory_order_release);
        if (released)
        {
            reblocker_.release();
            backend_.setTransport(nullptr);
        }
        lifecycle_.store(released ? LifecycleState::Released : LifecycleState::Failed,
                         std::memory_order_release);
        return released;
    }

    bool resetStream(std::uint32_t timeoutMs)
    {
        if (! process_.isRunning() || preparedBlockSamples_ <= 0)
            return false;

        realtimeGateOpen_.store(false, std::memory_order_release);
        active_.store(false, std::memory_order_release);
        if (! waitForRealtimeDrain(timeoutMs))
            return false;

        PluginSandboxAudioTransportCore::Configuration configuration;
        configuration.maxInputChannels = static_cast<std::uint32_t>(mainInputChannels_);
        configuration.maxOutputChannels = static_cast<std::uint32_t>(mainOutputChannels_);
        configuration.maxSamples = static_cast<std::uint32_t>(preparedBlockSamples_);
        configuration.nominalBlockSamples = static_cast<std::uint32_t>(preparedBlockSamples_);
        configuration.sampleRate = preparedSampleRate_;
        configuration.outstandingQuantumDepth = preparedOutstandingDepth_;
        if (! process_.reprepareAudio(configuration, static_cast<DWORD>(timeoutMs)))
        {
            lifecycle_.store(LifecycleState::Failed, std::memory_order_release);
            return false;
        }

        resetAutomationStagingState();
        reblocker_.resetForGeneration(process_.audioTransport().generation(), isBypassed());
        active_.store(true, std::memory_order_release);
        realtimeGateOpen_.store(true, std::memory_order_release);
        lifecycle_.store(LifecycleState::Active, std::memory_order_release);
        return true;
    }

    PrepareResult reprepare(const Preparation& preparation)
    {
        const bool bypassed = isBypassed();
        if (! release(preparation.startupTimeoutMs))
            return PrepareResult::WorkerStartFailed;
        setBypassed(bypassed);
        return prepare(preparation);
    }

    PluginSandboxFixedQuantumReblockerCore::ProcessSummary processBlock(
        juce::AudioBuffer<float>& buffer) noexcept
    {
        return processBlock(buffer, buffer.getNumSamples());
    }

    PluginSandboxFixedQuantumReblockerCore::ProcessSummary processBlock(
        juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        RealtimeCallScope realtimeCall(*this);
        if (! realtimeCall.entered())
            return {};
        if (automationEnabled())
            automationHostPosition_ += static_cast<std::uint64_t>(
                juce::jmax(0, numSamples));
        return reblocker_.processBlock(buffer, numSamples, backend_);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE E2A — realtime parameter-event transport primitive (additive).
    // E2 disabled by default: no automation mapping, no publish, no read —
    // the exact frozen Phase C path is preserved.
    // ═══════════════════════════════════════════════════════════════════════

    /** Control-plane opt-in. Must be called BEFORE the first prepare() so the
        worker receives the sidecar mapping during bootstrap. E2A does not
        enable the sidecar on an already-running worker. */
    bool enableAutomationTransport(juce::String& error)
    {
        if (prepared_.load(std::memory_order_acquire))
        {
            error = "E2A automation must be enabled before prepare";
            return false;
        }
        automationRequested_ = true;
        return true;
    }

    bool automationEnabled() const noexcept
    {
        return automationRequested_
            && process_.automationTransport().isPrepared();
    }

    /** Parent-side sequence origin for the next host callback. E2B uses this
        to retain canonical producer values until the matching exact quantum
        reports a successful audio + automation submission. The value is read
        on the same realtime owner thread as processBlock(). */
    std::uint64_t automationSequenceForNextCallback() const noexcept
    {
        return automationHostPosition_ / kAutomationQuantumSamples + 1;
    }

    /** Host callback with direct test-injected automation events (callback-
        local sample offsets). Events are staged BEFORE any quantum is emitted
        in this callback, then the frozen reblocker path runs unchanged. */
    PluginSandboxFixedQuantumReblockerCore::ProcessSummary processBlockWithAutomation(
        juce::AudioBuffer<float>& buffer,
        int numSamples,
        const PluginSandboxAutomationShared::SandboxAutomationEvent* events,
        std::uint32_t eventCount) noexcept
    {
        if (automationEnabled() && events != nullptr && eventCount > 0)
            stageAutomationEvents(events, eventCount);
        return processBlock(buffer, numSamples);
    }

    const PluginSandboxAutomationTransportCore::Diagnostics automationDiagnostics() const noexcept
    {
        return process_.automationTransport().diagnostics();
    }

   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    /** Phase E2A test hook: direct parent-side automation sidecar access for
        controlled defect injection (wrong-sequence publish, stale-mapping
        inspection) and mapping-name evidence. Test builds only — production
        builds omit this surface entirely. */
     PluginSandboxAutomationTransportCore& automationTransportForTest() noexcept
     {
         return process_.automationTransport();
     }

     /** E2B failure-injection seam: make the next live-value read fail at the
         real control-plane proxy boundary. Test builds only. */
      void setForceLiveParameterValuesFailureForTest(bool shouldFail) noexcept
      {
          forceLiveParameterValuesFailureForTest_.store(
              shouldFail, std::memory_order_release);
      }

      /** E2B failure-injection seam: make the next N audio submissions fail
          before the transport claims a shared-memory slot. Test builds only. */
       void forceNextSubmissionFailureForTest(std::uint32_t count = 1) noexcept
       {
           process_.audioTransport().forceNextSubmissionFailureForTest(count);
       }

        /** E2C control-plane failure injection: reject exactly one state chunk
            on the next fresh worker launch. The parent consumes the request;
            a replacement worker is unarmed unless the test rearms it. */
        void setForceStateRestoreFailureForTest(bool shouldFail) noexcept
        {
            forceStateRestoreFailureForTest_.store(
                shouldFail, std::memory_order_release);
        }

        std::uint64_t stateRestoreFailureCountForTest() const noexcept
        {
            return stateRestoreFailureCountForTest_.load(
                std::memory_order_acquire);
        }

        /** Phase E2C test-only forwarding seam for the existing parent audio
            transport stale-output injection. The underlying helper performs
            all generation validation and remains unchanged. */
        bool injectStaleGenerationOutputForTest(
            std::uint64_t staleGeneration,
            std::uint64_t sequence,
            std::uint32_t channels,
            std::uint32_t samples,
            float recognizableValue) noexcept
        {
            return process_.audioTransport().injectStaleGenerationOutputForSelfTest(
                staleGeneration, sequence, channels, samples, recognizableValue);
        }

        DWORD processExitCodeForTest() const noexcept
        {
            return process_.processExitCodeForTest();
        }

        /** Phase F1 test seam: exercise parent identity validation with a
            deliberately supplied editor identity. */
        bool resizeEditorIdentityForTest(
                                         const PluginSandboxProcessCore::EditorInfo& identity,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         DWORD timeoutMs,
                                         juce::String& error)
        {
            return process_.resizeEditor(identity, width, height,
                                         timeoutMs, error);
        }

        /** Phase F1 test seam: terminate the worker through the existing
            bounded retirement path while an editor may still be open. */
        bool terminateWorkerForEditorTest(std::uint32_t drainTimeoutMs)
        {
            beginRecovery(true);
            return process_.retireWorkerAfterFailure(drainTimeoutMs);
        }
     #endif

    const juce::PluginDescription& getDescription() const noexcept { return description_; }
    const juce::String& getPluginInstanceId() const noexcept { return pluginInstanceId_; }
    double getPreparedSampleRate() const noexcept { return preparedSampleRate_; }
    int getPreparedBlockSamples() const noexcept { return preparedBlockSamples_; }
    int getPreparedMaximumHostBlockSamples() const noexcept
    {
        return preparedMaximumHostBlockSamples_;
    }
    int getPreparedOutstandingDepth() const noexcept
    {
        return static_cast<int>(preparedOutstandingDepth_);
    }

    int getMainInputChannels() const noexcept { return mainInputChannels_; }
    int getMainOutputChannels() const noexcept { return mainOutputChannels_; }
    int getWorkerPluginLatencySamples() const noexcept { return workerPluginLatencySamples_; }
    int getPhaseBQuantumLatencySamples() const noexcept
    {
        return phaseBQuantumLatencySamples_;
    }
    int getReblockTransportLatencySamples() const noexcept
    {
        return reblocker_.transportLatencySamples();
    }
    int getEffectiveLatencySamples() const noexcept
    {
        return reblocker_.effectiveLatencySamples();
    }

    bool isPrepared() const noexcept { return prepared_.load(std::memory_order_acquire); }
    bool isActive() const noexcept { return active_.load(std::memory_order_acquire); }
    bool isBypassed() const noexcept { return bypassed_.load(std::memory_order_relaxed); }
    void setBypassed(bool bypassed) noexcept
    {
        bypassed_.store(bypassed, std::memory_order_relaxed);
        reblocker_.requestBypass(bypassed);
    }

    LifecycleDiagnostics lifecycleDiagnostics() const noexcept
    {
        LifecycleDiagnostics result;
        result.lifecycle = lifecycle_.load(std::memory_order_acquire);
        result.prepared = isPrepared();
        result.active = isActive();
        result.bypassed = isBypassed();
        result.activeRealtimeCalls = activeRealtimeCalls_.load(std::memory_order_acquire);
        result.mainInputChannels = mainInputChannels_;
        result.mainOutputChannels = mainOutputChannels_;
        result.workerPluginLatencySamples = workerPluginLatencySamples_;
        result.phaseBQuantumLatencySamples = phaseBQuantumLatencySamples_;
        result.reblockTransportLatencySamples = getReblockTransportLatencySamples();
        result.effectiveLatencySamples = getEffectiveLatencySamples();
        result.preparedBlockSamples = preparedBlockSamples_;
        result.transport = process_.audioTransport().counters();
        result.reblocker = reblocker_.diagnostics();
        return result;
    }

    const PluginSandboxProcessCore::Diagnostics& processDiagnostics() const noexcept
    {
        return process_.diagnostics();
    }

    using EditorInfo = PluginSandboxProcessCore::EditorInfo;

    enum class EditorLifecycleState
    {
        Closed,
        Requesting,
        Creating,
        Slow,
        Ready,
        Failed,
        CancelPending,
        Cancelled,
        WorkerGone
    };

    struct EditorRequestIdentity
    {
        std::uint64_t workerGeneration = 0;
        std::uint64_t editorRequestId = 0;
    };

    struct EditorLifecycleSnapshot
    {
        EditorLifecycleState state = EditorLifecycleState::Closed;
        EditorRequestIdentity identity;
        EditorInfo info;
        juce::String error;
        bool desiredOpen = false;
    };

    EditorLifecycleSnapshot editorLifecycle() const noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        return editorLifecycle_;
    }

   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
     bool acceptEditorStatusForTest(const EditorLifecycleSnapshot& snapshot) noexcept
     {
         return acceptEditorStatus(snapshot);
     }
   #endif

    bool canRequestEditor() const noexcept
    {
        return prepared_.load(std::memory_order_acquire)
            && process_.isRunning();
    }

    bool isEditorOpen() const noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        return editorOpen_
            || (editorLifecycle_.desiredOpen
                && editorLifecycle_.state == EditorLifecycleState::Ready
                && editorLifecycle_.info.nativeWindowHandle != 0);
    }

    EditorInfo editorInfo() const noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        if (editorLifecycle_.info.nativeWindowHandle != 0)
            return editorLifecycle_.info;
        return editorInfo_;
    }

    /** Phase F3 parent-side submit. Only a bounded IPC acknowledgement is
        awaited; worker GUI construction is observed by the coordinator. */
    bool requestEditorOpen(juce::String& error)
    {
        if (! canRequestEditor())
        {
            error = "sandbox plugin is not active";
            return false;
        }

        const auto generation = process_.currentWorkerGeneration();
        if (generation == 0)
        {
            error = "sandbox worker has no active generation";
            return false;
        }

        std::uint64_t requestSequence = 0;
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (editorLifecycle_.desiredOpen)
            {
                if (editorLifecycle_.state == EditorLifecycleState::Requesting
                    || editorLifecycle_.state == EditorLifecycleState::Creating
                    || editorLifecycle_.state == EditorLifecycleState::Slow
                    || editorLifecycle_.state == EditorLifecycleState::Ready)
                    return true;
                if (editorLifecycle_.state == EditorLifecycleState::CancelPending)
                {
                    error = "sandbox editor close is still pending";
                    return false;
                }
            }

            requestSequence = ++nextEditorRequestSequence_;
            editorLifecycle_ = {};
            editorLifecycle_.state = EditorLifecycleState::Requesting;
            editorLifecycle_.identity = { generation, requestSequence };
            editorLifecycle_.desiredOpen = true;
            editorInfo_ = {};
            editorOpen_ = false;
        }

        PluginSandboxProcessCore::EditorAsyncStatus response;
        if (! process_.submitEditorOpen(pluginInstanceId_, requestSequence,
                                        response, kEditorSubmitAckTimeoutMs,
                                        error))
        {
            markEditorRequestFailed(generation, requestSequence, error);
            return false;
        }

        if (! acceptProcessEditorStatus(response))
        {
            const auto snapshot = editorLifecycle();
            if (snapshot.state == EditorLifecycleState::Failed
                || snapshot.state == EditorLifecycleState::WorkerGone)
            {
                error = snapshot.error.isNotEmpty()
                    ? snapshot.error : "worker rejected asynchronous editor submit";
                return false;
            }
        }
        return true;
    }

    /** Phase F3 parent-side cancellation intent. This returns after the
        bounded cancel acknowledgement; it never waits for vendor destruction. */
    bool requestEditorClose(juce::String& error)
    {
        EditorRequestIdentity identity;
        bool waitForReadyEditorClose = false;
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (! editorLifecycle_.desiredOpen
                && editorLifecycle_.state != EditorLifecycleState::Ready
                && editorLifecycle_.state != EditorLifecycleState::Requesting
                && editorLifecycle_.state != EditorLifecycleState::Creating
                && editorLifecycle_.state != EditorLifecycleState::Slow
                && editorLifecycle_.state != EditorLifecycleState::CancelPending)
            {
                editorOpen_ = false;
                editorInfo_ = {};
                return true;
            }

            identity = editorLifecycle_.identity;
            waitForReadyEditorClose = editorLifecycle_.state == EditorLifecycleState::Ready;
            editorLifecycle_.desiredOpen = false;
            editorLifecycle_.state = EditorLifecycleState::CancelPending;
            editorOpen_ = false;
        }

        if (identity.workerGeneration == 0 || identity.editorRequestId == 0)
        {
            std::lock_guard<std::mutex> lock(editorStateMutex_);
            editorLifecycle_.state = EditorLifecycleState::Closed;
            editorLifecycle_.info = {};
            editorInfo_ = {};
            return true;
        }

        PluginSandboxProcessCore::EditorAsyncStatus response;
        if (! process_.cancelEditor(pluginInstanceId_, identity.editorRequestId,
                                    response, kEditorSubmitAckTimeoutMs, error))
        {
            std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (editorLifecycle_.identity.workerGeneration == identity.workerGeneration
                && editorLifecycle_.identity.editorRequestId == identity.editorRequestId)
                editorLifecycle_.state = process_.isRunning()
                    ? EditorLifecycleState::Failed
                    : EditorLifecycleState::WorkerGone;
            return false;
        }
        acceptProcessEditorStatus(response);
        if (waitForReadyEditorClose && ! waitForEditorClosed(kEditorCloseSettleTimeoutMs))
        {
            error = "sandbox editor close did not settle after cancellation";
            return false;
        }
        return true;
    }

    /** F1 worker-owned editor create. The parent receives only a validated
        identity record; it never constructs a vendor plugin/editor object. */
    bool createEditor(juce::String& error)
    {
        return createEditor(static_cast<DWORD>(preparedEditorCreateTimeoutMs_), error);
    }

    bool createEditor(DWORD timeoutMs, juce::String& error)
    {
        if (! canRequestEditor())
        {
            error = "sandbox plugin is not active";
            return false;
        }

        std::uint64_t requestSequence = 0;
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            requestSequence = editorOpen_
                ? editorInfo_.requestSequence : ++nextEditorRequestSequence_;
        }
        EditorInfo result;
        if (! process_.createEditor(pluginInstanceId_, requestSequence,
                                    result, timeoutMs, error))
        {
            if (! process_.isRunning())
                invalidateEditorIdentity();
            return false;
        }
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            editorInfo_ = result;
            editorOpen_ = true;
        }
        return true;
    }

    bool resizeEditor(std::uint32_t width,
                      std::uint32_t height,
                      DWORD timeoutMs,
                      juce::String& error)
    {
        EditorInfo current;
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (! editorOpen_)
            {
                error = "sandbox editor is not open";
                return false;
            }
            current = editorInfo_;
        }
        if (! process_.resizeEditor(current, width, height,
                                    timeoutMs, error))
        {
            if (! process_.isRunning())
                invalidateEditorIdentity();
            return false;
        }
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            editorInfo_.width = width;
            editorInfo_.height = height;
        }
        return true;
    }

    bool closeEditor(DWORD timeoutMs, juce::String& error)
    {
        EditorInfo current;
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (! editorOpen_)
                return true;
            current = editorInfo_;
        }
        if (! process_.closeEditor(current, timeoutMs, error))
        {
            if (! process_.isRunning())
                invalidateEditorIdentity();
            return false;
        }
        invalidateEditorIdentity();
        return true;
    }

    void invalidateEditorIdentity() noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        editorOpen_ = false;
        editorInfo_ = {};
        editorLifecycle_.state = EditorLifecycleState::WorkerGone;
        editorLifecycle_.desiredOpen = false;
        editorLifecycle_.info = {};
    }

    std::uint64_t workerCompletedSequence() const noexcept
    {
        return process_.audioTransport().workerCompletedSequence();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE E1 — static parameter control + state-chunk transport + parent
    // runtime shadow + automatic-restart replay. CONTROL PLANE ONLY.
    // ═══════════════════════════════════════════════════════════════════════

    struct SandboxParameterInfo
    {
        int index = -1;
        juce::String parameterId;       // stable JUCE paramID ("param_N" fallback)
        juce::String name;              // display metadata only
        float defaultValue = 0.0f;
        int numSteps = 0;
        bool isDiscrete = false;
        bool isBoolean = false;
    };

    bool fetchParameterMetadata(juce::Array<SandboxParameterInfo>& out,
                                juce::String& error)
    {
        out.clear();
        if (! prepared_.load(std::memory_order_acquire))
        {
            error = "sandbox plugin is not prepared";
            return false;
        }
        juce::MemoryBlock payload;
        if (! process_.requestParameterMetadata(payload, error))
            return false;
        return parseParameterMetadata(payload, out, error);
    }

    /** Static host→worker parameter control. The parent shadow is updated
        ONLY after the worker acknowledges success; a failed set leaves the
        previous authoritative value intact. */
    bool setParameter(const juce::String& parameterId,
                      float normalizedValue,
                      juce::String& error)
    {
        if (! prepared_.load(std::memory_order_acquire))
        {
            error = "sandbox plugin is not prepared";
            return false;
        }
        float appliedValue = 0.0f;
        if (! process_.setPluginParameter(parameterId, normalizedValue,
                                          appliedValue, error))
            return false;
        parameterShadow_[parameterId] = appliedValue;
        return true;
    }

    struct SandboxLiveParameterValue
    {
        int index = -1;
        float normalizedValue = 0.0f;
    };

    /** Phase E2B (control plane): read the ACTUAL live worker parameter
        values. Observational — never mutates the E1 shadows. */
    bool fetchLiveParameterValues(juce::Array<SandboxLiveParameterValue>& out,
                                  juce::String& error)
    {
        out.clear();
        if (! prepared_.load(std::memory_order_acquire))
        {
            error = "sandbox plugin is not prepared";
            return false;
        }
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (forceLiveParameterValuesFailureForTest_.load(
                std::memory_order_acquire))
        {
            error = "test-injected live value fetch failure";
            return false;
        }
       #endif
        juce::MemoryBlock payload;
        if (! process_.requestLiveParameterValues(payload, error))
            return false;
        if (payload.getSize() < sizeof(std::int32_t))
        {
            error = "live value payload is truncated";
            return false;
        }
        juce::MemoryInputStream stream(payload.getData(), payload.getSize(), false);
        const int count = stream.readInt();
        if (count < 0 || count > static_cast<int>(
                PluginSandboxWin32::kE2BMaximumLiveValueEntries))
        {
            error = "live value count exceeds the E2B bound";
            return false;
        }
        const auto expectedBytes = sizeof(std::int32_t)
            + static_cast<std::size_t>(count) * (sizeof(std::int32_t) + sizeof(float));
        if (payload.getSize() != expectedBytes)
        {
            error = "live value payload size does not match its entry count";
            return false;
        }
        bool seen[PluginSandboxWin32::kE2BMaximumLiveValueEntries] {};
        for (int i = 0; i < count; ++i)
        {
            SandboxLiveParameterValue value;
            value.index = stream.readInt();
            value.normalizedValue = stream.readFloat();
            if (value.index < 0
                || value.index >= static_cast<int>(
                    PluginSandboxWin32::kE2BMaximumLiveValueEntries)
                || ! std::isfinite(value.normalizedValue))
            {
                error = "invalid live value entry";
                out.clear();
                return false;
            }
            if (value.normalizedValue < 0.0f || value.normalizedValue > 1.0f)
            {
                error = "live value is outside normalized range";
                out.clear();
                return false;
            }
            if (seen[value.index])
            {
                error = "duplicate live value index";
                out.clear();
                return false;
            }
            seen[value.index] = true;
            out.add(value);
        }
        return true;
    }

    bool captureState(juce::MemoryBlock& out, juce::String& error)
    {
        out.reset();
        if (! prepared_.load(std::memory_order_acquire))
        {
            error = "sandbox plugin is not prepared";
            return false;
        }
        if (! process_.capturePluginState(out, error))
            return false;

        // A successful control-plane capture is the newest validated opaque
        // state checkpoint.  Keep the existing E1 shadow as the sole state
        // authority used by worker restart/replay; never commit a failed or
        // unbounded response.
        stateShadow_ = out;
        return true;
    }

    /** Apply a state chunk to the worker plugin. stateShadow_ is updated only
        after the worker acknowledges success. */
    bool restoreState(const juce::MemoryBlock& state, juce::String& error)
    {
        if (! prepared_.load(std::memory_order_acquire))
        {
            error = "sandbox plugin is not prepared";
            return false;
        }
        if (! process_.restorePluginState(state, error))
            return false;
        stateShadow_ = state;
        return true;
    }

    const juce::MemoryBlock& lastAuthoritativeState() const noexcept { return stateShadow_; }
    const std::map<juce::String, float>& lastAuthoritativeParameters() const noexcept
    {
        return parameterShadow_;
    }

    static bool parseParameterMetadata(const juce::MemoryBlock& payload,
                                       juce::Array<SandboxParameterInfo>& out,
                                       juce::String& error)
    {
        out.clear();
        juce::MemoryInputStream stream(payload.getData(), payload.getSize(), false);
        const int count = stream.readInt();
        if (count < 0 || count > static_cast<int>(PluginSandboxWin32::kE1MaximumParameters))
        {
            error = "parameter metadata count exceeds the E1 bound";
            return false;
        }
        for (int i = 0; i < count; ++i)
        {
            SandboxParameterInfo info;
            info.index = stream.readInt();
            info.parameterId = stream.readString();
            info.name = stream.readString();
            info.defaultValue = stream.readFloat();
            info.numSteps = stream.readInt();
            const int flags = stream.readByte();
            info.isDiscrete = (flags & 1) != 0;
            info.isBoolean = (flags & 2) != 0;
            if (info.parameterId.isEmpty())
            {
                error = "parameter metadata contains an empty stable ID";
                out.clear();
                return false;
            }
            out.add(info);
        }
        return true;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE D — automatic worker-death / hang containment + restart
    // ═══════════════════════════════════════════════════════════════════════
    //
    // Every method in this section is CONTROL PLANE ONLY. The realtime path
    // is untouched: while Recovering the realtime gate stays closed and the
    // existing validated fallback contract produces deterministic dry output.

    /** Cheap realtime-safe snapshot: true while remote processing is gated
        open. Tests and diagnostics only; the RT path uses the gate itself. */
    bool remoteAvailable() const noexcept
    {
        return prepared_.load(std::memory_order_acquire)
            && active_.load(std::memory_order_acquire)
            && realtimeGateOpen_.load(std::memory_order_acquire);
    }

    HealthState healthState() const noexcept
    {
        return health_.load(std::memory_order_acquire);
    }

    std::uint32_t currentWorkerPid() const noexcept
    {
        return process_.diagnostics().workerProcessId;
    }

    std::uint64_t currentWorkerGeneration() const noexcept
    {
        return process_.currentWorkerGeneration();
    }

    std::uint64_t currentGeneration() const noexcept
    {
        return process_.audioTransport().generation();
    }

    RecoveryDiagnostics recoveryDiagnostics() const noexcept
    {
        RecoveryDiagnostics result;
        result.health = healthState();
        result.detectedDeaths = detectedDeaths_.load(std::memory_order_relaxed);
        result.detectedStalls = detectedStalls_.load(std::memory_order_relaxed);
        result.restartAttempts = restartAttempts_.load(std::memory_order_relaxed);
        result.restartSuccesses = restartSuccesses_.load(std::memory_order_relaxed);
        result.restartFailures = restartFailures_.load(std::memory_order_relaxed);
        result.forcedTerminations = forcedTerminations_.load(std::memory_order_relaxed);
        result.currentWorkerPid = currentWorkerPid();
        result.currentGeneration = currentGeneration();
        result.stallStreakPolls = stallStreakPolls_;
        result.hangWindowMs = hangWindowMs_;
        result.pollIntervalMs = pollIntervalMs_;
        result.restartWindowMs = restartWindowMs_;
        result.maxRestartsPerWindow = maxRestartsPerWindow_;
        result.restartsInWindow = restartsInWindow();
        result.remoteAvailable = remoteAvailable();
        return result;
    }

    /** Phase D main control-plane entry. Call from the existing production
        maintenance tick (2 s message-thread cadence) or a test harness.

        - Healthy: classifies unexpected process death and live-worker stalls
          (progress-based, never by mere idleness).
        - Recovering: continues the bounded automatic restart ladder.
        - Failed: no automatic work; an explicit control-plane reprepare
          (the existing prepare()/reprepare() path) clears the incident. */
    void pollHealthAndRecover()
    {
        pollHealthAndRecover(pollIntervalMs_, hangWindowMs_,
                             preparedStartupTimeoutMs_);
    }

    void pollHealthAndRecover(std::uint32_t pollIntervalMs,
                              std::uint32_t hangWindowMs,
                              std::uint32_t startupTimeoutMs)
    {
        if (recoveryActivationPending_)
            return;   // owner-side E2B completion must finish first

        if (! prepared_.load(std::memory_order_acquire))
            return;

        const auto state = health_.load(std::memory_order_acquire);
        if (state == HealthState::Failed)
            return;   // explicit control-plane reset required

        if (state == HealthState::Recovering)
        {
            attemptAutomaticRestart(startupTimeoutMs);
            return;
        }

        // Healthy → detection.
        const auto liveness = process_.pollLiveness();
        if (liveness == PluginSandboxProcessCore::Liveness::Dead)
        {
            ++detectedDeaths_;
            beginRecovery(false);
            attemptAutomaticRestart(startupTimeoutMs);
            return;
        }

        if (liveness == PluginSandboxProcessCore::Liveness::Alive)
        {
            if (stallEvidenceAccumulating(pollIntervalMs, hangWindowMs))
            {
                ++detectedStalls_;
                beginRecovery(true);
                attemptAutomaticRestart(startupTimeoutMs);
            }
        }
    }

    /** True while automatic recovery has completed worker/plugin creation and
        E1 replay, but the owning PluginInstanceCore has not yet published its
        current-generation E2B bindings and reopened realtime processing. */
    bool recoveryCompletionPending() const noexcept
    {
        return recoveryActivationPending_;
    }

    /** Complete the automatic-recovery publication handshake. The caller must
        first rebuild the owner-side E2B bindings from the fresh worker. This
        method is control-plane only and opens the transport gates last. */
    bool completeAutomaticRecoveryActivation() noexcept
    {
        if (! recoveryActivationPending_)
            return true;
        if (! prepared_.load(std::memory_order_acquire)
            || ! active_.load(std::memory_order_acquire)
            || health_.load(std::memory_order_acquire) != HealthState::Recovering)
            return false;

        process_.audioTransport().openRealtimeGate();
        if (automationRequested_)
            process_.automationTransport().openRealtimeGate();
        realtimeGateOpen_.store(true, std::memory_order_release);
        recoveryActivationPending_ = false;
        health_.store(HealthState::Healthy, std::memory_order_release);
        return true;
    }

    /** Explicit control-plane reset of the automatic-restart circuit breaker.
        Cumulative diagnostics counters are preserved. */
    void clearRecoveryIncident()
    {
        clearRestartHistory();
        stallStreakPolls_ = 0;
        progressSnapshot_ = currentProgressSnapshot();
        if (prepared_.load(std::memory_order_acquire)
            && health_.load(std::memory_order_acquire) == HealthState::Failed)
            health_.store(HealthState::Healthy, std::memory_order_release);
    }

   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    void setPollIntervalMsForTest(std::uint32_t ms) noexcept { pollIntervalMs_ = ms; }
    void setHangWindowMsForTest(std::uint32_t ms) noexcept { hangWindowMs_ = ms; }
    void setRestartPolicyForTest(std::uint32_t maxPerWindow, std::uint32_t windowMs) noexcept
    {
        maxRestartsPerWindow_ = maxPerWindow;
        restartWindowMs_ = windowMs;
    }
    /** Phase D diagnostics: expose the process-core liveness classification
        (0 NotPrepared / 1 Alive / 2 Dead). Test/diagnostic use only. */
    int pollLivenessForTest() const noexcept
    {
        return static_cast<int>(process_.pollLiveness());
    }
    bool processCoreRunningForTest() const noexcept { return process_.isRunning(); }
    std::uint32_t processHandlePidForTest() const noexcept
    {
        return process_.processHandlePidForTest();
    }
    std::uint32_t workerStartCountForTest() const noexcept
    {
        return process_.workerStartCountForTest();
    }
    std::uint64_t workerHeartbeatForTest() const noexcept
    {
        return process_.audioTransport().workerHeartbeatCount();
    }
    std::uint64_t workerMetadataErrorsForTest() const noexcept
    {
        return process_.audioTransport().workerMetadataErrorsCount();
    }
    juce::String processHandleRawForTest() const noexcept
    {
        return process_.processHandleRawForTest();
    }
    /** Deterministic restart-failure injection: the NEXT automatic restart
        uses this worker executable path (an invalid path fails the real
        ProcessCore start path). Cleared after one automatic restart attempt. */
    void setNextRestartWorkerPathForTest(const juce::String& path)
    {
        nextRestartWorkerPathForTest_ = path;
    }
   #endif

private:
    static constexpr DWORD kEditorSubmitAckTimeoutMs = 250;
    static constexpr DWORD kEditorStatusQueryTimeoutMs = 250;
    static constexpr DWORD kEditorCloseSettleTimeoutMs = 5000;
    static constexpr std::uint32_t kEditorStatusPollIntervalMs = 25;

    bool waitForEditorClosed(std::uint32_t timeoutMs) const noexcept
    {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto snapshot = editorLifecycle();
            if (! isEditorOpen()
                && (snapshot.state == EditorLifecycleState::Closed
                    || snapshot.state == EditorLifecycleState::Cancelled
                    || snapshot.state == EditorLifecycleState::Failed
                    || snapshot.state == EditorLifecycleState::WorkerGone))
                return true;

#if JUCE_MODAL_LOOPS_PERMITTED
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating();
                mm != nullptr && mm->isThisTheMessageThread())
                mm->runDispatchLoopUntil(5);
            else
#endif
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return ! isEditorOpen();
    }

    void resetEditorLifecycle() noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        editorOpen_ = false;
        editorInfo_ = {};
        editorLifecycle_ = {};
        editorLifecycle_.state = EditorLifecycleState::Closed;
    }

    bool hasAsyncEditorIntent() const noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        return editorLifecycle_.desiredOpen
            || editorLifecycle_.state == EditorLifecycleState::Requesting
            || editorLifecycle_.state == EditorLifecycleState::Creating
            || editorLifecycle_.state == EditorLifecycleState::Slow
            || editorLifecycle_.state == EditorLifecycleState::Ready
            || editorLifecycle_.state == EditorLifecycleState::CancelPending;
    }

    void markEditorRequestFailed(std::uint64_t generation,
                                 std::uint64_t requestSequence,
                                 const juce::String& error) noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        if (editorLifecycle_.identity.workerGeneration != generation
            || editorLifecycle_.identity.editorRequestId != requestSequence)
            return;
        editorLifecycle_.state = process_.isRunning()
            ? EditorLifecycleState::Failed : EditorLifecycleState::WorkerGone;
        editorLifecycle_.desiredOpen = false;
        editorLifecycle_.info = {};
        editorLifecycle_.error = error;
        editorOpen_ = false;
        editorInfo_ = {};
    }

    bool acceptProcessEditorStatus(
        const PluginSandboxProcessCore::EditorAsyncStatus& status) noexcept
    {
        EditorLifecycleSnapshot snapshot;
        snapshot.identity.workerGeneration = status.info.workerGeneration;
        snapshot.identity.editorRequestId = status.info.requestSequence;
        snapshot.info = status.info;
        snapshot.error = status.error;
        snapshot.desiredOpen = status.desiredOpen;
        switch (status.status)
        {
            case PluginSandboxWin32::EditorAsyncStatus::Accepted:
                snapshot.state = EditorLifecycleState::Requesting;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::AlreadyCreating:
            case PluginSandboxWin32::EditorAsyncStatus::Creating:
                snapshot.state = EditorLifecycleState::Creating;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::Slow:
                snapshot.state = EditorLifecycleState::Slow;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::AlreadyReady:
            case PluginSandboxWin32::EditorAsyncStatus::Ready:
                snapshot.state = EditorLifecycleState::Ready;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::CancelPending:
                snapshot.state = EditorLifecycleState::CancelPending;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::Cancelled:
                snapshot.state = EditorLifecycleState::Cancelled;
                snapshot.desiredOpen = false;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::Closed:
                snapshot.state = EditorLifecycleState::Closed;
                snapshot.desiredOpen = false;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::WorkerGone:
                snapshot.state = EditorLifecycleState::WorkerGone;
                snapshot.desiredOpen = false;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::Failed:
            case PluginSandboxWin32::EditorAsyncStatus::Rejected:
                snapshot.state = EditorLifecycleState::Failed;
                snapshot.desiredOpen = false;
                break;
            case PluginSandboxWin32::EditorAsyncStatus::Stale:
                return false;
        }
        return acceptEditorStatus(snapshot);
    }

    bool acceptEditorStatus(const EditorLifecycleSnapshot& incoming) noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        if (incoming.identity.workerGeneration == 0
            || incoming.identity.editorRequestId == 0
            || incoming.identity.workerGeneration
                   != editorLifecycle_.identity.workerGeneration
            || incoming.identity.editorRequestId
                   != editorLifecycle_.identity.editorRequestId)
            return false;

        // A close/cancel intent is authoritative in the parent. A late READY
        // response may not reopen the editor after that intent was published.
        const bool cancelIntent = ! editorLifecycle_.desiredOpen
            || editorLifecycle_.state == EditorLifecycleState::CancelPending;
        if (cancelIntent
            && (incoming.state == EditorLifecycleState::Requesting
                || incoming.state == EditorLifecycleState::Creating
                || incoming.state == EditorLifecycleState::Slow
                || incoming.state == EditorLifecycleState::Ready))
            return false;

        editorLifecycle_.state = incoming.state;
        editorLifecycle_.info = incoming.info;
        editorLifecycle_.error = incoming.error;
        if (incoming.state == EditorLifecycleState::CancelPending)
            editorLifecycle_.desiredOpen = false;
        else
            editorLifecycle_.desiredOpen = incoming.desiredOpen;

        if (editorLifecycle_.state == EditorLifecycleState::Ready
            && editorLifecycle_.desiredOpen)
        {
            editorOpen_ = true;
            editorInfo_ = editorLifecycle_.info;
        }
        else if (editorLifecycle_.state == EditorLifecycleState::Cancelled
                 || editorLifecycle_.state == EditorLifecycleState::Closed
                 || editorLifecycle_.state == EditorLifecycleState::Failed
                 || editorLifecycle_.state == EditorLifecycleState::WorkerGone)
        {
            editorOpen_ = false;
            editorInfo_ = {};
            editorLifecycle_.info = {};
        }
        return true;
    }

    void startEditorStatusCoordinator()
    {
        bool expected = false;
        if (! editorStatusThreadRunning_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel))
            return;
        editorStatusThread_ = std::thread([this] { editorStatusLoop(); });
    }

    void stopEditorStatusCoordinator() noexcept
    {
        if (! editorStatusThreadRunning_.exchange(false, std::memory_order_acq_rel))
        {
            if (editorStatusThread_.joinable())
                editorStatusThread_.join();
            return;
        }
        editorStatusWake_.notify_all();
        if (editorStatusThread_.joinable())
            editorStatusThread_.join();
    }

    void editorStatusLoop() noexcept
    {
        while (editorStatusThreadRunning_.load(std::memory_order_acquire))
        {
            {
                std::unique_lock<std::mutex> waitLock(editorStatusWaitMutex_);
                editorStatusWake_.wait_for(
                    waitLock,
                    std::chrono::milliseconds(kEditorStatusPollIntervalMs),
                    [this]
                    {
                        return ! editorStatusThreadRunning_.load(
                            std::memory_order_acquire);
                    });
            }
            if (! editorStatusThreadRunning_.load(std::memory_order_acquire))
                break;

            EditorLifecycleSnapshot current;
            {
                const std::lock_guard<std::mutex> lock(editorStateMutex_);
                current = editorLifecycle_;
            }

            if (current.identity.workerGeneration == 0
                || current.identity.editorRequestId == 0
                || (current.state != EditorLifecycleState::Requesting
                    && current.state != EditorLifecycleState::Creating
                    && current.state != EditorLifecycleState::Slow
                    && current.state != EditorLifecycleState::CancelPending))
                continue;

            PluginSandboxProcessCore::EditorAsyncStatus status;
            juce::String error;
            if (process_.queryEditorStatus(
                    pluginInstanceId_, current.identity.editorRequestId,
                    status, kEditorStatusQueryTimeoutMs, error))
            {
                acceptProcessEditorStatus(status);
            }
            else if (! process_.isRunning())
            {
                std::lock_guard<std::mutex> lock(editorStateMutex_);
                if (editorLifecycle_.identity.workerGeneration
                        == current.identity.workerGeneration
                    && editorLifecycle_.identity.editorRequestId
                        == current.identity.editorRequestId)
                {
                    editorLifecycle_.state = EditorLifecycleState::WorkerGone;
                    editorLifecycle_.desiredOpen = false;
                    editorLifecycle_.info = {};
                    editorLifecycle_.error = error;
                    editorOpen_ = false;
                    editorInfo_ = {};
                }
            }
        }
    }

    struct ProgressSnapshot
    {
        std::uint64_t submitted = 0;
        std::uint64_t completed = 0;
        std::uint64_t heartbeat = 0;
    };

    ProgressSnapshot currentProgressSnapshot() const noexcept
    {
        return { process_.audioTransport().counters().submitted,
                 process_.audioTransport().workerCompletedSequence(),
                 process_.audioTransport().workerHeartbeatCount() };
    }

    std::uint32_t restartsInWindow() const noexcept
    {
       #if JUCE_WINDOWS
        const auto now = GetTickCount64();
        std::uint32_t count = 0;
        for (std::size_t i = 0; i < kRestartHistoryCapacity; ++i)
        {
            const auto tick = restartHistory_[i];
            if (tick != 0 && now - tick <= restartWindowMs_)
                ++count;
        }
        return count;
       #else
        return 0;
       #endif
    }

    void recordRestartAttempt() noexcept
    {
       #if JUCE_WINDOWS
        restartHistory_[restartHistoryIndex_++ % kRestartHistoryCapacity] =
            GetTickCount64();
       #endif
    }

    void clearRestartHistory() noexcept
    {
        restartHistory_.fill(0);
        restartHistoryIndex_ = 0;
    }

    /** Progress-based stall evidence. A live worker is hung only when BOTH
        worker-side signals (completed sequence, heartbeat) are frozen while
        submitted work is still outstanding. An idle worker (submitted ==
        completed) never accumulates stall evidence. */
    bool stallEvidenceAccumulating(std::uint32_t pollIntervalMs,
                                   std::uint32_t hangWindowMs) noexcept
    {
        const auto snapshot = currentProgressSnapshot();
        const bool workerProgressed = snapshot.completed > progressSnapshot_.completed
                                   || snapshot.heartbeat > progressSnapshot_.heartbeat;
        const bool fullyDrained = snapshot.submitted <= snapshot.completed;

        progressSnapshot_ = snapshot;
        if (workerProgressed || fullyDrained)
            stallStreakPolls_ = 0;
        else
            ++stallStreakPolls_;

        return stallStreakPolls_ * pollIntervalMs >= hangWindowMs;
    }

    /** Close the realtime gate instantly; the RT path immediately falls back
        to the validated deterministic dry contract. All OS work happens here,
        on the control plane. */
    void beginRecovery(bool forceTerminationExpected)
    {
        juce::ignoreUnused(forceTerminationExpected);
        invalidateEditorIdentity();
        active_.store(false, std::memory_order_release);
        realtimeGateOpen_.store(false, std::memory_order_release);
        health_.store(HealthState::Recovering, std::memory_order_release);
    }

    void attemptAutomaticRestart(std::uint32_t startupTimeoutMs)
    {
        if (health_.load(std::memory_order_acquire) != HealthState::Recovering)
            return;

        if (restartsInWindow() >= maxRestartsPerWindow_)
        {
            health_.store(HealthState::Failed, std::memory_order_release);
            return;
        }

        recordRestartAttempt();
        restartAttempts_.fetch_add(1, std::memory_order_relaxed);
        automaticRecoveryInProgress_ = true;

        // Retire the dead/hung worker and every owned OS resource. On a
        // bounded failure, stay Recovering and retry on a later tick.
        const auto drainMs = juce::jmin<std::uint32_t>(startupTimeoutMs, 3000u);
        if (! process_.retireWorkerAfterFailure(drainMs))
        {
            restartFailures_.fetch_add(1, std::memory_order_relaxed);
            automaticRecoveryInProgress_ = false;
            return;
        }
        if (process_.diagnostics().forcedTermination)
            forcedTerminations_.fetch_add(1, std::memory_order_relaxed);

        auto preparation = rebuildPreparation(startupTimeoutMs);
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        nextRestartWorkerPathForTest_.clear();   // single-shot fault injection
       #endif
        const auto result = prepare(preparation);
        automaticRecoveryInProgress_ = false;

        if (result == PrepareResult::Prepared)
        {
            restartSuccesses_.fetch_add(1, std::memory_order_relaxed);
            if (! recoveryActivationPending_)
                health_.store(HealthState::Healthy, std::memory_order_release);
        }
        else
        {
            restartFailures_.fetch_add(1, std::memory_order_relaxed);
            // Stay Recovering; the throttle promotes to Failed after the
            // bounded automatic budget is exhausted.
        }
    }

    PluginSandboxPreparation rebuildPreparation(std::uint32_t startupTimeoutMs) const
    {
        PluginSandboxPreparation preparation;
        preparation.sampleRate = preparedSampleRate_;
        preparation.blockSamples = static_cast<std::uint32_t>(
            juce::jmax(1, preparedBlockSamples_));
        preparation.mainInputChannels = static_cast<std::uint32_t>(
            juce::jmax(1, mainInputChannels_));
        preparation.mainOutputChannels = static_cast<std::uint32_t>(
            juce::jmax(1, mainOutputChannels_));
        preparation.maximumHostBlockSamples = static_cast<std::uint32_t>(
            juce::jmax(1, preparedMaximumHostBlockSamples_));
        preparation.startupTimeoutMs = startupTimeoutMs;
        preparation.pluginCreateTimeoutMs = preparedPluginCreateTimeoutMs_;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
         if (nextRestartWorkerPathForTest_.isNotEmpty())
             preparation.workerExecutablePathForTest = nextRestartWorkerPathForTest_;
         else if (workerExecutablePathForTest_.isNotEmpty())
             preparation.workerExecutablePathForTest = workerExecutablePathForTest_;
         // Fault requests are parent-owned one-shots. The next worker launch
         // consumes forceStateRestoreFailureForTest_ in prepare(); no fault
         // configuration is copied into the replacement preparation.
         preparation.audioFaultPoint = PluginSandboxAudioTestFaultPoint::None;
         preparation.failStateRestoreForTest = false;
       #endif
        return preparation;
    }

    /** Phase E1 replay onto a FRESH worker: saved state chunk first, then the
        newest host-known parameter values (which are authoritative over any
        older state). Called from prepare() before the worker is published
        operational. Returns true WITHOUT any protocol traffic when no shadow
        exists — the frozen Phase C/D default behavior. */
    bool restoreHostConfigurationForReplay(juce::String& error)
    {
        const bool hasState = stateShadow_.getSize() > 0;
        const bool hasParameters = ! parameterShadow_.empty();
        if (! hasState && ! hasParameters)
            return true;

        if (hasState
            && ! process_.restorePluginState(stateShadow_, error))
        {
           #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
            if (error == kPluginSandboxTestStateRestoreFailure)
                stateRestoreFailureCountForTest_.fetch_add(
                    1, std::memory_order_acq_rel);
           #endif
            return false;
        }

        for (const auto& [parameterId, value] : parameterShadow_)
        {
            float appliedValue = 0.0f;
            if (! process_.setPluginParameter(parameterId, value,
                                              appliedValue, error))
                return false;
        }
        return true;
    }

    struct AutomationBatchPublication
    {
        bool published = false;
        bool valid = false;
    };

    class PhaseBExactQuantumBackend final : public PluginSandboxExactQuantumBackendCore
    {
    public:
        void setTransport(PluginSandboxAudioTransportCore* transport) noexcept
        {
            transport_ = transport;
        }

        void setAutomationOwner(SandboxedPluginProxyCore* owner) noexcept
        {
            owner_ = owner;
        }

        PluginSandboxAudioTransportCore::ExchangeResult exchangeQuantum(
            const float* const* input,
            std::uint32_t inputChannels,
            float* const* output,
            std::uint32_t outputChannels,
            std::uint32_t quantumSamples) noexcept override
        {
            const bool automationActive = owner_ != nullptr
                                       && owner_->automationEnabled();
            const auto expectedSequence = automationActive
                ? owner_->automationExpectedSequence_ : 0;

            // Phase E2A: publish the automation batch for THIS quantum BEFORE
            // the audio submit (zero-event batch when nothing is staged).
            AutomationBatchPublication publication;
            if (automationActive)
                publication = owner_->publishPendingAutomationBatch(expectedSequence);

            if (transport_ == nullptr)
            {
                if (automationActive)
                    owner_->cancelAutomationBatch(expectedSequence);
                PluginSandboxAudioTransportCore::ExchangeResult result;
                result.submittedSequence = expectedSequence;
                result.automationBatchPublished = automationActive
                    ? publication.published : true;
                result.automationBatchValid = automationActive
                    ? publication.valid : true;
                result.workerUnavailable = true;
                return result;
            }

            auto result = transport_->exchangeBlock(input, inputChannels,
                                                    output, outputChannels,
                                                    quantumSamples, quantumSamples);
            if (automationActive)
            {
                result.automationBatchPublished = publication.published;
                result.automationBatchValid = publication.valid;
            }

            if (automationActive)
            {
                // Orphan-batch rollback: if the audio quantum was NOT actually
                // submitted (or the sequence mismatched), cancel the batch so
                // the sidecar slot returns Free for sequence+5 reuse. Logical
                // numbering is NOT renumbered — it was consumed either way.
                if (! result.submitted || result.submittedSequence != expectedSequence)
                    owner_->cancelAutomationBatch(expectedSequence);
                ++owner_->automationExpectedSequence_;
            }
            return result;
        }

    private:
        PluginSandboxAudioTransportCore* transport_ = nullptr;
        SandboxedPluginProxyCore* owner_ = nullptr;   // Phase E2A (additive)
    };

    class RealtimeCallScope
    {
    public:
        explicit RealtimeCallScope(SandboxedPluginProxyCore& owner) noexcept
            : owner_(owner)
        {
            if (! owner_.realtimeGateOpen_.load(std::memory_order_acquire))
                return;
            owner_.activeRealtimeCalls_.fetch_add(1, std::memory_order_acq_rel);
            if (! owner_.realtimeGateOpen_.load(std::memory_order_acquire))
            {
                owner_.activeRealtimeCalls_.fetch_sub(1, std::memory_order_release);
                return;
            }
            entered_ = true;
        }

        ~RealtimeCallScope()
        {
            if (entered_)
                owner_.activeRealtimeCalls_.fetch_sub(1, std::memory_order_release);
        }

        bool entered() const noexcept { return entered_; }

    private:
        SandboxedPluginProxyCore& owner_;
        bool entered_ = false;
    };

    bool waitForRealtimeDrain(std::uint32_t timeoutMs) const noexcept
    {
       #if JUCE_WINDOWS
        const auto deadline = GetTickCount64() + timeoutMs;
        while (activeRealtimeCalls_.load(std::memory_order_acquire) != 0)
        {
            if (GetTickCount64() >= deadline)
                return false;
            Sleep(1);
        }
       #else
        juce::ignoreUnused(timeoutMs);
       #endif
        return true;
    }

    const juce::PluginDescription description_;
    const juce::String pluginInstanceId_;
    std::atomic<LifecycleState> lifecycle_ { LifecycleState::Constructed };
    std::atomic<bool> prepared_ { false };
    std::atomic<bool> active_ { false };
    std::atomic<bool> bypassed_ { false };
    std::atomic<bool> realtimeGateOpen_ { false };
    std::atomic<std::uint32_t> activeRealtimeCalls_ { 0 };
    int mainInputChannels_ = 0;
    int mainOutputChannels_ = 0;
    int workerPluginLatencySamples_ = 0;
    int phaseBQuantumLatencySamples_ = 0;
    int preparedBlockSamples_ = 0;
    int preparedMaximumHostBlockSamples_ = 0;
    std::uint32_t preparedOutstandingDepth_ = 1;
    double preparedSampleRate_ = 0.0;
    std::uint32_t preparedStartupTimeoutMs_ = kPluginSandboxStartupTimeoutMs;
    std::uint32_t preparedPluginCreateTimeoutMs_ =
        kPluginSandboxPluginCreateTimeoutMs;
    std::uint32_t preparedEditorCreateTimeoutMs_ =
        kPluginSandboxEditorCreateTimeoutMs;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
     juce::String workerExecutablePathForTest_;
     juce::String nextRestartWorkerPathForTest_;
       std::atomic<bool> forceLiveParameterValuesFailureForTest_ { false };
       std::atomic<bool> forceStateRestoreFailureForTest_ { false };
       std::atomic<std::uint64_t> stateRestoreFailureCountForTest_ { 0 };
     #endif
    PluginSandboxFixedQuantumReblockerCore reblocker_;
    PhaseBExactQuantumBackend backend_;
    PluginSandboxProcessCore process_;

    // Phase F1 editor identity is parent metadata only. The vendor editor and
    // its native peer remain owned by the worker process.
    bool editorOpen_ = false;
    EditorInfo editorInfo_;
    std::uint64_t nextEditorRequestSequence_ = 0;
    EditorLifecycleSnapshot editorLifecycle_;
    mutable std::mutex editorStateMutex_;
    std::atomic<bool> editorStatusThreadRunning_ { false };
    std::thread editorStatusThread_;
    std::mutex editorStatusWaitMutex_;
    std::condition_variable editorStatusWake_;

    // ── Phase D health/recovery state (control plane only) ──
    std::atomic<HealthState> health_ { HealthState::Constructed };
    std::atomic<std::uint64_t> detectedDeaths_ { 0 };
    std::atomic<std::uint64_t> detectedStalls_ { 0 };
    std::atomic<std::uint64_t> restartAttempts_ { 0 };
    std::atomic<std::uint64_t> restartSuccesses_ { 0 };
    std::atomic<std::uint64_t> restartFailures_ { 0 };
    std::atomic<std::uint64_t> forcedTerminations_ { 0 };
    std::uint32_t stallStreakPolls_ = 0;
    ProgressSnapshot progressSnapshot_;
    std::uint32_t pollIntervalMs_ = kDefaultPollIntervalMs;
    std::uint32_t hangWindowMs_ = kDefaultHangWindowMs;
    std::uint32_t restartWindowMs_ = kDefaultRestartWindowMs;
    std::uint32_t maxRestartsPerWindow_ = kDefaultMaxRestartsPerWindow;
    std::array<std::uint64_t, kRestartHistoryCapacity> restartHistory_ {};
    std::size_t restartHistoryIndex_ = 0;
    bool automaticRecoveryInProgress_ = false;
    bool recoveryActivationPending_ = false;

    // ── Phase E1 parent-side runtime shadow (control plane only) ──
    // Latest successfully ACKED state chunk + latest successfully applied
    // parameter values. Replayed onto every fresh worker before it is
    // published operational. Empty = frozen Phase C/D behavior.
    juce::MemoryBlock stateShadow_;
    std::map<juce::String, float> parameterShadow_;

    // ── Phase E2A: realtime parameter-event transport primitive ──
    // E2 DISABLED default: automationRequested_ false → no mapping, no
    // staging, no publish — the exact frozen Phase C path.
    bool automationRequested_ = false;
    std::uint64_t automationHostPosition_ = 0;
    std::uint64_t automationExpectedSequence_ = 1;

    struct PendingAutomationEntry
    {
        std::uint64_t sequence = 0;
        bool active = false;
        bool overflow = false;
        std::uint32_t count = 0;
        PluginSandboxAutomationShared::SandboxAutomationEvent events[
            PluginSandboxAutomationShared::kMaxAutomationEventsPerQuantum] {};
    };
    // Bmax=2048 → at most 4 completed quanta + 1 new partial in one callback.
    std::array<PendingAutomationEntry, 5> automationPending_ {};

    PendingAutomationEntry* findPendingAutomationEntry(std::uint64_t sequence) noexcept
    {
        auto& slot = automationPending_[
            static_cast<std::size_t>(sequence
                % PluginSandboxAutomationShared::kAutomationBatchSlotCount)];
        if (slot.active && slot.sequence == sequence)
            return &slot;
        return nullptr;
    }

    PendingAutomationEntry* ensurePendingAutomationEntry(std::uint64_t sequence) noexcept
    {
        if (auto* existing = findPendingAutomationEntry(sequence))
            return existing;
        auto& slot = automationPending_[
            static_cast<std::size_t>(sequence
                % PluginSandboxAutomationShared::kAutomationBatchSlotCount)];
        if (slot.active)
            return nullptr;   // capacity exceeded — caller marks overflow
        slot = PendingAutomationEntry {};
        slot.sequence = sequence;
        slot.active = true;
        return &slot;
    }

    /** RT staging: translate callback-local events to absolute positions and
        stage them into per-quantum pending entries. Runs BEFORE the reblocker
        can emit any quantum for this callback. No allocation, no sorting
        (absolute monotonicity preserves offset order per quantum). */
    void stageAutomationEvents(
        const PluginSandboxAutomationShared::SandboxAutomationEvent* events,
        std::uint32_t eventCount) noexcept
    {
        using namespace PluginSandboxAutomationShared;
        constexpr std::uint64_t kQuantum = 512;   // frozen sandbox quantum Q
        for (std::uint32_t i = 0; i < eventCount; ++i)
        {
            const auto& event = events[i];
            const std::uint64_t absolute =
                automationHostPosition_ + event.sampleOffset;
            const std::uint64_t sequence = absolute / kQuantum + 1;
            const std::uint32_t local = static_cast<std::uint32_t>(absolute % kQuantum);

            auto* entry = ensurePendingAutomationEntry(sequence);
            if (entry == nullptr || entry->overflow)
            {
                // Capacity exceeded for this quantum: mark it invalid the same
                // way — an explicit overflow batch.
                if (entry == nullptr)
                    continue;   // slot stolen by a newer sequence — drop to the
                                // overflow path on publish (defensive; the
                                // 5-entry bound is provably sufficient)
                entry->overflow = true;
                continue;
            }
            if (entry->count >= kMaxAutomationEventsPerQuantum)
            {
                entry->overflow = true;
                continue;
            }
            entry->events[entry->count].parameterOrdinal = event.parameterOrdinal;
            entry->events[entry->count].sampleOffset = local;
            entry->events[entry->count].normalizedValue = event.normalizedValue;
            ++entry->count;
        }
    }

    /** Publish the pending batch (or an intentional zero-event batch) for the
        quantum the backend is about to submit. No-op when E2 is disabled. */
    AutomationBatchPublication publishPendingAutomationBatch(
        std::uint64_t sequence) noexcept
    {
        using namespace PluginSandboxAutomationShared;
        if (! automationEnabled())
            return {};
        auto* entry = findPendingAutomationEntry(sequence);
        if (entry == nullptr)
        {
            const bool published = process_.automationTransport().publishBatch(
                sequence, nullptr, 0, false);
            return { published, published };
        }
        const bool overflow = entry->overflow;
        const bool published = process_.automationTransport().publishBatch(
            sequence, entry->events, entry->count, entry->overflow);
        *entry = PendingAutomationEntry {};   // consumed
        return { published, published && ! overflow };
    }

    /** Cancel an orphaned batch after its audio submit failed. Clears the
        pending entry and releases the sidecar slot (ownership-validated). */
    void cancelAutomationBatch(std::uint64_t sequence) noexcept
    {
        using namespace PluginSandboxAutomationShared;
        if (! automationEnabled())
            return;
        process_.automationTransport().cancelBatch(sequence);
        if (auto* entry = findPendingAutomationEntry(sequence))
            *entry = PendingAutomationEntry {};
    }

    void resetAutomationStagingState() noexcept
    {
        automationHostPosition_ = 0;
        automationExpectedSequence_ = 1;
        for (auto& entry : automationPending_)
            entry = PendingAutomationEntry {};
    }
};

} // namespace DAW
