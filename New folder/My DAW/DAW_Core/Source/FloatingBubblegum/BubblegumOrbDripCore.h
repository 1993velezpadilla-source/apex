#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumOrbDripCore — slow liquid edge deformation for premium gel feel.
 *
 * Generates subtle, non-repetitive edge displacement values
 * using layered sine waves (Perlin-like approach).
 * The result is a smooth, organic wobble on the orb boundary.
 */
class BubblegumOrbDripCore
{
public:
    static constexpr int   kNumPoints     = 32;   // edge sample points around the orb
    static constexpr float kBasePeriodMs  = 4200.f;
    static constexpr float kBaseAmplitude = 1.8f;  // pixels of displacement

    struct DripState
    {
        float displacements[kNumPoints] = {};
    };

    void tick(float deltaMs)
    {
        phase_ += deltaMs;
    }

    void setIntensity(float i) noexcept { intensity_ = juce::jlimit(0.0f, 1.0f, i); }
    float getIntensity() const noexcept { return intensity_; }

    DripState compute() const
    {
        DripState s;
        float amp = kBaseAmplitude * intensity_;

        for (int i = 0; i < kNumPoints; ++i)
        {
            float angle = (float)i / (float)kNumPoints;

            // Three layered sine waves at different frequencies and phases
            float t1 = std::sin((phase_ / kBasePeriodMs + angle * 2.3f) * juce::MathConstants<float>::twoPi);
            float t2 = std::sin((phase_ / (kBasePeriodMs * 0.73f) + angle * 3.7f + 0.5f) * juce::MathConstants<float>::twoPi) * 0.5f;
            float t3 = std::sin((phase_ / (kBasePeriodMs * 1.41f) + angle * 1.1f + 1.2f) * juce::MathConstants<float>::twoPi) * 0.3f;

            s.displacements[i] = (t1 + t2 + t3) * amp;
        }
        return s;
    }

private:
    float phase_     = 0.f;
    float intensity_ = 0.3f; // default idle drip amount
};

} // namespace DAW
