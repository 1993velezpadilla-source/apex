#pragma once
#include <JuceHeader.h>
#include "PluginInstanceCore.h"
#include "BypassCrossfadeCore.h"
#include "PluginPlayheadInfoCore.h"
#include "../AutomationCore/PluginSlotMixCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../PluginScanCore/PluginScanAuditLogCore.h"
#include "../PluginSafetyCore/PluginSafeLoadWrapperCore.h"
#include "../PluginUICore/PluginUserFacingFailureMessageCore.h"
#include "../Automation/AutomationSystemCore.h"
#include "../Automation/AutomationParameterKeyCore.h"
#include <cstdint>
#include <memory>
#include <unordered_map>

#ifndef APEX_AUDIO_DEBUG_LOGS
#define APEX_AUDIO_DEBUG_LOGS 0
#endif

namespace DAW {

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
    /** Immutable snapshot of the plugin chain for lock-free audio-thread use. */
    struct Snapshot
    {
        std::vector<std::shared_ptr<PluginInstanceCore>> slots;
        std::vector<BypassCrossfadeCore*> bypassCores;
    };

    PluginChainCore()
    {
        publishSnapshot();
    }

    void setAutomationContext(const TrackID& trackId,
                              PluginAutomationGestureCore* gestureCore,
                              LastTouchedPluginParameterCore* lastTouchedCore) noexcept
    {
        trackId_ = trackId;
        gestureCore_ = gestureCore;
        lastTouchedCore_ = lastTouchedCore;
        refreshAutomationContexts();
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
        retiredSlots_.clear();
        retiredBypassCores_.clear();
        slots_.clear();
        bypassCores_.clear();
    }

    // ── Lifecycle ────────────────────────────────────────────────────────

    void prepare(double sampleRate, int blockSize)
    {
        updateActiveSidechainBusConfig();
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;
        offlinePrepared_ = false;

        for (int i = 0; i < (int) slots_.size(); ++i)
            if (auto& slot = slots_[(size_t) i])
                slot->prepare(sampleRate, blockSize, getEnabledAuxInputBusesForSlot(i));
        for (auto& core : bypassCores_)
            if (core) core->prepare(sampleRate);

        drySnapshotBuffer_.setSize(2, blockSize, false, false, true);
        wetBuffer_.setSize(2, blockSize, false, false, true);
        combinedBuffer_.setSize(2, blockSize, false, false, true);

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

        drySnapshotBuffer_.setSize(2, blockSize, false, false, true);
        wetBuffer_.setSize(2, blockSize, false, false, true);
        combinedBuffer_.setSize(2, blockSize, false, false, true);

        for (int i = 0; i < (int) slots_.size(); ++i)
            if (auto& slot = slots_[(size_t) i])
                slot->prepareForOffline(sampleRate, blockSize, getEnabledAuxInputBusesForSlot(i));
        for (auto& core : bypassCores_)
            if (core) core->prepare(sampleRate);

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

        drySnapshotBuffer_.setSize(2, blockSize, false, false, true);
        wetBuffer_.setSize(2, blockSize, false, false, true);
        combinedBuffer_.setSize(2, blockSize, false, false, true);

        for (int i = 0; i < (int) slots_.size(); ++i)
            if (auto& slot = slots_[(size_t) i])
                slot->restoreRealtimePrepare(sampleRate, blockSize, getEnabledAuxInputBusesForSlot(i));
        for (auto& core : bypassCores_)
            if (core) core->prepare(sampleRate);

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
    const juce::Array<int>& getEnabledAuxInputBusesForSlot(int slotIndex) const noexcept
    {
        auto it = activeSidechainAuxInputBuses_.find(slotIndex);
        return it != activeSidechainAuxInputBuses_.end() ? it->second : emptyAuxInputBusList_;
    }

    void setActiveSidechainBusConfig(const std::unordered_map<int, juce::Array<int>>& sidechainAuxInputBuses)
    {
        const auto oldActiveConfig = activeSidechainAuxInputBuses_;
        sidechainAuxInputBuses_ = sidechainAuxInputBuses;
        updateActiveSidechainBusConfig();

        if (sampleRate_ > 0.0 && blockSize_ > 0)
            reprepareSlotsWithChangedSidechainBuses(oldActiveConfig, activeSidechainAuxInputBuses_);
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

    /** Append a plugin to the end of the chain. Returns slot index or -1. */
    int appendPlugin(const juce::PluginDescription& desc,
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
        wrapped->prepare(sampleRate_, blockSize_, getEnabledAuxInputBusesForSlot((int) slots_.size()));

        slots_.push_back(std::move(wrapped));
        auto bypassCore = std::make_unique<BypassCrossfadeCore>();
        bypassCore->prepare(sampleRate_);
        bypassCores_.push_back(std::move(bypassCore));
        publishSnapshot();
        notifyChanged();
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginChainCore::appendPlugin success"
                " plugin=\"" + desc.name + "\""
                + " slotIndex=" + juce::String((int) slots_.size() - 1)
                + " slotCount=" + juce::String((int) slots_.size()));
        return (int)slots_.size() - 1;
    }

    /** Insert a plugin at a specific index. */
    bool loadPlugin(int slotIndex,
                    const juce::PluginDescription& desc,
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
        wrapped->prepare(sampleRate_, blockSize_, getEnabledAuxInputBusesForSlot(slotIndex));

        // Retire the old plugin at this slot (if any)
        if (slotIndex >= 0 && slotIndex < (int)slots_.size() && slots_[slotIndex])
        {
            removeSlotAutomation (slotIndex);
            slots_[slotIndex]->closeEditor();
            retiredSlots_.push_back(std::move(slots_[slotIndex]));
            retiredBypassCores_.push_back(std::move(bypassCores_[slotIndex]));
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

        publishSnapshot();
        refreshAutomationContexts();
        notifyChanged();
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginChainCore::loadPlugin success"
                " slotIndex=" + juce::String(slotIndex)
                + " plugin=\"" + desc.name + "\""
                + " slotCount=" + juce::String((int) slots_.size()));
        return true;
    }

    void removePlugin(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= (int)slots_.size()) return;
        if (!slots_[slotIndex]) return;

        removeSlotAutomation (slotIndex);

        // Close editor now (message thread) so destructor has no UI work
        slots_[slotIndex]->closeEditor();
        retiredSlots_.push_back(std::move(slots_[slotIndex]));
        retiredBypassCores_.push_back(std::move(bypassCores_[slotIndex]));
        slots_.erase(slots_.begin() + slotIndex);
        bypassCores_.erase(bypassCores_.begin() + slotIndex);

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
                                 double bpm = 120.0) noexcept
    {
        for (int i = 0; i < (int)slots_.size(); ++i)
        {
            auto* slot = slots_[(size_t)i].get();
            if (slot == nullptr)
                continue;

            if (automationSnap != nullptr)
            {
                if (auto* lane = automationSnap->findLane(trackId, AutomationManagerCore::makePluginSlotMixId(i)))
                    if (lane->enabled && !lane->points.empty())
                        slot->setSlotMixNormalized(lane->getValueAtSample(samplePosition, slot->getSlotMixNormalized()));
            }

            slot->applyAutomationAtSample(trackId, i, automationSnap, samplePosition, sampleRate, bpm);
        }
    }

    /** Move a slot from one index to another (reorder). */
    void moveSlot(int fromIndex, int toIndex)
    {
        int n = (int)slots_.size();
        if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n) return;
        if (fromIndex == toIndex) return;

        auto moving = std::move(slots_[fromIndex]);
        auto movingBypass = std::move(bypassCores_[fromIndex]);
        slots_.erase(slots_.begin() + fromIndex);
        bypassCores_.erase(bypassCores_.begin() + fromIndex);
        slots_.insert(slots_.begin() + juce::jmin(toIndex, (int)slots_.size()), std::move(moving));
        bypassCores_.insert(bypassCores_.begin() + juce::jmin(toIndex, (int)bypassCores_.size()), std::move(movingBypass));

        publishSnapshot();
        refreshAutomationContexts();
        notifyChanged();
    }

    /** Copy a plugin's state from another chain into this chain (for drag-copy). */
    int copyPluginFrom(const PluginChainCore& srcChain, int srcSlot,
                       juce::AudioPluginFormatManager& formatManager)
    {
        auto* src = srcChain.getSlot(srcSlot);
        if (!src) return -1;

        auto desc = src->getDescription();
        juce::String err;
        int idx = appendPlugin(desc, formatManager, err);
        if (idx >= 0)
        {
            // Copy plugin state (preset/parameters)
            auto state = src->getState();
            if (state.getSize() > 0)
                slots_[idx]->setState(state);
        }
        return idx;
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
        if (! validateProcessBuffer(buffer, numSamples)) return;
        if (! validateOfflinePreparedForProcess(numSamples)) return;

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
            processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                  buffer, emptyMidi, numSamples);
            logAfterPluginIfNeeded(logThisBlock, *slot, buffer, numSamples);
        }

        if (applyChainOutputMix)
            PluginSlotMixCore::apply(chainInputBuffer_, buffer, chainOutputMix, SlotMixCurve::EqualPower);
    }

    /** Like processBlock() but forwards a live MidiBuffer to the first plugin
        (instrument), then passes empty MIDI to subsequent FX slots. */
    void processBlockWithMidi(juce::AudioBuffer<float>& buffer,
                              juce::MidiBuffer& midiIn,
                              int numSamples)
    {
        if (! validateProcessBuffer(buffer, numSamples)) return;
        if (! validateOfflinePreparedForProcess(numSamples)) return;

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
            PluginSlotMixCore::apply(chainInputBuffer_, buffer, chainOutputMix, SlotMixCurve::EqualPower);
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
        if (! validateProcessBuffer(mainBuffer, numSamples)) return;
        if (! validateOfflinePreparedForProcess(numSamples)) return;

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
                // process buffer layout JUCE expects for the enabled aux buses.
                const int mainCh = mainBuffer.getNumChannels();
                const int totalCh = juce::jmax(proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels());
                combinedBuffer_.setSize(totalCh, numSamples, false, false, true);

                for (int ch = 0; ch < totalCh; ++ch)
                    combinedBuffer_.clear(ch, 0, numSamples);

                for (int ch = 0; ch < juce::jmin(mainCh, totalCh); ++ch)
                    combinedBuffer_.copyFrom(ch, 0, mainBuffer, ch, 0, numSamples);

                for (int bus : enabledAuxBuses)
                {
                    if (auto* inputBus = proc->getBus(true, bus))
                    {
                        if (!inputBus->isEnabled() || inputBus->getCurrentLayout().isDisabled())
                            continue;

                        const int busChannels = inputBus->getNumberOfChannels();
                        const int busOffset = proc->getChannelIndexInProcessBlockBuffer(true, bus, 0);
                        for (int ch = 0; ch < busChannels && busOffset + ch < totalCh; ++ch)
                            combinedBuffer_.copyFrom(busOffset + ch, 0, sidechainBuffer,
                                                     juce::jmin(ch, scCh - 1), 0, numSamples);
                    }
                }

                processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                      combinedBuffer_, emptyMidi, numSamples);

                // Copy main channels back (sidechain channels are discarded)
                for (int ch = 0; ch < mainCh; ++ch)
                    mainBuffer.copyFrom(ch, 0, combinedBuffer_, ch, 0, numSamples);
            }
            else
            {
                processSlotWithBypass(*slot, i < snap->bypassCores.size() ? snap->bypassCores[i] : nullptr,
                                      mainBuffer, emptyMidi, numSamples);
            }
            logAfterPluginIfNeeded(logThisBlock, *slot, mainBuffer, numSamples);
        }

        if (applyChainOutputMix)
            PluginSlotMixCore::apply(chainInputBuffer_, mainBuffer, chainOutputMix, SlotMixCurve::EqualPower);
    }

    // ── Listener ─────────────────────────────────────────────────────────

    std::function<void(int slotIndex)> onChainChanged;

    // ── Serialization ────────────────────────────────────────────────────

    /** Save chain to ValueTree (plugin descriptions + states) */
    juce::ValueTree getState() const
    {
        juce::ValueTree chain("PluginChain");
        chain.setProperty("chainOutputMixNormalized", getChainOutputMix(), nullptr);
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

            // Save plugin description
            auto desc = plugin->getDescription();
            juce::ValueTree descTree("Description");
            descTree.setProperty("name", desc.name, nullptr);
            descTree.setProperty("descriptiveName", desc.descriptiveName, nullptr);
            descTree.setProperty("pluginFormatName", desc.pluginFormatName, nullptr);
            descTree.setProperty("category", desc.category, nullptr);
            descTree.setProperty("manufacturerName", desc.manufacturerName, nullptr);
            descTree.setProperty("version", desc.version, nullptr);
            descTree.setProperty("fileOrIdentifier", desc.fileOrIdentifier, nullptr);
            descTree.setProperty("lastFileModTime", (juce::int64)desc.lastFileModTime.toMilliseconds(), nullptr);
            descTree.setProperty("lastInfoUpdateTime", (juce::int64)desc.lastInfoUpdateTime.toMilliseconds(), nullptr);
            descTree.setProperty("uniqueId", desc.deprecatedUid, nullptr);
            descTree.setProperty("isInstrument", desc.isInstrument, nullptr);
            descTree.setProperty("numInputChannels", desc.numInputChannels, nullptr);
            descTree.setProperty("numOutputChannels", desc.numOutputChannels, nullptr);
            descTree.setProperty("hasSharedContainer", desc.hasSharedContainer, nullptr);
            slot.addChild(descTree, -1, nullptr);

            // Save plugin state (parameters + preset)
            auto stateBlock = plugin->getState();
            if (stateBlock.getSize() > 0)
            {
                juce::ValueTree stateTree("State");
                stateTree.setProperty("data", juce::Base64::toBase64(stateBlock.getData(), stateBlock.getSize()), nullptr);
                slot.addChild(stateTree, -1, nullptr);
            }

            chain.addChild(slot, -1, nullptr);
        }
        return chain;
    }

    /** Restore chain from ValueTree */
    void restoreState(const juce::ValueTree& chain, juce::AudioPluginFormatManager& formatManager)
    {
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
            if (!descTree.isValid()) continue;

            juce::PluginDescription desc;
            desc.name = descTree.getProperty("name", "");
            desc.descriptiveName = descTree.getProperty("descriptiveName", "");
            desc.pluginFormatName = descTree.getProperty("pluginFormatName", "");
            desc.category = descTree.getProperty("category", "");
            desc.manufacturerName = descTree.getProperty("manufacturerName", "");
            desc.version = descTree.getProperty("version", "");
            desc.fileOrIdentifier = descTree.getProperty("fileOrIdentifier", "");
            desc.lastFileModTime = juce::Time((juce::int64)descTree.getProperty("lastFileModTime", 0));
            desc.lastInfoUpdateTime = juce::Time((juce::int64)descTree.getProperty("lastInfoUpdateTime", 0));
            desc.deprecatedUid = descTree.getProperty("uniqueId", 0);
            desc.isInstrument = descTree.getProperty("isInstrument", false);
            desc.numInputChannels = descTree.getProperty("numInputChannels", 2);
            desc.numOutputChannels = descTree.getProperty("numOutputChannels", 2);
            desc.hasSharedContainer = descTree.getProperty("hasSharedContainer", false);

            // Load plugin
            juce::String errorMsg;
            if (index >= 0 && appendPlugin(desc, formatManager, errorMsg) >= 0)
            {
                int loadedIndex = (int)slots_.size() - 1;
                if (auto* plugin = slots_[loadedIndex].get())
                {
                    plugin->setPluginInstanceId(pluginInstanceId);
                    plugin->setBypassed(bypassed);
                    plugin->setSlotMixNormalized(slotMix);
                    plugin->setSlotMixBypass(slotMixBypass);

                    // Restore plugin state
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
                                plugin->setState(block);
                            }
                        }
                    }
                }
            }
        }

        publishSnapshot();
        refreshAutomationContexts();
        notifyChanged();
    }

private:
    // Message-thread canonical state (UI reads/writes this directly)
    std::vector<std::shared_ptr<PluginInstanceCore>> slots_;
    std::vector<std::unique_ptr<BypassCrossfadeCore>> bypassCores_;

    // Published snapshot — swapped atomically between message and audio threads.
    // Audio thread loads this once at block start and processes the stable list.
    std::shared_ptr<const Snapshot> published_;

    // Plugins removed from slots_ but kept alive so the audio thread's old
    // snapshot references remain valid. Cleared at the start of each
    // publishSnapshot() — by that point the audio thread has loaded the
    // current snapshot and released any older ones.
    std::vector<std::shared_ptr<PluginInstanceCore>> retiredSlots_;
    std::vector<std::unique_ptr<BypassCrossfadeCore>> retiredBypassCores_;

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

            const auto key = KR::pluginParamKey (trackId_, slotIndex,
                                                binding.pluginName,
                                                paramID);
            const auto id  = KR::getInstance().getOrCreateID (key);

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

        // Lane data is intentionally kept: orphaned lanes are harmless and may
        // be recovered if the user restores the same plugin to the same slot.
        slotAutomationBindings_.erase (it);
    }

    void refreshAutomationContexts()
    {
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

    void ensureBypassScratch(int channels, int numSamples)
    {
        if (drySnapshotBuffer_.getNumChannels() < channels || drySnapshotBuffer_.getNumSamples() < numSamples)
            drySnapshotBuffer_.setSize(channels, juce::jmax(numSamples, blockSize_), false, false, true);
        if (wetBuffer_.getNumChannels() < channels || wetBuffer_.getNumSamples() < numSamples)
            wetBuffer_.setSize(channels, juce::jmax(numSamples, blockSize_), false, false, true);
    }

    void captureChainInput(const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        const int channels = buffer.getNumChannels();
        if (chainInputBuffer_.getNumChannels() < channels || chainInputBuffer_.getNumSamples() < numSamples)
            chainInputBuffer_.setSize(channels, juce::jmax(numSamples, blockSize_), false, false, true);

        for (int ch = 0; ch < channels; ++ch)
            chainInputBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    bool validateProcessBuffer(const juce::AudioBuffer<float>& buffer, int numSamples) const
    {
        if (buffer.getNumChannels() <= 0 || numSamples <= 0 || buffer.getNumSamples() < numSamples)
        {
            jassertfalse;
            juce::Logger::writeToLog("[EXPORT CHAIN][WARN] invalid plugin buffer chain=" + debugName_
                + " channels=" + juce::String(buffer.getNumChannels())
                + " samples=" + juce::String(numSamples));
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
            juce::Logger::writeToLog("[EXPORT CHAIN][WARN] offline chain not prepared for export chain=" + debugName_
                + " preparedSR=" + juce::String(offlinePreparedSampleRate_)
                + " preparedBlock=" + juce::String(offlinePreparedBlockSize_)
                + " processSamples=" + juce::String(numSamples));
            return false;
        }

        return true;
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
                               int numSamples)
    {
        if (bypassCore == nullptr)
        {
            const float mix = slot.getSlotMixNormalized();
            if (slot.getSlotMixBypass() || mix < 1.0e-6f)
                return;

            if (mix >= 0.999999f)
            {
                logProcessBlockDiag(slot, buffer);
                slot.processBlock(buffer, midi);
                return;
            }

            ensureBypassScratch(buffer.getNumChannels(), numSamples);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                drySnapshotBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                wetBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            }
            logProcessBlockDiag(slot, wetBuffer_);
            slot.processBlockForced(wetBuffer_, midi);
            PluginSlotMixCore::apply(drySnapshotBuffer_, wetBuffer_, mix, SlotMixCurve::EqualPower);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.copyFrom(ch, 0, wetBuffer_, ch, 0, numSamples);
            return;
        }

        ensureBypassScratch(buffer.getNumChannels(), numSamples);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            drySnapshotBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            wetBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        }

        const float mix = slot.getSlotMixNormalized();
        if (!bypassCore->isFullyBypassed() && !slot.getSlotMixBypass() && mix >= 1.0e-6f)
        {
            logProcessBlockDiag(slot, wetBuffer_);
            slot.processBlockForced(wetBuffer_, midi);
        }

        if (!slot.getSlotMixBypass() && mix < 0.999999f)
            PluginSlotMixCore::apply(drySnapshotBuffer_, wetBuffer_, mix, SlotMixCurve::EqualPower);

        bypassCore->processSlotBypass(drySnapshotBuffer_, wetBuffer_, numSamples);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom(ch, 0, wetBuffer_, ch, 0, numSamples);
    }

    /** Create a shared_ptr with a custom deleter that guarantees
     *  PluginInstanceCore is always destroyed on the message thread. */
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
                else if (mm != nullptr)
                {
                    juce::MessageManager::callSync([p]()
                    {
                        delete p;
                    });
                }
                else
                {
                    delete p;
                }
            });
    }

    /** Build and atomically publish a new immutable snapshot from the
     *  current slots_ state.  Message thread only. */
    void publishSnapshot()
    {
        // Release plugins retired by the PREVIOUS mutation.  By now the
        // audio thread has loaded the current snapshot and dropped any
        // reference to older ones, so these are safe to free.
        retiredSlots_.clear();
        retiredBypassCores_.clear();

        auto snap = std::make_shared<Snapshot>();
        snap->slots = slots_;  // cheap: copies shared_ptrs, not plugins
        snap->bypassCores.reserve(bypassCores_.size());
        for (auto& core : bypassCores_)
            snap->bypassCores.push_back(core.get());
        std::atomic_store(&published_,
                          std::shared_ptr<const Snapshot>(std::move(snap)));
    }

    void publishEmptySnapshot()
    {
        auto snap = std::make_shared<Snapshot>();
        std::atomic_store(&published_,
                          std::shared_ptr<const Snapshot>(std::move(snap)));
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
        static std::unordered_map<void*, int> apexDiagLastChannelCount;
        auto* pluginKey = const_cast<PluginInstanceCore*>(&slot);
        const int currentChannels = buffer.getNumChannels();
        if (apexDiagLastChannelCount[pluginKey] != currentChannels)
        {
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << slot.getName()
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
            apexDiagLastChannelCount[pluginKey] = currentChannels;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChainCore)
};

} // namespace DAW
