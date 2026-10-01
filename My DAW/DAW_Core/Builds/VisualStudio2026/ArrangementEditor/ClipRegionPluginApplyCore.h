// ===========================================================================
// ClipRegionPluginApplyCore.h
// Abstract interface for applying plugins to clip regions.
// Your DAW audio engine implements this.
// ===========================================================================
#pragma once
#include "ClipPluginSlotModel.h"
#include "ClipPluginCategoryCore.h"
#include <JuceHeader.h>
#include <string>
#include <functional>

namespace ClipPlugins
{
    struct ClipRegionApplyRequest
    {
        juce::Uuid   clipId;
        double       regionStart;      // absolute project time
        double       regionLength;     // seconds
        double       regionOffset;     // sourceOffset within the audio file

        std::string  pluginName;
        std::string  pluginIdent;
        std::string  format;
        SlotApplyMode applyMode;
        ClipPlugins::PluginCategory category;

        int          slotIndex = 0;
    };

    struct ClipRegionApplyResult
    {
        bool        success      = false;
        int         pluginIndex  = -1;
        std::string errorMessage;
        bool        editorOpened = false;
    };

    // -----------------------------------------------------------------------
    class IClipRegionPluginEngine
    {
    public:
        virtual ~IClipRegionPluginEngine() = default;

        virtual ClipRegionApplyResult applyPluginToClipRegion(
            const ClipRegionApplyRequest& request) = 0;

        virtual bool removePluginFromClip(const juce::Uuid& clipId,
                                          int slotIndex) = 0;

        virtual void showClipPluginEditor(const juce::Uuid& clipId,
                                          int slotIndex,
                                          bool show) = 0;

        virtual bool isPluginActiveOnClip(const juce::Uuid& clipId,
                                          int slotIndex) = 0;

        virtual const ClipPluginChain* getClipChain(
            const juce::Uuid& clipId) = 0;

        std::function<void(const juce::Uuid& clipId)> onChainChanged;
        std::function<void(const std::string& errorMsg)> onError;
    };

    // -----------------------------------------------------------------------
    // STUB — compile-safe default. Replace with real engine implementation.
    // -----------------------------------------------------------------------
    class ClipRegionPluginEngineStub : public IClipRegionPluginEngine
    {
    public:
        ClipRegionApplyResult applyPluginToClipRegion(
            const ClipRegionApplyRequest&) override
        {
            // TODO: Implement clip region audio routing.
            // Steps needed:
            //   1. Resolve clip audio buffer from clipId + regionStart + regionLength
            //   2. If ARA2: register clip buffer as ARA2 audio source, open extension
            //   3. If ClipInsert: insert plugin into clip's inline FX chain at slotIndex
            //   4. If OfflineRender: process clip audio offline, write result to new source
            //   5. Return the plugin's chain index so the panel can track it
            return { false, -1, "Engine not yet implemented", false };
        }

        bool removePluginFromClip(const juce::Uuid&, int) override
        {
            // TODO: Remove plugin from clip chain, release ARA2 binding if any
            return false;
        }

        void showClipPluginEditor(const juce::Uuid&, int, bool) override
        {
            // TODO: Show/hide floating editor window for the plugin at slotIndex
        }

        bool isPluginActiveOnClip(const juce::Uuid&, int) override
        {
            // TODO: Query whether plugin is instantiated and active on this clip
            return false;
        }

        const ClipPluginChain* getClipChain(const juce::Uuid&) override
        {
            // TODO: Return the live plugin chain for this clip from the engine
            return nullptr;
        }
    };

} // namespace ClipPlugins
