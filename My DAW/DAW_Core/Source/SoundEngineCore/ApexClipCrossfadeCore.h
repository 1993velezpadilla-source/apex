#pragma once

#include <cmath>
#include <cstdint>
#include <utility>

namespace DAW::SoundEngine
{

/** A timeline-sample interval used by the prepared clip crossfade plan.
    The interval is half-open for rendering: [startSample, endSample).
    Crossfade curves are evaluated continuously at the two boundaries. */
struct ClipCrossfadeRange
{
    int64_t startSample = 0;
    int64_t endSample   = 0;

    constexpr bool isValid() const noexcept
    {
        return endSample > startSample;
    }
};

struct ClipCrossfadeState
{
    ClipCrossfadeRange fadeIn;
    ClipCrossfadeRange fadeOut;

    constexpr bool hasAnyRange() const noexcept
    {
        return fadeIn.isValid() || fadeOut.isValid();
    }
};

struct ClipCrossfadeGeometry
{
    int64_t startSample = 0;
    int64_t endSample   = 0;
};

/**
    Stateless, allocation-free audio crossfade math shared by the control-plane
    planner and the realtime/offline mixer.

    A clip's existing gain/fade/tape/plugin processing remains upstream.  This
    core contributes only the prepared relationship gain for an overlapping
    pair, so ordinary per-clip fade curves are not changed or double-applied.
 */
class ApexClipCrossfadeCore final
{
public:
    static ClipCrossfadeRange makeOverlap(int64_t aStart,
                                          int64_t aEnd,
                                          int64_t bStart,
                                          int64_t bEnd) noexcept
    {
        if (aEnd <= aStart || bEnd <= bStart)
            return {};

        const int64_t start = aStart > bStart ? aStart : bStart;
        const int64_t end   = aEnd < bEnd ? aEnd : bEnd;
        return { start, end };
    }

    /** Build one deterministic adjacent relationship.  The caller supplies
        clips in stable left-to-right order; the relationship itself owns only
        the exact intersection, never a manually chosen fade duration. */
    static bool prepareAdjacentRelationship(const ClipCrossfadeGeometry& left,
                                            const ClipCrossfadeGeometry& right,
                                            ClipCrossfadeState& leftState,
                                            ClipCrossfadeState& rightState) noexcept
    {
        const auto overlap = makeOverlap(left.startSample,
                                         left.endSample,
                                         right.startSample,
                                         right.endSample);
        if (!overlap.isValid())
            return false;

        leftState.fadeOut = overlap;
        rightState.fadeIn = overlap;
        return true;
    }

    /** Return the complementary equal-power pair at a timeline sample.
        The first value is the earlier/left clip (cosine); the second is the
        later/right clip (sine).  An invalid range is unity for both paths. */
    static std::pair<float, float> equalPowerPairAt(int64_t timelineSample,
                                                     ClipCrossfadeRange range) noexcept
    {
        if (!range.isValid())
            return { 1.0f, 1.0f };

        const double length = static_cast<double>(range.endSample - range.startSample);
        const double t = clamp01((static_cast<double>(timelineSample) -
                                  static_cast<double>(range.startSample)) / length);
        const float theta = static_cast<float>(t * 1.57079632679489661923);
        return { std::cos(theta), std::sin(theta) };
    }

    /** Gain for the clip that fades in across the prepared range. */
    static float fadeInGainAt(int64_t timelineSample,
                              ClipCrossfadeRange range) noexcept
    {
        if (!range.isValid())
            return 1.0f;
        if (timelineSample <= range.startSample)
            return 0.0f;
        if (timelineSample >= range.endSample)
            return 1.0f;

        return equalPowerPairAt(timelineSample, range).second;
    }

    /** Gain for the clip that fades out across the prepared range. */
    static float fadeOutGainAt(int64_t timelineSample,
                               ClipCrossfadeRange range) noexcept
    {
        if (!range.isValid())
            return 1.0f;
        if (timelineSample <= range.startSample)
            return 1.0f;
        if (timelineSample >= range.endSample)
            return 0.0f;

        return equalPowerPairAt(timelineSample, range).first;
    }

    /** Compose the crossfade relationship for one isolated clip. */
    static float gainAt(int64_t timelineSample,
                        const ClipCrossfadeState& state) noexcept
    {
        return fadeInGainAt(timelineSample, state.fadeIn)
             * fadeOutGainAt(timelineSample, state.fadeOut);
    }

private:
    static double clamp01(double value) noexcept
    {
        return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    }
};

} // namespace DAW::SoundEngine
