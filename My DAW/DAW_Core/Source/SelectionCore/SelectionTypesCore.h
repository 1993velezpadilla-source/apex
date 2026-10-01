#pragma once
#include <JuceHeader.h>

namespace DAW {

enum class SelectionKind
{
    Track,
    Clip,
    PluginSlot
};

struct SelectionTarget
{
    SelectionKind kind     = SelectionKind::Track;
    juce::String  trackId;
    juce::String  clipId;
    int           pluginSlotIndex = -1;

    bool operator==(const SelectionTarget& o) const noexcept
    {
        if (kind != o.kind) return false;
        switch (kind)
        {
            case SelectionKind::Track:      return trackId == o.trackId;
            case SelectionKind::Clip:       return trackId == o.trackId && clipId == o.clipId;
            case SelectionKind::PluginSlot: return trackId == o.trackId && pluginSlotIndex == o.pluginSlotIndex;
        }
        return false;
    }

    bool operator!=(const SelectionTarget& o) const noexcept { return !(*this == o); }

    bool isValid() const noexcept
    {
        switch (kind)
        {
            case SelectionKind::Track:      return trackId.isNotEmpty();
            case SelectionKind::Clip:       return trackId.isNotEmpty() && clipId.isNotEmpty();
            case SelectionKind::PluginSlot: return trackId.isNotEmpty() && pluginSlotIndex >= 0;
        }
        return false;
    }

    static SelectionTarget track(const juce::String& id)
    {
        SelectionTarget t;
        t.kind = SelectionKind::Track;
        t.trackId = id;
        return t;
    }

    static SelectionTarget clip(const juce::String& trackId, const juce::String& clipId)
    {
        SelectionTarget t;
        t.kind = SelectionKind::Clip;
        t.trackId = trackId;
        t.clipId = clipId;
        return t;
    }

    static SelectionTarget pluginSlot(const juce::String& trackId, int slotIndex)
    {
        SelectionTarget t;
        t.kind = SelectionKind::PluginSlot;
        t.trackId = trackId;
        t.pluginSlotIndex = slotIndex;
        return t;
    }
};

} // namespace DAW
