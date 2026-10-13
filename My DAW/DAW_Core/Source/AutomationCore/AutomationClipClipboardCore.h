#pragma once
#include <JuceHeader.h>
#include "AutomationClipRegionCore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace DAW {

/**
 * A reusable automation-clip state clipboard, *not* an audio-thread API.
 *
 * Clipboard values stay in the target's real parameter domain.  Prior code
 * silently clipped pan (-1..1), track volume (0..2) and clip pitch values to
 * 0..1, corrupting valid producer automation. Paste is now transactional and
 * intentionally limited to the exact same parameter identity.
 */
class AutomationClipClipboardCore
{
public:
    struct ClipState
    {
        juce::String sourceParameterId;
        int64_t sourceLengthSamples = 0;
        // Historical name retained for source compatibility. Each point
        // stores ORIGINAL local samples, not quantized millionths: sub-ms
        // automation edits survive long multi-hour clip duplication.
        std::vector<AutomationPoint> normalisedPoints;
        juce::String name;
        juce::Colour color = juce::Colour(0xfff06bd8);
    };

    static constexpr size_t maxClipboardPoints = 32768;
    // The clipboard rescales sample positions through floating-point time.
    // Stay within the exact-integer range of IEEE-754 double so llround()
    // cannot overflow when given malformed project lengths near INT64_MAX.
    static constexpr int64_t maxExactSampleLength = (int64_t{1} << 53) - 1;

    // A failed copy must be reported as failed: previously the manager
    // returned true for a malformed/empty region, while the clipboard still
    // contained an older pattern. A subsequent Paste could surprise the user.
    bool copyRegionState(const AutomationClipRegionCore& region)
    {
        if (!region.isValid() || region.trackId.isEmpty()
            || region.lengthSamples > maxExactSampleLength
            || region.startSample < 0
            || region.lengthSamples > std::numeric_limits<int64_t>::max()
                                     - region.startSample
            || region.localPoints.empty()
            || region.localPoints.size() > maxClipboardPoints)
            return false;

        ClipState state;
        state.sourceParameterId = region.parameterId;
        state.sourceLengthSamples = region.lengthSamples;
        state.name = region.name;
        state.color = region.color;
        state.normalisedPoints.reserve(region.localPoints.size());

        for (auto point : region.localPoints)
        {
            if (point.timeSamples < 0 || point.timeSamples > region.lengthSamples
                || !std::isfinite(point.value) || !std::isfinite(point.tensionToNext))
                return false; // leave previous valid clipboard intact

            point.value = AutomationLaneCore::clampValueForParameter(
                state.sourceParameterId, point.value);
            point.tensionToNext = juce::jlimit(-1.0f, 1.0f, point.tensionToNext);
            state.normalisedPoints.push_back(point);
        }

        clipState_ = std::move(state);
        return true;
    }

    bool hasClipState() const noexcept { return clipState_.has_value(); }
    void clear() noexcept { clipState_.reset(); }
    const std::optional<ClipState>& getClipState() const noexcept { return clipState_; }

    bool canPasteTo(const AutomationClipRegionCore& destination) const noexcept
    {
        return clipState_.has_value()
            && destination.isValid()
            && destination.trackId.isNotEmpty()
            && destination.lengthSamples <= maxExactSampleLength
            && destination.startSample >= 0
            && destination.lengthSamples <= std::numeric_limits<int64_t>::max()
                                                    - destination.startSample
            && destination.parameterId == clipState_->sourceParameterId
            && !clipState_->normalisedPoints.empty();
    }

    bool pasteStateToRegion(AutomationClipRegionCore& destination) const
    {
        if (!canPasteTo(destination))
            return false;

        std::vector<AutomationPoint> updated;
        updated.reserve(clipState_->normalisedPoints.size());

        for (auto point : clipState_->normalisedPoints)
        {
            if (point.timeSamples < 0 || point.timeSamples > clipState_->sourceLengthSamples
                || !std::isfinite(point.value) || !std::isfinite(point.tensionToNext))
                return false;

            const double t = static_cast<double>(point.timeSamples)
                           / static_cast<double>(clipState_->sourceLengthSamples);
            const double pos = t * static_cast<double>(destination.lengthSamples);
            if (!std::isfinite(pos) || pos < 0.0
                || pos > static_cast<double>(destination.lengthSamples))
                return false;

            point.timeSamples = static_cast<int64_t>(std::llround(pos));
            point.value = AutomationLaneCore::clampValueForParameter(
                destination.parameterId, point.value);
            point.tensionToNext = juce::jlimit(-1.0f, 1.0f, point.tensionToNext);
            updated.push_back(point);
        }

        std::stable_sort(updated.begin(), updated.end(),
            [](const AutomationPoint& a, const AutomationPoint& b)
            {
                return a.timeSamples < b.timeSamples;
            });

        // Deterministic collision resolution when a destination is shorter
        // than the copied pattern. Keep last source point at a given sample.
        if (updated.size() > 1)
        {
            size_t write = 0;
            for (size_t read = 1; read < updated.size(); ++read)
            {
                if (updated[read].timeSamples == updated[write].timeSamples)
                    updated[write] = updated[read];
                else
                    updated[++write] = updated[read];
            }
            updated.resize(write + 1);
        }

        destination.localPoints = std::move(updated);
        return true;
    }

private:
    std::optional<ClipState> clipState_;
};

} // namespace DAW
