// ===========================================================================
// ClipFadeCore.h
// Pure logic for fade gain calculations.
// Supports multiple fade curve shapes.
// ===========================================================================
#pragma once
#include <cmath>

namespace ArrangementEditor
{
    enum class FadeShape
    {
        Linear,
        SCurve,
        EqualPower,
        Logarithmic,
        Exponential
    };

    class ClipFadeCore
    {
    public:
        // Compute fade-in gain at a specific time position within the clip
        // timeInClip: time from clip start (0..clip length)
        // fadeInLength: duration of fade in seconds
        // Returns: gain multiplier 0..1
        static float fadeInGainAt(double timeInClip, double fadeInLength, FadeShape shape = FadeShape::Linear)
        {
            if (fadeInLength <= 0.0 || timeInClip >= fadeInLength)
                return 1.f;
            if (timeInClip <= 0.0)
                return 0.f;

            float t = (float)(timeInClip / fadeInLength); // 0..1
            return applyShape(t, shape);
        }

        // Compute fade-out gain at a specific time position within the clip
        // timeInClip: time from clip start
        // clipLength: total clip length
        // fadeOutLength: duration of fade in seconds
        // Returns: gain multiplier 0..1
        static float fadeOutGainAt(double timeInClip, double clipLength, double fadeOutLength, FadeShape shape = FadeShape::Linear)
        {
            if (fadeOutLength <= 0.0)
                return 1.f;

            double fadeStart = clipLength - fadeOutLength;
            if (timeInClip <= fadeStart)
                return 1.f;
            if (timeInClip >= clipLength)
                return 0.f;

            float t = (float)((timeInClip - fadeStart) / fadeOutLength); // 0..1
            return 1.f - applyShape(t, shape);
        }

        // Combined fade gain (multiply fade-in * fade-out)
        static float combinedFadeGain(double timeInClip, double clipLength,
                                       double fadeInLength, double fadeOutLength,
                                       FadeShape shape = FadeShape::Linear)
        {
            float fi = fadeInGainAt(timeInClip, fadeInLength, shape);
            float fo = fadeOutGainAt(timeInClip, clipLength, fadeOutLength, shape);
            return fi * fo;
        }

    private:
        static float applyShape(float t, FadeShape shape)
        {
            t = juce::jlimit(0.f, 1.f, t);

            switch (shape)
            {
            case FadeShape::Linear:
                return t;

            case FadeShape::SCurve:
                return t * t * (3.f - 2.f * t); // smoothstep

            case FadeShape::EqualPower:
                return std::sinf(t * juce::MathConstants<float>::halfPi);

            case FadeShape::Logarithmic:
                return std::log10f(1.f + 9.f * t); // log10(1..10) = 0..1

            case FadeShape::Exponential:
                return (std::expf(t) - 1.f) / (juce::MathConstants<float>::euler - 1.f);

            default:
                return t;
            }
        }
    };

} // namespace ArrangementEditor
