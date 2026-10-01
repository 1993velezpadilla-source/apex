#include "PluginInstanceCore.h"

#include "../PluginSandboxCore/SandboxedPluginProxyCore.h"
#include "../Automation/AutomationParameterKeyCore.h"
#include "../Automation/AutomationLaneStoreCore.h"

#include <array>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace DAW {

namespace
{
#if JUCE_WINDOWS
bool validateAndPresentSandboxEditorImpl(const SandboxedPluginProxyCore& proxy,
                                         juce::String& error)
{
    const auto& info = proxy.editorInfo();
    if (info.nativeWindowHandle == 0)
    {
        error = "worker returned an empty editor window handle";
        return false;
    }

    auto* hwnd = reinterpret_cast<HWND>(
        static_cast<std::uintptr_t>(info.nativeWindowHandle));
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE)
    {
        error = "worker editor window is no longer valid";
        return false;
    }

    DWORD ownerPid = 0;
    if (GetWindowThreadProcessId(hwnd, &ownerPid) == 0
        || ownerPid == 0
        || ownerPid != proxy.currentWorkerPid())
    {
        error = "worker editor window belongs to an unexpected process";
        return false;
    }

    if (info.workerGeneration == 0
        || info.workerGeneration != proxy.currentWorkerGeneration())
    {
        error = "worker editor window belongs to a stale generation";
        return false;
    }

    ShowWindow(hwnd, IsIconic(hwnd) != FALSE ? SW_RESTORE : SW_SHOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    return true;
}
#endif
}

#if JUCE_WINDOWS
bool PluginInstanceCore::validateAndPresentSandboxEditor(
    const SandboxedPluginProxyCore& proxy,
    juce::String& error)
{
    return validateAndPresentSandboxEditorImpl(proxy, error);
}
#endif

void PluginInstanceCore::openSandboxEditor(juce::Component* parent)
{
    juce::ignoreUnused(parent);
    auto logOpen = [&](const juce::String& stage, const juce::String& detail)
    {
        juce::Logger::writeToLog("[PluginInstanceCore][OpenTrace] plugin=\"" + getName()
            + "\" stage=" + stage
            + " detail=" + detail);
    };

#if JUCE_WINDOWS
    juce::String error;
    if (sandboxProxy_->isEditorOpen())
    {
        if (validateAndPresentSandboxEditor(*sandboxProxy_, error))
        {
            logOpen("reuseWorkerEditor", "windowValid=1 workerOwned=1");
            return;
        }

        // The worker may have lost its native peer without a control message
        // reaching the parent. Clear only the parent identity; the next
        // bounded create request lets the worker reconcile its own state.
        sandboxProxy_->invalidateEditorIdentity();
    }

    if (! sandboxProxy_->canRequestEditor())
    {
        error = "sandbox plugin is not active";
    }
    else if (sandboxProxy_->requestEditorOpen(error))
    {
        // F3 submit is intentionally non-blocking. If the worker completed a
        // zero-latency request before the ACK reached us, present it now;
        // otherwise the control-plane coordinator will publish READY and a
        // later open/show call will present the validated worker HWND.
        if (sandboxProxy_->isEditorOpen()
            && validateAndPresentSandboxEditor(*sandboxProxy_, error))
        {
            logOpen("success", "workerEditorReady=1 workerOwned=1");
            return;
        }

        const auto lifecycle = sandboxProxy_->editorLifecycle();
        logOpen("submitted", "workerGeneration="
            + juce::String(static_cast<juce::int64>(
                lifecycle.identity.workerGeneration))
            + " editorRequestId="
            + juce::String(static_cast<juce::int64>(
                lifecycle.identity.editorRequestId)));
        return;
    }

    logOpen("failed", "reason=sandboxEditorCreate detail=" + error);
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Sandboxed Editor Unavailable",
        "The sandboxed plug-in editor could not be opened.\n\n"
            + error
            + "\n\nThe plug-in remains sandboxed; no in-process fallback was attempted.");
#else
    logOpen("failed", "reason=sandboxEditorHostingIsWindowsOnly");
#endif
}

void PluginInstanceCore::closeSandboxEditor()
{
    juce::String error;
    if (! sandboxProxy_->requestEditorClose(error) && error.isNotEmpty())
        juce::Logger::writeToLog(
            "[PluginInstanceCore][CloseTrace] plugin=\"" + getName()
            + "\" sandbox editor close failed: " + error);
    if (onEditorClosed)
        onEditorClosed();
}

bool PluginInstanceCore::isSandboxEditorOpen() const noexcept
{
    return sandboxProxy_ != nullptr && sandboxProxy_->isEditorOpen();
}

struct PluginInstanceCore::SandboxEventScratch
{
    // 65 entries: event #65 reaches the frozen E2A explicit whole-batch
    // overflow path. Additional changes in one boundary are semantically
    // irrelevant — the entire affected quantum is already destined for
    // whole-batch invalidation.
    std::array<DAW::PluginSandboxAutomationShared::SandboxAutomationEvent, 65>
        events {};

    static constexpr std::size_t kDeliverySlotCount = 5;
    static constexpr std::size_t kMaxDeliveryRecordsPerQuantum = 64;

    struct DeliveryRecord
    {
        std::uint32_t parameterOrdinal = 0;
        float normalizedValue = 0.0f;
    };

    struct PendingDeliveryEntry
    {
        std::uint64_t workerGeneration = 0;
        std::uint64_t sequence = 0;
        std::uint32_t count = 0;
        bool active = false;
        std::array<DeliveryRecord, kMaxDeliveryRecordsPerQuantum> records {};
    };

    std::array<PendingDeliveryEntry, kDeliverySlotCount> deliveries {};

    PendingDeliveryEntry* findDeliveryEntry(std::uint64_t sequence) noexcept
    {
        auto& entry = deliveries[static_cast<std::size_t>(
            sequence % kDeliverySlotCount)];
        return entry.active && entry.sequence == sequence ? &entry : nullptr;
    }

    PendingDeliveryEntry* findDeliveryEntry(std::uint64_t workerGeneration,
                                             std::uint64_t sequence) noexcept
    {
        auto* entry = findDeliveryEntry(sequence);
        return entry != nullptr && entry->workerGeneration == workerGeneration
            ? entry : nullptr;
    }

    PendingDeliveryEntry* ensureDeliveryEntry(std::uint64_t workerGeneration,
                                              std::uint64_t sequence) noexcept
    {
        if (workerGeneration == 0 || sequence == 0)
            return nullptr;
        if (auto* existing = findDeliveryEntry(sequence))
            return existing->workerGeneration == workerGeneration
                ? existing : nullptr;

        auto& entry = deliveries[static_cast<std::size_t>(
            sequence % kDeliverySlotCount)];
        if (entry.active)
            return nullptr;  // bounded tracking window is still occupied
        entry = PendingDeliveryEntry {};
        entry.workerGeneration = workerGeneration;
        entry.sequence = sequence;
        entry.active = true;
        return &entry;
    }

    void appendDeliveryRecord(std::uint64_t workerGeneration,
                              std::uint64_t sequence,
                              std::uint32_t parameterOrdinal,
                              float normalizedValue) noexcept
    {
        auto* entry = ensureDeliveryEntry(workerGeneration, sequence);
        if (entry == nullptr
            || entry->count >= kMaxDeliveryRecordsPerQuantum)
            return;

        entry->records[entry->count++] = { parameterOrdinal, normalizedValue };
    }

    void clearDeliveryRecords() noexcept
    {
        for (auto& entry : deliveries)
            entry = PendingDeliveryEntry {};
    }
};

void PluginInstanceCore::clearSandboxAutomationBindings() noexcept
{
    // The binding table and both ordinal mirrors are all-or-nothing E2B state.
    // Resetting the count also invalidates any event contents left in the
    // preallocated scratch; the RT handoff never observes an event without a
    // positive count. This is control-plane state transition work.
    sandboxAutomationBindings_.clear();
    sandboxLastAutomationValues_.clear();
    sandboxLastDeliveredValues_.clear();
    sandboxKeyGeneration_ = ~0ull;
    sandboxStagedEventCount_ = 0;
    clearSandboxAutomationPendingDeliveryRecords();
}

void PluginInstanceCore::clearSandboxAutomationPendingDeliveryRecords() noexcept
{
    sandboxStagedEventCount_ = 0;
    if (sandboxEventScratch_ != nullptr)
        sandboxEventScratch_->clearDeliveryRecords();
}

bool PluginInstanceCore::rebuildSandboxAutomationBindings()
{
   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    ++sandboxAutomationRebuildCount_;
   #endif
    if (sandboxProxy_ == nullptr)
    {
        clearSandboxAutomationBindings();
        return true;
    }
    if (trackId_.isEmpty())
    {
        clearSandboxAutomationBindings();
        return true;
    }
    if (pluginSlotIndex_ < 0)
    {
        clearSandboxAutomationBindings();
        return true;
    }

    using KR = apex::automation::AutomationParameterKeyRegistry;
    auto& keyRegistry = KR::getInstance();
    const auto metadataGeneration = sandboxProxy_->currentGeneration();

    juce::String error;
    juce::Array<SandboxedPluginProxyCore::SandboxParameterInfo> metadata;
    if (! sandboxProxy_->fetchParameterMetadata(metadata, error))
    {
        // A failed metadata read may indicate a new/dead worker. Retaining the
        // previous table would allow old identities to produce events against
        // an unverified generation, so disarm the complete E2B state.
        clearSandboxAutomationBindings();
        return false;   // worker not ready — a later control-plane refresh retries
    }

    juce::Array<SandboxedPluginProxyCore::SandboxLiveParameterValue> liveValues;
    juce::String liveError;
    if (! sandboxProxy_->fetchLiveParameterValues(liveValues, liveError))
    {
        // A default is not a valid substitute for the worker's post-restore
        // value. Leave the sandbox automation sink unarmed until a later
        // control-plane refresh can obtain an exact live seed.
        clearSandboxAutomationBindings();
        return false;
    }

    // Both control-plane reads must describe the same live worker generation.
    // The protocol validates each response's session/generation envelope; this
    // second check closes the gap between the two reads before publication.
    const auto liveGeneration = sandboxProxy_->currentGeneration();

    const int componentUid = sandboxProxy_->getDescription().uniqueId;
    const auto keyGen = keyRegistry.getChangeGeneration();

    // Ordinal-indexed RT state (128 = frozen E2A worker target space).
    constexpr int kOrdinalSpace = 128;
    std::vector<SandboxAutomationBinding> candidateBindings;
    candidateBindings.reserve(static_cast<std::size_t>(metadata.size()));
    std::vector<float> candidateLastAutomationValues(
        static_cast<std::size_t>(kOrdinalSpace), 0.0f);
    std::vector<float> candidateLastDeliveredValues(
        static_cast<std::size_t>(kOrdinalSpace), -1.0f);

    for (const auto& info : metadata)
    {
        if (info.index < 0 || info.index >= kOrdinalSpace)
            continue;   // outside the frozen representable domain

        SandboxAutomationBinding binding;
        binding.coreParamId = "plugin." + juce::String(pluginSlotIndex_)
            + "." + pluginInstanceId_ + "." + info.parameterId;
        binding.apexKey = KR::pluginParamKey(trackId_, pluginSlotIndex_,
                                             getName(), info.parameterId,
                                             componentUid);
        binding.bridgeKey = "plugin." + trackId_ + ".core."
            + binding.coreParamId;
        binding.apexPid = keyRegistry.findID(binding.apexKey);
        if (binding.apexPid == apex::automation::kInvalidParameterID)
            binding.apexPid = keyRegistry.findID(
                KR::pluginParamKey(trackId_, pluginSlotIndex_,
                                   getName(), info.parameterId, 0));
        if (binding.apexPid == apex::automation::kInvalidParameterID)
            binding.apexPid = keyRegistry.getOrCreateID(binding.apexKey);
        binding.bridgePid = keyRegistry.findID(binding.bridgeKey);
        binding.keyGeneration = keyGen;
        binding.ordinal = static_cast<std::uint32_t>(info.index);
        binding.representable = true;

        // Exact seed: the ACTUAL live worker value — the sandbox equivalent
        // of in-process params[i]->getValue().
        float liveValue = 0.0f;
        bool foundLiveValue = false;
        for (const auto& live : liveValues)
            if (live.index == info.index)
            {
                liveValue = live.normalizedValue;
                foundLiveValue = true;
                break;
            }
        if (! foundLiveValue)
        {
            // Do not fabricate a smoother seed when the worker did not
            // return this metadata entry.
            clearSandboxAutomationBindings();
            return false;
        }
        candidateLastAutomationValues[
            static_cast<std::size_t>(info.index)] = liveValue;
        candidateLastDeliveredValues[
            static_cast<std::size_t>(info.index)] = liveValue;

        candidateBindings.push_back(std::move(binding));
    }

    if (metadataGeneration == 0
        || sandboxProxy_->currentGeneration() != metadataGeneration
        || sandboxProxy_->currentGeneration() != liveGeneration)
    {
        // A worker replacement/reprepare raced the control-plane read pair;
        // never publish a table whose seed belongs to the older generation.
        clearSandboxAutomationBindings();
        return false;
    }

    // Build the complete candidate before replacing the RT-visible table.
    // Reconfiguration is a control-plane safe-boundary operation; the swap
    // ensures readers never observe a half-filled vector or half-seeded mirror.
    sandboxAutomationBindings_.swap(candidateBindings);
    sandboxLastAutomationValues_.swap(candidateLastAutomationValues);
    sandboxLastDeliveredValues_.swap(candidateLastDeliveredValues);
    sandboxKeyGeneration_ = keyGen;
    clearSandboxAutomationPendingDeliveryRecords();

    // One-time control-plane scratch allocation: the RT producer only
    // indexes into this fixed array — zero heap activity on the audio thread.
    if (sandboxEventScratch_ == nullptr)
        sandboxEventScratch_ = std::make_unique<SandboxEventScratch>();
    return true;
}

bool PluginInstanceCore::completeSandboxRecovery()
{
    if (sandboxProxy_ == nullptr)
        return false;
    if (! sandboxProxy_->recoveryCompletionPending())
        return true;
    if (! rebuildSandboxAutomationBindings())
        return false;
    return sandboxProxy_->completeAutomaticRecoveryActivation();
}

bool PluginInstanceCore::sandboxAutomationDeliveryActive() const noexcept
{
    return sandboxProxy_ != nullptr
        && sandboxProxy_->automationEnabled();
}

bool PluginInstanceCore::commitSandboxAutomationDeliveryReport(
    std::uint64_t workerGeneration,
    std::uint64_t sequence,
    bool submitted,
    bool automationBatchPublished,
    bool automationBatchValid) noexcept
{
    if (sandboxEventScratch_ == nullptr)
        return false;

    // E2B authority is owned by the composite identity {generation, sequence}.
    // A sequence collision in a replacement generation must not find or clear
    // the current pending record.
    auto* pending = sandboxEventScratch_->findDeliveryEntry(
        workerGeneration, sequence);
    if (pending == nullptr)
        return false;

    const bool delivered = submitted
        && automationBatchPublished
        && automationBatchValid;
    if (delivered)
    {
        for (std::uint32_t record = 0;
             record < pending->count; ++record)
        {
            const auto& value = pending->records[record];
            if (value.parameterOrdinal < sandboxLastDeliveredValues_.size())
                sandboxLastDeliveredValues_[value.parameterOrdinal]
                    = value.normalizedValue;
        }
    }

    // A failed/unpublished/invalid batch is consumed by the E2A protocol and
    // must be retried from the canonical producer on a later callback; never
    // retain a record that could match a reused physical sequence slot.
    *pending = PluginInstanceCore::SandboxEventScratch::PendingDeliveryEntry {};
    return true;
}

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
bool PluginInstanceCore::sandboxConsumeAutomationDeliveryReportForTesting(
    std::uint64_t workerGeneration,
    std::uint64_t sequence,
    bool submitted,
    bool automationBatchPublished,
    bool automationBatchValid) noexcept
{
    return commitSandboxAutomationDeliveryReport(
        workerGeneration, sequence, submitted,
        automationBatchPublished, automationBatchValid);
}
#endif

void PluginInstanceCore::applySandboxedAutomationAtSample(
    const TrackID& trackId,
    const AutomationSnapshot* automationSnap,
    int64_t samplePosition,
    double sampleRate,
    double bpm,
    int numSamples) noexcept
{
    sandboxStagedEventCount_ = 0;   // stale-event safety: fresh per callback

    if (sandboxAutomationBindings_.empty()
        || ! sandboxAutomationDeliveryActive()
        || sandboxEventScratch_ == nullptr)
        return;

    auto& keyRegistry = apex::automation::AutomationParameterKeyRegistry::getInstance();
    auto& laneStore   = apex::automation::AutomationLaneStore::getInstance();
    const double ppqPosition = (sampleRate > 0.0 && bpm > 0.0)
        ? ((double)samplePosition / sampleRate) * (bpm / 60.0)
        : 0.0;

    const auto keyGen = keyRegistry.getChangeGeneration();
    if (sandboxKeyGeneration_ != keyGen)
    {
        for (auto& binding : sandboxAutomationBindings_)
        {
            binding.apexPid   = keyRegistry.findIDRT(binding.apexKey);
            binding.bridgePid  = keyRegistry.findIDRT(binding.bridgeKey);
            binding.keyGeneration = keyGen;
        }
        sandboxKeyGeneration_ = keyGen;
    }

    const auto deliverySequence =
        sandboxProxy_->automationSequenceForNextCallback();
    // Capture the owner generation while this callback is staging the event.
    // The transport submission reports the same captured generation on the
    // normal path; report consumption never relabels stale input from the
    // proxy's later currentGeneration().
    const auto deliveryGeneration = sandboxProxy_->currentGeneration();
    auto& stagedEvents = sandboxEventScratch_->events;
    for (auto& binding : sandboxAutomationBindings_)
    {
        const std::size_t ordinalIndex =
            static_cast<std::size_t>(binding.ordinal);
        float& lastVal = sandboxLastAutomationValues_[ordinalIndex];

        const float target = computeCanonicalTarget(
            binding, laneStore, automationSnap, trackId,
            samplePosition, ppqPosition, lastVal);

        float smoothedValue = target;
        if (std::abs(lastVal - target) > 0.0001f)
        {
            const float coeff = AutomationSmootherCore::makeCoeff(sampleRate, 0.010);
            smoothedValue = AutomationSmootherCore::advance(
                lastVal, target, coeff, numSamples);
        }
        lastVal = smoothedValue;

        if (! binding.representable)
            continue;   // outside frozen E2A — later E2B test owns policy

        // Emit only when the canonical value actually changed: rewriting the
        // exact same normalized value is effect-free.
        float& lastDelivered = sandboxLastDeliveredValues_[ordinalIndex];
        if (smoothedValue == lastDelivered)
            continue;

        if (sandboxStagedEventCount_
            < static_cast<std::uint32_t>(stagedEvents.size()))
        {
            auto& event = stagedEvents[sandboxStagedEventCount_++];
            event.parameterOrdinal = binding.ordinal;
            event.sampleOffset = 0;   // host-block boundary semantics
            event.normalizedValue = smoothedValue;
            sandboxEventScratch_->appendDeliveryRecord(
                deliveryGeneration, deliverySequence,
                binding.ordinal, smoothedValue);
        }
    }
}

bool PluginInstanceCore::sandboxProcessHandoff(juce::AudioBuffer<float>& buffer,
                                               int numSamples)
{
    if (sandboxProxy_ == nullptr)
        return false;
    PluginSandboxFixedQuantumReblockerCore::ProcessSummary summary;
    if (sandboxAutomationDeliveryActive()
        && sandboxStagedEventCount_ > 0
        && sandboxEventScratch_ != nullptr)
    {
        summary = sandboxProxy_->processBlockWithAutomation(
            buffer, numSamples, sandboxEventScratch_->events.data(),
            sandboxStagedEventCount_);
    }
    else
    {
        summary = sandboxProxy_->processBlock(buffer, numSamples);
    }

    if (sandboxEventScratch_ != nullptr)
    {
        for (std::uint32_t i = 0;
             i < summary.automationDeliveryReportCount; ++i)
        {
            const auto& report = summary.automationDeliveryReports[i];
            commitSandboxAutomationDeliveryReport(
                report.workerGeneration,
                report.sequence,
                report.submitted,
                report.automationBatchPublished,
                report.automationBatchValid);
        }
    }
    sandboxStagedEventCount_ = 0;   // consumed — never replays
    return true;
}

PluginInstanceCore::PluginInstanceCore(std::unique_ptr<juce::AudioPluginInstance> plugin)
    : plugin_(std::move(plugin))
    , lifetimeToken_(std::make_shared<int>(0))
{
    jassert(plugin_ != nullptr);
}

PluginInstanceCore::PluginInstanceCore(std::unique_ptr<SandboxedPluginProxyCore> proxy)
    : sandboxProxy_(std::move(proxy))
    , lifetimeToken_(std::make_shared<int>(0))
{
    jassert(sandboxProxy_ != nullptr);
    if (sandboxProxy_)
    {
        pluginInstanceId_ = sandboxProxy_->getPluginInstanceId();
        // Reflect the proxy's prepared configuration so a chain prepare with
        // identical values does not trigger a wasteful worker restart.
        sampleRate_ = sandboxProxy_->getPreparedSampleRate();
        sandboxQuantumSamples_ = sandboxProxy_->getPreparedBlockSamples();
        blockSize_ = sandboxProxy_->getPreparedMaximumHostBlockSamples();
    }
}

PluginInstanceCore::~PluginInstanceCore()
{
    unregisterParameterListeners();
    lifetimeToken_.reset();
    // Close the editor BEFORE releasing the plugin so the plugin editor
    // is destroyed while the plugin instance is still fully alive
    // (two-phase teardown: window/HWND first, then the editor).
    // closeEditor() asserts the message thread for the in-process shell. The
    // sandbox editor uses the bounded proxy close below so off-thread teardown
    // does not enter the parent-side JUCE window path.
    if (editorWindow_ != nullptr)
        closeEditor();
    if (sandboxProxy_ != nullptr && sandboxProxy_->isEditorOpen())
    {
        juce::String editorError;
        sandboxProxy_->closeEditor(500, editorError);
    }
    if (plugin_)
        plugin_->releaseResources();
    // Sandbox worker shutdown + reap happens in the proxy destructor.
    // PluginChainCore::makeSharedSandboxedPlugin() defers that deletion to
    // a control-plane thread; it must never run on the realtime thread.
}

juce::String PluginInstanceCore::getName() const
{
    if (sandboxProxy_) return sandboxProxy_->getDescription().name;
    return plugin_ ? plugin_->getName() : "";
}

juce::String PluginInstanceCore::getVendor() const
{
    if (sandboxProxy_) return sandboxProxy_->getDescription().manufacturerName;
    return plugin_ ? plugin_->getPluginDescription().manufacturerName : "";
}

juce::PluginDescription PluginInstanceCore::getDescription() const
{
    if (sandboxProxy_) return sandboxProxy_->getDescription();
    return plugin_ ? plugin_->getPluginDescription() : juce::PluginDescription{};
}

bool PluginInstanceCore::capturePluginState(juce::MemoryBlock& state,
                                            juce::String& error) const
{
    state.reset();
    error.clear();

    if (sandboxProxy_)
        return sandboxProxy_->captureState(state, error);

    if (! plugin_)
    {
        error = "Plugin instance is unavailable for state capture.";
        return false;
    }

    try
    {
        plugin_->getStateInformation(state);
        return true;
    }
    catch (const std::exception& e)
    {
        error = "Plugin state capture failed: " + juce::String(e.what());
    }
    catch (...)
    {
        error = "Plugin state capture failed (unknown exception).";
    }

    state.reset();
    return false;
}

bool PluginInstanceCore::restorePluginState(const juce::MemoryBlock& state,
                                            juce::String& error)
{
    error.clear();
    if (state.getSize() == 0)
        return true;

    if (sandboxProxy_)
    {
        if (! sandboxProxy_->restoreState(state, error))
            return false;

        // E2B seeds must describe the same live worker generation after the
        // E1 state operation.  Do not fabricate defaults on a failed read.
        if (! rebuildSandboxAutomationBindings())
        {
            error = "Sandbox state restored, but live automation bindings could not be reseeded.";
            return false;
        }
        return true;
    }

    if (! plugin_)
    {
        error = "Plugin instance is unavailable for state restore.";
        return false;
    }

    const auto result = DAW::safelySetStateInformation(
        plugin_.get(), state.getData(), static_cast<int>(state.getSize()));
    if (result.succeeded)
        return true;

    error = "Plugin state restore failed";
    if (result.exceptionCode != 0)
        error += " (exception=0x"
            + juce::String::toHexString(static_cast<int>(result.exceptionCode))
            + ")";
    return false;
}

juce::MemoryBlock PluginInstanceCore::getState() const
{
    if (sandboxProxy_)
        return sandboxProxy_->lastAuthoritativeState();

    juce::MemoryBlock block;
    if (plugin_)
    {
        try { plugin_->getStateInformation(block); }
        catch (...) { block.reset(); }
    }
    return block;
}

bool PluginInstanceCore::setState(const juce::MemoryBlock& block)
{
    juce::String error;
    return restorePluginState(block, error);
}

void PluginInstanceCore::setBypassed(bool b)
{
    bypassed_.store(b, std::memory_order_relaxed);
    if (sandboxProxy_)
        sandboxProxy_->setBypassed(b);
}

void PluginInstanceCore::reset()
{
    if (sandboxProxy_ && prepared_)
    {
        resetCount_.fetch_add(1, std::memory_order_relaxed);
        // Control-plane stream reset only: gate closed, drains callbacks,
        // flushes the reblocker and reopens on the new transport generation.
        const bool resetSucceeded = sandboxProxy_->resetStream(5000);
        clearSandboxAutomationPendingDeliveryRecords();
        if (resetSucceeded)
            rebuildSandboxAutomationBindings();
        return;
    }
    if (plugin_ && prepared_)
    {
        resetCount_.fetch_add(1, std::memory_order_relaxed);
        plugin_->reset();
    }
}

void PluginInstanceCore::prepareSandboxed(double sampleRate, int blockSize,
                                          const juce::Array<int>& enabledAuxInputBuses)
{
    juce::ignoreUnused(enabledAuxInputBuses);   // sidechain unsupported for sandboxed slots

    // The proxy was prepared before insertion (appendSandboxedPlugin). A chain
    // re-prepare with an identical rate/block must not restart the worker.
    if (sandboxProxy_->isPrepared()
        && juce::approximatelyEqual(sampleRate, sampleRate_)
        && blockSize == blockSize_)
    {
        prepared_ = true;
        activeMainInputChannels_ = sandboxProxy_->getMainInputChannels();
        activeMainOutputChannels_ = sandboxProxy_->getMainOutputChannels();
        activeProcessWidth_ = juce::jmax(activeMainInputChannels_, activeMainOutputChannels_);
        // Even an identical host prepare is a control-plane lifecycle
        // boundary. Re-read the current worker values so an E2B smoother never
        // carries a seed from an earlier state/replay operation.
        rebuildSandboxAutomationBindings();
        return;
    }

    PluginSandboxPreparation preparation;
    preparation.sampleRate = sampleRate;
    // The fixed sandbox quantum Q never changes with the host callback size.
    preparation.blockSamples = static_cast<std::uint32_t>(
        juce::jmax(1, sandboxQuantumSamples_));
    // The chain's prepared block size is the MAXIMUM host callback size the
    // adapter must accept; callbacks are reblocked into exact Q quanta.
    preparation.maximumHostBlockSamples = static_cast<std::uint32_t>(
        juce::jmax(1, blockSize));
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;

    const auto result = sandboxProxy_->isPrepared()
        ? sandboxProxy_->reprepare(preparation)
        : sandboxProxy_->prepare(preparation);
    prepared_ = (result == SandboxedPluginProxyCore::PrepareResult::Prepared);
    if (prepared_)
    {
        blockSize_ = static_cast<int>(preparation.maximumHostBlockSamples);
        sandboxQuantumSamples_ = static_cast<int>(preparation.blockSamples);
        activeMainInputChannels_  = sandboxProxy_->getMainInputChannels();
        activeMainOutputChannels_ = sandboxProxy_->getMainOutputChannels();
        activeProcessWidth_ = juce::jmax(activeMainInputChannels_, activeMainOutputChannels_);
        // SandboxedPluginProxyCore::prepare() has completed its fresh-worker
        // state/parameter replay before returning. Seed E2B from that worker,
        // not from the pre-reprepare parent mirror.
        rebuildSandboxAutomationBindings();
    }
    else
    {
        clearSandboxAutomationBindings();
    }
}

void PluginInstanceCore::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midi, int numSamples)
{
    if (HostedPluginIsolationCore::shouldBypassHostedDsp()) return;
    if (sandboxProxy_)
    {
        juce::ignoreUnused(midi);
        if (!prepared_) return;
        if (numSamples <= 0 || numSamples > buffer.getNumSamples() || numSamples > blockSize_)
        {
            jassertfalse;
            return;
        }
        // The proxy owns bypass: it must keep producing latency-aligned
        // dry output while bypassed, so this path intentionally does not
        // early-return on the bypass flag.
        sandboxProcessHandoff(buffer, numSamples);
        return;
    }
    if (!plugin_ || !prepared_ || bypassed_.load(std::memory_order_relaxed)) return;
    if (numSamples <= 0 || numSamples > buffer.getNumSamples() || numSamples > blockSize_)
    {
        jassertfalse;
        return;
    }
    try
    {
        juce::AudioBuffer<float> activeView(buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), numSamples);
        processBlockInternal(activeView, midi);
    }
    catch (...)
    {
        // Plugin access violation (SEH) on the audio thread — bypass permanently
        bypassed_.store(true, std::memory_order_relaxed);
        juce::Logger::writeToLog("[APEX-SEH] plugin=\"" + getName()
            + "\" crashed in processBlockInternal() — auto-bypassing");
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    }
}

void PluginInstanceCore::processBlockForced(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi, int numSamples)
{
    if (HostedPluginIsolationCore::shouldBypassHostedDsp()) return;
    if (sandboxProxy_)
    {
        juce::ignoreUnused(midi);
        if (!prepared_) return;
        if (numSamples <= 0 || numSamples > buffer.getNumSamples() || numSamples > blockSize_)
        {
            jassertfalse;
            return;
        }
        sandboxProcessHandoff(buffer, numSamples);
        return;
    }
    if (!plugin_ || !prepared_) return;
    if (numSamples <= 0 || numSamples > buffer.getNumSamples() || numSamples > blockSize_)
    {
        jassertfalse;
        return;
    }
    try
    {
        juce::AudioBuffer<float> activeView(buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), numSamples);
        processBlockInternal(activeView, midi);
    }
    catch (...)
    {
        bypassed_.store(true, std::memory_order_relaxed);
        juce::Logger::writeToLog("[APEX-SEH] plugin=\"" + getName()
            + "\" crashed in processBlockForced() — auto-bypassing");
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    }
}

void PluginInstanceCore::processBlockCombinedForced(juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midi, int numSamples)
{
    if (HostedPluginIsolationCore::shouldBypassHostedDsp()) return;
    if (sandboxProxy_)
    {
        juce::ignoreUnused(midi);
        if (!prepared_) return;
        if (numSamples <= 0 || numSamples > buffer.getNumSamples() || numSamples > blockSize_)
        {
            jassertfalse;
            return;
        }
        sandboxProcessHandoff(buffer, numSamples);
        return;
    }
    if (!plugin_ || !prepared_) return;
    if (numSamples <= 0 || numSamples > buffer.getNumSamples() || numSamples > blockSize_)
    {
        jassertfalse;
        return;
    }
    try
    {
        juce::AudioBuffer<float> activeView(buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), numSamples);
        processBlockCombinedInternal(activeView, midi);
    }
    catch (...)
    {
        bypassed_.store(true, std::memory_order_relaxed);
        juce::Logger::writeToLog("[APEX-SEH] plugin=\"" + getName()
            + "\" crashed in processBlockCombinedForced() — auto-bypassing");
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    }
}

int PluginInstanceCore::getLatencySamples() const
{
    if (sandboxProxy_) return sandboxProxy_->getEffectiveLatencySamples();
    return plugin_ ? plugin_->getLatencySamples() : 0;
}

} // namespace DAW
