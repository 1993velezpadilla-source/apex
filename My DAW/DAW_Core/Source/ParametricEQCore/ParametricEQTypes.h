#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace APEX::ParametricEQ
{

// Stable product limits.  Band slots are fixed so future processor parameters
// can retain permanent identities even when a user deletes or reorders bands.
constexpr int kMaxBands = 24;
constexpr int kMaxChannels = 2;
constexpr int kMaxCutOrder = 16; // 96 dB/octave in six-dB order increments.
constexpr int kMaxSectionsPerCascade = 8;

constexpr double kMinimumFrequencyHz = 5.0;
constexpr double kMaximumFrequencyHz = 40000.0;
constexpr double kNyquistSafetyFraction = 0.49;
constexpr double kMinimumGainDb = -36.0;
constexpr double kMaximumGainDb = 36.0;
constexpr double kMinimumQ = 0.025;
constexpr double kMaximumQ = 100.0;
constexpr double kMaximumCutSlopeDbPerOctave = 96.0;

enum class FilterShape : std::uint8_t
{
    Bell = 0,
    LowShelf,
    HighShelf,
    LowCut,
    HighCut,
    Notch,
    BandPass,
    Tilt,
    FlatTilt,
    AllPass
};

// Realtime uses the robust W3C/RBJ bilinear family.  AnalogMatched uses
// Vicanek's impulse-invariant-pole magnitude fits where that paper defines a
// filter, with an explicit safe fallback to Realtime for unsupported or
// ill-conditioned cases.  Both remain minimum-phase and add zero latency.
enum class DesignMode : std::uint8_t
{
    Realtime = 0,
    AnalogMatched
};

enum class ChannelPlacement : std::uint8_t
{
    Stereo = 0,
    Left,
    Right,
    Mid,
    Side
};

enum class DetectorSource : std::uint8_t
{
    Internal = 0,
    External
};

// Per-band Dynamic EQ parameters (Phase 5). Detector/envelope ballistics
// come from the reusable APEX::Dynamics core; this struct carries the
// PEQ-owned band semantics (threshold/range contract and enabled state).
struct DynamicBandParameters
{
    bool enabled = false;
    double thresholdDb = -24.0;
    double rangeDb = 0.0;       // signed: + cuts above threshold, - boosts below
    double attackSeconds = 0.010;
    double releaseSeconds = 0.100;

    static DynamicBandParameters sanitised (DynamicBandParameters value) noexcept
    {
        if (! std::isfinite (value.thresholdDb)) value.thresholdDb = -24.0;
        value.thresholdDb = std::clamp (value.thresholdDb, -60.0, 0.0);
        if (! std::isfinite (value.rangeDb)) value.rangeDb = 0.0;
        value.rangeDb = std::clamp (value.rangeDb, -24.0, 24.0);
        if (! std::isfinite (value.attackSeconds)) value.attackSeconds = 0.010;
        if (! std::isfinite (value.releaseSeconds)) value.releaseSeconds = 0.100;
        value.attackSeconds = std::clamp (value.attackSeconds, 0.0005, 5.0);
        value.releaseSeconds = std::clamp (value.releaseSeconds, 0.001, 20.0);
        return value;
    }
};

constexpr double kDynamicKneeDb = 6.0;

// Soft-knee activation r in [0, 1] for a detector level above/below the
// threshold (quadratic over the +/- 6 dB knee).
inline double dynamicActivationForLevel (double levelDb,
                                         double thresholdDb) noexcept
{
    const auto over = levelDb - thresholdDb;
    if (over <= -kDynamicKneeDb)
        return 0.0;
    if (over >= kDynamicKneeDb)
        return 1.0;
    const auto t = (over + kDynamicKneeDb) / (2.0 * kDynamicKneeDb);
    return t * t;
}

// The APEX Dynamic EQ range law (product-level mapping; the detector and
// envelope remain the reusable APEX::Dynamics core):
//   r    = soft-knee activation over [-knee, +knee], clamped [0, 1]
//   range >= 0: dynamicGainDb = -range * r      (cut as the signal rises)
//   range <  0: dynamicGainDb = -range * (1-r)  (boost as the signal falls)
// Zero range yields exactly 0 dB in both branches, and the two branches meet
// continuously at range = 0: deterministic static-EQ behaviour with no sign
// inversion, NaN, or discontinuity while automation crosses zero range.
inline double dynamicGainDbForLevel (double levelDb,
                                     const DynamicBandParameters& parameters) noexcept
{
    const auto activation = dynamicActivationForLevel (levelDb,
                                                       parameters.thresholdDb);
    return parameters.rangeDb >= 0.0
         ? -parameters.rangeDb * activation
         : -parameters.rangeDb * (1.0 - activation);
}

constexpr int kChannelPlacementCount = 5;

inline bool isValidPlacement (ChannelPlacement placement) noexcept
{
    const auto value = static_cast<int> (placement);
    return value >= static_cast<int> (ChannelPlacement::Stereo)
        && value <= static_cast<int> (ChannelPlacement::Side);
}

inline const char* channelPlacementName (ChannelPlacement placement) noexcept
{
    switch (placement)
    {
        case ChannelPlacement::Stereo: return "Stereo";
        case ChannelPlacement::Left: return "Left";
        case ChannelPlacement::Right: return "Right";
        case ChannelPlacement::Mid: return "Mid";
        case ChannelPlacement::Side: return "Side";
    }
    return "Stereo";
}

// Peak-safe matching Mid/Side convention. A dual-mono sample x encodes to
// Mid=x, while an anti-correlated pair x/-x encodes to Side=x. The matching
// inverse is unity and does not introduce a hidden 3 dB internal gain.
inline void encodeMidSide (double left, double right,
                           double& mid, double& side) noexcept
{
    mid = 0.5 * (left + right);
    side = 0.5 * (left - right);
}

inline void decodeMidSide (double mid, double side,
                           double& left, double& right) noexcept
{
    left = mid + side;
    right = mid - side;
}

struct BandSettings
{
    bool enabled = false;
    bool bypassed = false;
    FilterShape shape = FilterShape::Bell;
    ChannelPlacement placement = ChannelPlacement::Stereo;
    double frequencyHz = 1000.0;
    double gainDb = 0.0;
    double q = 1.0;
    double slopeDbPerOctave = 12.0;
};

inline double maximumUsableFrequency (double sampleRate) noexcept
{
    const auto validRate = std::isfinite (sampleRate) && sampleRate > 1.0
                         ? sampleRate : 44100.0;
    return std::max (kMinimumFrequencyHz,
                     std::min (kMaximumFrequencyHz,
                               validRate * kNyquistSafetyFraction));
}

inline BandSettings sanitise (BandSettings value, double sampleRate) noexcept
{
    if (! std::isfinite (value.frequencyHz))
        value.frequencyHz = 1000.0;
    if (! std::isfinite (value.gainDb))
        value.gainDb = 0.0;
    if (! std::isfinite (value.q))
        value.q = 1.0;
    if (! std::isfinite (value.slopeDbPerOctave))
        value.slopeDbPerOctave = 12.0;

    const auto shapeValue = static_cast<int> (value.shape);
    if (shapeValue < static_cast<int> (FilterShape::Bell)
        || shapeValue > static_cast<int> (FilterShape::AllPass))
        value.shape = FilterShape::Bell;
    if (! isValidPlacement (value.placement))
        value.placement = ChannelPlacement::Stereo;

    value.frequencyHz = std::clamp (value.frequencyHz,
                                    kMinimumFrequencyHz,
                                    maximumUsableFrequency (sampleRate));
    value.gainDb = std::clamp (value.gainDb,
                               kMinimumGainDb,
                               kMaximumGainDb);
    value.q = std::clamp (value.q, kMinimumQ, kMaximumQ);
    value.slopeDbPerOctave = std::clamp (value.slopeDbPerOctave,
                                         0.0,
                                         kMaximumCutSlopeDbPerOctave);
    return value;
}

inline bool isCutShape (FilterShape shape) noexcept
{
    return shape == FilterShape::LowCut || shape == FilterShape::HighCut;
}

inline bool isDynamicCompatibleShape (FilterShape shape) noexcept
{
    return shape == FilterShape::Bell
        || shape == FilterShape::LowShelf
        || shape == FilterShape::HighShelf
        || shape == FilterShape::Tilt
        || shape == FilterShape::FlatTilt;
}

} // namespace APEX::ParametricEQ
