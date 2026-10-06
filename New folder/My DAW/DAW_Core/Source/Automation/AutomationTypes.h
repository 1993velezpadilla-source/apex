#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace apex::automation
{
    // ----- Identification --------------------------------------------------

    using ParameterID       = std::uint32_t;
    using PluginInstanceID  = std::uint32_t;

    static constexpr ParameterID      kInvalidParameterID = 0xFFFFFFFFu;
    static constexpr PluginInstanceID kNativeInstanceID   = 0u;

    enum class ParameterScope : std::uint8_t
    {
        Native = 0,  // APEX-internal control (mixer fader, pan, send, etc.)
        Plugin = 1   // hosted plugin parameter
    };

    // ----- Automation modes (Logic/Pro Tools nomenclature) -----------------

    enum class AutomationMode : std::uint8_t
    {
        Off   = 0,   // ignore both read and write
        Read  = 1,   // play back, ignore user input on this parameter
        Write = 2,   // overwrite continuously while transport rolls
        Touch = 3,   // write only while user holds control; revert on release
        Latch = 4,   // write while user holds; hold last value on release
        Trim  = 5    // offset existing automation by the user's movement
    };

    // ----- Curve segment types --------------------------------------------

    enum class CurveType : std::uint8_t
    {
        Hold   = 0,  // step: value held until next breakpoint
        Linear = 1,  // straight line between this and next breakpoint
        Smooth = 2,  // smoothstep S-curve, shaped by tension
        Bezier = 3   // cubic Bezier with handle (reserved for Phase 2)
    };

    // ----- Tag identifying who wrote a value -------------------------------

    enum class ChangeSource : std::uint8_t
    {
        User         = 0,  // an APEX widget was dragged/clicked
        Plugin       = 1,  // a hosted plugin's own UI emitted the change
        Automation   = 2,  // the evaluator wrote it during playback
        Programmatic = 3   // preset load, undo/redo, scripted set, etc.
    };

    // ----- Range / taper ---------------------------------------------------

    struct ParameterRange
    {
        float minValue     = 0.0f;
        float maxValue     = 1.0f;
        float defaultValue = 0.0f;
        float skew         = 1.0f;   // 1.0 = linear; <1 = boost low end; >1 = boost high end
        bool  isStepped    = false;
        int   numSteps     = 0;      // only meaningful if isStepped

        float denormalize (float n) const noexcept
        {
            n = std::clamp (n, 0.0f, 1.0f);
            const float t = (skew == 1.0f) ? n : std::pow (n, skew);
            float v = minValue + t * (maxValue - minValue);
            if (isStepped && numSteps > 1)
            {
                const float stepSize = (maxValue - minValue) / float (numSteps - 1);
                v = minValue + std::round ((v - minValue) / stepSize) * stepSize;
            }
            return v;
        }

        float normalize (float v) const noexcept
        {
            const float range = maxValue - minValue;
            if (range == 0.0f) return 0.0f;
            float t = (v - minValue) / range;
            t = std::clamp (t, 0.0f, 1.0f);
            return (skew == 1.0f) ? t : std::pow (t, 1.0f / skew);
        }
    };

    // ----- Breakpoint stored in an automation lane -------------------------

    struct Breakpoint
    {
        double    timePPQ         = 0.0;            // position in PPQ ticks
        float     normalizedValue = 0.0f;           // 0..1
        CurveType curveType       = CurveType::Linear;
        float     curveTension    = 0.0f;           // -1..+1, shapes Smooth/Bezier

        bool operator< (const Breakpoint& other) const noexcept
        {
            return timePPQ < other.timePPQ;
        }
    };
}
