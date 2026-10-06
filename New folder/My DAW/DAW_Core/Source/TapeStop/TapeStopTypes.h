#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cstdint>

namespace APEX {
namespace TapeStop {

// Curve interpolation type between two adjacent automation points.
enum class CurveType : uint8_t
{
    Linear      = 0,  // y = t
    SCurve      = 1,  // smoothstep: t*t*(3-2t)
    Exponential = 2,  // y = t*t   (fast-then-slow)
    VinylBrake  = 3   // y = 1 - (1-t)^2.5  (slow-then-fast, mechanical brake)
};

// A single automation point on the tape stop lane.
// Time is stored in SAMPLES at the project sample rate — never seconds, never
// beats. Conversion from beats happens at preset-insertion time using the
// current project BPM and is then frozen. This matches how the existing clip
// automation system stores timing.
struct AutomationPoint
{
    int64_t   timeSamples = 0;     // position on the timeline, in samples
    float     value       = 0.0f;  // 0.0 = normal, 1.0 = fully stopped
    CurveType curveToNext = CurveType::SCurve;  // interpolation to next point

    bool operator<(const AutomationPoint& other) const noexcept
    {
        return timeSamples < other.timeSamples;
    }
};

// Identifies a preset curve template the user can insert.
enum class PresetType : uint8_t
{
    QuickStopQuarter,   // 1/4 beat,  Exponential, 0 -> 1
    TapeStopHalf,       // 1/2 beat,  SCurve,      0 -> 1
    TapeStopOne,        // 1 beat,    SCurve,      0 -> 1
    TapeStopTwo,        // 2 beats,   SCurve,      0 -> 1
    VinylBrake,         // 1 beat,    VinylBrake,  0 -> 0.35 -> 1
    SlowDownReturn,     // 2 beats,   SCurve,      0 -> 1 -> 0
    HardStop,           // 1/8 beat,  Exponential, 0 -> 1 (held briefly)
    VocalWordStop,      // 1/2 beat,  VinylBrake,  0 -> 1
    BeatDropStop        // 1 beat,    VinylBrake,  0 -> 1 (held briefly)
};

} // namespace TapeStop
} // namespace APEX
