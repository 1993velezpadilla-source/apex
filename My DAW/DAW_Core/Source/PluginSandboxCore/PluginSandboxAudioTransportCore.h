#pragma once

#include "PluginSandboxAudioTransportShared.h"
#include "PluginSandboxProtocolCore.h"

#include <atomic>

namespace DAW {

class PluginSandboxAudioTransportCore
{
public:
    struct Configuration
    {
        std::uint32_t maxInputChannels = 2;
        std::uint32_t maxOutputChannels = 2;
        std::uint32_t maxSamples = 2048;
        std::uint32_t nominalBlockSamples = 512;
        double sampleRate = 48000.0;
        // Depth-aware consume window: exchange k consumes slot k-depth.
        // depth = ceil(maximumHostBlockSamples / nominalBlockSamples).
        // Must be in [1, kMaxOutstandingDepth]; 1 reproduces the legacy
        // one-quantum pipeline exactly.
        std::uint32_t outstandingQuantumDepth = 1;
    };

    struct ExchangeResult
    {
        std::uint64_t submittedSequence = 0;
        // Parent-local identity captured at the transport submission boundary.
        // This is the worker generation governing this exchange; status below
        // distinguishes an accepted submission from a failed attempt.
        std::uint64_t submittedGeneration = 0;
        std::uint64_t expectedSequence = 0;
        std::uint64_t completedSequence = 0;
        std::uint64_t completedGeneration = 0;
        std::uint32_t outputSamples = 0;
        std::uint32_t outputChannels = 0;
        bool submitted = false;
        bool remoteOutput = false;
        bool fallback = true;
        bool workerUnavailable = false;
        bool boundsRejected = false;
        // E2B parent-side delivery metadata. These fields describe the
        // automation publication paired with this audio exchange; they are
        // not part of the frozen shared-memory layout.
        bool automationBatchPublished = true;
        bool automationBatchValid = true;
    };

    struct Counters
    {
        std::uint64_t calls = 0;
        std::uint64_t submitted = 0;
        std::uint64_t submitMisses = 0;
        std::uint64_t completed = 0;
        std::uint64_t deadlineMisses = 0;
        std::uint64_t staleOutputsRejected = 0;
        std::uint64_t metadataRejected = 0;
        std::uint64_t fallbacks = 0;
        std::uint64_t boundsRejected = 0;
    };

    PluginSandboxAudioTransportCore() = default;
    ~PluginSandboxAudioTransportCore()
    {
        closeRealtimeGate();
        if (activeRealtimeCalls_.load(std::memory_order_acquire) == 0)
            releaseAfterWorkerStopped();
    }

    PluginSandboxAudioTransportCore(const PluginSandboxAudioTransportCore&) = delete;
    PluginSandboxAudioTransportCore& operator=(const PluginSandboxAudioTransportCore&) = delete;

    bool create(const juce::String& sessionToken,
                const Configuration& configuration,
                juce::String& error)
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxAudioShared;
        if (region_ != nullptr || mapping_.isValid())
        {
            error = "audio transport mapping is already prepared";
            return false;
        }
        if (! validConfiguration(configuration.maxInputChannels,
                                  configuration.maxOutputChannels,
                                  configuration.maxSamples,
                                  configuration.nominalBlockSamples,
                                  configuration.sampleRate)
            || ! validOutstandingDepth(configuration.outstandingQuantumDepth))
        {
            error = "invalid Phase B audio transport configuration";
            return false;
        }

        sessionToken_ = sessionToken;
        mappingName_ = "Local\\APEX.PluginSandbox.Audio." + sessionToken;
        mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                          0, static_cast<DWORD>(sizeof(SharedAudioTransportRegion)),
                                          mappingName_.toWideCharPointer()));
        if (! mapping_.isValid())
        {
            error = PluginSandboxWin32::windowsError("CreateFileMappingW");
            return false;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            error = "Phase B audio transport mapping name already exists";
            mapping_.reset();
            return false;
        }

        region_ = static_cast<SharedAudioTransportRegion*>(
            MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0,
                          sizeof(SharedAudioTransportRegion)));
        if (region_ == nullptr)
        {
            error = PluginSandboxWin32::windowsError("MapViewOfFile");
            mapping_.reset();
            return false;
        }

        std::memset(region_, 0, sizeof(SharedAudioTransportRegion));
        generation_ = static_cast<std::uint64_t>(sessionToken.substring(0, 16).getHexValue64());
        if (generation_ == 0)
            generation_ = 1;
        configuration_ = configuration;
        initialiseStoppedRegion();
        resetLocalState();
        return true;
       #else
        juce::ignoreUnused(sessionToken, configuration);
        error = "Phase B audio transport is Windows-only";
        return false;
       #endif
    }

    bool reconfigureAfterWorkerStopped(const Configuration& configuration,
                                       juce::String& error) noexcept
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxAudioShared;
        if (region_ == nullptr || realtimeGateOpen_.load(std::memory_order_acquire)
            || activeRealtimeCalls_.load(std::memory_order_acquire) != 0)
        {
            error = "audio transport must be gated and drained before reconfigure";
            return false;
        }
        if (! validConfiguration(configuration.maxInputChannels,
                                  configuration.maxOutputChannels,
                                  configuration.maxSamples,
                                  configuration.nominalBlockSamples,
                                  configuration.sampleRate)
            || ! validOutstandingDepth(configuration.outstandingQuantumDepth))
        {
            error = "invalid Phase B audio reconfiguration";
            return false;
        }

        configuration_ = configuration;
        ++generation_;
        if (generation_ == 0)
            ++generation_;
        initialiseStoppedRegion();
        resetLocalState();
        return true;
       #else
        juce::ignoreUnused(configuration, error);
        return false;
       #endif
    }

    void markWorkerAvailable(bool available) noexcept
    {
        workerAvailable_.store(available, std::memory_order_release);
       #if JUCE_WINDOWS
        if (region_ != nullptr)
            PluginSandboxAudioShared::storeRelease(&region_->workerOnline.value,
                                                   available ? 1 : 0);
       #endif
    }

    void openRealtimeGate() noexcept
    {
       #if JUCE_WINDOWS
        if (region_ != nullptr && workerAvailable_.load(std::memory_order_acquire))
        {
            PluginSandboxAudioShared::storeRelease(&region_->transportActive.value, 1);
            realtimeGateOpen_.store(true, std::memory_order_release);
        }
       #endif
    }

    void closeRealtimeGate() noexcept
    {
        realtimeGateOpen_.store(false, std::memory_order_release);
       #if JUCE_WINDOWS
        if (region_ != nullptr)
            PluginSandboxAudioShared::storeRelease(&region_->transportActive.value, 0);
       #endif
    }

    bool waitForRealtimeDrain(DWORD timeoutMs) const noexcept
    {
       #if JUCE_WINDOWS
        const auto deadline = GetTickCount64() + timeoutMs;
        while (activeRealtimeCalls_.load(std::memory_order_acquire) != 0)
        {
            if (GetTickCount64() >= deadline)
                return false;
            Sleep(1);
        }
        return true;
       #else
        juce::ignoreUnused(timeoutMs);
        return true;
       #endif
    }

    ExchangeResult exchangeBlock(const float* const* input,
                                 std::uint32_t inputChannels,
                                 float* const* output,
                                 std::uint32_t outputChannels,
                                 std::uint32_t inputSamples,
                                 std::uint32_t outputCapacitySamples) noexcept
    {
        ExchangeResult result;
        result.outputSamples = juce::jmin(juce::jmin(inputSamples,
                                                     outputCapacitySamples),
                                          configuration_.maxSamples);
        result.outputChannels = outputChannels;
        calls_.fetch_add(1, std::memory_order_relaxed);

        const auto applyDryFallback = [&]() noexcept
        {
            copyDryFallback(input, inputChannels, output, outputChannels,
                            result.outputSamples);
            recordFallback();
        };

       #if JUCE_WINDOWS
        RealtimeCallScope call(*this);
        if (! call.entered())
        {
            result.workerUnavailable = true;
            applyDryFallback();
            return result;
        }

        if (region_ == nullptr || ! workerAvailable_.load(std::memory_order_acquire))
        {
            result.workerUnavailable = true;
            applyDryFallback();
            return result;
        }

        if (inputSamples == 0 || inputSamples > configuration_.maxSamples
            || inputChannels == 0 || inputChannels > configuration_.maxInputChannels
            || outputChannels == 0 || outputChannels > configuration_.maxOutputChannels
            || outputCapacitySamples == 0)
        {
            boundsRejected_.fetch_add(1, std::memory_order_relaxed);
            result.boundsRejected = true;
            applyDryFallback();
            return result;
        }

        const auto sequence = nextSequence_++;
        result.submittedSequence = sequence;
        // Capture the generation before the submit attempt. A failed attempt
        // still needs this identity so its report can consume the matching
        // pending record without committing lastDelivered state.
        result.submittedGeneration = generation_;
        // Depth-aware consume: quantum k is resolved at exchange k+depth so
        // the worker receives one full host-callback period even when several
        // quanta are submitted back-to-back inside a single large callback.
        const auto depth = static_cast<std::uint64_t>(
            configuration_.outstandingQuantumDepth);
        result.expectedSequence = sequence > depth ? sequence - depth : 0;

        discardLateReadySlots(result.expectedSequence);
        if (result.expectedSequence > 0)
            tryConsume(result.expectedSequence, output, outputChannels,
                       outputCapacitySamples, result);

        if (result.expectedSequence > 0 && ! result.remoteOutput)
            deadlineMisses_.fetch_add(1, std::memory_order_relaxed);

        result.submitted = trySubmit(sequence, input, inputChannels,
                                     outputChannels, inputSamples);
        if (result.submitted)
            submitted_.fetch_add(1, std::memory_order_relaxed);
        else
            submitMisses_.fetch_add(1, std::memory_order_relaxed);

        result.fallback = ! result.remoteOutput;
        if (result.fallback)
            applyDryFallback();
        return result;
       #else
        juce::ignoreUnused(input, inputChannels, output, outputChannels,
                           inputSamples, outputCapacitySamples);
        result.workerUnavailable = true;
        applyDryFallback();
        return result;
       #endif
    }

    Counters counters() const noexcept
    {
        return { calls_.load(std::memory_order_relaxed),
                 submitted_.load(std::memory_order_relaxed),
                 submitMisses_.load(std::memory_order_relaxed),
                 completed_.load(std::memory_order_relaxed),
                 deadlineMisses_.load(std::memory_order_relaxed),
                 staleOutputsRejected_.load(std::memory_order_relaxed),
                 metadataRejected_.load(std::memory_order_relaxed),
                 fallbacks_.load(std::memory_order_relaxed),
                 boundsRejected_.load(std::memory_order_relaxed) };
    }

   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    /** Test-only RT-safe seam: make the next N host-to-worker slot claims
        fail without touching shared-memory state. The control plane arms it;
        the audio path only performs a bounded atomic decrement. */
    void forceNextSubmissionFailureForTest(std::uint32_t count = 1) noexcept
    {
        forcedSubmitFailuresForTest_.store(count, std::memory_order_release);
    }
   #endif

    std::uint64_t workerCompletedSequence() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr
            ? static_cast<std::uint64_t>(PluginSandboxAudioShared::loadAcquire(
                  &region_->workerCompletedSequence.value))
            : 0;
       #else
        return 0;
       #endif
    }

    std::uint64_t workerProcessedBlocks() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr
            ? static_cast<std::uint64_t>(PluginSandboxAudioShared::loadAcquire(
                  &region_->workerProcessedBlocks.value))
            : 0;
       #else
        return 0;
       #endif
    }

    /** Phase D: parent-side read of the existing frozen workerHeartbeat field.
        The worker increments this from its main loop; a live-but-hung worker
        stops incrementing it. No shared-memory layout change. */
    std::uint64_t workerHeartbeatCount() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr
            ? static_cast<std::uint64_t>(PluginSandboxAudioShared::loadAcquire(
                  &region_->workerHeartbeat.value))
            : 0;
       #else
        return 0;
       #endif
    }

    /** E1 diagnostics: parent-side read of the frozen workerMetadataErrors
        counter (used by the worker to encode E1 failure paths). */
    std::uint64_t workerMetadataErrorsCount() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr
            ? static_cast<std::uint64_t>(PluginSandboxAudioShared::loadAcquire(
                  &region_->workerMetadataErrors.value))
            : 0;
       #else
        return 0;
       #endif
    }

    std::uint64_t workerProcessTicks() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr
            ? static_cast<std::uint64_t>(PluginSandboxAudioShared::loadAcquire(
                  &region_->workerProcessTicks.value))
            : 0;
       #else
        return 0;
       #endif
    }

    std::uint64_t generation() const noexcept { return generation_; }
    int getTransportLatencySamples() const noexcept
    {
        return static_cast<int>(configuration_.nominalBlockSamples);
    }
    static int getTransportLatencySamples(int activeHostBlockSamples) noexcept
    {
        return activeHostBlockSamples > 0 ? activeHostBlockSamples : 0;
    }
    static constexpr int getTransportLatencyBlocks() noexcept { return 1; }
    bool isPrepared() const noexcept { return region_ != nullptr; }
    const juce::String& mappingName() const noexcept { return mappingName_; }
    std::size_t mappedBytes() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr ? sizeof(PluginSandboxAudioShared::SharedAudioTransportRegion) : 0;
       #else
        return 0;
       #endif
    }

    bool injectStaleGenerationOutputForSelfTest(std::uint64_t staleGeneration,
                                                std::uint64_t sequence,
                                                std::uint32_t channels,
                                                std::uint32_t samples,
                                                float recognizableValue) noexcept
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxAudioShared;
        if (region_ == nullptr || staleGeneration == 0 || staleGeneration == generation_
            || sequence == 0 || channels == 0
            || channels > configuration_.maxOutputChannels
            || samples == 0 || samples > configuration_.maxSamples)
            return false;

        for (auto& slot : region_->slots)
        {
            if (! compareExchange(&slot.state.value, SlotState::Free,
                                  SlotState::HostWriting))
                continue;
            slot.metadata.sequence = sequence;
            slot.metadata.generation = staleGeneration;
            slot.metadata.numSamples = samples;
            slot.metadata.inputChannels = channels;
            slot.metadata.outputChannels = channels;
            slot.metadata.flags = SlotFlagNone;
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                std::fill_n(channelData(slot.output, channel), samples,
                            recognizableValue + static_cast<float>(channel));
            storeRelease(&slot.state.value, static_cast<LONG>(SlotState::ReadyForHost));
            return true;
        }
       #else
        juce::ignoreUnused(staleGeneration, sequence, channels, samples,
                           recognizableValue);
       #endif
        return false;
    }

    void releaseAfterWorkerStopped() noexcept
    {
       #if JUCE_WINDOWS
        closeRealtimeGate();
        markWorkerAvailable(false);
        if (activeRealtimeCalls_.load(std::memory_order_acquire) != 0)
            return;
        if (region_ != nullptr)
        {
            UnmapViewOfFile(region_);
            region_ = nullptr;
        }
        mapping_.reset();
       #endif
    }

    static bool mappingExists(const juce::String& mappingName) noexcept
    {
       #if JUCE_WINDOWS
        PluginSandboxWin32::UniqueHandle mapping(
            OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName.toWideCharPointer()));
        return mapping.isValid();
       #else
        juce::ignoreUnused(mappingName);
        return false;
       #endif
    }

    /** The slot ring holds d outstanding quanta plus the one being submitted;
        reject any prepared depth the physical ring cannot serve. */
    static bool validOutstandingDepth(std::uint32_t depth) noexcept
    {
        return depth >= 1
            && depth <= PluginSandboxAudioShared::kMaxOutstandingDepth;
    }

private:
    class RealtimeCallScope
    {
    public:
        explicit RealtimeCallScope(PluginSandboxAudioTransportCore& owner) noexcept
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
        PluginSandboxAudioTransportCore& owner_;
        bool entered_ = false;
    };

    static void copyDryFallback(const float* const* input,
                                std::uint32_t inputChannels,
                                float* const* output,
                                std::uint32_t outputChannels,
                                std::uint32_t numSamples) noexcept
    {
        if (output == nullptr)
            return;
        for (std::uint32_t channel = 0; channel < outputChannels; ++channel)
        {
            auto* destination = output[channel];
            if (destination == nullptr)
                continue;
            if (input != nullptr && channel < inputChannels && input[channel] != nullptr)
                std::memcpy(destination, input[channel], sizeof(float) * numSamples);
            else
                std::fill_n(destination, numSamples, 0.0f);
        }
    }

   #if JUCE_WINDOWS
    void initialiseStoppedRegion() noexcept
    {
        using namespace PluginSandboxAudioShared;
        auto& header = region_->header;
        header.magic = kMagic;
        header.layoutVersion = kLayoutVersion;
        header.headerBytes = sizeof(SharedAudioTransportHeader);
        header.regionBytes = sizeof(SharedAudioTransportRegion);
        header.sampleFormat = kSampleFormatFloat32;
        header.slotCount = kSlotCount;
        header.physicalMaxChannels = kPhysicalMaxChannels;
        header.physicalMaxSamples = kPhysicalMaxSamples;
        header.configuredMaxInputChannels = configuration_.maxInputChannels;
        header.configuredMaxOutputChannels = configuration_.maxOutputChannels;
        header.configuredMaxSamples = configuration_.maxSamples;
        header.nominalBlockSamples = configuration_.nominalBlockSamples;
        header.sampleRate = configuration_.sampleRate;
        header.generation = generation_;
        std::memset(header.sessionToken, 0, sizeof(header.sessionToken));
        sessionToken_.copyToUTF8(header.sessionToken, sizeof(header.sessionToken));

        storeRelease(&region_->transportActive.value, 0);
        storeRelease(&region_->workerOnline.value, 0);
        storeRelease(&region_->workerHeartbeat.value, 0);
        storeRelease(&region_->workerCompletedSequence.value, 0);
        storeRelease(&region_->workerProcessedBlocks.value, 0);
        storeRelease(&region_->workerProcessTicks.value, 0);
        storeRelease(&region_->workerMetadataErrors.value, 0);
        for (auto& slot : region_->slots)
        {
            std::memset(&slot.metadata, 0, sizeof(slot.metadata));
            std::memset(slot.input, 0, sizeof(slot.input));
            std::memset(slot.output, 0, sizeof(slot.output));
            storeRelease(&slot.state.value, static_cast<LONG>(SlotState::Free));
        }
    }

    void resetLocalState() noexcept
    {
        nextSequence_ = 1;
        workerAvailable_.store(false, std::memory_order_release);
        realtimeGateOpen_.store(false, std::memory_order_release);
        calls_.store(0, std::memory_order_relaxed);
        submitted_.store(0, std::memory_order_relaxed);
        submitMisses_.store(0, std::memory_order_relaxed);
        completed_.store(0, std::memory_order_relaxed);
        deadlineMisses_.store(0, std::memory_order_relaxed);
        staleOutputsRejected_.store(0, std::memory_order_relaxed);
        metadataRejected_.store(0, std::memory_order_relaxed);
        fallbacks_.store(0, std::memory_order_relaxed);
        boundsRejected_.store(0, std::memory_order_relaxed);
    }

    void discardLateReadySlots(std::uint64_t expectedSequence) noexcept
    {
        using namespace PluginSandboxAudioShared;
        for (auto& slot : region_->slots)
        {
            if (slotState(slot) != SlotState::ReadyForHost)
                continue;
            const auto slotSequence = slot.metadata.sequence;
            const auto slotGeneration = slot.metadata.generation;
            if (slotGeneration != generation_
                || (expectedSequence > 0 && slotSequence < expectedSequence))
            {
                if (compareExchange(&slot.state.value, SlotState::ReadyForHost, SlotState::Free))
                    staleOutputsRejected_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    void tryConsume(std::uint64_t expectedSequence,
                    float* const* output,
                    std::uint32_t outputChannels,
                    std::uint32_t outputCapacitySamples,
                    ExchangeResult& result) noexcept
    {
        using namespace PluginSandboxAudioShared;
        auto& slot = region_->slots[expectedSequence % kSlotCount];
        if (slotState(slot) != SlotState::ReadyForHost)
            return;

        const auto metadata = slot.metadata;
        if (metadata.sequence > expectedSequence && metadata.generation == generation_)
            return;

        const bool valid = metadata.sequence == expectedSequence
                        && metadata.generation == generation_
                        && validSlotMetadata(*region_, metadata)
                        && metadata.flags == SlotFlagNone
                        && metadata.outputChannels == outputChannels
                        && metadata.numSamples <= outputCapacitySamples;

        if (valid)
        {
            for (std::uint32_t channel = 0; channel < outputChannels; ++channel)
                std::memcpy(output[channel], channelData(slot.output, channel),
                            sizeof(float) * metadata.numSamples);
            result.remoteOutput = true;
            result.fallback = false;
            result.completedSequence = metadata.sequence;
            result.completedGeneration = metadata.generation;
            result.outputSamples = metadata.numSamples;
            result.outputChannels = metadata.outputChannels;
            completed_.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            metadataRejected_.fetch_add(1, std::memory_order_relaxed);
            staleOutputsRejected_.fetch_add(1, std::memory_order_relaxed);
        }

        compareExchange(&slot.state.value, SlotState::ReadyForHost, SlotState::Free);
    }

    bool trySubmit(std::uint64_t sequence,
                   const float* const* input,
                   std::uint32_t inputChannels,
                   std::uint32_t outputChannels,
                   std::uint32_t numSamples) noexcept
    {
        using namespace PluginSandboxAudioShared;

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        auto forcedFailures = forcedSubmitFailuresForTest_.load(
            std::memory_order_acquire);
        while (forcedFailures > 0)
        {
            if (forcedSubmitFailuresForTest_.compare_exchange_weak(
                    forcedFailures, forcedFailures - 1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
                return false;
        }
       #endif

        auto& slot = region_->slots[sequence % kSlotCount];
        if (! compareExchange(&slot.state.value, SlotState::Free, SlotState::HostWriting))
            return false;

        slot.metadata.sequence = sequence;
        slot.metadata.generation = generation_;
        slot.metadata.numSamples = numSamples;
        slot.metadata.inputChannels = inputChannels;
        slot.metadata.outputChannels = outputChannels;
        slot.metadata.flags = SlotFlagNone;
        for (std::uint32_t channel = 0; channel < inputChannels; ++channel)
            std::memcpy(channelData(slot.input, channel), input[channel],
                        sizeof(float) * numSamples);

        storeRelease(&slot.state.value, static_cast<LONG>(SlotState::ReadyForWorker));
        return true;
    }
   #endif

    void recordFallback() noexcept
    {
        fallbacks_.fetch_add(1, std::memory_order_relaxed);
    }

   #if JUCE_WINDOWS
    PluginSandboxWin32::UniqueHandle mapping_;
    PluginSandboxAudioShared::SharedAudioTransportRegion* region_ = nullptr;
   #endif
    juce::String sessionToken_;
    juce::String mappingName_;
    Configuration configuration_;
    std::uint64_t generation_ = 0;
    std::uint64_t nextSequence_ = 1;
    std::atomic<bool> workerAvailable_ { false };
    std::atomic<bool> realtimeGateOpen_ { false };
    std::atomic<std::uint32_t> activeRealtimeCalls_ { 0 };
    std::atomic<std::uint64_t> calls_ { 0 };
    std::atomic<std::uint64_t> submitted_ { 0 };
    std::atomic<std::uint64_t> submitMisses_ { 0 };
    std::atomic<std::uint64_t> completed_ { 0 };
    std::atomic<std::uint64_t> deadlineMisses_ { 0 };
    std::atomic<std::uint64_t> staleOutputsRejected_ { 0 };
    std::atomic<std::uint64_t> metadataRejected_ { 0 };
    std::atomic<std::uint64_t> fallbacks_ { 0 };
    std::atomic<std::uint64_t> boundsRejected_ { 0 };
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    std::atomic<std::uint32_t> forcedSubmitFailuresForTest_ { 0 };
   #endif
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

} // namespace DAW
