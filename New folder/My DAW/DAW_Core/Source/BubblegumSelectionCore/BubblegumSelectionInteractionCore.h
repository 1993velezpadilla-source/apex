#pragma once
#include "BubblegumSelectionStateCore.h"

namespace DAW::BubblegumSelection
{
    class BubblegumSelectionInteractionCore
    {
    public:
        static void beginDrag(BubblegumSelectionStateCore& state, juce::Point<float> point) noexcept
        {
            state.beginDrag(point);
        }

        static void updateDrag(BubblegumSelectionStateCore& state, juce::Point<float> point) noexcept
        {
            state.updateDrag(point);
        }

        static void endDrag(BubblegumSelectionStateCore& state) noexcept
        {
            state.endDrag();
        }
    };
}
