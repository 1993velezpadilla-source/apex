#pragma once
#include <JuceHeader.h>
#include "AutomationPointCore.h"

namespace DAW {

struct AutomationTensionHandleCore
{
    TrackID trackId;
    juce::String parameterId;
    int segmentIndex = -1;
    float tension = 0.0f;
    bool selected = false;

    bool isValidFor(const std::vector<AutomationPoint>& points) const noexcept
    {
        return segmentIndex >= 0 && segmentIndex + 1 < (int)points.size();
    }

    static float dragDeltaToTension(float startTension, float dragDeltaY, bool fine) noexcept
    {
        const float divisor = fine ? 600.0f : 180.0f;
        return juce::jlimit(-1.0f, 1.0f, startTension - dragDeltaY / divisor);
    }

    static juce::Point<float> getHandlePosition(const AutomationPoint& a, const AutomationPoint& b,
                                                const juce::Rectangle<float>& segmentBounds) noexcept
    {
        const auto midX = segmentBounds.getX() + segmentBounds.getWidth() * 0.5f;
        const auto midY = segmentBounds.getY() + segmentBounds.getHeight() * 0.5f;
        (void)a;
        (void)b;
        return { midX, midY };
    }
};

} // namespace DAW
