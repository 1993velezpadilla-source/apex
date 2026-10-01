#pragma once
#include "BubblegumCableTypes.h"

namespace DAW {

// =====================================================================
// BubblegumCableInteractionCore — input state only.
//
// Tracks per-cable interaction flags (hover, drag, selection). Other
// cores read these; this core never draws and never mutates geometry.
// Currently a thin container — kept as a dedicated nucleo so future
// input logic has a single owner.
// =====================================================================
class BubblegumCableInteractionCore
{
public:
    void setHover(float cableId, bool hovered) noexcept
    {
        if (hovered) hoveredId_ = cableId;
        else if (std::abs(hoveredId_ - cableId) < 0.5f) hoveredId_ = kNone;
    }

    void beginDrag(float cableId) noexcept { draggingId_ = cableId; }
    void endDrag(float cableId) noexcept
    {
        if (std::abs(draggingId_ - cableId) < 0.5f) draggingId_ = kNone;
    }

    bool isHovered(float cableId)  const noexcept
    {
        return hoveredId_ != kNone && std::abs(hoveredId_ - cableId) < 0.5f;
    }
    bool isDragging(float cableId) const noexcept
    {
        return draggingId_ != kNone && std::abs(draggingId_ - cableId) < 0.5f;
    }

    void clear() noexcept { hoveredId_ = draggingId_ = kNone; }

private:
    static constexpr float kNone = -1e9f;
    float hoveredId_  = kNone;
    float draggingId_ = kNone;
};

} // namespace DAW
