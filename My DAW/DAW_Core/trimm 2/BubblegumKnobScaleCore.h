#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace DAW {

/**
 * BubblegumKnobScaleCore
 *
 * Maps a knob value to a rotation angle on the dial. Two modes:
 *
 *   Linear         : value linearly maps from [min..max] to [minAngle..maxAngle].
 *                    Unity ends up wherever the linear interpolation places it.
 *
 *   BipolarUnity   : unity always sits at the top (angle 0). Each side of unity
 *                    is scaled independently so that minValue->minAngle and
 *                    maxValue->maxAngle even when the range is asymmetric (e.g.
 *                    -24..+24 dB unity 0, or -12..+24 dB unity 0).
 *
 * Convention: angles in degrees from vertical, positive = clockwise (right).
 */
class BubblegumKnobScaleCore
{
public:
    enum class Mode { Linear, BipolarUnity };

    struct Config
    {
        Mode  mode        { Mode::BipolarUnity };
        float minAngleDeg { -135.0f };
        float maxAngleDeg {  135.0f };
        float unityValue  {    0.0f };
    };

    BubblegumKnobScaleCore() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    /** Rotation angle in degrees for the given value. */
    float valueToAngleDeg(float value, float minValue, float maxValue) const noexcept
    {
        if (config_.mode == Mode::Linear)
            return mapLinear(value, minValue, maxValue);

        return mapBipolar(value, minValue, maxValue);
    }

    /** Helpful for renderers that need to know where unity sits. */
    float unityAngleDeg() const noexcept
    {
        return config_.mode == Mode::BipolarUnity ? 0.0f
                                                  : (config_.minAngleDeg + config_.maxAngleDeg) * 0.5f;
    }

    float minAngleDeg() const noexcept { return config_.minAngleDeg; }
    float maxAngleDeg() const noexcept { return config_.maxAngleDeg; }

    /** Cartesian point on a circle around centre at the given angle. */
    static juce::Point<float> pointOnArc(juce::Point<float> centre,
                                         float radius,
                                         float angleDeg) noexcept
    {
        const float r = juce::degreesToRadians(angleDeg);
        return { centre.x + radius * std::sin(r),
                 centre.y - radius * std::cos(r) };
    }

private:
    float mapLinear(float v, float minV, float maxV) const noexcept
    {
        const float t = juce::jlimit(0.0f, 1.0f,
                                     (v - minV) / juce::jmax(1.0e-6f, maxV - minV));
        return juce::jmap(t, 0.0f, 1.0f, config_.minAngleDeg, config_.maxAngleDeg);
    }

    float mapBipolar(float v, float minV, float maxV) const noexcept
    {
        const float u = config_.unityValue;
        if (v < u)
        {
            const float span = juce::jmax(1.0e-6f, u - minV);
            const float t = juce::jlimit(0.0f, 1.0f, (v - minV) / span);
            return juce::jmap(t, 0.0f, 1.0f, config_.minAngleDeg, 0.0f);
        }
        else
        {
            const float span = juce::jmax(1.0e-6f, maxV - u);
            const float t = juce::jlimit(0.0f, 1.0f, (v - u) / span);
            return juce::jmap(t, 0.0f, 1.0f, 0.0f, config_.maxAngleDeg);
        }
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumKnobScaleCore)
};

} // namespace DAW
