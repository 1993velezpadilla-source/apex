#pragma once

#include "PluginSandboxAutomationTransportShared.h"

namespace DAW {

// ═══════════════════════════════════════════════════════════════════════════
// PHASE E2A — realtime parameter-event transport primitive (additive).
//
// Parent side: publishes bounded event batches bound to exact audio quantum
// sequences; cancels orphaned batches when the corresponding audio submit
// fails. Worker side: acquires a batch only while it exactly owns the
// matching sequence, validates it, and applies events at exact sample offsets
// with the canonical non-notifying parameter setter.
//
// E2 DISABLED (default): no mapping exists, nothing publishes, nothing reads —
// the exact frozen Phase C path is preserved.
// ═══════════════════════════════════════════════════════════════════════════

#if JUCE_WINDOWS
class PluginSandboxAutomationTransportCore
{
public:
    PluginSandboxAutomationTransportCore() = default;
    ~PluginSandboxAutomationTransportCore() { releaseAfterWorkerStopped(); }

    PluginSandboxAutomationTransportCore(const PluginSandboxAutomationTransportCore&) = delete;
    PluginSandboxAutomationTransportCore& operator=(const PluginSandboxAutomationTransportCore&) = delete;

    bool create(const juce::String& sessionToken,
                std::uint64_t generation,
                juce::String& error)
    {
        using namespace PluginSandboxAutomationShared;
        if (region_ != nullptr || mapping_.isValid())
        {
            error = "automation transport mapping is already prepared";
            return false;
        }
        if (sessionToken.length() != 32 || generation == 0)
        {
            error = "invalid E2A automation mapping identity";
            return false;
        }

        sessionToken_ = sessionToken;
        mappingName_ = "Local\\APEX.PluginSandbox.Automation." + sessionToken;
        mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                          0, static_cast<DWORD>(sizeof(AutomationTransportRegion)),
                                          mappingName_.toWideCharPointer()));
        if (! mapping_.isValid())
        {
            error = PluginSandboxWin32::windowsError("CreateFileMappingW automation");
            return false;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            error = "E2A automation mapping name already exists";
            mapping_.reset();
            return false;
        }

        region_ = static_cast<AutomationTransportRegion*>(
            MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0,
                          sizeof(AutomationTransportRegion)));
        if (region_ == nullptr)
        {
            error = PluginSandboxWin32::windowsError("MapViewOfFile automation");
            mapping_.reset();
            return false;
        }

        std::memset(region_, 0, sizeof(AutomationTransportRegion));
        auto& header = region_->header;
        header.magic = kAutomationMagic;
        header.layoutVersion = kAutomationLayoutVersion;
        header.headerBytes = sizeof(AutomationBatchHeader);
        header.regionBytes = sizeof(AutomationTransportRegion);
        header.batchSlotCount = kAutomationBatchSlotCount;
        header.maxEventsPerBatch = kMaxAutomationEventsPerQuantum;
        header.maxParameters = kMaxAutomationParameters;
        header.generation = generation;
        std::memset(header.sessionToken, 0, sizeof(header.sessionToken));
        sessionToken_.copyToUTF8(header.sessionToken, sizeof(header.sessionToken));

        storeRelease(&region_->transportActive.value, 0);
        storeRelease(&region_->latestPublishedSequence.value, 0);
        storeRelease(&region_->parentOverflowRejected.value, 0);
        storeRelease(&region_->workerInvalidBatches.value, 0);
        storeRelease(&region_->workerAppliedEvents.value, 0);
        for (auto& slot : region_->slots)
        {
            storeRelease(&slot.state.value, static_cast<LONG>(AutomationBatchState::Free));
            storeRelease(&slot.audioSequence.value, 0);
            storeRelease(&slot.eventCount.value, 0);
            std::memset(slot.events, 0, sizeof(slot.events));
        }
        return true;
    }

    void openRealtimeGate() noexcept
    {
        if (region_ != nullptr)
            PluginSandboxAutomationShared::storeRelease(&region_->transportActive.value, 1);
    }

    void closeRealtimeGate() noexcept
    {
        if (region_ != nullptr)
            PluginSandboxAutomationShared::storeRelease(&region_->transportActive.value, 0);
    }

    /** Reset the existing sidecar for a new audio-transport generation while
        the worker audio loop is stopped. The mapping identity and frozen E2A
        layout remain unchanged; only the generation, counters, and bounded
        batch slots are reinitialized. */
    bool reconfigureAfterWorkerStopped(std::uint64_t generation) noexcept
    {
        using namespace PluginSandboxAutomationShared;
        if (region_ == nullptr || generation == 0)
            return false;

        closeRealtimeGate();
        region_->header.generation = generation;
        storeRelease(&region_->latestPublishedSequence.value, 0);
        storeRelease(&region_->parentOverflowRejected.value, 0);
        storeRelease(&region_->workerInvalidBatches.value, 0);
        storeRelease(&region_->workerAppliedEvents.value, 0);
        for (auto& slot : region_->slots)
        {
            storeRelease(&slot.state.value,
                         static_cast<LONG>(AutomationBatchState::Free));
            storeRelease(&slot.audioSequence.value, 0);
            storeRelease(&slot.eventCount.value, 0);
            std::memset(slot.events, 0, sizeof(slot.events));
        }
        return true;
    }

    /** Publish a complete batch for audio quantum `sequence`. events may be
        nullptr when eventCount == 0. `overflow` publishes the explicit
        invalid marker. Returns false when the slot is not Free (a stale batch
        was never canceled — caller bug) without corrupting shared state. */
    bool publishBatch(std::uint64_t sequence,
                      const PluginSandboxAutomationShared::SandboxAutomationEvent* events,
                      std::uint32_t eventCount,
                      bool overflow)
    {
        using namespace PluginSandboxAutomationShared;
        if (region_ == nullptr || sequence == 0)
            return false;

        auto& slot = region_->slots[sequence % kAutomationBatchSlotCount];
        if (! compareExchange(&slot.state.value,
                              AutomationBatchState::Free,
                              AutomationBatchState::Writing))
            return false;

        if (overflow)
        {
            storeRelease(&slot.eventCount.value, static_cast<LONG>(kOverflowMarker));
            InterlockedIncrement64(&region_->parentOverflowRejected.value);
        }
        else
        {
            const auto count = juce::jmin<std::uint32_t>(
                eventCount, kMaxAutomationEventsPerQuantum);
            for (std::uint32_t i = 0; i < count; ++i)
                slot.events[i] = events != nullptr
                    ? events[i] : SandboxAutomationEvent {};
            storeRelease(&slot.eventCount.value, static_cast<LONG>(count));
        }

        storeRelease(&slot.audioSequence.value, static_cast<LONG64>(sequence));
        storeRelease(&slot.state.value, static_cast<LONG>(AutomationBatchState::Ready));
        storeRelease(&region_->latestPublishedSequence.value,
                     static_cast<LONG64>(sequence));
        return true;
    }

    /** Cancel an orphaned batch AFTER its audio submit failed. Ownership is
        validated: only the batch whose audioSequence == sequence is released;
        a newer batch is never touched. */
    void cancelBatch(std::uint64_t sequence) noexcept
    {
        using namespace PluginSandboxAutomationShared;
        if (region_ == nullptr || sequence == 0)
            return;

        auto& slot = region_->slots[sequence % kAutomationBatchSlotCount];
        if (loadAcquire(&slot.audioSequence.value) != static_cast<LONG64>(sequence))
            return;   // not ours anymore — never clear a newer batch

        compareExchange(&slot.state.value, AutomationBatchState::Ready,
                        AutomationBatchState::Free);
    }

    bool isPrepared() const noexcept { return region_ != nullptr; }
    const juce::String& mappingName() const noexcept { return mappingName_; }
    std::uint64_t generation() const noexcept
    {
        return region_ != nullptr ? region_->header.generation : 0;
    }

    struct Diagnostics
    {
        std::uint64_t latestPublishedSequence = 0;
        std::uint64_t parentOverflowRejected = 0;
        std::uint64_t workerInvalidBatches = 0;
        std::uint64_t workerAppliedEvents = 0;
    };

    Diagnostics diagnostics() const noexcept
    {
        if (region_ == nullptr)
            return {};
        return { static_cast<std::uint64_t>(
                     PluginSandboxAutomationShared::loadAcquire(
                         &region_->latestPublishedSequence.value)),
                 static_cast<std::uint64_t>(
                     PluginSandboxAutomationShared::loadAcquire(
                         &region_->parentOverflowRejected.value)),
                 static_cast<std::uint64_t>(
                     PluginSandboxAutomationShared::loadAcquire(
                         &region_->workerInvalidBatches.value)),
                 static_cast<std::uint64_t>(
                     PluginSandboxAutomationShared::loadAcquire(
                         &region_->workerAppliedEvents.value)) };
    }

    void releaseAfterWorkerStopped() noexcept
    {
        if (region_ != nullptr)
        {
            UnmapViewOfFile(region_);
            region_ = nullptr;
        }
        mapping_.reset();
    }

private:
    PluginSandboxWin32::UniqueHandle mapping_;
    PluginSandboxAutomationShared::AutomationTransportRegion* region_ = nullptr;
    juce::String sessionToken_;
    juce::String mappingName_;
};

// ── Worker side ──────────────────────────────────────────────────────────────
class PluginWorkerAutomationTransportCore
{
public:
    enum class AcquireResult
    {
        Acquired,
        Disabled,
        Missing,
        Mismatch,
        Invalid
    };

    PluginWorkerAutomationTransportCore() = default;
    ~PluginWorkerAutomationTransportCore() { close(); }

    PluginWorkerAutomationTransportCore(const PluginWorkerAutomationTransportCore&) = delete;
    PluginWorkerAutomationTransportCore& operator=(const PluginWorkerAutomationTransportCore&) = delete;

    bool open(const juce::String& mappingName,
              const juce::String& sessionToken,
              juce::String& error)
    {
        using namespace PluginSandboxAutomationShared;
        mapping_.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
                                        mappingName.toWideCharPointer()));
        if (! mapping_.isValid())
        {
            error = PluginSandboxWin32::windowsError("worker OpenFileMappingW automation");
            return false;
        }
        region_ = static_cast<AutomationTransportRegion*>(
            MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0,
                          sizeof(AutomationTransportRegion)));
        if (region_ == nullptr)
        {
            error = PluginSandboxWin32::windowsError("worker MapViewOfFile automation");
            mapping_.reset();
            return false;
        }
        const auto sessionUtf8 = sessionToken.toRawUTF8();
        if (! validRegion(*region_, sessionUtf8))
        {
            const auto& header = region_->header;
            error = "worker rejected invalid E2A automation region"
                " magic=" + juce::String::toHexString(
                    static_cast<juce::int64>(header.magic))
                + " layout=" + juce::String(static_cast<int>(header.layoutVersion))
                + " headerBytes=" + juce::String(static_cast<int>(header.headerBytes))
                + " regionBytes=" + juce::String(static_cast<int>(header.regionBytes))
                + " slots=" + juce::String(static_cast<int>(header.batchSlotCount))
                + " maxEvents=" + juce::String(static_cast<int>(header.maxEventsPerBatch))
                + " maxParams=" + juce::String(static_cast<int>(header.maxParameters))
                + " generation=" + juce::String(static_cast<juce::int64>(header.generation))
                + " sessionTerm=" + juce::String(static_cast<int>(
                    static_cast<unsigned char>(header.sessionToken[32])));
            close();
            return false;
        }
        generation_ = region_->header.generation;
        quantumSamples_ = 512;   // frozen sandbox quantum
        return true;
    }

    bool isOpen() const noexcept { return region_ != nullptr; }
    std::uint64_t generation() const noexcept { return generation_; }

    /** AudioStart follows the parent-side generation reset over the control
        pipe. Refresh the worker-side expected generation before processing. */
    void refreshGenerationForAudioStart() noexcept
    {
        if (region_ != nullptr)
            generation_ = region_->header.generation;
    }

    /** Acquire the automation batch for audio quantum `sequence`. On success
        the caller receives a validated copy in `out` (preallocated storage —
        no allocation). The shared slot returns to Free. */
    AcquireResult tryAcquire(std::uint64_t sequence,
                             std::uint64_t expectedGeneration,
                             PluginSandboxAutomationShared::SandboxAutomationEvent* outEvents,
                             std::uint32_t& outEventCount) noexcept
    {
        using namespace PluginSandboxAutomationShared;
        outEventCount = 0;
        if (region_ == nullptr || sequence == 0)
            return AcquireResult::Disabled;
        if (expectedGeneration != 0 && expectedGeneration != generation_)
            return AcquireResult::Mismatch;
        if (loadAcquire(&region_->transportActive.value) == 0)
            return AcquireResult::Disabled;

        auto& slot = region_->slots[sequence % kAutomationBatchSlotCount];
        if (! compareExchange(&slot.state.value,
                              AutomationBatchState::Ready,
                              AutomationBatchState::WorkerReading))
            return AcquireResult::Missing;

        if (loadAcquire(&slot.audioSequence.value) != static_cast<LONG64>(sequence))
        {
            compareExchange(&slot.state.value,
                            AutomationBatchState::WorkerReading,
                            AutomationBatchState::Free);
            return AcquireResult::Mismatch;
        }

        const auto count = static_cast<std::uint32_t>(
            loadAcquire(&slot.eventCount.value));
        bool valid = true;
        if (count == kOverflowMarker)
        {
            outEventCount = 0;
            valid = false;   // explicit overflow marker → invalid batch
        }
        else
        {
            if (! validBatchEvents(slot.events, count, quantumSamples_))
                valid = false;
            else if (outEvents != nullptr && count > 0)
                std::memcpy(outEvents, slot.events,
                            sizeof(SandboxAutomationEvent) * count);
            outEventCount = valid ? count : 0;
        }

        // Consume the batch regardless; diagnostics reflect classification.
        if (! valid)
            InterlockedIncrement64(&region_->workerInvalidBatches.value);
        storeRelease(&slot.audioSequence.value, 0);
        storeRelease(&slot.eventCount.value, 0);
        compareExchange(&slot.state.value,
                        AutomationBatchState::WorkerReading,
                        AutomationBatchState::Free);
        return valid ? AcquireResult::Acquired : AcquireResult::Invalid;
    }

    void recordAppliedEvents(std::uint64_t count) noexcept
    {
        if (region_ != nullptr && count > 0)
            InterlockedAdd64(&region_->workerAppliedEvents.value,
                             static_cast<LONG64>(count));
    }

    void close() noexcept
    {
        if (region_ != nullptr)
        {
            UnmapViewOfFile(region_);
            region_ = nullptr;
        }
        mapping_.reset();
        generation_ = 0;
    }

private:
    PluginSandboxWin32::UniqueHandle mapping_;
    PluginSandboxAutomationShared::AutomationTransportRegion* region_ = nullptr;
    std::uint64_t generation_ = 0;
    std::uint32_t quantumSamples_ = 512;
};
#endif

} // namespace DAW
