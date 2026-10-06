#pragma once
#include <JuceHeader.h>
#include "PluginPlayheadInfoCore.h"
#include "../ClipCore/Clip.h"
#include "../PluginSafetyCore/PluginSafeLoadWrapperCore.h"
#include <map>
#include <memory>
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
        bool bypassed = false;
    };

    struct EntryInfo
    {
        juce::String instanceId;
        juce::String name;
        juce::String manufacturer;
        juce::String format;
        bool bypassed = false;
    };

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

        auto entry = std::make_unique<Entry>();
        entry->instanceId = makeInstanceId(clipId);
        entry->clipId = clipId;
        entry->description = desc;
        entry->sampleRate = sampleRate_;
        entry->blockSize = blockSize_;
        entry->instance = std::move(loaded.instance);

        auto* raw = entry.get();
        entriesByClip_[clipId].push_back(std::move(entry));

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
                               entry->description.name,
                               entry->description.manufacturerName,
                               entry->description.pluginFormatName,
                               entry->bypassed });
        }

        return result;
    }

    void setBypassed(const ClipID& clipId, const juce::String& instanceId, bool bypassed)
    {
        if (auto* entry = findEntryById(clipId, instanceId))
            entry->bypassed = bypassed;
    }

    void removeEntry(const ClipID& clipId, const juce::String& instanceId)
    {
        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end()) return;

        auto& entries = it->second;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [&instanceId](const std::unique_ptr<Entry>& entry)
            {
                return entry != nullptr && entry->instanceId == instanceId;
            }), entries.end());

        if (entries.empty())
            entriesByClip_.erase(it);
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

    bool hasPluginsForClip(const ClipID& clipId) const
    {
        auto it = entriesByClip_.find(clipId);
        return it != entriesByClip_.end() && !it->second.empty();
    }

    void processClipBlock(const ClipID& clipId, juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (buffer.getNumChannels() <= 0 || numSamples <= 0 || buffer.getNumSamples() < numSamples)
        {
            jassertfalse;
            return;
        }

        auto it = entriesByClip_.find(clipId);
        if (it == entriesByClip_.end()) return;

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
            if (!entry || !entry->instance || entry->bypassed) continue;
            DBG("[ClipRegionPlugin] processing clipId=" << clipId
                << " plugin=" << entry->description.name
                << " numSamples=" << buffer.getNumSamples()
                << " chans=" << buffer.getNumChannels());
            logProcessBlockDiag(entry->instance.get(), buffer);
            entry->instance->processBlock(buffer, scratchMidi_);
        }
    }

private:
    PluginPlayheadInfoCore* playheadInfo_ = nullptr;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    bool offlinePrepared_ = false;
    std::map<ClipID, std::vector<std::unique_ptr<Entry>>> entriesByClip_;
    juce::MidiBuffer scratchMidi_;
    uint64_t nextInstanceId_ = 1;

    void logProcessBlockDiag(juce::AudioPluginInstance* pluginInstance, const juce::AudioBuffer<float>& buffer) const
    {
        static std::unordered_map<void*, int> apexDiagLastChannelCount;
        void* pluginKey = (void*)pluginInstance;
        const int currentChannels = buffer.getNumChannels();
        if (apexDiagLastChannelCount[pluginKey] != currentChannels)
        {
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << (pluginInstance != nullptr ? pluginInstance->getName() : juce::String("null"))
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
            apexDiagLastChannelCount[pluginKey] = currentChannels;
        }
    }

    juce::String makeInstanceId(const ClipID& clipId)
    {
        return clipId + "_clipfx_" + juce::String((juce::int64) nextInstanceId_++);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipRegionPluginCore)
};

} // namespace DAW
