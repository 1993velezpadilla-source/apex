#pragma once
#include <JuceHeader.h>
#include "../PluginStorageCore/PluginDescriptionPersistenceCore.h"
#include "PluginInstanceCore.h"
#include "BypassCrossfadeCore.h"
#include "PluginPlayheadInfoCore.h"
#include "../AutomationCore/PluginSlotMixCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../AutomationCore/AutomationSmootherCore.h"
#include "../PluginScanCore/PluginScanAuditLogCore.h"
#include "../PluginSafetyCore/PluginSafeLoadWrapperCore.h"
#include "../PluginSandboxCore/PluginSandboxPreparationCore.h"
#include "../PluginUICore/PluginUserFacingFailureMessageCore.h"
#include "../Automation/AutomationSystemCore.h"
#include "../Automation/AutomationParameterKeyCore.h"
#include <cstdint>
#include <array>
#include <memory>
#include <unordered_map>

#ifndef APEX_AUDIO_DEBUG_LOGS
#define APEX_AUDIO_DEBUG_LOGS 0
#endif

namespace DAW {

// Forward declaration only: sandboxed chain members are defined in
// PluginChainCore.cpp so windows.h stays out of UI translation units.
class SandboxedPluginProxyCore;

/** Control-plane options for one plugin insertion/replacement operation.
    The execution mode is the only new policy input.  Publication and the
    persisted C7 layout hint are retained here so the canonical dispatcher
    cannot be called with ambiguous raw booleans or borrowed layout pointers. */
struct PluginInsertOptions final
{
    PluginExecutionMode executionMode = PluginExecutionMode::InProcess;
    bool publishAndNotify = true;
    juce::ValueTree restoredLayoutTree;
};

struct PluginChainDiagSnapshot
{
    int processedBlocks = 0;
    int totalPrepareCalls = 0;
    int totalResetCalls = 0;
    int firstPluginSamples = 0;
    bool firstPluginBypassed = false;
    juce::String firstPluginName;
};

/**
 * PluginChainCore
 *
 * Nucleus: an ordered chain of insert plugins for one track.
 * NO slot limit — dynamically sized vector.
 *
 * Thread ownership:
 *   loadPlugin / removePlugin / moveSlot / copySlot → message thread only
 *   processBlock → audio thread only
 *   prepare / releaseResources → message thread, before/after audio starts
 *
 * Audio-thread safety — lock-free snapshot swap:
 *   The message thread builds an immutable Snapshot of
 *   shared_ptr<PluginInstanceCore> and publishes it atomically.
 *   The audio thread loads the current snapshot at block start and
 *   processes the entire block from that stable list.
 *   Zero locks, zero skips, no dry-pass glitches.
 *   Old snapshots stay alive via shared_ptr refcount until the audio
 *   thread drops its reference at the next block boundary.
 */
class PluginChainCore
{
public:
    static constexpr int kAutomationSliceSamples = 64;

    /** Immutable snapshot of the plugin chain for lock-free audio-thread use. */
    struct Snapshot
    {
        std::vector<std::shared_ptr<PluginInstanceCore>> slots;
        std::vector<BypassCrossfadeCore*> bypassCores;
        // Built with the immutable snapshot so the audio thread never has to
        // construct the slot-mix parameter ID while evaluating automation.
        std::vector<juce::String> slotMixParameterIds;
    };

private:
    // Automation bindings belong to mutable processor instances, unlike the
    // immutable order snapshot. Readers enter without waiting. A control-plane
    // edit closes admission and drains existing readers before rebinding.
    // Sequential consistency makes the admission/count handshake unambiguous.
    class RealtimeReadScope
    {
    public:
        explicit RealtimeReadScope(PluginChainCore& owner) noexcept : owner_(owner)
        {
            if (owner_.controlEditPending_.load()) return;
            owner_.activeRealtimeReaders_.fetch_add(1);
            if (owner_.controlEditPending_.load())
                owner_.activeRealtimeReaders_.fetch_sub(1);
            else
                entered_ = true;
        }
        ~RealtimeReadScope()
        {
            if (entered_) owner_.activeRealtimeReaders_.fetch_sub(1);
        }
        explicit operator bool() const noexcept { return entered_; }
    private:
        PluginChainCore& owner_;
        bool entered_ = false;
        JUCE_DECLARE_NON_COPYABLE(RealtimeReadScope)
    };

    class ControlEditScope
    {
    public:
        explicit ControlEditScope(PluginChainCore& owner) : owner_(owner)
        {
            if (owner_.controlEditDepth_ > 0)
            {
                ++owner_.controlEditDepth_;
                entered_ = true;
                return;
            }
            owner_.controlEditPending_.store(true);
            const auto start = juce::Time::getMillisecondCounter();
            while (owner_.activeRealtimeReaders_.load() != 0)
            {
                if (juce::Time::getMillisecondCounter() - start >= 2000)
                {
                    owner_.controlEditPending_.store(false);
                    return; // no mutation occurred; retain the valid chain
                }
                juce::Thread::yield(); // control plane only
            }
            owner_.controlEditDepth_ = 1;
            entered_ = true;
        }
        ~ControlEditScope()
        {
            if (entered_ && --owner_.controlEditDepth_ == 0)
                owner_.controlEditPending_.store(false);
        }
        explicit operator bool() const noexcept { return entered_; }
    private:
        PluginChainCore& owner_;
        bool entered_ = false;
        JUCE_DECLARE_NON_COPYABLE(ControlEditScope)
    };

    struct RetiredSnapshot
    {
        std::shared_ptr<const Snapshot> snapshot;
    };

    struct RetiredBypassCore
    {
        std::weak_ptr<const Snapshot> protectingSnapshot;
        std::unique_ptr<BypassCrossfadeCore> core;
    };

    /**
     * A chain can be destroyed after it has published an empty replacement
     * while an audio reader still owns the previous Snapshot.  The chain
     * object cannot retain the raw bypass targets after its destructor returns,
     * so its preallocated retirement group is handed to the process-level
     * control-plane reclaimer.  The group is linked without allocation at the
     * handoff; reclamation remains conditional on every retained Snapshot
     * becoming quiescent.
     */
    struct RetiredBypassShutdownGroup
    {
        std::vector<RetiredBypassCore> retiredCores;
        std::vector<std::unique_ptr<BypassCrossfadeCore>> activeCores;
        std::vector<RetiredSnapshot> snapshots;
        RetiredBypassShutdownGroup* next = nullptr;
    };

public:

    PluginChainCore()
        : shutdownGroup_(std::make_unique<RetiredBypassShutdownGroup>())
    {
        publishSnapshot();
    }

    void setAutomationContext(const TrackID& trackId,
                              PluginAutomationGestureCore* gestureCore,
                              LastTouchedPluginParameterCore* lastTouchedCore) noexcept
    {
        ControlEditScope edit(*this);
        if (!edit) return;
        trackId_ = trackId;
        gestureCore_ = gestureCore;
        lastTouchedCore_ = lastTouchedCore;
        refreshAutomationContexts();
    }

    void setAutomationManager(AutomationManagerCore* manager) noexcept
    {
        automationManager_ = manager;
    }

    void setPlayheadInfoCore(PluginPlayheadInfoCore* playheadInfo) noexcept
    {
        playheadInfo_ = playheadInfo;

        for (auto& slot : slots_)
            attachPlayhead(slot.get());
    }

    ~PluginChainCore()
    {
        closeAllEditors();
        releaseResources();
        publishEmptySnapshot();

        // AutomationGestureBridge is a process-lifetime singleton that stores
        // non-owning AudioProcessor pointers. Detach and null every parameter
        // binding while the plugin instances are still alive; otherwise its
        // atexit destructor dereferences freed processors during shutdown.
        for (int i = 0; i < (int) slots_.size(); ++i)
            removeSlotAutomation(i);

        // The published Snapshot contains raw bypass-core pointers.  Move all
        // bypass owners and every retired Snapshot to the process-level
        // control-plane reclaimer before the chain's vectors are destroyed;
        // an audio-like reader may still hold the old Snapshot after this
        // chain object is cleared by project replacement or shutdown.
        handoffBypassRetirementForDestruction();

        retiredSlots_.clear();
        slots_.clear();
    }

    // ── Lifecycle ────────────────────────────────────────────────────────

    void prepare(double sampleRate, int blockSize)
    {
        updateActiveSidechainBusConfig();
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
        offlinePrepared_ = false;

        for (int i = 0; i < (int) slots_.size(); ++i)
        {
            if (auto& slot = slots_[(size_t) i])
            {
                // Some third-party processors reset their program/parameters
                // in prepareToPlay(). Preserve the user's opaque state across
                // a fallback-rate -> real-device re-prepare.
                const auto state = slot->getState();
                slot->prepare(sampleRate, blockSize, getEnabledAuxInputBusesForSlot(i));
                if (state.getSize() > 0)
                    slot->setState(state);
            }
        }
        for (auto& core : bypassCores_)
            if (core) core->prepare(sampleRate);

        // C1+SC: pre-allocate every scratch buffer used by the sidechain/
        // bypass processing path to the worst-case channel/sample capacity on
        // this NON-REALTIME thread. The audio callback only verifies capacity
        // and never resizes these buffers.
        prepareSidechainScratchCapacity(blockSize);

        // Re-seed automation smoothers from the restored parameter values
        // before the chain becomes visible to realtime processing.
        refreshAutomationContexts();
        publishSnapshot();
    }

    void prepareForOffline(double sampleRate, int blockSize)
    {
        if (sampleRate <= 0.0 || blockSize <= 0)
        {
            jassertfalse;
            return;
        }

        updateActiveSidechainBusConfig();
        sampleRate_ = sampleRate;
        blockSize_ = blockSize;
        offlinePreparedSampleRate_ = sampleRate;
        offlinePreparedBlockSize_ = blockSize;
        offlinePrepared_ = true;
        offlineDebugBlocksRemaining_ = 0;

        for (int i = 0; i < (int) slots_.size(); ++i)
            if (auto& slot = slots_[(size_t) i])
                slot->prepareForOffline(sampleRate, blockSize, getEnabledAuxInputBusesForSlot(i));
        for (auto& core : bypassCores_)
            if (core) core->prepare(sampleRate);

        // Offline export may use larger block sizes than the realtime device.
        // Allocate the full scratch set (all channels, capacity >= blockSize)
        // on this non-realtime thread AFTER the slots have negotiated their
        // offline bus layouts, so the computed worst-case channel count is
        // authoritative before any offline processing begins.
        prepareSidechainScratchCapacity(blockSize);

        publishSnapshot();
    }

    void restoreRealtimePrepare(double sampleRate, int blockSize)
    {
        if (sampleRate <= 0.0 || blockSize <= 0)
            return;

        updateActiveSidechainBusConfig();
        sampleRate_ = sampleRate;
        blockSize_ = blockSize;
        offlinePrepared_ = false;
        offlinePreparedSampleRate_ = 0.0;
        offlinePreparedBlockSize_ = 0;
        offlineDebugBlocksRemaining_ = 0;

        for (int i = 0; i < (int) slots_.size(); ++i)
            if (auto& slot = slots_[(size_t) i])
                slot->restoreRealtimePrepare(sampleRate, blockSize, getEnabledAuxInputBusesForSlot(i));
        for (auto& core : bypassCores_)
            if (core) core->prepare(sampleRate);

        // Re-establish the full scratch capacity for the realtime device on
        // this non-realtime thread AFTER the slots re-negotiated their
        // realtime bus layouts (transition back from offline export).
        prepareSidechainScratchCapacity(blockSize);

        publishSnapshot();
    }

    void releaseResources()
    {
        for (auto& slot : slots_)
            if (slot) slot->releaseResources();
        for (auto& slot : retiredSlots_)
            if (slot) slot->releaseResources();
    }

    void closeAllEditors()
    {
        for (auto& slot : slots_)
            if (slot) slot->closeEditor();
        for (auto& slot : retiredSlots_)
            if (slot) slot->closeEditor();
    }

    void reset()
    {
        for (auto& slot : slots_)
            if (slot) slot->reset();
        for (auto& core : bypassCores_)
            if (core) core->reset();
    }

    // ── Slot management (message thread) ─────────────────────────────────

    int getNumSlots() const { return (int)slots_.size(); }
    double getPreparedSampleRate() const noexcept { return sampleRate_; }
    int getPreparedBlockSize() const noexcept { return blockSize_; }
    bool supportsOfflineRender(juce::String& error) const
    {
        for (int i = 0; i < static_cast<int>(slots_.size()); ++i)
        {
            const auto* slot = slots_[static_cast<std::size_t>(i)].get();
            if (slot != nullptr && slot->isSandboxed())
            {
                error = "Track plugin slot " + juce::String(i)
                    + " ('" + slot->getName()
                    + "') is sandboxed; offline processing is not supported yet.";
                return false;
            }
        }
        return true;
    }
    const juce::Array<int>& getEnabledAuxInputBusesForSlot(int slotIndex) const noexcept
    {
        auto it = activeSidechainAuxInputBuses_.find(slotIndex);
        return it != activeSidechainAuxInputBuses_.end() ? it->second : emptyAuxInputBusList_;
    }

    /** C6-sidechain-transaction: true when the given auxiliary input bus is
        COMMITTED in the active config for the slot (i.e. the bus was
        negotiated and actually enabled — verifyActiveSidechainBuses prunes
        anything the plugin rejected). The routing layer uses this to keep
        "route ACTIVE" and "plugin bus enabled" in agreement. */
    bool isAuxInputBusActive(int slotIndex, int bus) const noexcept
    {
        const auto& buses = getEnabledAuxInputBusesForSlot(slotIndex);
        return buses.contains(bus);
    }

    /** Apply the active sidechain configuration, rejecting sandbox targets
        explicitly because the current worker protocol has no auxiliary-bus
        transport.  Valid non-sandbox targets in the same update still apply. */
    bool setActiveSidechainBusConfig(
        const std::unordered_map<int, juce::Array<int>>& sidechainAuxInputBuses,
        juce::String& error)
    {
        error.clear();
        auto acceptedConfig = sidechainAuxInputBuses;
        for (const auto& [slotIndex, buses] : sidechainAuxInputBuses)
        {
            if (buses.isEmpty()
                || slotIndex < 0
                || slotIndex >= static_cast<int>(slots_.size()))
                continue;

            const auto* slot = slots_[static_cast<std::size_t>(slotIndex)].get();
            if (slot == nullptr || !slot->isSandboxed())
                continue;

            acceptedConfig.erase(slotIndex);
            const auto message = "Sandboxed plugin slot " + juce::String(slotIndex)
                + " ('" + slot->getName()
                + "') does not support sidechain input in this build.";
            if (error.isNotEmpty())
                error += " ";
            error += message;
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::setActiveSidechainBusConfig rejected sandbox target"
                    " slotIndex=" + juce::String(slotIndex)
                    + " error=\"" + message + "\"");
        }

        const auto oldActiveConfig = activeSidechainAuxInputBuses_;
        sidechainAuxInputBuses_ = acceptedConfig;
        updateActiveSidechainBusConfig();

        if (sampleRate_ > 0.0 && blockSize_ > 0)
        {
            reprepareSlotsWithChangedSidechainBuses(oldActiveConfig, activeSidechainAuxInputBuses_);

            // C6-sidechain-negotiation: re-validate the ACTIVE config against
            // the plugin's REAL negotiated state. A plugin/wrapper may reject
            // or skip enabling an auxiliary bus during setBusesLayout;
            // leaving the entry would silently deliver no sidechain signal
            // (the "silent sidechain" defect). Prune such buses so the active
            // config always mirrors the negotiated truth.
            verifyActiveSidechainBuses();

            // Release-visible commit forensic (message thread only).
            {
                juce::String committed;
                for (auto& [slot, buses] : activeSidechainAuxInputBuses_)
                    for (int b : buses)
                        committed += juce::String(slot) + ":" + juce::String(b) + ",";
                juce::Logger::writeToLog("[APEX-SC-VERIFY] chain=" + getDebugName()
                    + " committedAux=" + committed);
            }

            // The negotiated bus layouts may now require MORE channels (e.g.
            // an enabled stereo sidechain bus on a previously 2-channel slot).
            // Grow the complete scratch set HERE — this method is only ever
            // invoked from the message thread inside the realtime-drained
            // reprepare phase (see ApplicationCore::syncPluginChainSidechainBusConfig)
            // so the audio thread never observes a resizing buffer.
            prepareSidechainScratchCapacity(blockSize_);
        }

        return error.isEmpty();
    }

    /** Compatibility wrapper for existing control-plane callers. */
    void setActiveSidechainBusConfig(
        const std::unordered_map<int, juce::Array<int>>& sidechainAuxInputBuses)
    {
        juce::String error;
        if (! setActiveSidechainBusConfig(sidechainAuxInputBuses, error)
            && error.isNotEmpty())
        {
            juce::Logger::writeToLog("[SIDECHAIN] " + error);
        }
    }

    /** C1 pre-check: would setActiveSidechainBusConfig(newConfig) actually
     *  re-prepare any slot (setBusesLayout/prepareToPlay on a live plugin)?
     *  Pure query — no mutation; message thread only. Conservative: may
     *  over-report (extra drain) but never under-reports, so the caller can
     *  skip the realtime drain whenever nothing would change. */
    bool wouldSidechainBusConfigChange(
        const std::unordered_map<int, juce::Array<int>>& newConfig) const noexcept
    {
        for (int i = 0; i < (int) slots_.size(); ++i)
        {
            const auto& oldBuses = findSidechainBusList(activeSidechainAuxInputBuses_, i, emptyAuxInputBusList_);
            const auto& newBuses = findSidechainBusList(newConfig, i, emptyAuxInputBusList_);
            if (! sidechainBusListsEqual(oldBuses, newBuses))
                return true;
        }
        return false;
    }

    void setDebugName(const juce::String& name) { debugName_ = name; }
    juce::String getDebugName() const { return debugName_; }
    juce::String getStableDebugId() const
    {
        return juce::String::toHexString((juce::int64) reinterpret_cast<std::uintptr_t>(this));
    }

    juce::String getPluginNamesForDebug() const
    {
        juce::StringArray names;
        for (const auto& slot : slots_)
            if (slot) names.add(slot->getName());
        return names.joinIntoString(", ");
    }

    void resetOfflineDebugLogging(int blocks) noexcept
    {
        offlineDebugBlocksRemaining_ = juce::jmax(0, blocks);
    }

    /** Canonical control-plane insertion dispatcher.  Mode is resolved before
        either ownership path constructs a real plugin.  Returns slot index or
        -1. */
    int appendPlugin(const juce::PluginDescription& desc,
                     const PluginInsertOptions& options,
                     juce::AudioPluginFormatManager& formatManager,
                     juce::String& errorMsg)
    {
        jassert(sampleRate_ > 0.0 && "sampleRate_ read before prepareToPlay");
        jassert(blockSize_  > 0   && "blockSize_ read before prepareToPlay");
        if (sampleRate_ <= 0.0 || blockSize_ <= 0)
        {
            errorMsg = "Audio engine is not prepared yet.";
            return -1;
        }

        if (options.executionMode == PluginExecutionMode::Sandboxed)
        {
            // The format manager belongs to the in-process creation branch.
            // A sandboxed insertion must never consult it to create a parent
            // AudioPluginInstance.
            juce::ignoreUnused(formatManager);

            PluginSandboxPreparation preparation;
            if (options.restoredLayoutTree.isValid()
                && options.restoredLayoutTree.hasType("Layout"))
            {
                const auto inputChannels = juce::jlimit(
                    0, 64,
                    static_cast<int>(options.restoredLayoutTree.getProperty(
                        "mainInputChannels", 0)));
                const auto outputChannels = juce::jlimit(
                    0, 64,
                    static_cast<int>(options.restoredLayoutTree.getProperty(
                        "mainOutputChannels", 0)));

                if (inputChannels > 0)
                    preparation.mainInputChannels = static_cast<std::uint32_t>(inputChannels);
                if (outputChannels > 0)
                    preparation.mainOutputChannels = static_cast<std::uint32_t>(outputChannels);
            }

            return appendSandboxedPlugin(desc, errorMsg, preparation,
                                         options.publishAndNotify);
        }

        if (options.executionMode != PluginExecutionMode::InProcess)
        {
            errorMsg = "Invalid plugin execution mode; insertion was rejected.";
            return -1;
        }

        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginChainCore::appendPlugin start"
                " plugin=\"" + desc.name + "\""
                + " format=\"" + desc.pluginFormatName + "\""
                + " existingSlotCount=" + juce::String((int) slots_.size()));

        auto safeLoad = PluginSafeLoadWrapperCore::createPluginInstance(
            formatManager, desc, sampleRate_, blockSize_, "plugin_chain_append");
        auto instance = std::move(safeLoad.instance);
        if (!instance)
        {
            errorMsg = safeLoad.userFacingMessage.isNotEmpty()
                ? safeLoad.userFacingMessage
                : safeLoad.failure.rawErrorText;
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::appendPlugin failure"
                    " plugin=\"" + desc.name + "\""
                    + " error=\"" + errorMsg + "\"");
            return -1;
        }

        logInstantiationDiag(instance.get(), "appendPlugin");

        auto wrapped = makeSharedPlugin(std::move(instance));
        attachPlayhead(wrapped.get());
        configureSlotAutomation(wrapped.get(), (int)slots_.size());

        // C7: replay the persisted negotiated layout as a PREFERRED candidate
        // before the first prepare. The saved layout is a hint — prepare()
        // still validates it through isBusesLayoutSupported() and falls back
        // generically when the current plugin version no longer supports it.
        if (options.restoredLayoutTree.isValid())
            wrapped->applyRestoredLayout(options.restoredLayoutTree);

        // prepare() calls setBusesLayout() and prepareToPlay() which can throw
        // or crash in misbehaving third-party plugins (e.g. Antares Auto-Tune EFX).
        try
        {
            wrapped->prepare(sampleRate_, blockSize_, getEnabledAuxInputBusesForSlot((int) slots_.size()));
            if (! wrapped->isPrepared())
            {
                errorMsg = "Plugin failed during prepare and was not inserted.";
                return -1;
            }
        }
        catch (const std::exception& e)
        {
            errorMsg = "Plugin crashed during prepare: " + juce::String(e.what());
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::appendPlugin prepare EXCEPTION"
                    " plugin=\"" + desc.name + "\""
                    + " error=\"" + errorMsg + "\"");
            return -1;
        }
        catch (...)
        {
            errorMsg = "Plugin crashed during prepare (unknown exception)";
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::appendPlugin prepare UNKNOWN EXCEPTION"
                    " plugin=\"" + desc.name + "\"");
            return -1;
        }

        slots_.push_back(std::move(wrapped));
        auto bypassCore = std::make_unique<BypassCrossfadeCore>();
        bypassCore->prepare(sampleRate_);
        bypassCores_.push_back(std::move(bypassCore));

        // A new slot may introduce a wider bus topology (e.g. a sidechain-
        // capable processor). Grow the scratch set on this message thread
        // before the chain becomes visible to realtime processing.
        prepareSidechainScratchCapacity(blockSize_);

        if (options.publishAndNotify)
        {
            publishSnapshot();
            notifyChanged();
        }
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginChainCore::appendPlugin success"
                " plugin=\"" + desc.name + "\""
                + " slotIndex=" + juce::String((int) slots_.size() - 1)
                + " slotCount=" + juce::String((int) slots_.size()));
        return (int)slots_.size() - 1;
    }

    /** Compatibility wrapper.  No-options insertion is explicitly
        InProcess; new product call sites must pass PluginInsertOptions. */
    int appendPlugin(const juce::PluginDescription& desc,
                     juce::AudioPluginFormatManager& formatManager,
                     juce::String& errorMsg,
                     bool publishAndNotify = true,
                     const juce::ValueTree* restoredLayoutTree = nullptr)
    {
        PluginInsertOptions options;
        options.executionMode = PluginExecutionMode::InProcess;
        options.publishAndNotify = publishAndNotify;
        if (restoredLayoutTree != nullptr)
            options.restoredLayoutTree = *restoredLayoutTree;
        return appendPlugin(desc, options, formatManager, errorMsg);
    }

#if APEX_ENABLE_TEST_HOOKS
    /** Test-only deterministic injection seam. Production plugin creation must
        continue through PluginSafeLoadWrapperCore. */
    int appendPluginInstanceForTesting(std::unique_ptr<juce::AudioPluginInstance> instance)
    {
        if (instance == nullptr || sampleRate_ <= 0.0 || blockSize_ <= 0)
            return -1;

        auto wrapped = makeSharedPlugin(std::move(instance));
        wrapped->prepare(sampleRate_, blockSize_);
        if (! wrapped->isPrepared())
            return -1;

        slots_.push_back(std::move(wrapped));
        auto bypassCore = std::make_unique<BypassCrossfadeCore>();
        bypassCore->prepare(sampleRate_);
        bypassCores_.push_back(std::move(bypassCore));

        // Test seam must honour the same capacity contract as production.
        prepareSidechainScratchCapacity(blockSize_);

        publishSnapshot();
        return (int) slots_.size() - 1;
    }

    /** Test-only capacity introspection for the C1+SC scratch contract
        (see Tests/Source/PluginHost/SidechainRoutingRegressionTests.cpp).
        Read-only; safe to call from any thread. */
    int getScratchChannelCapacityForTesting() const noexcept { return combinedBuffer_.getNumChannels(); }
    int getScratchSampleCapacityForTesting() const noexcept { return combinedBuffer_.getNumSamples(); }
    int getDrySnapshotChannelsForTesting() const noexcept { return drySnapshotBuffer_.getNumChannels(); }
    int getWetSnapshotChannelsForTesting() const noexcept { return wetBuffer_.getNumChannels(); }

    /** Test-only equivalent of the audio callback's published-snapshot load. */
    std::shared_ptr<const Snapshot> acquirePublishedSnapshotForTesting() const noexcept
    {
        return std::atomic_load_explicit(&published_, std::memory_order_acquire);
    }

    std::size_t getPendingRetiredBypassCoreCountForTesting() const noexcept
    {
        return retiredBypassCores_.size();
    }
#endif

    /** Opt-in production seam for a worker-hosted (sandboxed) real plugin.

        The parent never loads the plugin module or creates an
        AudioPluginInstance; PluginWorkerHostedPluginCore owns both inside the
        worker. Preparation is transactional: on failure the chain is left
        unchanged. Message thread only. Defined in PluginChainCore.cpp. */
    int appendSandboxedPlugin(const juce::PluginDescription& description,
                              juce::String& errorMsg,
                              const PluginSandboxPreparation& preparation,
                              bool publishAndNotify = true);

private:
    /** Sandbox replacement implementation.  Defined out-of-line so the
        complete proxy type remains out of UI translation units. */
    bool loadSandboxedPlugin(int slotIndex,
                             const juce::PluginDescription& description,
                             const PluginInsertOptions& options,
                             juce::String& errorMsg);

public:
    /** Canonical control-plane replacement dispatcher. */
    bool loadPlugin(int slotIndex,
                    const juce::PluginDescription& desc,
                    const PluginInsertOptions& options,
                    juce::AudioPluginFormatManager& formatManager,
                    juce::String& errorMsg)
    {
        if (options.executionMode == PluginExecutionMode::Sandboxed)
            return loadSandboxedPlugin(slotIndex, desc, options, errorMsg);

        if (options.executionMode == PluginExecutionMode::InProcess)
            return loadInProcessPlugin(slotIndex, desc, options,
                                       formatManager, errorMsg);

        errorMsg = "Invalid plugin execution mode; replacement was rejected.";
        return false;
    }

    /** Compatibility replacement wrapper.  Existing occupied slots retain
        their current execution mode; an empty slot defaults to InProcess. */
    bool loadPlugin(int slotIndex,
                    const juce::PluginDescription& desc,
                    juce::AudioPluginFormatManager& formatManager,
                    juce::String& errorMsg)
    {
        PluginInsertOptions options;
        if (slotIndex >= 0 && slotIndex < static_cast<int>(slots_.size())
            && slots_[static_cast<std::size_t>(slotIndex)] != nullptr)
        {
            options.executionMode = slots_[static_cast<std::size_t>(slotIndex)]
                ->getExecutionMode();
        }
        return loadPlugin(slotIndex, desc, options, formatManager, errorMsg);
    }

private:
    /** Existing in-process replacement implementation. */
    bool loadInProcessPlugin(int slotIndex,
                             const juce::PluginDescription& desc,
                             const PluginInsertOptions& options,
                             juce::AudioPluginFormatManager& formatManager,
                             juce::String& errorMsg)
    {
        jassert(sampleRate_ > 0.0 && "sampleRate_ read before prepareToPlay");
        jassert(blockSize_  > 0   && "blockSize_ read before prepareToPlay");
        if (sampleRate_ <= 0.0 || blockSize_ <= 0)
        {
            errorMsg = "Audio engine is not prepared yet.";
            return false;
        }

        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginChainCore::loadPlugin start"
                " slotIndex=" + juce::String(slotIndex)
                + " plugin=\"" + desc.name + "\""
                + " format=\"" + desc.pluginFormatName + "\"");

        auto safeLoad = PluginSafeLoadWrapperCore::createPluginInstance(
            formatManager, desc, sampleRate_, blockSize_, "plugin_chain_load");
        auto instance = std::move(safeLoad.instance);
        if (!instance)
        {
            errorMsg = safeLoad.userFacingMessage.isNotEmpty()
                ? safeLoad.userFacingMessage
                : safeLoad.failure.rawErrorText;
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::loadPlugin failure"
                    " slotIndex=" + juce::String(slotIndex)
                    + " plugin=\"" + desc.name + "\""
                    + " error=\"" + errorMsg + "\"");
            return false;
        }

        logInstantiationDiag(instance.get(), "loadPlugin");

        auto wrapped = makeSharedPlugin(std::move(instance));
        attachPlayhead(wrapped.get());
        configureSlotAutomation(wrapped.get(), slotIndex);

        // C7: the persisted layout is only a preferred candidate.  The
        // instance negotiates it against the current plugin capabilities in
        // prepare(); unsupported saved layouts remain recoverable.
        if (options.restoredLayoutTree.isValid())
            wrapped->applyRestoredLayout(options.restoredLayoutTree);

        // prepare() calls setBusesLayout() and prepareToPlay() which can throw
        // or crash in misbehaving third-party plugins (e.g. Antares Auto-Tune EFX).
        try
        {
            wrapped->prepare(sampleRate_, blockSize_, getEnabledAuxInputBusesForSlot(slotIndex));
            if (! wrapped->isPrepared())
            {
                errorMsg = "Plugin failed during prepare and was not inserted.";
                return false;
            }
        }
        catch (const std::exception& e)
        {
            errorMsg = "Plugin crashed during prepare: " + juce::String(e.what());
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::loadPlugin prepare EXCEPTION"
                    " slotIndex=" + juce::String(slotIndex)
                    + " plugin=\"" + desc.name + "\""
                    + " error=\"" + errorMsg + "\"");
            return false;
        }
        catch (...)
        {
            errorMsg = "Plugin crashed during prepare (unknown exception)";
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::loadPlugin prepare UNKNOWN EXCEPTION"
                    " slotIndex=" + juce::String(slotIndex)
                    + " plugin=\"" + desc.name + "\"");
            return false;
        }

        // Retire the old plugin at this slot (if any)
        if (slotIndex >= 0 && slotIndex < (int)slots_.size() && slots_[slotIndex])
        {
            removeSlotAutomation (slotIndex);
            slots_[slotIndex]->closeEditor();
            retiredSlots_.push_back(std::move(slots_[slotIndex]));
            retireBypassCore(std::move(bypassCores_[slotIndex]));
        }

        if (slotIndex >= 0 && slotIndex < (int)slots_.size())
        {
            slots_[slotIndex] = std::move(wrapped);
            auto bypassCore = std::make_unique<BypassCrossfadeCore>();
            bypassCore->prepare(sampleRate_);
            bypassCores_[slotIndex] = std::move(bypassCore);
        }
        else
        {
            // Pad to reach the index
            while ((int)slots_.size() <= slotIndex)
            {
                slots_.push_back(nullptr);
                auto bypassCore = std::make_unique<BypassCrossfadeCore>();
                bypassCore->prepare(sampleRate_);
                bypassCores_.push_back(std::move(bypassCore));
            }
            slots_[slotIndex] = std::move(wrapped);
            auto bypassCore = std::make_unique<BypassCrossfadeCore>();
            bypassCore->prepare(sampleRate_);
            bypassCores_[slotIndex] = std::move(bypassCore);
        }

        // Saved state must be fully applied and automation bindings re-seeded
        // before publishing the replacement chain to the audio thread.
        // The replacement may change the bus topology — refresh the scratch
        // capacity contract on this message thread before publishing.
        prepareSidechainScratchCapacity(blockSize_);
        refreshAutomationContexts();
        if (options.publishAndNotify)
        {
            publishSnapshot();
            notifyChanged();
        }
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginChainCore::loadPlugin success"
                " slotIndex=" + juce::String(slotIndex)
                + " plugin=\"" + desc.name + "\""
                + " slotCount=" + juce::String((int) slots_.size()));
        return true;
    }

public:
    void removePlugin(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= (int)slots_.size()) return;
        if (!slots_[slotIndex]) return;

        removeSlotAutomation (slotIndex);

        // Close editor now (message thread) so destructor has no UI work
        slots_[slotIndex]->closeEditor();
        retiredSlots_.push_back(std::move(slots_[slotIndex]));
        retireBypassCore(std::move(bypassCores_[slotIndex]));
        slots_.erase(slots_.begin() + slotIndex);
        bypassCores_.erase(bypassCores_.begin() + slotIndex);

        // Recompute the scratch capacity for the reduced slot set on this
        // message thread (capacity only ever grows — shrinking is avoided by
        // avoidReallocating so retired capacity never invalidates the audio
        // thread's view mid-block).
        prepareSidechainScratchCapacity(blockSize_);

        publishSnapshot();
        refreshAutomationContexts();
        notifyChanged();
    }

    void setSlotMix(int slotIndex, float mixNormalized)
    {
        if (auto* slot = getSlot(slotIndex))
            slot->setSlotMixNormalized(mixNormalized);
    }

    float getSlotMix(int slotIndex) const noexcept
    {
        if (auto* slot = getSlot(slotIndex))
            return slot->getSlotMixNormalized();
        return 1.0f;
    }

    void setSlotMixBypass(int slotIndex, bool bypass)
    {
        if (auto* slot = getSlot(slotIndex))
            slot->setSlotMixBypass(bypass);
    }

    bool getSlotMixBypass(int slotIndex) const noexcept
    {
        if (auto* slot = getSlot(slotIndex))
            return slot->getSlotMixBypass();
        return false;
    }

    void applyAutomationAtSample(const TrackID& trackId,
                                 const AutomationSnapshot* automationSnap,
                                 int64_t samplePosition,
                                 double sampleRate = 44100.0,
                                 double bpm = 120.0,
                                 int numSamples = 512) noexcept
    {
        RealtimeReadScope reader(*this);
        if (!reader) return;
        // The engine may have captured its automation snapshot before this
        // chain's reorder completed. Adopt the current lane addresses inside
        // the admission gate so an old wet/dry lane cannot hit a new occupant.
        const auto currentAutomation = automationManager_ != nullptr
            ? automationManager_->getSnapshotPublisher().get() : nullptr;
        if (currentAutomation != nullptr)
            automationSnap = currentAutomation.get();
        // Iterate the PUBLISHED slot snapshot — the same immutable view the
        // audio processing path uses — never the live slots_ vector, which
        // the message thread mutates (this was a data race before).
        auto snap = std::atomic_load(&published_);
        if (!snap) return;
        applyAutomationForSnapshot(trackId, automationSnap, samplePosition,
                                   sampleRate, bpm, numSamples, *snap);
    }

    /** Move a slot from one index to another (reorder). */
    bool moveSlot(int fromIndex, int toIndex)
    {
        int n = (int)slots_.size();
        if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n) return false;
        if (fromIndex == toIndex) return false;

        ControlEditScope edit(*this);
        if (!edit) return false;
        std::vector<int> oldToNew((size_t)n);
        for (int i = 0; i < n; ++i)
            oldToNew[(size_t)i] = i == fromIndex ? toIndex
                : (fromIndex < toIndex && i > fromIndex && i <= toIndex) ? i - 1
                : (toIndex < fromIndex && i >= toIndex && i < fromIndex) ? i + 1 : i;

        // IDs, registry pointers, gestures, wet/dry lanes and sidechain bus
        // ownership move with the existing instances; no plugin is recreated
        // or re-prepared for an order change.
        apex::automation::AutomationParameterKeyRegistry::getInstance()
            .remapPluginSlots(trackId_, oldToNew);
        decltype(slotAutomationBindings_) movedBindings;
        for (auto& [index, binding] : slotAutomationBindings_)
            movedBindings.emplace(index >= 0 && index < n ? oldToNew[(size_t)index] : index,
                                  std::move(binding));
        slotAutomationBindings_.swap(movedBindings);
        const auto remapBusConfig = [&](auto& config)
        {
            std::decay_t<decltype(config)> next;
            for (auto& [index, buses] : config)
                next.emplace(index >= 0 && index < n ? oldToNew[(size_t)index] : index, buses);
            config.swap(next);
        };
        remapBusConfig(sidechainAuxInputBuses_);
        remapBusConfig(activeSidechainAuxInputBuses_);
        if (automationManager_ != nullptr)
            automationManager_->remapPluginSlots(trackId_, oldToNew);

        auto moving = std::move(slots_[fromIndex]);
        auto movingBypass = std::move(bypassCores_[fromIndex]);
        slots_.erase(slots_.begin() + fromIndex);
        bypassCores_.erase(bypassCores_.begin() + fromIndex);
        slots_.insert(slots_.begin() + juce::jmin(toIndex, (int)slots_.size()), std::move(moving));
        bypassCores_.insert(bypassCores_.begin() + juce::jmin(toIndex, (int)bypassCores_.size()), std::move(movingBypass));

        refreshAutomationContexts();
        publishSnapshot();
        notifyChanged();
        return true;
    }

    /** Copy a plugin into this chain, preserving its resolved execution mode.
        The source checkpoint is captured before constructing the destination;
        the candidate is not published until its owner-specific restore works. */
    int copyPluginFrom(const PluginChainCore& srcChain, int srcSlot,
                       juce::AudioPluginFormatManager& formatManager,
                       juce::String& error)
    {
        auto* src = srcChain.getSlot(srcSlot);
        if (!src)
        {
            error = "Source plugin slot is unavailable.";
            return -1;
        }

        auto desc = src->getDescription();
        juce::MemoryBlock sourceState;
        if (! src->capturePluginState(sourceState, error))
            return -1;

        PluginInsertOptions options;
        options.executionMode = src->getExecutionMode();
        options.publishAndNotify = false;
        options.restoredLayoutTree = src->getSavedLayoutTree();

        const int idx = appendPlugin(desc, options, formatManager, error);
        if (idx < 0)
            return -1;

        auto* destination = getSlot(idx);
        if (destination == nullptr
            || ! destination->restorePluginState(sourceState, error))
        {
            if (error.isEmpty())
                error = "Destination plugin state restore failed.";
            removePlugin(idx);
            return -1;
        }

        // Plugin processor state and slot/container state are separate
        // authorities.  Copy both layers while retaining the freshly-created
        // destination instance ID so the copy is independent but initially
        // identical in what the user hears/sees.
        destination->setBypassed(src->isBypassed());
        destination->setSlotMixNormalized(src->getSlotMixNormalized());
        destination->setSlotMixBypass(src->getSlotMixBypass());
        destination->setLastSlotMixAutomationValue(src->getLastSlotMixAutomationValue());

        prepareSidechainScratchCapacity(blockSize_);
        refreshAutomationContexts();
        publishSnapshot();
        notifyChanged();
        return idx;
    }

    /** Compatibility copy wrapper. */
    int copyPluginFrom(const PluginChainCore& srcChain, int srcSlot,
                       juce::AudioPluginFormatManager& formatManager)
    {
        juce::String error;
        return copyPluginFrom(srcChain, srcSlot, formatManager, error);
    }

    void setSlotBypassed(int slotIndex, bool bypassed)
    {
        if (slotIndex >= 0 && slotIndex < (int)slots_.size() && slots_[slotIndex])
        {
            slots_[slotIndex]->setBypassed(bypassed);
            if (slotIndex < (int)bypassCores_.size() && bypassCores_[slotIndex])
                bypassCores_[slotIndex]->setTargetBypassed(bypassed);
        }
    }

    int getNumActiveSlots() const noexcept
    {
        int count = 0;
        for (const auto& slot : slots_)
            if (slot != nullptr)
                ++count;
        return count;
    }

    bool areAllActiveSlotsBypassed() const noexcept
    {
        bool hasActiveSlot = false;
        for (int i = 0; i < (int) slots_.size(); ++i)
        {
            if (slots_[(size_t) i] == nullptr)
                continue;

            hasActiveSlot = true;
            if (slots_[(size_t) i]->isSandboxed())
            {
                // Sandboxed slots publish a null chain bypass core; the proxy
                // is the sole bypass authority for them.
                if (! slots_[(size_t) i]->isBypassed())
                    return false;
                continue;
            }
            if (i >= (int) bypassCores_.size() || bypassCores_[(size_t) i] == nullptr || !bypassCores_[(size_t) i]->isFullyBypassed())
                return false;
        }

        return hasActiveSlot;
    }

    void setAllActiveSlotsBypassed(bool bypassed)
    {
        for (int i = 0; i < (int) slots_.size(); ++i)
            if (slots_[(size_t) i] != nullptr)
                setSlotBypassed(i, bypassed);
    }

    void setAllActiveSlotMixes(float mixNormalized)
    {
        for (int i = 0; i < (int) slots_.size(); ++i)
            if (slots_[(size_t) i] != nullptr)
                setSlotMix(i, mixNormalized);
    }

    void setChainOutputMix(float mixNormalized) noexcept
    {
        chainOutputMixNormalized_.store(juce::jlimit(0.0f, 1.0f, mixNormalized), std::memory_order_relaxed);
    }

    float getChainOutputMix() const noexcept
    {
        return juce::jlimit(0.0f, 1.0f, chainOutputMixNormalized_.load(std::memory_order_relaxed));
    }

    float getAverageActiveSlotMix() const noexcept
    {
        float total = 0.0f;
        int count = 0;

        for (const auto& slot : slots_)
        {
            if (slot == nullptr)
                continue;

            total += slot->getSlotMixNormalized();
            ++count;
        }

        return count > 0 ? (total / (float) count) : 1.0f;
    }

    PluginChainDiagSnapshot getAndResetDiagSnapshot() noexcept
    {
        PluginChainDiagSnapshot snapshot;
        snapshot.processedBlocks = diagProcessedBlocks_.exchange(0, std::memory_order_relaxed);

        auto snap = std::atomic_load(&published_);
        if (!snap || snap->slots.empty() || snap->slots.front() == nullptr)
            return snapshot;

        if (auto firstSlot = snap->slots.front())
        {
            const auto pluginSnapshot = firstSlot->getDiagSnapshot();
            snapshot.totalPrepareCalls = pluginSnapshot.prepareCount;
            snapshot.totalResetCalls = pluginSnapshot.resetCount;
            snapshot.firstPluginSamples = pluginSnapshot.lastProcessSamples;
            snapshot.firstPluginBypassed = pluginSnapshot.bypassed;
            snapshot.firstPluginName = pluginSnapshot.pluginName;
        }

        return snapshot;
    }

    void processBlockPerSlotWetDry(juce::AudioBuffer<float>& buffer,
                                   int numSamples,
                                   const std::vector<float>& slotWetDry)
    {
        for (int i = 0; i < (int)slotWetDry.size(); ++i)
            setSlotMix(i, slotWetDry[(size_t)i]);
        processBlock(buffer, numSamples);
    }

    bool hasPlugin(int slotIndex) const
    {
        return slotIndex >= 0 && slotIndex < (int)slots_.size() && slots_[slotIndex] != nullptr;
    }

    PluginInstanceCore* getSlot(int slotIndex)
    {
        if (slotIndex >= 0 && slotIndex < (int)slots_.size()) return slots_[slotIndex].get();
        return nullptr;
    }

    const PluginInstanceCore* getSlot(int slotIndex) const
    {
        if (slotIndex >= 0 && slotIndex < (int)slots_.size()) return slots_[slotIndex].get();
        return nullptr;
    }

    int totalLatencySamples() const
    {
        int total = 0;
        for (auto& slot : slots_)
            if (slot) total += slot->getLatencySamples();
        return total;
    }

    // ── Audio thread ─────────────────────────────────────────────────────

    void processBlock(juce::AudioBuffer<float>& buffer, int numSamples)
    {
        RealtimeReadScope reader(*this);
        if (!reader) return;
        if (! validateProcessBuffer(buffer, numSamples)) return;
        if (! validateOfflinePreparedForProcess(numSamples)) return;
        if (HostedPluginIsolationCore::shouldBypassHostedDsp()) return;

        diagProcessedBlocks_.fetch_add(1, std::memory_order_relaxed);

        // Lock-free: atomically load the published snapshot.
        auto snap = std::atomic_load(&published_);
        if (!snap) return;

        const float chainOutputMix = getChainOutputMix();
        const bool applyChainOutputMix = chainOutputMix < 0.999999f && !snap->slots.empty();
        if (applyChainOutputMix)
            captureChainInput(buffer, numSamples);

        juce::MidiBuffer emptyMidi;
        const bool logThisBlock = beginOfflineBlockLog(buffer, numSamples, snap->slots.size());
        for (size_t i = 0; i < snap->slots.size(); ++i)
        {
            auto& slot = snap->slots[i];
            if (!slot) continue;
#if APEX_AUDIO_DEBUG_LOGS
            DBG("[PluginChain] processing plugin=" << slot->getName()
                << " numSamples=" << buffer.getNumSamples()
                << " chans=" << buffer.getNumChannels());
#endif
            // A slot prepared with auxiliary (sidechain) input buses MUST get a
            // buffer that CONTAINS those channels. Passing the plain main
            // buffer leaves the aux channels out of range and the plugin reads
            // stale memory as its sidechain — the "ducking even without an
            // active sidechain" defect. Build the combined layout with the aux
            // buses ZEROED (silence), exactly like the sidechain path does.
            {
                auto* proc = slot->getProcessor();
                const auto& enabledAuxBuses = getEnabledAuxInputBusesForSlot((int) i);
                if (proc != nullptr && ! enabledAuxBuses.isEmpty())
                {
                    const int mainCh  = buffer.getNumChannels();
                    const int totalCh = juce::jmax(proc->getTotalNumInputChannels(),
                                                   proc->getTotalNumOutputChannels());
                    if (sidechainScratchCovers(totalCh, numSamples))
                    {
                        for (int ch = 0; ch < totalCh; ++ch)
                            combinedBuffer_.clear(ch, 0, numSamples);

                        const int slotMainIn = slot->getActiveMainInputChannels();
                        if (slotMainIn == 1 && mainCh >= 2)
                        {
                            combinedBuffer_.addFrom(0, 0, buffer, 0, 0, numSamples, 0.5f);
                            combinedBuffer_.addFrom(0, 0, buffer, 1, 0, numSamples, 0.5f);
                        }
                        else
                        {
                            for (int ch = 0; ch < juce::jmin(juce::jmin(mainCh, slotMainIn), totalCh); ++ch)
                                combinedBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                        }

                        // Aux buses stay ZEROED: no sidechain source routed here.

                        processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                              combinedBuffer_, emptyMidi, numSamples, /*combinedLayout=*/true);

                        const int slotMainOut = slot->getActiveMainOutputChannels();
                        for (int ch = 0; ch < juce::jmin(mainCh, combinedBuffer_.getNumChannels()); ++ch)
                            buffer.copyFrom(ch, 0, combinedBuffer_,
                                            slotMainOut == 1 ? 0 : juce::jmin(ch, slotMainOut - 1),
                                            0, numSamples);

                        logAfterPluginIfNeeded(logThisBlock, *slot, buffer, numSamples);
                        continue;
                    }
                }
            }

            processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                  buffer, emptyMidi, numSamples);
            logAfterPluginIfNeeded(logThisBlock, *slot, buffer, numSamples);
        }

        if (applyChainOutputMix)
            PluginSlotMixCore::apply(chainInputBuffer_, buffer, chainOutputMix,
                                     SlotMixCurve::EqualPower, numSamples);
    }

    /** Like processBlock() but forwards a live MidiBuffer to the first plugin
        (instrument), then passes empty MIDI to subsequent FX slots. */
    void processBlockWithMidi(juce::AudioBuffer<float>& buffer,
                              juce::MidiBuffer& midiIn,
                              int numSamples)
    {
        RealtimeReadScope reader(*this);
        if (!reader) return;
        if (! validateProcessBuffer(buffer, numSamples)) return;
        if (! validateOfflinePreparedForProcess(numSamples)) return;
        if (HostedPluginIsolationCore::shouldBypassHostedDsp()) return;

        diagProcessedBlocks_.fetch_add(1, std::memory_order_relaxed);

        auto snap = std::atomic_load(&published_);
        if (!snap) return;

        const float chainOutputMix = getChainOutputMix();
        const bool applyChainOutputMix = chainOutputMix < 0.999999f && !snap->slots.empty();
        if (applyChainOutputMix)
            captureChainInput(buffer, numSamples);

        bool firstSlot = true;
        juce::MidiBuffer emptyMidi;
        const bool logThisBlock = beginOfflineBlockLog(buffer, numSamples, snap->slots.size());
        for (size_t i = 0; i < snap->slots.size(); ++i)
        {
            auto& slot = snap->slots[i];
            if (!slot) continue;
#if APEX_AUDIO_DEBUG_LOGS
            DBG("[PluginChain] processing plugin=" << slot->getName()
                << " numSamples=" << buffer.getNumSamples()
                << " chans=" << buffer.getNumChannels());
#endif
            if (firstSlot)
            {
                processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                      buffer, midiIn, numSamples);
                firstSlot = false;
            }
            else
            {
                processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                      buffer, emptyMidi, numSamples);
            }
            logAfterPluginIfNeeded(logThisBlock, *slot, buffer, numSamples);
        }

        if (applyChainOutputMix)
            PluginSlotMixCore::apply(chainInputBuffer_, buffer, chainOutputMix,
                                     SlotMixCurve::EqualPower, numSamples);
    }

    /**
     * Like processBlock() but also feeds a sidechain buffer to any plugin in the
     * chain that declares a sidechain input bus (bus index 1).
     *
     * For each plugin in the chain:
     *   - If the plugin has no sidechain bus declared → normal processBlock()
     *   - If the plugin has a sidechain bus → the sidechain audio is mixed into
     *     the plugin's combined buffer at the bus offset, then processBlock() runs.
     *
     * This matches how AudioProcessorGraph routes multi-bus plugins — the engine
     * is responsible for compositing the per-bus audio into the combined buffer
     * before handing it to the plugin's processBlock().
     */
    void processBlockWithSidechain(juce::AudioBuffer<float>& mainBuffer,
                                    juce::AudioBuffer<float>& sidechainBuffer,
                                    int numSamples)
    {
        RealtimeReadScope reader(*this);
        if (!reader) return;
        if (! validateProcessBuffer(mainBuffer, numSamples)) return;
        if (! validateOfflinePreparedForProcess(numSamples)) return;
        if (HostedPluginIsolationCore::shouldBypassHostedDsp()) return;

        // C1+SC sidechain-source capacity contract: the engine prepares the
        // sidechain routing buffers to the current block capacity before the
        // snapshot becomes visible to the audio thread. If this invariant is
        // violated, degrade to the allocation-free main-only path instead of
        // reading past the source buffer.
        if (sidechainBuffer.getNumSamples() < numSamples
            || sidechainBuffer.getNumChannels() <= 0)
        {
            jassertfalse;   // sidechain source capacity contract violated
            processBlock(mainBuffer, numSamples);
            return;
        }

        diagProcessedBlocks_.fetch_add(1, std::memory_order_relaxed);

        auto snap = std::atomic_load(&published_);
        if (!snap) return;

        const float chainOutputMix = getChainOutputMix();
        const bool applyChainOutputMix = chainOutputMix < 0.999999f && !snap->slots.empty();
        if (applyChainOutputMix)
            captureChainInput(mainBuffer, numSamples);

        juce::MidiBuffer emptyMidi;
        const int scCh = sidechainBuffer.getNumChannels();
        const bool logThisBlock = beginOfflineBlockLog(mainBuffer, numSamples, snap->slots.size());

        for (size_t i = 0; i < snap->slots.size(); ++i)
        {
            auto& slot = snap->slots[i];
            if (!slot) continue;
#if APEX_AUDIO_DEBUG_LOGS
            DBG("[PluginChain] processing plugin=" << slot->getName()
                << " numSamples=" << mainBuffer.getNumSamples()
                << " chans=" << mainBuffer.getNumChannels());
#endif

            auto* proc = slot->getProcessor();
            const auto& enabledAuxBuses = getEnabledAuxInputBusesForSlot((int) i);
            if (proc && !enabledAuxBuses.isEmpty() && scCh > 0)
            {
                // This slot is the explicit sidechain target. Build the exact
                // process buffer layout JUCE expects for the enabled aux buses
                // by REUSING the control-side preallocated combinedBuffer_.
                // C1+SC: the audio thread never resizes. If the prepared
                // capacity does not cover the negotiated bus layout (contract
                // violation), take the deterministic allocation-free
                // main-only fallback instead of touching out-of-range
                // channels or allocating.
                const int mainCh = mainBuffer.getNumChannels();
                const int totalCh = juce::jmax(proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels());

                if (!sidechainScratchCovers(totalCh, numSamples))
                {
                    jassertfalse;   // prepareSidechainScratchCapacity() contract violated
                    processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                          mainBuffer, emptyMidi, numSamples);
                    continue;
                }

                for (int ch = 0; ch < totalCh; ++ch)
                    combinedBuffer_.clear(ch, 0, numSamples);

                // C7: build the main section from the NEGOTIATED active main
                // width (never assume two main input channels):
                //   mono main + stereo engine  -> 0.5 * (L + R)
                //   stereo main                -> 1:1 copy
                const int slotMainIn = slot->getActiveMainInputChannels();
                if (slotMainIn == 1 && mainCh >= 2)
                {
                    combinedBuffer_.addFrom(0, 0, mainBuffer, 0, 0, numSamples, 0.5f);
                    combinedBuffer_.addFrom(0, 0, mainBuffer, 1, 0, numSamples, 0.5f);
                }
                else
                {
                    for (int ch = 0; ch < juce::jmin(juce::jmin(mainCh, slotMainIn), totalCh); ++ch)
                        combinedBuffer_.copyFrom(ch, 0, mainBuffer, ch, 0, numSamples);
                }

                for (int bus : enabledAuxBuses)
                {
                    if (auto* inputBus = proc->getBus(true, bus))
                    {
                        if (!inputBus->isEnabled() || inputBus->getCurrentLayout().isDisabled())
                            continue;

                        const int busChannels = inputBus->getNumberOfChannels();
                        const int busOffset = proc->getChannelIndexInProcessBlockBuffer(true, bus, 0);
                        jassert(busOffset >= 0 && busOffset + busChannels <= totalCh);
                        for (int ch = 0; ch < busChannels && busOffset + ch < totalCh; ++ch)
                            combinedBuffer_.copyFrom(busOffset + ch, 0, sidechainBuffer,
                                                     juce::jmin(ch, scCh - 1), 0, numSamples);
                    }
                }

                processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                      combinedBuffer_, emptyMidi, numSamples, /*combinedLayout=*/true);

                // C7: copy the negotiated MAIN output section back (sidechain
                // channels are discarded):
                //   mono main output  -> duplicated to every engine channel
                //   stereo main output -> 1:1
                const int slotMainOut = slot->getActiveMainOutputChannels();
                for (int ch = 0; ch < juce::jmin(mainCh, combinedBuffer_.getNumChannels()); ++ch)
                    mainBuffer.copyFrom(ch, 0, combinedBuffer_,
                                        slotMainOut == 1 ? 0 : juce::jmin(ch, slotMainOut - 1),
                                        0, numSamples);
            }
            else
            {
                processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                      mainBuffer, emptyMidi, numSamples);
            }
            logAfterPluginIfNeeded(logThisBlock, *slot, mainBuffer, numSamples);
        }

        if (applyChainOutputMix)
            PluginSlotMixCore::apply(chainInputBuffer_, mainBuffer, chainOutputMix,
                                     SlotMixCurve::EqualPower, numSamples);
    }

    /** Applies plugin automation at short in-block intervals while preserving
        one immutable chain ordering for the full device callback. Static lanes
        and chains without moving automation retain their single-call path. */
    void processBlockWithAutomation(juce::AudioBuffer<float>& mainBuffer,
                                    juce::MidiBuffer* midiIn,
                                    juce::AudioBuffer<float>* sidechainBuffer,
                                    const TrackID& trackId,
                                    const AutomationSnapshot* automationSnap,
                                    int64_t samplePosition,
                                    double sampleRate,
                                    double bpm,
                                    int numSamples)
    {
        RealtimeReadScope reader(*this);
        if (!reader) return;
        if (!validateProcessBuffer(mainBuffer, numSamples)) return;
        if (!validateOfflinePreparedForProcess(numSamples)) return;
        if (HostedPluginIsolationCore::shouldBypassHostedDsp()) return;
        if (sidechainBuffer != nullptr
            && (sidechainBuffer->getNumSamples() < numSamples
                || sidechainBuffer->getNumChannels() <= 0))
        {
            jassertfalse;
            sidechainBuffer = nullptr;
        }

        diagProcessedBlocks_.fetch_add(1, std::memory_order_relaxed);
        auto snap = std::atomic_load(&published_);
        if (!snap) return;

        auto currentAutomation = automationManager_ != nullptr
            ? automationManager_->getSnapshotPublisher().get() : nullptr;
        if (currentAutomation != nullptr)
            automationSnap = currentAutomation.get();

        const bool canSlice = numSamples > kAutomationSliceSamples
            && canRenderAutomationInSlices(trackId, automationSnap,
                                           samplePosition, sampleRate, bpm,
                                           numSamples, *snap)
            && midiBufferFitsAutomationScratch(midiIn)
            && mainBuffer.getNumChannels() == kAutomationSliceMaxChannels
            && (sidechainBuffer == nullptr
                || sidechainBuffer->getNumChannels() <= kAutomationSliceMaxChannels);

        if (!canSlice)
        {
            applyAutomationForSnapshot(trackId, automationSnap, samplePosition,
                                       sampleRate, bpm, numSamples, *snap);
            processBlockWithSnapshot(mainBuffer, midiIn, sidechainBuffer,
                                     numSamples, *snap);
            return;
        }

        const int mainChannelCount = mainBuffer.getNumChannels();
        int offset = 0;
        while (offset < numSamples)
        {
            const int sliceSamples = juce::jmin(kAutomationSliceSamples, numSamples - offset);
            for (int ch = 0; ch < mainChannelCount; ++ch)
                automationSliceMainBuffer_.copyFrom(ch, 0, mainBuffer, ch, offset, sliceSamples);
            if (sidechainBuffer != nullptr)
                for (int ch = 0; ch < sidechainBuffer->getNumChannels(); ++ch)
                    automationSliceSidechainBuffer_.copyFrom(
                        ch, 0, *sidechainBuffer, ch, offset, sliceSamples);

            juce::MidiBuffer* sliceMidi = nullptr;
            if (midiIn != nullptr)
            {
                automationMidiScratch_.clear();
                automationMidiScratch_.addEvents(*midiIn, offset, sliceSamples, -offset);
                sliceMidi = &automationMidiScratch_;
            }

            applyAutomationForSnapshot(trackId, automationSnap,
                                       samplePosition + offset, sampleRate, bpm,
                                       sliceSamples, *snap);
            processBlockWithSnapshot(automationSliceMainBuffer_, sliceMidi,
                                     sidechainBuffer != nullptr
                                         ? &automationSliceSidechainBuffer_ : nullptr,
                                     sliceSamples, *snap);
            for (int ch = 0; ch < mainChannelCount; ++ch)
                mainBuffer.copyFrom(ch, offset, automationSliceMainBuffer_, ch, 0, sliceSamples);
            offset += sliceSamples;
        }
    }

    // ── Listener ─────────────────────────────────────────────────────────

    std::function<void(int slotIndex)> onChainChanged;

    // ── Serialization ────────────────────────────────────────────────────

    static juce::String executionModeToString(PluginExecutionMode mode)
    {
        switch (mode)
        {
            case PluginExecutionMode::InProcess: return "in_process";
            case PluginExecutionMode::Sandboxed: return "sandboxed";
        }
        return "invalid";
    }

    static bool parseExecutionMode(const juce::ValueTree& slot,
                                   PluginExecutionMode& mode,
                                   juce::String& error)
    {
        error.clear();

        if (! slot.hasProperty("executionMode"))
        {
            mode = PluginExecutionMode::InProcess;
            return true;
        }

        const auto value = slot.getProperty("executionMode").toString();
        if (value == "in_process")
        {
            mode = PluginExecutionMode::InProcess;
            return true;
        }

        if (value == "sandboxed")
        {
            mode = PluginExecutionMode::Sandboxed;
            return true;
        }

        error = "Invalid plugin executionMode '" + value + "'.";
        return false;
    }

    /** Result-bearing control-plane project snapshot.  The worker state
        capture is completed before the returned tree is considered valid. */
    bool captureStateForPersistence(juce::ValueTree& state,
                                    juce::String& error) const
    {
        state = juce::ValueTree("PluginChain");
        error.clear();
        state.setProperty("chainOutputMixNormalized", getChainOutputMix(), nullptr);

        for (int i = 0; i < (int)slots_.size(); ++i)
        {
            auto* plugin = slots_[i].get();
            if (!plugin) continue;

            juce::ValueTree slot("Slot");
            slot.setProperty("index", i, nullptr);
            slot.setProperty("bypassed", plugin->isBypassed(), nullptr);
            slot.setProperty("slotMixNormalized", plugin->getSlotMixNormalized(), nullptr);
            slot.setProperty("slotMixBypass", plugin->getSlotMixBypass(), nullptr);
            slot.setProperty("pluginInstanceId", plugin->getPluginInstanceId(), nullptr);
            slot.setProperty("executionMode",
                             executionModeToString(plugin->getExecutionMode()), nullptr);

            // Save plugin description
            auto desc = plugin->getDescription();
            auto descTree = PluginDescriptionPersistenceCore::toValueTree(desc);
            slot.addChild(descTree, -1, nullptr);

            // C7: persist the negotiated effective bus layout so project
            // restore replays the SAME main configuration the state blob was
            // recorded under (a preferred hint — never a forced layout).
            slot.addChild(plugin->getSavedLayoutTree(), -1, nullptr);

            // Save plugin state (parameters + preset)
            juce::MemoryBlock stateBlock;
            juce::String stateError;
            if (! plugin->capturePluginState(stateBlock, stateError))
            {
                error = "Plugin slot " + juce::String(i)
                    + " ('" + plugin->getName() + "') state capture failed"
                    + (stateError.isNotEmpty() ? ": " + stateError : juce::String());
                state = juce::ValueTree();
                return false;
            }
            if (stateBlock.getSize() > 0)
            {
                juce::ValueTree stateTree("State");
                stateTree.setProperty("data", juce::Base64::toBase64(stateBlock.getData(), stateBlock.getSize()), nullptr);
                slot.addChild(stateTree, -1, nullptr);
            }

            state.addChild(slot, -1, nullptr);
        }
        return true;
    }

    /** Compatibility value-returning wrapper.  Durable save paths must use
        captureStateForPersistence() so a sandbox capture failure is visible. */
    juce::ValueTree getState() const
    {
        juce::ValueTree state;
        juce::String error;
        if (! captureStateForPersistence(state, error))
        {
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::captureStateForPersistence failure"
                    " error=" + error);
        }
        return state;
    }

    /** Restore chain from ValueTree.  Every slot is attempted, but a mode,
        construction, or opaque-state failure is returned to the project
        publisher instead of being silently discarded. */
    bool restoreState(const juce::ValueTree& chain,
                      juce::AudioPluginFormatManager& formatManager,
                      juce::String& restoreError)
    {
        restoreError.clear();
        bool allSlotsRestored = true;
        setChainOutputMix((float) chain.getProperty("chainOutputMixNormalized", 1.0f));

        // Clear existing chain
        while (!slots_.empty())
            removePlugin(0);

        for (int i = 0; i < chain.getNumChildren(); ++i)
        {
            auto slot = chain.getChild(i);
            if (!slot.hasType("Slot")) continue;

            int index = slot.getProperty("index", -1);
            bool bypassed = slot.getProperty("bypassed", false);
            const float slotMix = (float)slot.getProperty("slotMixNormalized", 1.0f);
            const bool slotMixBypass = (bool)slot.getProperty("slotMixBypass", false);
            const juce::String pluginInstanceId = slot.getProperty("pluginInstanceId", "").toString();

            // Restore plugin description
            auto descTree = slot.getChildWithName("Description");
            if (!descTree.isValid())
            {
                allSlotsRestored = false;
                if (restoreError.isEmpty())
                    restoreError = "Plugin slot is missing its Description tree.";
                continue;
            }

            auto desc = PluginDescriptionPersistenceCore::fromValueTree(descTree);

            PluginExecutionMode executionMode = PluginExecutionMode::InProcess;
            juce::String modeError;
            if (! parseExecutionMode(slot, executionMode, modeError))
            {
                allSlotsRestored = false;
                if (restoreError.isEmpty())
                    restoreError = "Plugin slot " + juce::String(index)
                        + " ('" + desc.name + "') rejected: " + modeError;
                PluginScanAuditLogCore::appendLine(
                    "plugin_ui_flow.log",
                    "PluginChainCore::restoreState mode rejection"
                        " slotIndex=" + juce::String(index)
                        + " plugin=\"" + desc.name + "\""
                        + " error=\"" + modeError + "\"");
                continue;
            }

            // Load plugin
            juce::String errorMsg;
            const auto layoutTree = slot.getChildWithName("Layout");
            PluginInsertOptions insertOptions;
            insertOptions.executionMode = executionMode;
            insertOptions.publishAndNotify = false;
            insertOptions.restoredLayoutTree = layoutTree;
            // Build each restored instance privately.  Publishing here would
            // expose its default parameters to the audio thread before the
            // opaque saved state below has been applied. C7: the persisted
            // layout is a preferred hint applied BEFORE the first prepare.
            if (index >= 0 && appendPlugin(desc, insertOptions,
                                           formatManager, errorMsg) >= 0)
            {
                int loadedIndex = (int)slots_.size() - 1;
                if (auto* plugin = slots_[loadedIndex].get())
                {
                    plugin->setPluginInstanceId(pluginInstanceId);
                    // Canonical bypass path: keeps the live bypass crossfade
                    // core and the serialized flag in agreement.
                    setSlotBypassed(loadedIndex, bypassed);
                    plugin->setSlotMixNormalized(slotMix);
                    plugin->setSlotMixBypass(slotMixBypass);

                    // Restore plugin state (C7C: guarded — a detectable
                    // failure is contained at the host boundary; the affected
                    // candidate remains represented for recovery, but is marked
                    // failed and kept out of the active processing path while
                    // later slots in this chain remain eligible for restore).
                    auto stateTree = slot.getChildWithName("State");
                    if (stateTree.isValid())
                    {
                        juce::String base64 = stateTree.getProperty("data", "");
                        if (base64.isNotEmpty())
                        {
                            juce::MemoryOutputStream mo;
                            if (juce::Base64::convertFromBase64(mo, base64))
                            {
                                juce::MemoryBlock block(mo.getData(), mo.getDataSize());
                                juce::String stateError;
                                if (! plugin->restorePluginState(block, stateError))
                                {
                                    allSlotsRestored = false;
                                    if (restoreError.isEmpty())
                                        restoreError = "Plugin slot " + juce::String(loadedIndex)
                                            + " ('" + desc.name + "') state restoration failed"
                                            + (stateError.isNotEmpty() ? ": " + stateError : juce::String());
                                    PluginScanAuditLogCore::appendLine(
                                        "plugin_ui_flow.log",
                                        "PluginChainCore::restoreState state failure"
                                            " slotIndex=" + juce::String(loadedIndex)
                                            + " plugin=\"" + desc.name + "\""
                                            + " error=\"" + stateError + "\"");
                                    plugin->markStateRestoreFailed();
                                }
                            }
                            else
                            {
                                allSlotsRestored = false;
                                if (restoreError.isEmpty())
                                    restoreError = "Plugin slot " + juce::String(loadedIndex)
                                        + " ('" + desc.name + "') state data is not valid Base64.";
                                plugin->markStateRestoreFailed();
                            }
                        }
                    }
                }
            }
            else
            {
                allSlotsRestored = false;
                if (restoreError.isEmpty())
                    restoreError = "Plugin slot " + juce::String(index)
                        + " ('" + desc.name + "') could not be loaded"
                        + (errorMsg.isNotEmpty() ? ": " + errorMsg : juce::String());
                // Plugin loading failed during project restore — log the
                // error so it can be diagnosed instead of silently skipped.
                PluginScanAuditLogCore::appendLine(
                    "plugin_ui_flow.log",
                    "PluginChainCore::restoreState load failure"
                        " plugin=\"" + desc.name + "\""
                        + " format=\"" + desc.pluginFormatName + "\""
                        + " instance=\"" + pluginInstanceId + "\""
                        + " error=\"" + errorMsg + "\"");
            }
        }

        publishSnapshot();
        refreshAutomationContexts();
        notifyChanged();
        return allSlotsRestored;
    }

    /** Compatibility restore wrapper. */
    void restoreState(const juce::ValueTree& chain,
                      juce::AudioPluginFormatManager& formatManager)
    {
        juce::String restoreError;
        if (! restoreState(chain, formatManager, restoreError)
            && restoreError.isNotEmpty())
        {
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginChainCore::restoreState compatibility failure"
                    " error=" + restoreError);
        }
    }

private:
    void retireBypassCore(std::unique_ptr<BypassCrossfadeCore> core)
    {
        if (core == nullptr)
            return;

        RetiredBypassCore retirement;
        retirement.protectingSnapshot = std::atomic_load_explicit(&published_,
                                                                   std::memory_order_acquire);
        retirement.core = std::move(core);
        retiredBypassCores_.push_back(std::move(retirement));
    }

    void retainRetiredSnapshot(std::shared_ptr<const Snapshot> snapshot)
    {
        if (snapshot != nullptr)
            retiredSnapshots_.push_back({ std::move(snapshot) });
    }

    void drainRetiredSnapshots() noexcept
    {
        auto it = retiredSnapshots_.begin();
        while (it != retiredSnapshots_.end())
        {
            if (it->snapshot.use_count() == 1)
                it = retiredSnapshots_.erase(it);
            else
                ++it;
        }
    }

    void handoffBypassRetirementForDestruction() noexcept
    {
        if (shutdownGroup_ == nullptr)
            return;

        // All three moves are allocator-free vector moves.  The group itself
        // was allocated by the chain constructor, so this destructor path does
        // not allocate while transferring ownership to the global reclaimer.
        shutdownGroup_->retiredCores = std::move(retiredBypassCores_);
        shutdownGroup_->activeCores = std::move(bypassCores_);
        shutdownGroup_->snapshots = std::move(retiredSnapshots_);

        auto* group = shutdownGroup_.release();
        group->next = retiredBypassShutdownGroups_.exchange(
            group, std::memory_order_acq_rel);
    }

    static bool shutdownGroupIsQuiescent(
        const RetiredBypassShutdownGroup& group) noexcept
    {
        for (const auto& retained : group.snapshots)
            if (retained.snapshot != nullptr && retained.snapshot.use_count() != 1)
                return false;

        return true;
    }

    // Message-thread canonical state (UI reads/writes this directly)
    std::vector<std::shared_ptr<PluginInstanceCore>> slots_;
    std::vector<std::unique_ptr<BypassCrossfadeCore>> bypassCores_;

    // Published snapshot — swapped atomically between message and audio threads.
    // Audio thread loads this once at block start and processes the stable list.
    std::shared_ptr<const Snapshot> published_;
    std::atomic<bool> controlEditPending_ { false };
    std::atomic<int> activeRealtimeReaders_ { 0 };
    int controlEditDepth_ = 0; // message thread only
    AutomationManagerCore* automationManager_ = nullptr; // ApplicationCore owns

    // Plugins removed from slots_ but kept alive so the audio thread's old
    // snapshot references remain valid. The custom plugin deleter defers the
    // final destruction if the last shared_ptr is released off-message-thread.
    std::vector<std::shared_ptr<PluginInstanceCore>> retiredSlots_;
    std::vector<RetiredBypassCore> retiredBypassCores_;
    std::vector<RetiredSnapshot> retiredSnapshots_;
    std::unique_ptr<RetiredBypassShutdownGroup> shutdownGroup_;

    // Chain destruction can outlive the ApplicationCore map through an
    // AudioEngine snapshot.  This process-level, control-plane-only handoff
    // list therefore cannot be a member of the dying chain.
    inline static std::atomic<RetiredBypassShutdownGroup*> retiredBypassShutdownGroups_ { nullptr };

    double sampleRate_ = 0.0;
    int    blockSize_  = 0;
    bool   offlinePrepared_ = false;
    double offlinePreparedSampleRate_ = 0.0;
    int    offlinePreparedBlockSize_ = 0;
    int    offlineDebugBlocksRemaining_ = 0;
    juce::String debugName_;
    std::unordered_map<int, juce::Array<int>> sidechainAuxInputBuses_;
    std::unordered_map<int, juce::Array<int>> activeSidechainAuxInputBuses_;
    juce::Array<int> emptyAuxInputBusList_;
    mutable juce::AudioBuffer<float> combinedBuffer_; // scratch buffer for sidechain compositing
    juce::AudioBuffer<float> drySnapshotBuffer_;
    juce::AudioBuffer<float> wetBuffer_;
    juce::AudioBuffer<float> chainInputBuffer_;
    juce::AudioBuffer<float> automationSliceMainBuffer_;
    juce::AudioBuffer<float> automationSliceSidechainBuffer_;
    juce::MidiBuffer automationMidiScratch_;

    // C1+SC scratch capacity contract. Written only by
    // prepareSidechainScratchCapacity() on non-realtime threads; the audio
    // thread reads them only for diagnostics/verification (never to resize).
    int preparedScratchChannels_ = 2;
    int preparedScratchSamples_  = 0;

    // Never let a block grow the scratch set beyond this floor. Device blocks
    // above it are covered by the capacitySamples argument at prepare time.
    static constexpr int kScratchCapacityFloor = 8192;

    PluginPlayheadInfoCore* playheadInfo_ = nullptr;
    TrackID trackId_;
    PluginAutomationGestureCore* gestureCore_ = nullptr;
    LastTouchedPluginParameterCore* lastTouchedCore_ = nullptr;
    std::atomic<float> chainOutputMixNormalized_ { 1.0f };
    std::atomic<int> diagProcessedBlocks_ { 0 };

    // Phase 1F: per-slot APEX automation binding.
    struct AutomationBinding
    {
        juce::String pluginName;
        std::unordered_map<int, apex::automation::ParameterID> indexToID;
    };
    std::unordered_map<int, AutomationBinding> slotAutomationBindings_;

    void configureSlotAutomation(PluginInstanceCore* plugin, int slotIndex)
    {
        ControlEditScope edit(*this);
        if (!edit) return;
        if (plugin != nullptr)
            plugin->configureAutomationContext(trackId_, slotIndex, gestureCore_, lastTouchedCore_);

        // Phase 1F: register this slot's plugin parameters with the APEX automation system.
        auto* processor = plugin ? plugin->getProcessor() : nullptr;
        if (processor == nullptr || trackId_.isEmpty())
            return;

        using KR  = apex::automation::AutomationParameterKeyRegistry;
        auto& sys = apex::automation::AutomationSystem::getInstance();

        AutomationBinding binding;
        binding.pluginName = processor->getName();

        // C7D-identity: display name alone is not component identity. Two
        // distinct components may share a name; the stable component UID
        // disambiguates them. Legacy name-only keys remain valid aliases so
        // existing projects, lanes, menus and recorder paths keep resolving.
        const int componentUid = plugin != nullptr ? plugin->getDescription().uniqueId : 0;

        std::unordered_map<int, apex::automation::ParameterID> indexMap;
        const auto& params = processor->getParameters();

        for (int i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            if (p == nullptr || !p->isAutomatable()) continue;

            juce::String paramID;
            if (auto* pwid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                paramID = pwid->paramID;
            if (paramID.isEmpty())
                paramID = "param" + juce::String (i);

            const auto strongKey = KR::pluginParamKey (trackId_, slotIndex,
                                                       binding.pluginName, paramID, componentUid);
            const auto legacyKey = KR::pluginParamKey (trackId_, slotIndex,
                                                       binding.pluginName, paramID, 0);

            // Identity-strong key first; a legacy project's existing lane
            // (name-only key) is REUSED so user automation is never orphaned.
            auto id = KR::getInstance().findID (strongKey);
            if (id == apex::automation::kInvalidParameterID)
                id = KR::getInstance().findID (legacyKey);
            if (id == apex::automation::kInvalidParameterID)
                id = KR::getInstance().getOrCreateID (strongKey);
            KR::getInstance().ensureAlias (legacyKey, id);   // non-destructive

            apex::automation::ParameterRange range;
            range.minValue     = 0.0f;
            range.maxValue     = 1.0f;
            range.defaultValue = p->getDefaultValue();
            range.skew         = 1.0f;
            range.isStepped    = p->isDiscrete();
            range.numSteps     = p->isDiscrete() ? p->getNumSteps() : 0;

            auto* ap = sys.getRegistry().find (id);
            if (ap == nullptr)
                ap = sys.createNativeParameter (id, binding.pluginName + " \xe2\x80\x94 " + p->getName (64), range);

            if (ap != nullptr)
            {
                ap->bindToPluginParameter (p);
                // Sync APEX's value FROM the plugin without writing back.
                // setValueProgrammatic was calling forwardToPluginIfBound which
                // called setValueNotifyingHost, resetting every plugin knob to
                // defaults on every refreshAutomationContexts call.
                ap->setValueFromPlugin (p->getValue());
                binding.indexToID.emplace (i, id);
                indexMap.emplace (i, id);
            }
        }

        // Detach first to prevent a duplicate AudioProcessorListener being added
        // every time configureSlotAutomation runs on an already-registered slot.
        apex::automation::AutomationGestureBridge::getInstance().detachFromPlugin (*processor);
        apex::automation::AutomationGestureBridge::getInstance().attachToPlugin (*processor, std::move (indexMap));
        slotAutomationBindings_[slotIndex] = std::move (binding);
    }

    void removeSlotAutomation(int slotIndex)
    {
        auto it = slotAutomationBindings_.find (slotIndex);
        if (it == slotAutomationBindings_.end()) return;

        if (auto* slot = getSlot (slotIndex))
            if (auto* processor = slot->getProcessor())
                apex::automation::AutomationGestureBridge::getInstance().detachFromPlugin (*processor);

        // Preserve lane/registry identity for undo, but remove every raw plugin
        // parameter pointer before the processor can be destroyed.
        auto& registry = apex::automation::AutomationSystem::getInstance().getRegistry();
        for (const auto& [parameterIndex, id] : it->second.indexToID)
            if (auto* parameter = registry.find(id))
                parameter->bindToPluginParameter(nullptr);

        // Lane data is intentionally kept: orphaned lanes are harmless and may
        // be recovered if the user restores the same plugin to the same slot.
        slotAutomationBindings_.erase (it);
    }

    void refreshAutomationContexts()
    {
        ControlEditScope edit(*this);
        if (!edit) return;
        for (int i = 0; i < (int)slots_.size(); ++i)
        {
            auto* slot = slots_[(size_t)i].get();
            if (slot == nullptr) continue;
            if (slotAutomationBindings_.count(i) == 0)
            {
                // New slot — do full parameter binding setup.
                configureSlotAutomation(slot, i);
            }
            else
            {
                // Already bound — only update the automation context pointers.
                // Do NOT re-run parameter binding: that would call setValueFromPlugin
                // for every parameter and could disrupt live automation read state.
                slot->configureAutomationContext(trackId_, i, gestureCore_, lastTouchedCore_);
            }
        }
    }

    void updateActiveSidechainBusConfig()
    {
        activeSidechainAuxInputBuses_.clear();

        for (const auto& [slotIndex, buses] : sidechainAuxInputBuses_)
        {
            if (slotIndex < 0 || slotIndex >= (int) slots_.size())
                continue;

            auto* slot = slots_[(size_t) slotIndex].get();
            auto* processor = slot != nullptr ? slot->getProcessor() : nullptr;
            if (processor == nullptr)
                continue;

            juce::Array<int> validBuses;
            for (int bus : buses)
                if (bus > 0 && bus < processor->getBusCount(true) && !validBuses.contains(bus))
                    validBuses.add(bus);

            if (validBuses.isEmpty())
                continue;

            activeSidechainAuxInputBuses_[slotIndex] = validBuses;
        }
    }

    /** C6-sidechain-negotiation: after the reprepare round-trip, drop any bus
        the plugin did not actually enable (setBusesLayout may be accepted by
        JUCE while a specific wrapper leaves an auxiliary bus disabled, or the
        plugin may veto the layout). Keeps the ACTIVE config honest so the
        engine never believes a silent bus is carrying a sidechain signal. */
    void verifyActiveSidechainBuses()
    {
        for (auto it = activeSidechainAuxInputBuses_.begin();
             it != activeSidechainAuxInputBuses_.end();)
        {
            const int slotIndex = it->first;
            auto& buses = it->second;

            auto* slot = slotIndex >= 0 && slotIndex < (int) slots_.size()
                ? slots_[(size_t) slotIndex].get() : nullptr;
            auto* processor = slot != nullptr ? slot->getProcessor() : nullptr;

            if (processor != nullptr)
            {
                for (int i = buses.size() - 1; i >= 0; --i)
                {
                    const int bus = buses[i];
                    auto* inputBus = bus > 0 && bus < processor->getBusCount(true)
                        ? processor->getBus(true, bus) : nullptr;
                    if (inputBus == nullptr
                        || !inputBus->isEnabled()
                        || inputBus->getCurrentLayout().isDisabled())
                    {
                        buses.remove(i);
                    }
                }
            }
            else
            {
                buses.clear();
            }

            // Iterator-safe erase: never invalidate the map iterator inside
            // the range iteration (a full-suite AV was reproduced here).
            if (buses.isEmpty())
                it = activeSidechainAuxInputBuses_.erase(it);
            else
                ++it;
        }
    }

    static bool sidechainBusListsEqual(const juce::Array<int>& a, const juce::Array<int>& b) noexcept
    {
        if (a.size() != b.size())
            return false;

        for (int i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return false;

        return true;
    }

    static const juce::Array<int>& findSidechainBusList(
        const std::unordered_map<int, juce::Array<int>>& config,
        int slotIndex,
        const juce::Array<int>& emptyList) noexcept
    {
        auto it = config.find(slotIndex);
        return it != config.end() ? it->second : emptyList;
    }

    void reprepareSlotsWithChangedSidechainBuses(
        const std::unordered_map<int, juce::Array<int>>& oldConfig,
        const std::unordered_map<int, juce::Array<int>>& newConfig)
    {
        for (int i = 0; i < (int) slots_.size(); ++i)
        {
            auto* slot = slots_[(size_t) i].get();
            if (slot == nullptr)
                continue;

            const auto& oldBuses = findSidechainBusList(oldConfig, i, emptyAuxInputBusList_);
            const auto& newBuses = findSidechainBusList(newConfig, i, emptyAuxInputBusList_);
            if (sidechainBusListsEqual(oldBuses, newBuses))
                continue;

            const bool bypassed = slot->isBypassed();
            const float slotMix = slot->getSlotMixNormalized();
            const bool slotMixBypass = slot->getSlotMixBypass();
            const auto state = slot->getState();

            if (offlinePrepared_)
                slot->prepareForOffline(sampleRate_, blockSize_, newBuses);
            else
                slot->prepare(sampleRate_, blockSize_, newBuses);

            if (state.getSize() > 0)
                slot->setState(state);
            slot->setBypassed(bypassed);
            slot->setSlotMixNormalized(slotMix);
            slot->setSlotMixBypass(slotMixBypass);
        }

        publishSnapshot();
    }

    /**
     * C1+SC capacity contract — NON-REALTIME THREADS ONLY.
     *
     * Pre-allocates every scratch buffer that processBlockWithSidechain() and
     * processSlotWithBypass() touch, to the worst-case channel topology of the
     * currently prepared slots and a sample capacity of at least
     * max(kScratchCapacityFloor, capacitySamples).
     *
     * maxChannels = max(2, maximum over slots of
     *                    max(plugin total input channels,
     *                        plugin total output channels))
     *
     * A slot with an enabled auxiliary sidechain input bus therefore raises
     * the requirement (e.g. stereo main + stereo sidechain = 4 channels) and
     * the whole scratch set is grown HERE, on the control side, before the
     * audio thread is allowed to observe the new bus configuration.
     *
     * Call sites (all message thread / drained reprepare phase):
     *   prepare(), prepareForOffline(), restoreRealtimePrepare(),
     *   setActiveSidechainBusConfig(), appendPlugin(), loadPlugin(),
     *   appendPluginInstanceForTesting(), removePlugin().
     *
     * The realtime path (processBlockWithSidechain) must NEVER call this and
     * must only VERIFY capacity; on violation it takes a deterministic,
     * allocation-free main-only fallback.
     */
    void prepareSidechainScratchCapacity(int capacitySamples)
    {
        const int capacity = juce::jmax(kScratchCapacityFloor, juce::jmax(1, capacitySamples));

        int requiredChannels = 2;
        for (const auto& slot : slots_)
        {
            auto* processor = slot != nullptr ? slot->getProcessor() : nullptr;
            if (processor == nullptr)
                continue;

            requiredChannels = juce::jmax(requiredChannels,
                juce::jmax(processor->getTotalNumInputChannels(),
                           processor->getTotalNumOutputChannels()));
        }

        // avoidReallocating=true keeps the underlying storage stable while the
        // prepared capacity only ever grows. Sizing the complete set together
        // keeps the dry/wet/combined buffers channel-consistent forever after.
        combinedBuffer_.setSize(requiredChannels, capacity, false, false, true);
        drySnapshotBuffer_.setSize(requiredChannels, capacity, false, false, true);
        wetBuffer_.setSize(requiredChannels, capacity, false, false, true);
        chainInputBuffer_.setSize(requiredChannels, capacity, false, false, true);
        automationSliceMainBuffer_.setSize(kAutomationSliceMaxChannels,
                                           kAutomationSliceSamples, false, false, true);
        automationSliceSidechainBuffer_.setSize(kAutomationSliceMaxChannels,
                                                kAutomationSliceSamples, false, false, true);
        automationMidiScratch_.ensureSize(kAutomationMidiScratchBytes);

        preparedScratchChannels_ = requiredChannels;
        preparedScratchSamples_  = capacity;
    }

    /** True when the prepared scratch set covers the given channel/sample
        requirement. Realtime-safe (plain int comparisons, no allocation). */
    bool sidechainScratchCovers(int channels, int samples) const noexcept
    {
        return channels <= combinedBuffer_.getNumChannels()
            && samples  <= combinedBuffer_.getNumSamples();
    }

    void ensureBypassScratch(int channels, int numSamples)
    {
        // C1+SC contract: the scratch set was grown by
        // prepareSidechainScratchCapacity() on the control side. The audio
        // callback never grows it. These jasserts fire in Debug if any caller
        // violated the contract; Release stays memory-safe via the bounded
        // copy loops in processSlotWithBypass().
        jassert(drySnapshotBuffer_.getNumChannels() >= channels && drySnapshotBuffer_.getNumSamples() >= numSamples);
        jassert(wetBuffer_.getNumChannels() >= channels && wetBuffer_.getNumSamples() >= numSamples);
    }

    void captureChainInput(const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        const int channels = buffer.getNumChannels();
        // Buffer is pre-allocated to 8192 in prepare(). Never grow in the audio callback.
        jassert(chainInputBuffer_.getNumChannels() >= channels && chainInputBuffer_.getNumSamples() >= numSamples);

        for (int ch = 0; ch < channels; ++ch)
            chainInputBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    bool validateProcessBuffer(const juce::AudioBuffer<float>& buffer, int numSamples) const
    {
        if (buffer.getNumChannels() <= 0 || numSamples <= 0 || buffer.getNumSamples() < numSamples)
        {
            jassertfalse;
            return false;
        }

        return true;
    }

    bool validateOfflinePreparedForProcess(int numSamples) const
    {
        if (! offlinePrepared_)
            return true;

        if (offlinePreparedSampleRate_ != sampleRate_
            || offlinePreparedBlockSize_ <= 0
            || numSamples > offlinePreparedBlockSize_)
        {
            jassertfalse;
            return false;
        }

        return true;
    }

    static constexpr int kAutomationSliceMaxChannels = 2;
    static constexpr size_t kAutomationMidiScratchBytes = 65536;

    static bool midiBufferFitsAutomationScratch(const juce::MidiBuffer* midi) noexcept
    {
        if (midi == nullptr)
            return true;
        size_t encodedBytes = 0;
        for (const auto metadata : *midi)
        {
            encodedBytes += (size_t) metadata.numBytes + 2 * sizeof(int);
            if (encodedBytes > kAutomationMidiScratchBytes)
                return false;
        }
        return true;
    }

    void applyAutomationForSnapshot(const TrackID& trackId,
                                    const AutomationSnapshot* automationSnap,
                                    int64_t samplePosition,
                                    double sampleRate,
                                    double bpm,
                                    int numSamples,
                                    const Snapshot& snap) noexcept
    {
        for (size_t i = 0; i < snap.slots.size(); ++i)
        {
            auto* slot = snap.slots[i].get();
            if (slot == nullptr)
                continue;

            if (automationSnap != nullptr && i < snap.slotMixParameterIds.size())
                if (auto* lane = automationSnap->findLaneRT(trackId, snap.slotMixParameterIds[i]))
                    if (lane->enabled && !lane->points.empty())
                    {
                        const float rawMix = lane->getValueAtSample(
                            samplePosition, slot->getSlotMixNormalized());
                        const float lastMix = slot->getLastSlotMixAutomationValue();
                        if (std::abs(lastMix - rawMix) > 0.0001f)
                        {
                            const float coeff = AutomationSmootherCore::makeCoeff(sampleRate, 0.010);
                            const float smoothed = AutomationSmootherCore::advance(
                                lastMix, rawMix, coeff, numSamples);
                            slot->setLastSlotMixAutomationValue(smoothed);
                            slot->setSlotMixNormalized(smoothed);
                        }
                        else
                        {
                            slot->setLastSlotMixAutomationValue(rawMix);
                            slot->setSlotMixNormalized(rawMix);
                        }
                    }

            slot->applyAutomationAtSample(trackId, (int)i, automationSnap,
                                          samplePosition, sampleRate, bpm, numSamples);
        }
    }

    bool canRenderAutomationInSlices(const TrackID& trackId,
                                     const AutomationSnapshot* automationSnap,
                                     int64_t samplePosition,
                                     double sampleRate,
                                     double bpm,
                                     int numSamples,
                                     const Snapshot& snap) noexcept
    {
        // Check execution mode before checking for automation. A later
        // in-process slot must not make us subdivide a chain that also owns a
        // fixed-quantum sandboxed plugin.
        for (const auto& slot : snap.slots)
            if (slot != nullptr && slot->isSandboxed())
                return false;

        for (size_t i = 0; i < snap.slots.size(); ++i)
        {
            auto* slot = snap.slots[i].get();
            if (slot == nullptr)
                continue;

            if (slot->hasAutomationChangeWithinBlock(trackId, automationSnap,
                                                     samplePosition, sampleRate,
                                                     bpm, numSamples))
                return true;

            if (automationSnap != nullptr && i < snap.slotMixParameterIds.size())
                if (const auto* lane = automationSnap->findLaneRT(
                        trackId, snap.slotMixParameterIds[i]))
                    if (lane->enabled && !lane->points.empty())
                    {
                        const int64_t end = samplePosition + numSamples - 1;
                        const float startValue = lane->getValueAtSample(
                            samplePosition, slot->getSlotMixNormalized());
                        const float endValue = lane->getValueAtSample(
                            end, slot->getSlotMixNormalized());
                        if (std::abs(slot->getLastSlotMixAutomationValue() - startValue) > 0.0001f
                            || std::abs(endValue - startValue) > 0.0001f)
                            return true;
                        const auto point = std::upper_bound(
                            lane->points.begin(), lane->points.end(), samplePosition,
                            [](int64_t position, const AutomationPoint& candidate)
                            { return position < candidate.timeSamples; });
                        if (point != lane->points.end() && point->timeSamples <= end)
                            return true;
                    }
        }
        return false;
    }

    void processBlockWithSnapshot(juce::AudioBuffer<float>& mainBuffer,
                                  juce::MidiBuffer* midiIn,
                                  juce::AudioBuffer<float>* sidechainBuffer,
                                  int numSamples,
                                  const Snapshot& snap)
    {
        const float chainOutputMix = getChainOutputMix();
        const bool applyChainOutputMix = chainOutputMix < 0.999999f && !snap.slots.empty();
        if (applyChainOutputMix)
            captureChainInput(mainBuffer, numSamples);

        juce::MidiBuffer emptyMidi;
        const bool logThisBlock = beginOfflineBlockLog(mainBuffer, numSamples, snap.slots.size());
        const bool hasMidiInput = midiIn != nullptr;
        const int scCh = sidechainBuffer != nullptr ? sidechainBuffer->getNumChannels() : 0;

        bool firstMidiSlot = true;
        for (size_t i = 0; i < snap.slots.size(); ++i)
        {
            auto& slot = snap.slots[i];
            if (!slot) continue;
            auto* bypass = i < snap.bypassCores.size() ? snap.bypassCores[i] : nullptr;

            if (hasMidiInput)
            {
                processSlotWithBypass(*slot, bypass, mainBuffer,
                                      firstMidiSlot ? *midiIn : emptyMidi, numSamples);
                firstMidiSlot = false;
                logAfterPluginIfNeeded(logThisBlock, *slot, mainBuffer, numSamples);
                continue;
            }

            auto* proc = slot->getProcessor();
            const auto& enabledAuxBuses = getEnabledAuxInputBusesForSlot((int)i);
            if (sidechainBuffer != nullptr && proc != nullptr
                && !enabledAuxBuses.isEmpty() && scCh > 0)
            {
                const int mainCh = mainBuffer.getNumChannels();
                const int totalCh = juce::jmax(proc->getTotalNumInputChannels(),
                                               proc->getTotalNumOutputChannels());
                if (!sidechainScratchCovers(totalCh, numSamples))
                {
                    jassertfalse;
                    processSlotWithBypass(*slot, bypass, mainBuffer, emptyMidi, numSamples);
                    logAfterPluginIfNeeded(logThisBlock, *slot, mainBuffer, numSamples);
                    continue;
                }

                for (int ch = 0; ch < totalCh; ++ch)
                    combinedBuffer_.clear(ch, 0, numSamples);

                const int slotMainIn = slot->getActiveMainInputChannels();
                if (slotMainIn == 1 && mainCh >= 2)
                {
                    combinedBuffer_.addFrom(0, 0, mainBuffer, 0, 0, numSamples, 0.5f);
                    combinedBuffer_.addFrom(0, 0, mainBuffer, 1, 0, numSamples, 0.5f);
                }
                else
                    for (int ch = 0; ch < juce::jmin(juce::jmin(mainCh, slotMainIn), totalCh); ++ch)
                        combinedBuffer_.copyFrom(ch, 0, mainBuffer, ch, 0, numSamples);

                for (int bus : enabledAuxBuses)
                    if (auto* inputBus = proc->getBus(true, bus))
                    {
                        if (!inputBus->isEnabled() || inputBus->getCurrentLayout().isDisabled())
                            continue;
                        const int busChannels = inputBus->getNumberOfChannels();
                        const int busOffset = proc->getChannelIndexInProcessBlockBuffer(true, bus, 0);
                        jassert(busOffset >= 0 && busOffset + busChannels <= totalCh);
                        for (int ch = 0; ch < busChannels && busOffset + ch < totalCh; ++ch)
                            combinedBuffer_.copyFrom(busOffset + ch, 0, *sidechainBuffer,
                                                     juce::jmin(ch, scCh - 1), 0, numSamples);
                    }

                processSlotWithBypass(*slot, bypass, combinedBuffer_, emptyMidi,
                                      numSamples, /*combinedLayout=*/true);
                const int slotMainOut = slot->getActiveMainOutputChannels();
                for (int ch = 0; ch < juce::jmin(mainCh, combinedBuffer_.getNumChannels()); ++ch)
                    mainBuffer.copyFrom(ch, 0, combinedBuffer_,
                                        slotMainOut == 1 ? 0 : juce::jmin(ch, slotMainOut - 1),
                                        0, numSamples);
            }
            else if (sidechainBuffer == nullptr && proc != nullptr
                     && !enabledAuxBuses.isEmpty())
            {
                const int mainCh = mainBuffer.getNumChannels();
                const int totalCh = juce::jmax(proc->getTotalNumInputChannels(),
                                               proc->getTotalNumOutputChannels());
                if (sidechainScratchCovers(totalCh, numSamples))
                {
                    for (int ch = 0; ch < totalCh; ++ch)
                        combinedBuffer_.clear(ch, 0, numSamples);
                    const int slotMainIn = slot->getActiveMainInputChannels();
                    if (slotMainIn == 1 && mainCh >= 2)
                    {
                        combinedBuffer_.addFrom(0, 0, mainBuffer, 0, 0, numSamples, 0.5f);
                        combinedBuffer_.addFrom(0, 0, mainBuffer, 1, 0, numSamples, 0.5f);
                    }
                    else
                        for (int ch = 0; ch < juce::jmin(juce::jmin(mainCh, slotMainIn), totalCh); ++ch)
                            combinedBuffer_.copyFrom(ch, 0, mainBuffer, ch, 0, numSamples);

                    processSlotWithBypass(*slot, bypass, combinedBuffer_, emptyMidi,
                                          numSamples, /*combinedLayout=*/true);
                    const int slotMainOut = slot->getActiveMainOutputChannels();
                    for (int ch = 0; ch < juce::jmin(mainCh, combinedBuffer_.getNumChannels()); ++ch)
                        mainBuffer.copyFrom(ch, 0, combinedBuffer_,
                                            slotMainOut == 1 ? 0 : juce::jmin(ch, slotMainOut - 1),
                                            0, numSamples);
                }
                else
                    processSlotWithBypass(*slot, bypass, mainBuffer, emptyMidi, numSamples);
            }
            else
                processSlotWithBypass(*slot, bypass, mainBuffer, emptyMidi, numSamples);

            logAfterPluginIfNeeded(logThisBlock, *slot, mainBuffer, numSamples);
        }

        if (applyChainOutputMix)
            PluginSlotMixCore::apply(chainInputBuffer_, mainBuffer, chainOutputMix,
                                     SlotMixCurve::EqualPower, numSamples);
    }

    bool beginOfflineBlockLog(const juce::AudioBuffer<float>& buffer, int numSamples, size_t slotCount)
    {
        if (! offlinePrepared_ || offlineDebugBlocksRemaining_ <= 0 || slotCount == 0)
            return false;

        --offlineDebugBlocksRemaining_;
        juce::Logger::writeToLog("[EXPORT CHAIN] name=" + debugName_
            + " id=" + getStableDebugId()
            + " pluginCount=" + juce::String((int) slotCount)
            + " pluginNames=\"" + getPluginNamesForDebug() + "\""
            + " preparedSR=" + juce::String(offlinePreparedSampleRate_)
            + " preparedBlock=" + juce::String(offlinePreparedBlockSize_)
            + " exportSR=" + juce::String(sampleRate_)
            + " exportBlock=" + juce::String(blockSize_));

        juce::Logger::writeToLog("[EXPORT PRE-PLUGIN RMS] name=" + debugName_
            + " L=" + juce::String(buffer.getRMSLevel(0, 0, numSamples))
            + " R=" + juce::String(buffer.getRMSLevel(juce::jmin(1, buffer.getNumChannels() - 1), 0, numSamples)));
        return true;
    }

    void logAfterPluginIfNeeded(bool shouldLog, const PluginInstanceCore& slot,
                                const juce::AudioBuffer<float>& buffer, int numSamples) const
    {
        if (! shouldLog)
            return;

        juce::Logger::writeToLog("[EXPORT AFTER PLUGIN] chain=" + debugName_
            + " plugin=" + slot.getName()
            + " L=" + juce::String(buffer.getRMSLevel(0, 0, numSamples))
            + " R=" + juce::String(buffer.getRMSLevel(juce::jmin(1, buffer.getNumChannels() - 1), 0, numSamples)));
    }

    void processSlotWithBypass(PluginInstanceCore& slot,
                               BypassCrossfadeCore* bypassCore,
                               juce::AudioBuffer<float>& buffer,
                               juce::MidiBuffer& midi,
                               int numSamples,
                               bool combinedLayout = false)
    {
        // C1+SC: the prepared scratch set (prepareSidechainScratchCapacity)
        // always covers the widest slot buffer this chain can produce, so the
        // copy channels normally equal buffer.getNumChannels() — including
        // auxiliary sidechain channels — and the wet/bypass path preserves
        // them. The jmin bound is the final defensive safety net so a Release
        // build can never copy through out-of-range channel indices even if a
        // contract violation slips through; Debug additionally jasserts.
        //
        // Each slot is presented with a VIEW of exactly copyChannels channels:
        // a non-target slot therefore sees the plain stereo topology it
        // negotiated, while the sidechain target slot sees the full combined
        // layout (main + auxiliary sidechain channels) so its sidechain input
        // survives the wet/bypass path intact.
        const int copyChannels = juce::jmin(buffer.getNumChannels(),
                                            drySnapshotBuffer_.getNumChannels());
        jassert(buffer.getNumChannels() <= drySnapshotBuffer_.getNumChannels());
        jassert(buffer.getNumChannels() <= wetBuffer_.getNumChannels());

        if (bypassCore == nullptr)
        {
            const float mix = slot.getSlotMixNormalized();
            if (slot.getSlotMixBypass() || mix < 1.0e-6f)
            {
                // A sandbox has no parent-side bypass crossfade core, but its
                // worker must still consume this callback's canonical E2B
                // events.  Process the already-prepared sandbox path against
                // a preallocated discarded-output view so parameter state and
                // E2A quantum timing remain aligned while the audible buffer
                // stays dry.  Never hand the worker's result back to the
                // bypassed output path.
                ensureBypassScratch(copyChannels, numSamples);
                for (int ch = 0; ch < copyChannels; ++ch)
                    wetBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);

                juce::AudioBuffer<float> discardedView(
                    wetBuffer_.getArrayOfWritePointers(), copyChannels, numSamples);
                if (combinedLayout)
                    slot.processBlockCombinedForced(discardedView, midi, numSamples);
                else
                    slot.processBlockForced(discardedView, midi, numSamples);
                return;
            }

            if (mix >= 0.999999f)
            {
                logProcessBlockDiag(slot, buffer);
                if (combinedLayout)
                    slot.processBlockCombinedForced(buffer, midi, numSamples);
                else
                    slot.processBlock(buffer, midi, numSamples);
                return;
            }

            ensureBypassScratch(copyChannels, numSamples);
            for (int ch = 0; ch < copyChannels; ++ch)
            {
                drySnapshotBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                wetBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            }

            // Non-owning views: the plugin sees exactly the slot's topology.
            juce::AudioBuffer<float> dryView(drySnapshotBuffer_.getArrayOfWritePointers(),
                                             copyChannels, numSamples);
            juce::AudioBuffer<float> wetView(wetBuffer_.getArrayOfWritePointers(),
                                             copyChannels, numSamples);

            logProcessBlockDiag(slot, wetView);
            if (combinedLayout)
                slot.processBlockCombinedForced(wetView, midi, numSamples);
            else
                slot.processBlockForced(wetView, midi, numSamples);
            PluginSlotMixCore::apply(dryView, wetView, mix,
                                     SlotMixCurve::EqualPower, numSamples);
            for (int ch = 0; ch < copyChannels; ++ch)
                buffer.copyFrom(ch, 0, wetView, ch, 0, numSamples);
            return;
        }

        ensureBypassScratch(copyChannels, numSamples);
        for (int ch = 0; ch < copyChannels; ++ch)
        {
            drySnapshotBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            wetBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        }

        // Non-owning views: the plugin sees exactly the slot's topology.
        juce::AudioBuffer<float> dryView(drySnapshotBuffer_.getArrayOfWritePointers(),
                                         copyChannels, numSamples);
        juce::AudioBuffer<float> wetView(wetBuffer_.getArrayOfWritePointers(),
                                         copyChannels, numSamples);

        const float mix = slot.getSlotMixNormalized();
        if (!bypassCore->isFullyBypassed() && !slot.getSlotMixBypass() && mix >= 1.0e-6f)
        {
            logProcessBlockDiag(slot, wetView);
            if (combinedLayout)
                slot.processBlockCombinedForced(wetView, midi, numSamples);
            else
                slot.processBlockForced(wetView, midi, numSamples);
        }

        if (!slot.getSlotMixBypass() && mix < 0.999999f)
            PluginSlotMixCore::apply(dryView, wetView, mix,
                                     SlotMixCurve::EqualPower, numSamples);

        bypassCore->processSlotBypass(dryView, wetView, numSamples);
        for (int ch = 0; ch < copyChannels; ++ch)
            buffer.copyFrom(ch, 0, wetView, ch, 0, numSamples);
    }

    /** Create a shared_ptr with a custom deleter that guarantees
     *  PluginInstanceCore is always destroyed on the message thread.
     *  C5: NEVER calls MessageManager::callSync — that could block the
     *  realtime audio thread for an unbounded time. Off-message-thread
     *  destruction is deferred via a bounded retire queue drained by
     *  drainRetiredPlugins() on the message thread. */
    static std::shared_ptr<PluginInstanceCore> makeSharedPlugin(
        std::unique_ptr<juce::AudioPluginInstance> instance)
    {
        return std::shared_ptr<PluginInstanceCore>(
            new PluginInstanceCore(std::move(instance)),
            [](PluginInstanceCore* p)
            {
                auto* mm = juce::MessageManager::getInstanceWithoutCreating();
                if (mm != nullptr && mm->isThisTheMessageThread())
                {
                    delete p;
                }
                else if (mm != nullptr && retirePluginForMessageThreadDeletion(p))
                {
                    // Queued — drainRetiredPlugins() will delete on the message thread.
                }
                else
                {
                    // No message thread (static teardown), or retire queue full
                    // (practically unreachable — drained every ~2 s; jassert in Debug).
                    jassert(mm == nullptr);
                    delete p;
                }
        });
    }

    /** Same message-thread-deferred deletion contract as makeSharedPlugin(),
        with one stronger safety rule: when the bounded retire queue is
        unavailable (no message thread at static teardown, or the queue is
        exhausted), the sandbox slot is intentionally leaked rather than
        destroyed inline. Inline destruction would run worker shutdown/reap
        waits on whichever thread dropped the last reference — never
        acceptable on the realtime thread. Defined in PluginChainCore.cpp. */
    static std::shared_ptr<PluginInstanceCore> makeSharedSandboxedPlugin(
        std::unique_ptr<SandboxedPluginProxyCore> proxy);

public:
    /** Bounded retire queue for deferred plugin destruction. Lock-free push
     *  from any thread; drainRetiredPlugins() runs on the message thread. */
    static bool retirePluginForMessageThreadDeletion(PluginInstanceCore* p) noexcept
    {
        const int idx = retiredPluginCount_.fetch_add(1, std::memory_order_acq_rel);
        if (idx >= kMaxRetiredPlugins)
        {
            retiredPluginCount_.fetch_sub(1, std::memory_order_acq_rel);
            jassertfalse;   // queue full — practically unreachable
            return false;
        }
        retiredPlugins_[(size_t)idx].store(p, std::memory_order_release);
        return true;
    }

    /** Delete every retired plugin. MESSAGE THREAD only — called from
     *  MainComponent::inputWatchdogTick (~2 s cadence). The full scan makes
     *  the push/drain race harmless: a pointer stored just after a drain is
     *  picked up by the next one. */
    static void drainRetiredPlugins() noexcept
    {
        for (int i = 0; i < kMaxRetiredPlugins; ++i)
            if (auto* p = retiredPlugins_[(size_t)i].exchange(nullptr, std::memory_order_acquire))
                delete p;
        retiredPluginCount_.store(0, std::memory_order_release);
    }

    /** Reclaim bypass cores whose protecting published snapshot has no
        realtime readers left. MESSAGE THREAD only. The snapshot protection is
        retained until after publication, so rapid control-plane publications
        cannot destroy a raw bypass target still present in an older snapshot. */
    void drainRetiredBypassCores() noexcept
    {
        // Keep every replaced Snapshot alive on this control-plane owner until
        // its realtime readers have dropped their shared_ptr. This also keeps
        // Snapshot's vector storage/decrements off the callback.
        drainRetiredSnapshots();

        auto it = retiredBypassCores_.begin();
        while (it != retiredBypassCores_.end())
        {
            const auto& protectingSnapshot = it->protectingSnapshot;
            if (protectingSnapshot.expired())
                it = retiredBypassCores_.erase(it);
            else
                ++it;
        }

        drainAllRetiredBypassCores();
    }

    /**
     * Drain retirement groups handed off by already-destroyed chains.
     * MESSAGE/CONTROL THREAD ONLY.  The sole strong reference held by a group
     * is counted as one; any additional owner is an active reader (including
     * an audio-thread block snapshot), so the group and every raw bypass target
     * remain untouched until a later control-plane drain.
     */
    static void drainAllRetiredBypassCores() noexcept
    {
        auto* pending = retiredBypassShutdownGroups_.exchange(
            nullptr, std::memory_order_acq_rel);
        RetiredBypassShutdownGroup* survivors = nullptr;

        while (pending != nullptr)
        {
            auto* next = pending->next;
            if (shutdownGroupIsQuiescent(*pending))
            {
                // The group destructor releases snapshots first and bypass
                // owners afterwards; this function is never an RT call site.
                delete pending;
            }
            else
            {
                pending->next = survivors;
                survivors = pending;
            }
            pending = next;
        }

        // Preserve groups handed off while the first exchange/drain was in
        // progress.  All producers are control-plane lifecycle paths; the
        // pointer exchange is used only to make the handoff lossless.
        while (survivors != nullptr)
        {
            auto* next = survivors->next;
            survivors->next = retiredBypassShutdownGroups_.exchange(
                survivors, std::memory_order_acq_rel);
            survivors = next;
        }
    }

    /** Phase D: production control-plane health service. MESSAGE THREAD only.
        For every sandboxed slot, runs the proxy's automatic death/hang
        detection and bounded restart ladder. The realtime path is untouched;
        a recovering slot falls back through the existing validated contract.
        Called from the established production maintenance tick
        (ApplicationCore::serviceSandboxWorkers). Defined in PluginChainCore.cpp
        (needs the complete SandboxedPluginProxyCore type). */
    void pollSandboxHealth();

    static constexpr int kMaxRetiredPlugins = 256;
private:
    inline static std::array<std::atomic<PluginInstanceCore*>, kMaxRetiredPlugins> retiredPlugins_ = {};
    inline static std::atomic<int> retiredPluginCount_ { 0 };

public:

    /** Build and atomically publish a new immutable snapshot from the
     *  current slots_ state.  Message thread only. */
    void publishSnapshot()
    {
        auto snap = std::make_shared<Snapshot>();
        snap->slots = slots_;  // cheap: copies shared_ptrs, not plugins
        snap->bypassCores.reserve(bypassCores_.size());
        for (auto& core : bypassCores_)
            snap->bypassCores.push_back(core.get());
        snap->slotMixParameterIds.reserve(snap->slots.size());
        for (std::size_t i = 0; i < snap->slots.size(); ++i)
            snap->slotMixParameterIds.push_back(
                AutomationManagerCore::makePluginSlotMixId(static_cast<int>(i)));
        auto oldSnapshot = std::atomic_exchange(
            &published_, std::shared_ptr<const Snapshot>(std::move(snap)));
        retainRetiredSnapshot(std::move(oldSnapshot));

        // Plugin shared_ptrs have their own message-thread-deferred deleter.
        // Bypass cores are raw pointers in the immutable snapshot, so their
        // owner must remain alive until the old snapshot itself is quiescent.
        drainRetiredBypassCores();
        retiredSlots_.clear();
    }

    void publishEmptySnapshot()
    {
        auto snap = std::make_shared<Snapshot>();
        auto oldSnapshot = std::atomic_exchange(
            &published_, std::shared_ptr<const Snapshot>(std::move(snap)));
        retainRetiredSnapshot(std::move(oldSnapshot));
        drainRetiredBypassCores();
    }

    void notifyChanged()
    {
        if (onChainChanged) onChainChanged(-1);
    }

    void attachPlayhead(PluginInstanceCore* plugin) const
    {
        if (plugin == nullptr || playheadInfo_ == nullptr) return;

        plugin->setPlayHead(playheadInfo_->getPlayhead());
        DBG("[PluginPlayhead] setPlayHead attached to plugin: " << plugin->getName());
    }

    void logInstantiationDiag(const juce::AudioPluginInstance* pluginInstance, const char* calledFrom) const
    {
        if (pluginInstance == nullptr)
            return;

        const bool isMasterTrack = debugName_.containsIgnoreCase("master") || trackId_.equalsIgnoreCase("master");
        DBG("[APEX-DIAG-INSTANTIATE] track=" << (trackId_.isNotEmpty() ? trackId_ : debugName_)
            << " isMaster=" << (isMasterTrack ? "YES" : "NO")
            << " plugin=" << pluginInstance->getName()
            << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginInstance)
            << " inputChannels=" << pluginInstance->getTotalNumInputChannels()
            << " outputChannels=" << pluginInstance->getTotalNumOutputChannels()
            << " mainInputLayout=" << pluginInstance->getChannelLayoutOfBus(true, 0).getDescription()
            << " mainOutputLayout=" << pluginInstance->getChannelLayoutOfBus(false, 0).getDescription()
            << " calledFrom=" << calledFrom);
    }

    void logProcessBlockDiag(const PluginInstanceCore& slot, const juce::AudioBuffer<float>& buffer) const
    {
#if APEX_AUDIO_DEBUG_LOGS
        static std::unordered_map<void*, int> apexDiagLastChannelCount;
        auto* pluginKey = const_cast<PluginInstanceCore*>(&slot);
        const int currentChannels = buffer.getNumChannels();
        auto it = apexDiagLastChannelCount.find(pluginKey);
        if (it == apexDiagLastChannelCount.end())
        {
            apexDiagLastChannelCount.emplace(pluginKey, currentChannels);
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << slot.getName()
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
        }
        else if (it->second != currentChannels)
        {
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << slot.getName()
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
            it->second = currentChannels;
        }
#else
        (void) slot;
        (void) buffer;
#endif
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChainCore)
};

} // namespace DAW
