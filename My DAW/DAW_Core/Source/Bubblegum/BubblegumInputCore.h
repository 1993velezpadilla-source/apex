#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * BubblegumInputCore — input priority and gesture mapping.
 *
 * Priority order:
 *   1. Knob drag (vertical drag on send knob = adjust level)
 *   2. Tap toggle (tap target strip = toggle send ON/OFF)
 *   3. Hold precision (hold + drag = fine-grained level control)
 *
 * Drag-to-route is NOT allowed. Paint routing is NOT allowed.
 * Knobs use vertical drag (up = increase, down = decrease).
 */
class BubblegumInputCore
{
public:
    enum class Action { None, ToggleSend, AdjustLevel, PrecisionLevel };

    struct InputResult
    {
        Action  action       = Action::None;
        TrackID targetTrackId;
        float   levelDelta   = 0.0f;
    };

    InputResult handleTap(const TrackID& targetId) const
    {
        return { Action::ToggleSend, targetId, 0.0f };
    }

    // Vertical drag: negative deltaY = up = increase
    InputResult handleKnobDrag(const TrackID& targetId, float dragDeltaY) const
    {
        float delta = -dragDeltaY * sensitivity_;
        return { Action::AdjustLevel, targetId, delta };
    }

    InputResult handlePrecisionDrag(const TrackID& targetId, float dragDeltaY) const
    {
        float delta = -dragDeltaY * sensitivity_ * precisionMultiplier_;
        return { Action::PrecisionLevel, targetId, delta };
    }

    void  setSensitivity(float s) noexcept { sensitivity_ = juce::jlimit(0.001f, 0.1f, s); }
    float getSensitivity() const noexcept  { return sensitivity_; }

    void setHoldActive(bool h) noexcept { holdActive_ = h; }
    bool isHoldActive() const noexcept  { return holdActive_; }

private:
    float sensitivity_         = 0.005f;
    float precisionMultiplier_ = 0.25f;
    bool  holdActive_          = false;
};

} // namespace DAW
