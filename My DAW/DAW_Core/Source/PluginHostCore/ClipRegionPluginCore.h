#pragma once
#include <JuceHeader.h>
#include "PluginPlayheadInfoCore.h"
#include "HostedPluginIsolationCore.h"
#include "../ClipCore/Clip.h"
#include "../PluginSafetyCore/PluginSafeLoadWrapperCore.h"
#include "../PluginStorageCore/PluginDescriptionPersistenceCore.h"
#include <map>
#include <functional>
#include <stdexcept>
#include <memory>
#include <limits>
#include <unordered_map>
#include <vector>

namespace DAW {

class ClipRegionPluginCore
{
public:
    ClipRegionPluginCore() = default;

    struct LoadResult
    {
        bool success = false;
        juce::String message;
        juce::AudioPluginInstance* instance = nullptr;
        bool reusedExisting = false;
    };

    struct Entry
    {
        juce::String instanceId;
        ClipID clipId;
        juce::PluginDescription description;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        double sampleRate = 44100.0;
        int blockSize = 512;
        std::atomic<bool> bypassed { false };   // atomic: read on the audio thread via the published snapshot
        juce::MemoryBlock unresolvedState;      // opaque state retained when plugin is missing
    };

    struct EntryInfo
    {
        juce::String instanceId;
        juce::String name;
        juce::String manufacturer;
        juce::String format;
        bool bypassed = false;
    };

    struct EntryStateSnapshot
    {
        juce::String instanceId;
        juce::PluginDescription description;
        juce::MemoryBlock state;
        bool bypassed = false;
        bool unresolved = false; // true when this is an opaque missing-plugin slot
    };

    using ClipStateSnapshot = std::vector<EntryStateSnapshot>;

    void setPlayheadInfoCore(PluginPlayheadInfoCore* playheadInfo) noexcept
    {
        playheadInfo_ = playheadInfo;
        for (auto& [id, entries] : entriesByClip_)
            for (auto& entry : entries)
                if (entry && entry->instance)
                    entry->instance->setPlayHead(playheadInfo_ ? playheadInfo_->getPlayhead() : nullptr);
    }

    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = sampleRate;
        blockSize_ = blockSize;
        offlinePrepared_ = false;
        scratchMidi_.clear();

        for (auto& [id, entries] : entriesByClip_)
        {
            for (auto& entry : entries)
            {
                if (!entry || !entry->instance) continue;
                entry->sampleRate = sampleRate_;
                entry->blockSize = blockSize_;
                entry->instance->setRateAndBufferSizeDetails(sampleRate_, blockSize_);
                DBG("[APEX-DIAG-PREPARE] plugin=" << entry->instance->getName()
                    << " sampleRate=" << sampleRate_
                    << " blockSize=" << blockSize_
                    << " calledFrom=" << __FUNCTION__);
                entry->instance->prepareToPlay(sampleRate_, blockSize_);
            }
        }
    }

    void prepareForOffline(double sampleRate, int blockSize)
    {
        if (sampleRate <= 0.0 || blockSize <= 0)
        {
            jassertfalse;
            return;
        }

        sampleRate_ = sampleRate;
        blockSize_ = blockSize;
        offlinePrepared_ = true;
        scratchMidi_.clear();

        for (auto& [id, entries] : entriesByClip_)
        {
            juce::ignoreUnused(id);
            for (auto& entry : entries)
            {
                if (!entry || !entry->instance) continue;
                entry->sampleRate = sampleRate_;
                entry->blockSize = blockSize_;
                entry->instance->releaseResources();
                entry->instance->setNonRealtime(true);
                entry->instance->setRateAndBufferSizeDetails(sampleRate_, blockSize_);
                DBG("[APEX-DIAG-PREPARE] plugin=" << entry->instance->getName()
                    << " sampleRate=" << sampleRate_
                    << " blockSize=" << blockSize_
                    << " calledFrom=" << __FUNCTION__);
                entry->instance->prepareToPlay(sampleRate_, blockSize_);
            }
        }
    }

    void restoreRealtimePrepare(double sampleRate, int blockSize)
    {
        if (sampleRate <= 0.0 || blockSize <= 0)
            return;

        sampleRate_ = sampleRate;
        blockSize_ = blockSize;
        offlinePrepared_ = false;
        scratchMidi_.clear();

        for (auto& [id, entries] : entriesByClip_)
        {
            juce::ignoreUnused(id);
            for (auto& entry : entries)
            {
                if (!entry || !entry->instance) continue;
                entry->sampleRate = sampleRate_;
                entry->blockSize = blockSize_;
                entry->instance->releaseResources();
                entry->instance->setNonRealtime(false);
                entry->instance->setRateAndBufferSizeDetails(sampleRate_, blockSize_);
                DBG("[APEX-DIAG-PREPARE] plugin=" << entry->instance->getName()
                    << " sampleRate=" << sampleRate_
                    << " blockSize=" << blockSize_
                    << " calledFrom=" << __FUNCTION__);
                entry->instance->prepareToPlay(sampleRate_, blockSize_);
            }
        }
    }

    void releaseResources()
    {
        for (auto& [id, entries] : entriesByClip_)
            for (auto& entry : entries)
                if (entry && entry->instance)
                    entry->instance->releaseResources();
    }

    LoadResult loadForClip(const ClipID& clipId,
                           const juce::PluginDescription& desc,
                           juce::AudioPluginFormatManager& formatManager)
    {
        jassert(sampleRate_ > 0.0 && "sampleRate_ read before prepareToPlay");
        jassert(blockSize_  > 0   && "blockSize_ read before prepareToPlay");
        if (sampleRate_ <= 0.0 || blockSize_ <= 0)
            return { false, "Audio engine is not prepared yet.", nullptr, false };

        auto loaded = PluginSafeLoadWrapperCore::createPluginInstance(
            formatManager, desc, sampleRate_, blockSize_, "clip_region_plugin");

        if (!loaded.instance)
            return { false,
                     loaded.userFacingMessage.isNotEmpty() ? loaded.userFacingMessage : "Could not load clip-region plugin.",
                     nullptr,
                     false };

        loaded.instance->setPlayHead(playheadInfo_ ? playheadInfo_->getPlayhead() : nullptr);
        loaded.instance->setRateAndBufferSizeDetails(sampleRate_, blockSize_);
        DBG("[APEX-DIAG-INSTANTIATE] track=" << clipId
            << " isMaster=NO"
            << " plugin=" << loaded.instance->getName()
            << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)loaded.instance.get())
            << " inputChannels=" << loaded.instance->getTotalNumInputChannels()
            << " outputChannels=" << loaded.instance->getTotalNumOutputChannels()
            << " mainInputLayout=" << loaded.instance->getChannelLayoutOfBus(true, 0).getDescription()
            << " mainOutputLayout=" << loaded.instance->getChannelLayoutOfBus(false, 0).getDescription());
        DBG("[APEX-DIAG-PREPARE] plugin=" << loaded.instance->getName()
            << " sampleRate=" << sampleRate_
            << " blockSize=" << blockSize_
            << " calledFrom=" << __FUNCTION__);
        loaded.instance->prepareToPlay(sampleRate_, blockSize_);

        auto entry = std::make_shared<Entry>();
        entry->instanceId = makeInstanceId(clipId);
        entry->clipId = clipId;
        entry->description = desc;
        entry->sampleRate = sampleRate_;
        entry->blockSize = blockSize_;
        entry->instance = std::move(loaded.instance);

        auto* raw = entry.get();
        entriesByClip_[clipId].push_back(std::move(entry));
        publishEntries();

        DBG("[ClipRegionPlugin] loaded dedicated plugin=" << desc.name << " clipId=" << clipId);
        return { true, {}, raw->instance.get(), false };
    }

    Entry* findEntryById(const ClipID& clipId, const juce::String& instanceId)
    {
        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end()) return nullptr;

        for (auto& entry : it->second)
            if (entry != nullptr && entry->instanceId == instanceId)
                return entry.get();

        return nullptr;
    }

    std::vector<EntryInfo> getEntriesForClip(const ClipID& clipId) const
    {
        std::vector<EntryInfo> result;
        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end()) return result;

        for (const auto& entry : it->second)
        {
            if (!entry) continue;
            result.push_back({ entry->instanceId,
                               entry->description.name + (entry->instance ? juce::String() : juce::String(" [Missing]")),
                               entry->description.manufacturerName,
                               entry->description.pluginFormatName,
                               entry->bypassed.load (std::memory_order_acquire) });
        }

        return result;
    }

    /** Capture a detached message-thread snapshot of one clip's plugin chain. */
    ClipStateSnapshot captureClipState(const ClipID& clipId) const
    {
        ClipStateSnapshot snapshot;
        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end())
            return snapshot;

        snapshot.reserve(it->second.size());
        for (const auto& entry : it->second)
        {
            if (!entry)
                continue;

            EntryStateSnapshot saved;
            saved.instanceId = entry->instanceId;
            saved.description = entry->description;
            saved.bypassed = entry->bypassed.load(std::memory_order_acquire);
            saved.unresolved = entry->instance == nullptr;
            if (entry->instance)
                entry->instance->getStateInformation(saved.state);
            else
                saved.state = entry->unresolvedState;
            snapshot.push_back(std::move(saved));
        }
        return snapshot;
    }


    /** Capture all per-clip chains. Fail the project save rather than discard an FX state. */
    bool captureProjectState(juce::ValueTree& state, juce::String& error) const
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        error.clear();
        state = juce::ValueTree("ClipRegionPlugins");
        for (const auto& [clipId, entries] : entriesByClip_)
        {
            juce::ValueTree clipNode("ClipFxClip");
            clipNode.setProperty("clipId", clipId, nullptr);
            for (const auto& entry : entries)
            {
                if (!entry) continue;
                juce::MemoryBlock blob = entry->unresolvedState;
                if (entry->instance)
                {
                    try
                    {
                        blob.reset();
                        entry->instance->getStateInformation(blob);
                    }
                    catch (const std::exception& e)
                    {
                        error = "Clip '" + clipId + "' FX '" + entry->description.name
                            + "' capture failed: " + juce::String(e.what());
                        state = {};
                        return false;
                    }
                    catch (...)
                    {
                        error = "Clip '" + clipId + "' FX '" + entry->description.name + "' capture failed";
                        state = {};
                        return false;
                    }
                }
                juce::ValueTree slot("ClipFxSlot");
                slot.setProperty("instanceId", entry->instanceId, nullptr);
                slot.setProperty("bypassed", entry->bypassed.load(std::memory_order_acquire), nullptr);
                slot.addChild(PluginDescriptionPersistenceCore::toValueTree(entry->description), -1, nullptr);
                juce::ValueTree savedState("State");
                savedState.setProperty("data", blob.getSize() > 0
                    ? juce::Base64::toBase64(blob.getData(), blob.getSize()) : juce::String(), nullptr);
                slot.addChild(savedState, -1, nullptr);
                clipNode.addChild(slot, -1, nullptr);
            }
            if (clipNode.getNumChildren() > 0)
                state.addChild(clipNode, -1, nullptr);
        }
        return true;
    }

    /** Parse and validate first; missing processors become lossless silent placeholders. */
    bool restoreProjectState(const juce::ValueTree& state,
                             juce::AudioPluginFormatManager& formatManager,
                             const std::function<bool(const ClipID&)>& clipExists,
                             juce::String& diagnostic)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        diagnostic.clear();
        if (!state.isValid())
        {
            clearAllEntries(); // old projects had no clip-region FX records
            return true;
        }
        if (!state.hasType("ClipRegionPlugins"))
        {
            diagnostic = "Invalid ClipRegionPlugins project node";
            return false;
        }
        std::vector<std::pair<ClipID, EntryStateSnapshot>> parsed;
        juce::StringArray seenIds;
        for (int c = 0; c < state.getNumChildren(); ++c)
        {
            const auto clipNode = state.getChild(c);
            const ClipID clipId = clipNode.getProperty("clipId").toString();
            if (!clipNode.hasType("ClipFxClip") || clipId.isEmpty()
                || (clipExists && !clipExists(clipId)))
            {
                diagnostic = "Invalid or missing clip FX owner: " + clipId;
                return false;
            }
            for (int i = 0; i < clipNode.getNumChildren(); ++i)
            {
                const auto slot = clipNode.getChild(i);
                const auto descNode = slot.getChildWithName("Description");
                const auto dataNode = slot.getChildWithName("State");
                EntryStateSnapshot saved;
                saved.instanceId = slot.getProperty("instanceId").toString();
                if (!slot.hasType("ClipFxSlot") || saved.instanceId.isEmpty()
                    || seenIds.contains(saved.instanceId) || !descNode.isValid()
                    || !dataNode.isValid())
                {
                    diagnostic = "Malformed or duplicate clip FX instance: " + saved.instanceId;
                    return false;
                }
                seenIds.add(saved.instanceId);
                saved.description = PluginDescriptionPersistenceCore::fromValueTree(descNode);
                if (saved.description.pluginFormatName.isEmpty()
                    || saved.description.fileOrIdentifier.isEmpty())
                {
                    diagnostic = "Invalid clip FX plugin identity: " + saved.instanceId;
                    return false;
                }
                saved.bypassed = (bool) slot.getProperty("bypassed", false);
                const auto base64 = dataNode.getProperty("data").toString();
                if (base64.isNotEmpty())
                {
                    juce::MemoryOutputStream decoded;
                    if (!juce::Base64::convertFromBase64(decoded, base64))
                    {
                        diagnostic = "Invalid clip FX Base64 state: " + saved.instanceId;
                        return false;
                    }
                    saved.state = juce::MemoryBlock(decoded.getData(), decoded.getDataSize());
                }
                parsed.emplace_back(clipId, std::move(saved));
            }
        }

        clearAllEntries();
        for (const auto& [clipId, saved] : parsed)
        {
            const auto loaded = loadForClip(clipId, saved.description, formatManager);
            bool usable = loaded.success && loaded.instance != nullptr;
            if (usable && saved.state.getSize() > 0)
            {
                try
                {
                    if (saved.state.getSize() > static_cast<size_t>(std::numeric_limits<int>::max()))
                        throw std::runtime_error("Clip FX state exceeds int range");
                    loaded.instance->setStateInformation(saved.state.getData(),
                                                         static_cast<int>(saved.state.getSize()));
                }
                catch (...) { usable = false; }
            }
            if (usable)
            {
                if (auto* entry = findEntryByInstance(clipId, loaded.instance))
                {
                    entry->instanceId = saved.instanceId;
                    entry->bypassed.store(saved.bypassed, std::memory_order_release);
                }
            }
            else
            {
                if (loaded.instance)
                    if (auto* entry = findEntryByInstance(clipId, loaded.instance))
                        removeEntry(clipId, entry->instanceId);
                auto unresolved = std::make_shared<Entry>();
                unresolved->instanceId = saved.instanceId;
                unresolved->clipId = clipId;
                unresolved->description = saved.description;
                unresolved->bypassed.store(saved.bypassed, std::memory_order_release);
                unresolved->unresolvedState = saved.state;
                entriesByClip_[clipId].push_back(std::move(unresolved));
                if (diagnostic.isNotEmpty()) diagnostic += "; ";
                diagnostic += "Clip '" + clipId + "' plugin '" + saved.description.name
                    + "' missing: opaque state preserved";
            }
        }
        publishEntries();
        return true;
    }

    /** Must be called with realtime callbacks suspended and drained. */
    void clearAllEntries()
    {
        entriesByClip_.clear();
        publishEntries();
    }

    /** Remove the complete runtime chain for one clip. */
    void removeAllEntriesForClip(const ClipID& clipId)
    {
        if (entriesByClip_.erase(clipId) > 0)
            publishEntries();
    }

    /** Restore one clip's chain from a detached snapshot. */
    bool restoreClipState(const ClipID& clipId,
                          const ClipStateSnapshot& snapshot,
                          juce::AudioPluginFormatManager& formatManager)
    {
        const auto previous = captureClipState(clipId);
        removeAllEntriesForClip(clipId);

        if (appendSnapshotEntries(clipId, snapshot, formatManager))
            return true;

        removeAllEntriesForClip(clipId);
        (void) appendSnapshotEntries(clipId, previous, formatManager);
        return false;
    }

    /** Clone a chain into a distinct clip-keyed runtime chain. */
    bool cloneClipState(const ClipID& sourceClipId,
                        const ClipID& targetClipId,
                        juce::AudioPluginFormatManager& formatManager)
    {
        // A self-copy must not destroy/reinstantiate live processors or change
        // instance IDs: other editor state may still refer to those IDs.
        if (sourceClipId == targetClipId)
            return true;

        // Third-party getStateInformation() can throw. Capture the source
        // BEFORE touching the target, and refuse the clone on failure rather
        // than crashing the editor or replacing a valid target chain.
        ClipStateSnapshot snapshot;
        try
        {
            snapshot = captureClipState(sourceClipId);
        }
        catch (...)
        {
            return false;
        }
        return restoreClipState(targetClipId, snapshot, formatManager);
    }

    /** Return source-instance to target-instance IDs in chain order. */
    std::vector<std::pair<juce::String, juce::String>> getInstanceIdMapping(
        const ClipStateSnapshot& sourceSnapshot,
        const ClipID& targetClipId) const
    {
        std::vector<std::pair<juce::String, juce::String>> mapping;
        const auto targetEntries = getEntriesForClip(targetClipId);
        const auto count = juce::jmin(sourceSnapshot.size(), targetEntries.size());
        mapping.reserve(count);
        for (size_t i = 0; i < count; ++i)
            mapping.emplace_back(sourceSnapshot[i].instanceId, targetEntries[i].instanceId);
        return mapping;
    }

    void setBypassed(const ClipID& clipId, const juce::String& instanceId, bool bypassed)
    {
        if (auto* entry = findEntryById(clipId, instanceId))
            entry->bypassed.store (bypassed, std::memory_order_release);
    }

    /** Move one clip's live processor to a new index without recreating it.
     *  Called on the message thread. The audio thread reads a newly published
     *  immutable ordering on its next block, while keeping every Entry alive.
     */
    bool moveEntry(const ClipID& clipId, int fromIndex, int toIndex)
    {
        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end())
            return false;

        auto& entries = it->second;
        const int count = static_cast<int>(entries.size());
        if (fromIndex < 0 || fromIndex >= count
            || toIndex < 0 || toIndex >= count || fromIndex == toIndex)
            return false;

        auto moved = std::move(entries[static_cast<size_t>(fromIndex)]);
        entries.erase(entries.begin() + fromIndex);
        entries.insert(entries.begin() + toIndex, std::move(moved));
        publishEntries();
        return true;
    }

    void removeEntry(const ClipID& clipId, const juce::String& instanceId)
    {
        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end()) return;

        auto& entries = it->second;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [&instanceId](const std::shared_ptr<Entry>& entry)
            {
                return entry != nullptr && entry->instanceId == instanceId;
            }), entries.end());

        if (entries.empty())
            entriesByClip_.erase(it);
        publishEntries();
    }

    Entry* findEntry(const ClipID& clipId, const juce::PluginDescription& desc)
    {
        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end()) return nullptr;

        for (auto& entry : it->second)
        {
            if (entry != nullptr
                && entry->description.name == desc.name
                && entry->description.pluginFormatName == desc.pluginFormatName
                && entry->description.fileOrIdentifier == desc.fileOrIdentifier)
                return entry.get();
        }

        return nullptr;
    }

    /** C4: audio-thread entry query — reads the published immutable snapshot
     *  (lock-free, allocation-free), never the live map. */
    bool hasPluginsForClip(const ClipID& clipId) const
    {
        auto snap = std::atomic_load_explicit (&publishedEntries_, std::memory_order_acquire);
        if (snap == nullptr)
            return false;
        auto it = snap->find (clipId);
        return it != snap->end() && ! it->second.empty();
    }

    void processClipBlock(const ClipID& clipId, juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (buffer.getNumChannels() <= 0 || numSamples <= 0 || buffer.getNumSamples() < numSamples)
        {
            jassertfalse;
            return;
        }

        if (HostedPluginIsolationCore::shouldBypassHostedDsp())
            return;

        // Hold the published snapshot for the duration of the block — entries
        // stay alive even if the message thread removes them concurrently.
        auto snap = std::atomic_load_explicit (&publishedEntries_, std::memory_order_acquire);
        if (snap == nullptr)
            return;
        auto it = snap->find (clipId);
        if (it == snap->end()) return;

        if (offlinePrepared_ && numSamples > blockSize_)
        {
            jassertfalse;
            juce::Logger::writeToLog("[EXPORT CHAIN][WARN] clip-region plugin not prepared for export block clipId=" + clipId
                + " preparedBlock=" + juce::String(blockSize_)
                + " processSamples=" + juce::String(numSamples));
            return;
        }

        scratchMidi_.clear();
        for (auto& entry : it->second)
        {
            if (!entry || !entry->instance || entry->bypassed.load (std::memory_order_acquire)) continue;
            DBG("[ClipRegionPlugin] processing clipId=" << clipId
                << " plugin=" << entry->description.name
                << " numSamples=" << buffer.getNumSamples()
                << " chans=" << buffer.getNumChannels());
            logProcessBlockDiag(entry->instance.get(), buffer);
            // Contract (PluginInstanceCore::processBlock): the plugin must never
            // receive inactive storage beyond the frame count used to prepare
            // and schedule this quantum. The host buffer is worst-case capacity
            // (engine clipRegionPluginBuffer_), so present only the active
            // callback range via a zero-copy view sharing the same channels.
            juce::AudioBuffer<float> activeView(buffer.getArrayOfWritePointers(),
                                                buffer.getNumChannels(), numSamples);
            entry->instance->processBlock(activeView, scratchMidi_);
        }
    }

private:
    PluginPlayheadInfoCore* playheadInfo_ = nullptr;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    bool offlinePrepared_ = false;
    std::map<ClipID, std::vector<std::shared_ptr<Entry>>> entriesByClip_;
    juce::MidiBuffer scratchMidi_;
    uint64_t nextInstanceId_ = 1;

    // ── C4: published immutable entries for the audio thread ─────────────
    using EntriesSnapshot = std::map<ClipID, std::vector<std::shared_ptr<Entry>>>;
    std::shared_ptr<const EntriesSnapshot> publishedEntries_;
    // Retired snapshot destroyed at the NEXT publish (message thread), so the
    // audio thread is never the last owner of a replaced snapshot.
    std::shared_ptr<const EntriesSnapshot> retiredEntriesSnapshot_;

    /** MESSAGE THREAD only. Publishes the current entries as an immutable
     *  snapshot (shared ownership keeps entries alive for in-flight blocks). */
    void publishEntries()
    {
        auto snap = std::make_shared<EntriesSnapshot>();
        for (const auto& [clipId, entries] : entriesByClip_)
        {
            auto& vec = (*snap)[clipId];
            vec.reserve (entries.size());
            for (const auto& e : entries)
                if (e) vec.push_back (e);
        }
        std::shared_ptr<const EntriesSnapshot> immutable = std::move (snap);
        auto old = std::atomic_exchange_explicit (&publishedEntries_, immutable, std::memory_order_acq_rel);
        retiredEntriesSnapshot_ = std::move (old);
    }

    Entry* findEntryByInstance(const ClipID& clipId,
                               juce::AudioPluginInstance* instance) noexcept
    {
        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end())
            return nullptr;

        for (auto& entry : it->second)
            if (entry != nullptr && entry->instance.get() == instance)
                return entry.get();
        return nullptr;
    }

    bool appendSnapshotEntries(const ClipID& clipId,
                               const ClipStateSnapshot& snapshot,
                               juce::AudioPluginFormatManager& formatManager)
    {
        for (const auto& saved : snapshot)
        {
            auto loaded = loadForClip(clipId, saved.description, formatManager);
            bool usable = loaded.success && loaded.instance != nullptr;
            if (!usable && !saved.unresolved)
                return false; // Preserve existing rollback semantics for live FX.

            if (usable && saved.state.getSize() > 0)
            {
                try
                {
                    if (saved.state.getSize() > static_cast<size_t>(std::numeric_limits<int>::max()))
                        throw std::runtime_error("Clip FX snapshot state exceeds int range");
                    loaded.instance->setStateInformation(
                        saved.state.getData(), static_cast<int>(saved.state.getSize()));
                }
                catch (...)
                {
                    if (!saved.unresolved)
                        return false; // Failed live processor restore must roll back.
                    usable = false; // An already missing plugin stays lossless.
                }
            }

            if (usable)
            {
                if (auto* entry = findEntryByInstance(clipId, loaded.instance))
                    entry->bypassed.store(saved.bypassed, std::memory_order_release);
                continue;
            }

            // Duplicating/splitting a clip with an unavailable plugin must
            // preserve its slot, order, bypass and opaque preset state.
            if (loaded.instance)
                if (auto* entry = findEntryByInstance(clipId, loaded.instance))
                    removeEntry(clipId, entry->instanceId);
            auto unresolved = std::make_shared<Entry>();
            unresolved->instanceId = makeInstanceId(clipId);
            unresolved->clipId = clipId;
            unresolved->description = saved.description;
            unresolved->bypassed.store(saved.bypassed, std::memory_order_release);
            unresolved->unresolvedState = saved.state;
            entriesByClip_[clipId].push_back(std::move(unresolved));
        }

        publishEntries();
        return true;
    }

    void logProcessBlockDiag(juce::AudioPluginInstance* pluginInstance, const juce::AudioBuffer<float>& buffer) const
    {
#if APEX_AUDIO_DEBUG_LOGS
        static std::unordered_map<void*, int> apexDiagLastChannelCount;
        void* pluginKey = (void*)pluginInstance;
        const int currentChannels = buffer.getNumChannels();
        auto it = apexDiagLastChannelCount.find(pluginKey);
        if (it == apexDiagLastChannelCount.end())
        {
            apexDiagLastChannelCount.emplace(pluginKey, currentChannels);
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << (pluginInstance != nullptr ? pluginInstance->getName() : juce::String("null"))
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
        }
        else if (it->second != currentChannels)
        {
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << (pluginInstance != nullptr ? pluginInstance->getName() : juce::String("null"))
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
            it->second = currentChannels;
        }
#else
        (void) pluginInstance;
        (void) buffer;
#endif
    }

    juce::String makeInstanceId(const ClipID& clipId)
    {
        juce::String candidate;
        do { candidate = clipId + "_clipfx_" + juce::String((juce::int64) nextInstanceId_++); }
        while (findEntryById(clipId, candidate) != nullptr);
        return candidate;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipRegionPluginCore)
};

} // namespace DAW
