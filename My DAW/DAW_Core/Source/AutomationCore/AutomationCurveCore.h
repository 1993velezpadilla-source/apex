#pragma once

namespace DAW {

/**
 * AutomationCurveCore — interpolation types for automation segments.
 *
 * Defines how values transition between automation points.
 */
enum class AutomationCurveType
{
    Linear,     // straight line between points
    Step,       // instant jump at next point
    SCurve,     // smooth S-curve (future)
    SlowCurve,  // logarithmic ease (future)
    FastCurve,  // exponential ease (future)
    Bezier      // user-adjustable curve (future)
};

class AutomationCurveCore
{
public:
    void setCurveType(AutomationCurveType type) noexcept { type_ = type; }
    AutomationCurveType getCurveType() const noexcept { return type_; }

    /** Interpolate between two values at a given fraction (0..1). */
    float interpolate(float startValue, float endValue, float fraction) const noexcept
    {
        fraction = juce::jlimit(0.0f, 1.0f, fraction);

        switch (type_)
        {
            case AutomationCurveType::Step:
                return startValue; // hold until next point
            case AutomationCurveType::Linear:
            default:
                return startValue + (endValue - startValue) * fraction;
        }
    }

private:
    AutomationCurveType type_ = AutomationCurveType::Linear;
};

} // namespace DAW
