#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace DAW {

/**
 * BubblegumKnobInteractionCore
 *
 * Pure drag/interaction state machine for the knob. No JUCE Component, no
 * paint code — just math turning mouse events into new values.
 *
 * Drag model: vertical drag, pixelsPerFullRange pixels of movement equals one
 * full minValue->maxValue sweep. Fine mode (Shift held) divides sensitivity
 * by fineModeMultiplier for precise edits. Snap mode (Ctrl held) rounds to
 * the nearest integer or to unity if within snapTolerance.
 *
 * Double-click resets to a caller-supplied default (typically the binding's
 * defaultValue). Right-click is intentionally not handled here — the
 * Component layer raises a context menu separately.
 */
class BubblegumKnobInteractionCore
{
public:
    struct Config
    {
        float pixelsPerFullRange  { 250.0f };
        float fineModeDivisor     { 10.0f  };
        float snapTolerance       { 0.5f   };  // value-space radius around an integer
        float unitySnapTolerance  { 1.0f   };  // value-space radius around unity
    };

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void beginDrag(float startValue, int startScreenY) noexcept
    {
        startValue_ = startValue;
        startY_     = startScreenY;
        active_     = true;
    }

    /**
     * @return  the new value the knob should write back through its binding
     */
    float computeDragValue(int currentScreenY,
                           float minValue,
                           float maxValue,
                           float unityValue,
                           bool  fineMode,
                           bool  snapMode) const noexcept
    {
        if (! active_) return startValue_;

        const float range = juce::jmax(1.0e-6f, maxValue - minValue);
        const float sensitivityPx = config_.pixelsPerFullRange
                                  * (fineMode ? config_.fineModeDivisor : 1.0f);
        const float deltaPx = static_cast<float>(currentScreenY - startY_);

        float v = startValue_ - deltaPx * range / sensitivityPx;
        v = juce::jlimit(minValue, maxValue, v);

        if (snapMode)
        {
            if (std::abs(v - unityValue) <= config_.unitySnapTolerance)
                v = unityValue;
            else
                v = std::round(v);
        }

        return v;
    }

    void endDrag() noexcept { active_ = false; }
    bool isDragging() const noexcept { return active_; }

private:
    Config config_;
    float  startValue_ { 0.0f };
    int    startY_     { 0 };
    bool   active_     { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumKnobInteractionCore)
};

} // namespace DAW
