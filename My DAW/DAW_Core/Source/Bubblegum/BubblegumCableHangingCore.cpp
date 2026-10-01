#include "BubblegumCableHangingCore.h"

namespace bubblegum
{
    BubblegumCableHangingCore::HangingResult BubblegumCableHangingCore::solve(
        const juce::Point<float>& source,
        const juce::Point<float>& destination,
        const BubblegumCableStyleSettingsCore::Style& style) const noexcept
    {
        HangingResult out;

        const auto delta = destination - source;
        const float distance = source.getDistanceFrom(destination);
        const float verticalDelta = std::abs(destination.y - source.y);

        float sag = style.baseSag
                  + distance * style.distanceSagScale
                  + verticalDelta * style.verticalSagScale;

        if (distance < style.shortCableStart)
            sag *= style.shortCableSagScale;
        else if (distance < style.shortCableEnd)
            sag *= juce::jmap(distance,
                              style.shortCableStart,
                              style.shortCableEnd,
                              style.shortCableSagScale,
                              1.0f);

        sag = juce::jlimit(style.minSag, style.maxSag, sag);

        const float horizontalDistance = std::abs(destination.x - source.x);
        const float tension01 = juce::jmap(
            juce::jlimit(90.0f, 900.0f, horizontalDistance),
            90.0f, 900.0f,
            0.0f, 1.0f);

        const float controlT = 1.0f / 3.0f - tension01 * 0.05f;
        const auto controlA = source + delta * controlT;
        const auto controlB = source + delta * (1.0f - controlT);

        const float bellyDrop = sag * (0.10f + tension01 * 0.18f);
        const float asymBias  = juce::jlimit(-sag * 0.12f, sag * 0.12f, delta.y * 0.08f);

        out.controlA = { controlA.x, controlA.y + sag + bellyDrop * 0.90f - asymBias * 0.35f };
        out.controlB = { controlB.x, controlB.y + sag + bellyDrop * 1.10f + asymBias * 0.35f };
        out.sagAmount = sag;

        return out;
    }
}
