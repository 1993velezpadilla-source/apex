#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace DAW {

/**
 * AnalogVuScaleCore
 *
 * Pure math / geometry for the VU face. Defines the dB <-> needle-angle curve
 * via a small lookup table (linearly interpolated in dB) so the curve matches
 * the mvMeter2 visual reference exactly instead of being fit to an analytic
 * formula.
 *
 * Convention: angles are degrees from vertical, positive = clockwise (needle
 * leans to the right). 0 degrees points straight up. The pivot is below the
 * face and is owned by the face layout — this class only does math relative
 * to a caller-supplied pivot point.
 *
 * No state. All static. Used by every renderer that needs to know where a
 * given dB value sits on the dial.
 */
class AnalogVuScaleCore
{
public:
    struct TickDef
    {
        float       db;         // dB value at this tick
        float       angleDeg;   // degrees from vertical, + = right
        const char* label;      // null = unlabeled subdivision
        bool        isRedZone;  // draw label/tick in red-zone ink colour
    };

    /** The labelled long-tick table, left to right. */
    static const std::array<TickDef, 11>& getMajorTicks() noexcept
    {
        static const std::array<TickDef, 11> kTicks = { {
            { -20.0f, -40.0f, "-20", false },
            { -10.0f, -22.0f, "-10", false },
            {  -7.0f, -13.0f,  "-7", false },
            {  -5.0f,  -7.0f,  "-5", false },
            {  -3.0f,  -1.0f,  "-3", false },
            {  -2.0f,   2.0f,  "-2", false },
            {  -1.0f,   6.0f,  "-1", false },
            {   0.0f,  10.0f,   "0", true  },
            {   1.0f,  20.0f,  "+1", true  },
            {   2.0f,  30.0f,  "+2", true  },
            {   3.0f,  38.0f,  "+3", true  },
        } };
        return kTicks;
    }

    /** Short, unlabelled subdivision ticks for finer reading. */
    static const std::array<TickDef, 5>& getMinorTicks() noexcept
    {
        static const std::array<TickDef, 5> kTicks = { {
            { -15.0f, -28.0f, nullptr, false },
            {  -8.0f, -16.0f, nullptr, false },
            {  -6.0f, -10.0f, nullptr, false },
            {  -4.0f,  -4.0f, nullptr, false },
            {   0.5f,  14.0f, nullptr, true  },
        } };
        return kTicks;
    }

    static float getMinDb()              noexcept { return -20.0f; }
    static float getMaxDb()              noexcept { return  +3.0f; }
    static float getRedZoneStartAngle()  noexcept { return  10.0f; }
    static float getRedZoneEndAngle()    noexcept { return  38.0f; }
    static float getMinAngleDeg()        noexcept { return -40.0f; }
    static float getMaxAngleDeg()        noexcept { return  38.0f; }

    /** Linear-in-dB interpolation across the major-tick table. */
    static float dbToAngleDeg(float db) noexcept
    {
        const auto& major = getMajorTicks();

        if (db <= major.front().db) return major.front().angleDeg;
        if (db >= major.back().db)  return major.back().angleDeg;

        for (size_t i = 1; i < major.size(); ++i)
        {
            if (db <= major[i].db)
            {
                const float t = (db - major[i - 1].db)
                              / (major[i].db - major[i - 1].db);
                return juce::jmap(t, 0.0f, 1.0f,
                                  major[i - 1].angleDeg,
                                  major[i].angleDeg);
            }
        }
        return major.back().angleDeg;
    }

    static float gainToAngleDeg(float gain) noexcept
    {
        const float db = gain <= 1.0e-6f ? -120.0f : 20.0f * std::log10(gain);
        return dbToAngleDeg(db);
    }

    /** Cartesian point on the arc at given radius from pivot. */
    static juce::Point<float> pointOnArc(juce::Point<float> pivot,
                                         float radius,
                                         float angleDeg) noexcept
    {
        const float rad = juce::degreesToRadians(angleDeg);
        return { pivot.x + radius * std::sin(rad),
                 pivot.y - radius * std::cos(rad) };
    }

    static bool isInRedZone(float angleDeg) noexcept
    {
        return angleDeg >= getRedZoneStartAngle()
            && angleDeg <= getRedZoneEndAngle();
    }

private:
    AnalogVuScaleCore() = delete;
};

} // namespace DAW
