#pragma once

#include "PluginSandboxAudioTransportShared.h"
#include "PluginSandboxAutomationTransportCore.h"   // Phase E2A (additive)
#include "PluginSandboxProtocolCore.h"
#include "PluginWorkerHostedPluginCore.h"

#include <atomic>

namespace DAW {

class PluginWorkerAudioTransportCore
{
public:
    void setHostedPlugin(PluginWorkerHostedPluginCore* hostedPlugin) noexcept
    {
        hostedPlugin_ = hostedPlugin;
    }

    /** Phase E2A: attach the automation sidecar and the pre-resolved compact
        parameter targets. Additive — when automation_ is null the worker uses
        the exact frozen whole-quantum path. */
    void setAutomationSources(PluginWorkerAutomationTransportCore* automation,
                              juce::AudioProcessorParameter* const* targets,
                              std::uint32_t targetCount) noexcept
    {
        automation_ = automation;
        targets_ = targets;
        targetCount_ = targetCount;
    }

    bool open(const PluginWorkerCommandLine& commandLine, juce::String& error)
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxAudioShared;
        if (! commandLine.audioTransportRequested)
            return true;

        mapping_.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
                                        commandLine.audioMappingName.toWideCharPointer()));
        if (! mapping_.isValid())
        {
            error = PluginSandboxWin32::windowsError("OpenFileMappingW");
            return false;
        }

        region_ = static_cast<SharedAudioTransportRegion*>(
            MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0,
                          sizeof(SharedAudioTransportRegion)));
        if (region_ == nullptr)
        {
            error = PluginSandboxWin32::windowsError("worker MapViewOfFile");
            mapping_.reset();
            return false;
        }

        const auto sessionUtf8 = commandLine.sessionToken.toRawUTF8();
        if (! validRegion(*region_, sessionUtf8))
        {
            error = "worker rejected invalid Phase B shared-memory layout";
            close();
            return false;
        }

        dspMode_ = commandLine.audioDspMode;
        delaySequence_ = commandLine.audioDelaySequence;
        delayMilliseconds_ = commandLine.audioDelayMilliseconds;
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
         faultPoint_.store(commandLine.audioFaultPoint, std::memory_order_release);
       #endif
        generation_ = region_->header.generation;
        enabled_ = true;
        storeRelease(&region_->workerOnline.value, 1);
        return true;
       #else
        juce::ignoreUnused(commandLine, error);
        return false;
       #endif
    }

    bool start() noexcept
    {
       #if JUCE_WINDOWS
        if (region_ == nullptr || ! PluginSandboxAudioShared::validRegion(*region_))
            return false;
        generation_ = region_->header.generation;
        if (automation_ != nullptr)
            automation_->refreshGenerationForAudioStart();
        delayed_ = false;
        enabled_ = true;
        PluginSandboxAudioShared::storeRelease(&region_->workerOnline.value, 1);
        return true;
       #else
        return false;
       #endif
    }

    void stop() noexcept
    {
       #if JUCE_WINDOWS
        enabled_ = false;
        if (region_ != nullptr)
            PluginSandboxAudioShared::storeRelease(&region_->workerOnline.value, 0);
       #endif
    }

    bool processOneAvailable() noexcept
    {
       #if JUCE_WINDOWS
        using namespace PluginSandboxAudioShared;
        if (! enabled_ || region_ == nullptr
            || loadAcquire(&region_->transportActive.value) == 0)
            return false;

        SharedAudioSlot* selected = nullptr;
        std::uint64_t selectedSequence = UINT64_MAX;
        for (auto& slot : region_->slots)
        {
            if (slotState(slot) != SlotState::ReadyForWorker)
                continue;
            if (slot.metadata.sequence < selectedSequence)
            {
                selected = &slot;
                selectedSequence = slot.metadata.sequence;
            }
        }
        if (selected == nullptr
            || ! compareExchange(&selected->state.value,
                                 SlotState::ReadyForWorker,
                                 SlotState::WorkerProcessing))
            return false;

       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
         if (consumeFaultPointForTest(
                 PluginSandboxAudioTestFaultPoint::AfterAudioSubmitBeforeAutomationApply))
         {
             TerminateProcess(GetCurrentProcess(),
                              PluginSandboxWin32::kE2CTestAfterAudioSubmitExitCode);
            return false;
        }
       #endif

        LARGE_INTEGER before {};
        LARGE_INTEGER after {};
        QueryPerformanceCounter(&before);

        if (dspMode_ == PluginSandboxAudioTestDspMode::DeadlineIdentity
            && ! delayed_ && selected->metadata.sequence == delaySequence_)
        {
            delayed_ = true;
            Sleep(delayMilliseconds_);
        }

        if (selected->metadata.generation != generation_
            || ! validSlotMetadata(*region_, selected->metadata))
        {
            selected->metadata.flags |= SlotFlagInvalidMetadata;
            increment(&region_->workerMetadataErrors.value);
        }
        else
        {
            const auto disposition = automationDispositionForQuantum(*selected);
            if (disposition == AutomationQuantumDisposition::Invalid)
            {
                // E2A invalid/mismatched automation batch: complete the quantum
                // with the frozen invalid-metadata marker so the parent rejects
                // it and engages the deterministic aligned dry fallback. The
                // ring keeps progressing — the slot is never stranded.
                selected->metadata.flags |= SlotFlagInvalidMetadata;
                increment(&region_->workerMetadataErrors.value);
            }
            else if (disposition == AutomationQuantumDisposition::RunWholeQuantum)
            {
                if (hostedPlugin_ != nullptr)
                    hostedPlugin_->process(*selected);
                else
                    processDeterministicDsp(*selected, dspMode_);

                if ((selected->metadata.flags & SlotFlagInvalidMetadata) != 0)
                    increment(&region_->workerMetadataErrors.value);
            }
            // ProcessedInSlices: the quantum was already fully processed with
            // exact sample-offset parameter application — nothing further.

           #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
             if (disposition == AutomationQuantumDisposition::ProcessedInSlices
                 && consumeFaultPointForTest(
                     PluginSandboxAudioTestFaultPoint::AfterAutomationApplyBeforeWorkerCommit))
             {
                 TerminateProcess(GetCurrentProcess(),
                                  PluginSandboxWin32::kE2CTestAfterAutomationApplyExitCode);
                return false;
            }
           #endif
        }

        QueryPerformanceCounter(&after);
        add(&region_->workerProcessTicks.value, after.QuadPart - before.QuadPart);
        increment(&region_->workerProcessedBlocks.value);
        storeRelease(&region_->workerCompletedSequence.value,
                     static_cast<LONG64>(selected->metadata.sequence));
        storeRelease(&selected->state.value, static_cast<LONG>(SlotState::ReadyForHost));
        return true;
       #else
        return false;
       #endif
    }

    void heartbeat() noexcept
    {
       #if JUCE_WINDOWS
        if (region_ != nullptr)
            PluginSandboxAudioShared::increment(&region_->workerHeartbeat.value);
       #endif
    }

    bool isOpen() const noexcept { return region_ != nullptr; }

    /** Phase E2A: classify and, when valid events exist, apply them at exact
        sample offsets through continuous sub-block segments. */
    enum class AutomationQuantumDisposition
    {
        RunWholeQuantum,   // no events / E2 disabled — callers run whole-Q path
        ProcessedInSlices, // events applied; quantum already fully processed
        Invalid            // caller must mark the quantum invalid (fallback)
    };

    AutomationQuantumDisposition automationDispositionForQuantum(
        PluginSandboxAudioShared::SharedAudioSlot& slot) noexcept
    {
        using namespace PluginSandboxAutomationShared;
        if (automation_ == nullptr)
            return AutomationQuantumDisposition::RunWholeQuantum;   // E2 disabled

        std::uint32_t eventCount = 0;
        const auto result = automation_->tryAcquire(
            static_cast<std::uint64_t>(slot.metadata.sequence),
            generation_, copiedEvents_, eventCount);

        if (result == PluginWorkerAutomationTransportCore::AcquireResult::Disabled)
            return AutomationQuantumDisposition::RunWholeQuantum;
        if (result != PluginWorkerAutomationTransportCore::AcquireResult::Acquired)
            return AutomationQuantumDisposition::Invalid;

        if (eventCount == 0)
            return AutomationQuantumDisposition::RunWholeQuantum;

        if (hostedPlugin_ == nullptr)
            return AutomationQuantumDisposition::Invalid;

        // Pre-validate every ordinal before touching any audio sample.
        for (std::uint32_t i = 0; i < eventCount; ++i)
        {
            const auto ordinal = copiedEvents_[i].parameterOrdinal;
            if (ordinal >= targetCount_ || targets_ == nullptr
                || targets_[ordinal] == nullptr)
                return AutomationQuantumDisposition::Invalid;
        }

        // Continuous sub-block application with same-offset grouping: all
        // events at an offset apply (in staged order) before that sample.
        const auto quantumSamples = static_cast<std::uint32_t>(
            slot.metadata.numSamples);
        std::uint32_t cursor = 0;
        std::uint32_t applied = 0;
        for (std::uint32_t i = 0; i < eventCount;)
        {
            const auto offset = copiedEvents_[i].sampleOffset;
            if (offset >= quantumSamples)
                return AutomationQuantumDisposition::Invalid;

            if (offset > cursor)
            {
                hostedPlugin_->processSlice(slot, static_cast<int>(cursor),
                                            static_cast<int>(offset - cursor));
                cursor = offset;
            }

            do
            {
                const auto ordinal = copiedEvents_[i].parameterOrdinal;
                targets_[ordinal]->setValue(copiedEvents_[i].normalizedValue);
                ++applied;
                ++i;
            }
            while (i < eventCount && copiedEvents_[i].sampleOffset == offset);
        }

        if (cursor < quantumSamples)
            hostedPlugin_->processSlice(slot, static_cast<int>(cursor),
                                        static_cast<int>(quantumSamples - cursor));

        automation_->recordAppliedEvents(applied);
        return AutomationQuantumDisposition::ProcessedInSlices;
    }

    double sampleRate() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr ? region_->header.sampleRate : 0.0;
       #else
        return 0.0;
       #endif
    }

    int maximumBlockSamples() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr
            ? static_cast<int>(region_->header.configuredMaxSamples) : 0;
       #else
        return 0;
       #endif
    }

    int inputChannels() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr
            ? static_cast<int>(region_->header.configuredMaxInputChannels) : 0;
       #else
        return 0;
       #endif
    }

    int outputChannels() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr
            ? static_cast<int>(region_->header.configuredMaxOutputChannels) : 0;
       #else
        return 0;
       #endif
    }

    std::uint64_t generation() const noexcept
    {
       #if JUCE_WINDOWS
        return region_ != nullptr ? region_->header.generation : 0;
       #else
        return 0;
       #endif
    }

    void close() noexcept
    {
       #if JUCE_WINDOWS
        stop();
        if (region_ != nullptr)
        {
            UnmapViewOfFile(region_);
            region_ = nullptr;
        }
        mapping_.reset();
       #endif
    }

   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    /** E1 diagnostics: direct region access for failure-path encoding.
        Test/diagnostic use only. */
    PluginSandboxAudioShared::SharedAudioTransportRegion* regionForTest() noexcept
    {
        return region_;
    }
   #endif

    /** E1 diagnostics: record a worker-side failure-path code in the frozen
        workerMetadataErrors region counter (read by the parent after the
        worker exits). Diagnostic only; no transport semantics. */
    void recordDiagnosticCode(std::int64_t code) noexcept
    {
       #if JUCE_WINDOWS
        if (region_ != nullptr)
            PluginSandboxAudioShared::add(&region_->workerMetadataErrors.value, code);
       #else
        juce::ignoreUnused(code);
       #endif
    }

    ~PluginWorkerAudioTransportCore() { close(); }

private:
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    bool consumeFaultPointForTest(
        PluginSandboxAudioTestFaultPoint requested) noexcept
    {
        auto expected = requested;
        return faultPoint_.compare_exchange_strong(
            expected,
            PluginSandboxAudioTestFaultPoint::None,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }
   #endif

   #if JUCE_WINDOWS
    PluginSandboxWin32::UniqueHandle mapping_;
    PluginSandboxAudioShared::SharedAudioTransportRegion* region_ = nullptr;
   #endif
    PluginSandboxAudioTestDspMode dspMode_ = PluginSandboxAudioTestDspMode::Identity;
    std::uint64_t generation_ = 0;
    std::uint64_t delaySequence_ = 0;
    DWORD delayMilliseconds_ = 0;
    bool delayed_ = false;
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
     static_assert(std::atomic<PluginSandboxAudioTestFaultPoint>::is_always_lock_free,
                   "E2C worker fault arm must use a lock-free atomic");
     std::atomic<PluginSandboxAudioTestFaultPoint> faultPoint_ {
         PluginSandboxAudioTestFaultPoint::None };
   #endif
    bool enabled_ = false;
    PluginWorkerHostedPluginCore* hostedPlugin_ = nullptr;

    // Phase E2A (additive): automation sidecar + pre-resolved parameter
    // targets. All null/frozen defaults when E2 is disabled.
    PluginWorkerAutomationTransportCore* automation_ = nullptr;
    juce::AudioProcessorParameter* const* targets_ = nullptr;
    std::uint32_t targetCount_ = 0;
    PluginSandboxAutomationShared::SandboxAutomationEvent copiedEvents_[
        PluginSandboxAutomationShared::kMaxAutomationEventsPerQuantum] {};
};

} // namespace DAW
