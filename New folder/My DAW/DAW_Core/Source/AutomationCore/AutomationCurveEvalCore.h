#pragma once
#include <JuceHeader.h>
#include "AutomationCurveTypesCore.h"
#include <cmath>

namespace DAW {

struct AutomationCurveEvalCore
{
    static float applyTension(float t, float tension) noexcept
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        tension = juce::jlimit(-0.99f, 0.99f, tension);

        if (std::abs(tension) < 0.001f)
            return t;

        if (tension > 0.0f)
        {
            const float power = 1.0f + tension * 4.0f;
            return 1.0f - std::pow(1.0f - t, power);
        }

        const float power = 1.0f + (-tension) * 4.0f;
        return std::pow(t, power);
    }

    static float shapePosition(float t, AutomationCurveType curve, float tension) noexcept
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        tension = juce::jlimit(-1.0f, 1.0f, tension);

        switch (curve)
        {
            case AutomationCurveType::Hold:
                return 0.0f;

            case AutomationCurveType::Smooth:
            {
                const float eased = t * t * (3.0f - 2.0f * t);
                return applyTension(eased, tension * 0.65f);
            }

            case AutomationCurveType::SingleCurve:
                return applyTension(t, tension == 0.0f ? 0.45f : tension);

            case AutomationCurveType::SingleCurve2:
                return applyTension(t, tension == 0.0f ? -0.45f : tension);

            case AutomationCurveType::SingleCurve3:
                return applyTension(t, tension == 0.0f ? 0.78f : tension);

            case AutomationCurveType::DoubleCurve:
            {
                const float s = t * t * (3.0f - 2.0f * t);
                return applyTension(s, tension * 0.35f);
            }

            case AutomationCurveType::DoubleCurve2:
            {
                const float s = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
                return applyTension(s, tension * 0.45f);
            }

            case AutomationCurveType::DoubleCurve3:
            {
                const float a = t < 0.5f ? 4.0f * t * t * t
                                         : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
                return applyTension(a, tension * 0.35f);
            }

            case AutomationCurveType::HalfSine:
                return applyTension(std::sin(t * juce::MathConstants<float>::halfPi), tension * 0.5f);

            case AutomationCurveType::Stairs:
            {
                const int steps = juce::jlimit(2, 32, 2 + (int)std::round(std::abs(tension) * 30.0f));
                const float biased = applyTension(t, tension * 0.35f);
                return std::floor(biased * (float)steps) / (float)steps;
            }

            case AutomationCurveType::SmoothStairs:
            {
                const int steps = juce::jlimit(2, 32, 2 + (int)std::round(std::abs(tension) * 30.0f));
                const float scaled = t * (float)steps;
                const float base = std::floor(scaled) / (float)steps;
                const float local = scaled - std::floor(scaled);
                const float smoothed = local * local * (3.0f - 2.0f * local);
                return juce::jlimit(0.0f, 1.0f, base + smoothed / (float)steps);
            }

            case AutomationCurveType::Wave:
            {
                const float cycles = 1.0f + std::abs(tension) * 7.0f;
                const float depth = 0.08f + std::abs(tension) * 0.22f;
                const float wave = std::sin(t * cycles * juce::MathConstants<float>::twoPi) * depth;
                return juce::jlimit(0.0f, 1.0f, t + wave);
            }

            case AutomationCurveType::Pulse:
            {
                const float cycles = 1.0f + std::abs(tension) * 7.0f;
                const float phase = std::fmod(t * cycles, 1.0f);
                const float width = juce::jlimit(0.1f, 0.9f, 0.5f + tension * 0.35f);
                return phase < width ? t : std::floor(t * cycles) / cycles;
            }

            case AutomationCurveType::Linear:
            default:
                return applyTension(t, tension);
        }
    }

    static float evaluate(float valueA, float valueB, float normalisedPosition,
                          AutomationCurveType curve, float tension) noexcept
    {
        const float shaped = shapePosition(normalisedPosition, curve, tension);
        return valueA + shaped * (valueB - valueA);
    }
};

static inline float applyAutomationCurve(float t, AutomationCurveType curve, float tension = 0.0f) noexcept
{
    return AutomationCurveEvalCore::shapePosition(t, curve, tension);
}

} // namespace DAW
