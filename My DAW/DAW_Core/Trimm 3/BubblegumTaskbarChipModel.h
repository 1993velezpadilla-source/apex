#pragma once
#include <JuceHeader.h>
#include "IBubblegumTaskbarHost.h"

namespace DAW {

/**
 * BubblegumTaskbarChipModel
 *
 * Per-chip data. POD-ish struct held inside the BubblegumTaskbarCore chip
 * list. The Layout core writes `bounds` based on the host's label width.
 * The Component writes `isHovered` from mouse events. The Manager writes
 * `isActive` based on host lifecycle (restore -> active, minimize -> idle).
 *
 * Lookup is by host pointer. Two panels with identical labels are still two
 * distinct chips because their host pointers differ.
 */
struct BubblegumTaskbarChipModel
{
    IBubblegumTaskbarHost* host { nullptr };
    juce::Rectangle<float> bounds;
    bool                   isActive  { false };
    bool                   isHovered { false };
};

} // namespace DAW
