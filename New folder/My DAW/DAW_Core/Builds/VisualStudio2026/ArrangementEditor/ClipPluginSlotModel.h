// ===========================================================================
// ClipPluginSlotModel.h
// Represents a single plugin assigned to a specific clip region.
// A clip can have multiple plugin slots (chain).
// ===========================================================================
#pragma once
#include <string>
#include <vector>
#include "ClipPluginCategoryCore.h"

namespace ClipPlugins
{
    enum class SlotApplyMode
    {
        ClipInsert,    // plugin processes the clip inline as an insert effect
        ARA2Region,    // plugin opens the clip region as an ARA2 extension
        OfflineRender  // clip audio is rendered through plugin, result replaces clip
    };

    struct ClipPluginSlot
    {
        int                 slotIndex    = 0;
        std::string         pluginName;
        std::string         pluginIdent;
        std::string         format;              // VST3 / VST2 / CLAP / ARA2
        ClipPlugins::PluginCategory category;
        SlotApplyMode       applyMode    = SlotApplyMode::ClipInsert;
        bool                enabled      = true;
        bool                editorOpen   = false;
        float               wetDryMix    = 1.f;  // 0=dry, 1=fully wet
        std::vector<uint8_t> stateBlob;
    };

    struct ClipPluginChain
    {
        std::vector<ClipPluginSlot> slots;

        bool hasSlots() const { return !slots.empty(); }

        bool hasARA2() const
        {
            for (auto& s : slots)
                if (s.applyMode == SlotApplyMode::ARA2Region)
                    return true;
            return false;
        }

        SlotApplyMode dominantMode() const
        {
            if (hasARA2()) return SlotApplyMode::ARA2Region;
            if (!slots.empty()) return slots[0].applyMode;
            return SlotApplyMode::ClipInsert;
        }

        void addSlot(const ClipPluginSlot& slot)
        {
            slots.push_back(slot);
        }

        void removeSlot(int index)
        {
            if (index >= 0 && index < (int)slots.size())
                slots.erase(slots.begin() + index);
        }

        void moveSlot(int from, int to)
        {
            if (from < 0 || from >= (int)slots.size()) return;
            if (to < 0 || to >= (int)slots.size()) return;
            if (from == to) return;
            auto slot = slots[from];
            slots.erase(slots.begin() + from);
            slots.insert(slots.begin() + to, slot);
        }

        void setEnabled(int index, bool enabled)
        {
            if (index >= 0 && index < (int)slots.size())
                slots[index].enabled = enabled;
        }
    };

} // namespace ClipPlugins
