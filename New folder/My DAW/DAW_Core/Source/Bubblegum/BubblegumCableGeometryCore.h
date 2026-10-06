#pragma once
#include <JuceHeader.h>
#include <vector>
#include "BubblegumCableTypes.h"
#include "BubblegumCableStyleSettingsCore.h"

namespace bubblegum
{
    class BubblegumCableGeometryCore
    {
    public:
        juce::Point<float> pointOnCurve(const CableResolvedFrame& f, float t) const noexcept;
        juce::Point<float> tangentOnCurve(const CableResolvedFrame& f, float t) const noexcept;

        std::vector<CableStrokeSample> sampleLiquidStroke(
            const CableResolvedFrame& frame,
            int steps,
            const BubblegumCableStyleSettingsCore::Style& style) const;

        juce::Path buildLiquidBodyPath(
            const CableResolvedFrame& frame,
            const std::vector<CableStrokeSample>& samples,
            const BubblegumCableStyleSettingsCore::Style& style) const;

        juce::Path buildMistEnvelopePath(
            const CableResolvedFrame& frame,
            const std::vector<CableStrokeSample>& samples,
            const BubblegumCableStyleSettingsCore::Style& style) const;

        juce::Path buildInnerCorePath(
            const CableResolvedFrame& frame,
            const std::vector<CableStrokeSample>& samples,
            const BubblegumCableStyleSettingsCore::Style& style) const;

        std::vector<juce::Path> buildSecondarySplashPaths(
            const CableResolvedFrame& frame,
            const std::vector<CableStrokeSample>& samples,
            const BubblegumCableStyleSettingsCore::Style& style) const;

        std::vector<DropletPoint> buildDropletPoints(
            const CableResolvedFrame& frame,
            const std::vector<CableStrokeSample>& samples,
            const BubblegumCableStyleSettingsCore::Style& style) const;

        juce::Path buildDestinationEndCapPath(
            const CableResolvedFrame& frame,
            const std::vector<CableStrokeSample>& samples,
            const BubblegumCableStyleSettingsCore::Style& style) const;

    private:
        static constexpr float tension = 0.40f;

        void buildSmoothForward(juce::Path& p, const std::vector<juce::Point<float>>& pts) const;
        void buildSmoothReverse(juce::Path& p, const std::vector<juce::Point<float>>& pts) const;

        float computeWidthMul(
            const CableResolvedFrame& frame,
            float t,
            const BubblegumCableStyleSettingsCore::Style& style,
            float lengthScale) const noexcept;

        float computeEdgeNoise(
            const CableResolvedFrame& frame,
            float t,
            const BubblegumCableStyleSettingsCore::Style& style,
            float lengthScale) const noexcept;

        float computeLowerSplashBias(
            float t,
            float lengthScale) const noexcept;

        float splashPresence(
            const CableResolvedFrame& frame,
            float t,
            float lengthScale) const noexcept;

        float cableLengthScale(
            const CableResolvedFrame& frame,
            const BubblegumCableStyleSettingsCore::Style& style) const noexcept;

        static float gaussian(float x, float center, float width) noexcept;
    };
}
