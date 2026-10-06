#include "BubblegumDropletCore.h"

namespace bubblegum
{
    void BubblegumDropletCore::paintSplashPaths(
        juce::Graphics& g,
        const std::vector<juce::Path>& splashes,
        const BubblegumCableStyleSettingsCore::Style& style,
        float centerX,
        float centerY,
        float thickness) const
    {
        for (const auto& drip : splashes)
        {
            juce::ColourGradient dripGrad(
                style.splash.withMultipliedAlpha(0.9f),
                centerX, centerY,
                style.splash.withMultipliedAlpha(0.10f),
                centerX, centerY + thickness * 5.0f,
                false);

            g.setGradientFill(dripGrad);
            g.fillPath(drip);

            g.setColour(style.highlightSoft.withMultipliedAlpha(0.22f));
            g.strokePath(drip, juce::PathStrokeType(
                0.55f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }
    }

    void BubblegumDropletCore::paintDropletPoints(
        juce::Graphics& g,
        const std::vector<DropletPoint>& droplets,
        const BubblegumCableStyleSettingsCore::Style& style,
        float thickness) const
    {
        for (const auto& dp : droplets)
        {
            // ── Tail: tapered filled path from cable surface to blob ────────
            if (dp.tailLength > 0.8f)
            {
                const auto  dir  = dp.position - dp.attachPoint;
                const float dlen = dir.getDistanceFromOrigin();
                if (dlen > 0.5f)
                {
                    const auto  unit = dir / dlen;
                    const juce::Point<float> perp(-unit.y, unit.x);

                    const float topW = juce::jmin(dp.radius * 0.90f, thickness * 0.30f);
                    const float botW = dp.radius * 0.55f;

                    juce::Path tail;
                    tail.startNewSubPath(dp.attachPoint + perp *  topW);
                    tail.lineTo        (dp.attachPoint - perp *  topW);
                    tail.lineTo        (dp.position   - perp *  botW);
                    tail.lineTo        (dp.position   + perp *  botW);
                    tail.closeSubPath();

                    // Deep pink gradient — bright at cable, saturated at tip
                    juce::ColourGradient grad(
                        style.bodyTop.withMultipliedAlpha(dp.opacity * 0.75f),
                        dp.attachPoint.x, dp.attachPoint.y,
                        style.bodyBottom.withMultipliedAlpha(dp.opacity),
                        dp.position.x,    dp.position.y,
                        false);
                    g.setGradientFill(grad);
                    g.fillPath(tail);
                }
            }

            // ── Blob: deep pink filled circle at tip ──────────────────────
            g.setColour(style.bodyBottom.withMultipliedAlpha(dp.opacity));
            g.fillEllipse(
                dp.position.x - dp.radius,
                dp.position.y - dp.radius,
                dp.radius * 2.0f,
                dp.radius * 2.0f);

            // Specular highlight on larger blobs
            if (dp.opacity > 0.22f && dp.radius > thickness * 0.04f)
            {
                const float hr = dp.radius * 0.32f;
                g.setColour(style.highlightSharp.withMultipliedAlpha(dp.opacity * 0.50f));
                g.fillEllipse(
                    dp.position.x - dp.radius * 0.20f - hr,
                    dp.position.y - dp.radius * 0.20f - hr,
                    hr * 2.0f,
                    hr * 2.0f);
            }
        }
    }
}
