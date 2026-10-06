#pragma once
#include <JuceHeader.h>

namespace DAW {

enum class AutomationCurveType
{
    Linear = 0,
    Smooth = 1,
    Hold = 2,
    SingleCurve = 3,
    SingleCurve2 = 4,
    SingleCurve3 = 5,
    DoubleCurve = 6,
    DoubleCurve2 = 7,
    DoubleCurve3 = 8,
    HalfSine = 9,
    Stairs = 10,
    SmoothStairs = 11,
    Wave = 12,
    Pulse = 13,

    Exponential = SingleCurve,
    Logarithmic = SingleCurve2,
    SCurve = DoubleCurve,
    TapeBrake = SingleCurve3
};

static inline const char* automationCurveTypeToString(AutomationCurveType curve) noexcept
{
    switch (curve)
    {
        case AutomationCurveType::Smooth:       return "Smooth";
        case AutomationCurveType::Hold:         return "Hold";
        case AutomationCurveType::SingleCurve:  return "SingleCurve";
        case AutomationCurveType::SingleCurve2: return "SingleCurve2";
        case AutomationCurveType::SingleCurve3: return "SingleCurve3";
        case AutomationCurveType::DoubleCurve:  return "DoubleCurve";
        case AutomationCurveType::DoubleCurve2: return "DoubleCurve2";
        case AutomationCurveType::DoubleCurve3: return "DoubleCurve3";
        case AutomationCurveType::HalfSine:     return "HalfSine";
        case AutomationCurveType::Stairs:       return "Stairs";
        case AutomationCurveType::SmoothStairs: return "SmoothStairs";
        case AutomationCurveType::Wave:         return "Wave";
        case AutomationCurveType::Pulse:        return "Pulse";
        case AutomationCurveType::Linear:
        default:                                return "Linear";
    }
}

static inline juce::String automationCurveTypeToDisplayName(AutomationCurveType curve)
{
    switch (curve)
    {
        case AutomationCurveType::Smooth:       return "Smooth";
        case AutomationCurveType::Hold:         return "Hold";
        case AutomationCurveType::SingleCurve:  return "Single Curve";
        case AutomationCurveType::SingleCurve2: return "Single Curve 2";
        case AutomationCurveType::SingleCurve3: return "Single Curve 3";
        case AutomationCurveType::DoubleCurve:  return "Double Curve";
        case AutomationCurveType::DoubleCurve2: return "Double Curve 2";
        case AutomationCurveType::DoubleCurve3: return "Double Curve 3";
        case AutomationCurveType::HalfSine:     return "Half Sine";
        case AutomationCurveType::Stairs:       return "Stairs";
        case AutomationCurveType::SmoothStairs: return "Smooth Stairs";
        case AutomationCurveType::Wave:         return "Wave";
        case AutomationCurveType::Pulse:        return "Pulse";
        case AutomationCurveType::Linear:
        default:                                return "Linear";
    }
}

static inline AutomationCurveType automationCurveTypeFromString(const juce::String& text) noexcept
{
    const auto key = text.trim().removeCharacters(" _-").toLowerCase();
    if (key == "smooth")       return AutomationCurveType::Smooth;
    if (key == "hold")         return AutomationCurveType::Hold;
    if (key == "singlecurve")  return AutomationCurveType::SingleCurve;
    if (key == "singlecurve2") return AutomationCurveType::SingleCurve2;
    if (key == "singlecurve3") return AutomationCurveType::SingleCurve3;
    if (key == "doublecurve")  return AutomationCurveType::DoubleCurve;
    if (key == "doublecurve2") return AutomationCurveType::DoubleCurve2;
    if (key == "doublecurve3") return AutomationCurveType::DoubleCurve3;
    if (key == "halfsine")     return AutomationCurveType::HalfSine;
    if (key == "stairs")       return AutomationCurveType::Stairs;
    if (key == "smoothstairs") return AutomationCurveType::SmoothStairs;
    if (key == "wave")         return AutomationCurveType::Wave;
    if (key == "pulse")        return AutomationCurveType::Pulse;
    return AutomationCurveType::Linear;
}

static inline AutomationCurveType automationCurveTypeFromStoredInt(int storedValue) noexcept
{
    switch (storedValue)
    {
        case 1:  return AutomationCurveType::Smooth;
        case 2:  return AutomationCurveType::Hold;
        case 3:  return AutomationCurveType::SingleCurve;
        case 4:  return AutomationCurveType::SingleCurve2;
        case 5:  return AutomationCurveType::SingleCurve3;
        case 6:  return AutomationCurveType::DoubleCurve;
        case 7:  return AutomationCurveType::DoubleCurve2;
        case 8:  return AutomationCurveType::DoubleCurve3;
        case 9:  return AutomationCurveType::HalfSine;
        case 10: return AutomationCurveType::Stairs;
        case 11: return AutomationCurveType::SmoothStairs;
        case 12: return AutomationCurveType::Wave;
        case 13: return AutomationCurveType::Pulse;
        case 0:
        default: return AutomationCurveType::Linear;
    }
}

static inline AutomationCurveType legacyAutomationCurveTypeFromStoredInt(int storedValue) noexcept
{
    switch (storedValue)
    {
        case 1:  return AutomationCurveType::SingleCurve;
        case 2:  return AutomationCurveType::SingleCurve2;
        case 3:  return AutomationCurveType::DoubleCurve;
        case 4:  return AutomationCurveType::SingleCurve3;
        case 0:
        default: return AutomationCurveType::Linear;
    }
}

static inline juce::Array<AutomationCurveType> getAllAutomationCurveTypes()
{
    return { AutomationCurveType::Linear,
             AutomationCurveType::Smooth,
             AutomationCurveType::Hold,
             AutomationCurveType::SingleCurve,
             AutomationCurveType::SingleCurve2,
             AutomationCurveType::SingleCurve3,
             AutomationCurveType::DoubleCurve,
             AutomationCurveType::DoubleCurve2,
             AutomationCurveType::DoubleCurve3,
             AutomationCurveType::HalfSine,
             AutomationCurveType::Stairs,
             AutomationCurveType::SmoothStairs,
             AutomationCurveType::Wave,
             AutomationCurveType::Pulse };
}

} // namespace DAW
