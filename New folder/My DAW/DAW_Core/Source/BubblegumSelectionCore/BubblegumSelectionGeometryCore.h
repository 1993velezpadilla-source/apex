#pragma once
#include "BubblegumSelectionTypesCore.h"

namespace DAW::BubblegumSelection
{
    class BubblegumSelectionGeometryCore
    {
    public:
        static BubblegumSelectionGeometry build(juce::Point<float> dragStart,
                                                juce::Point<float> dragEnd,
                                                float timeSeconds,
                                                const BubblegumSelectionSettings& settings)
        {
            BubblegumSelectionGeometry geometry;
            auto bounds = juce::Rectangle<float>::leftTopRightBottom(
                juce::jmin(dragStart.x, dragEnd.x),
                juce::jmin(dragStart.y, dragEnd.y),
                juce::jmax(dragStart.x, dragEnd.x),
                juce::jmax(dragStart.y, dragEnd.y));

            if (bounds.getWidth() < settings.minWidth || bounds.getHeight() < settings.minHeight)
                return geometry;

            geometry.outerBounds = bounds;
            geometry.outerPath = buildOuterPath(bounds, timeSeconds, settings);

            const float inset = juce::jmax(1.0f, settings.frameThickness);
            auto inner = bounds.reduced(inset);
            geometry.hasInner = inner.getWidth() > 1.0f && inner.getHeight() > 1.0f;
            if (geometry.hasInner)
            {
                geometry.innerBounds = inner;
                geometry.innerPath = buildInnerPath(inner, timeSeconds, settings);
                geometry.framePath = geometry.outerPath;
                geometry.framePath.addPath(geometry.innerPath);
                geometry.framePath.setUsingNonZeroWinding(false);
            }
            else
            {
                geometry.framePath = geometry.outerPath;
            }

            return geometry;
        }

        static juce::Path buildHighlightPath(const juce::Rectangle<float>& bounds, float timeSeconds,
                                             const BubblegumSelectionSettings& settings)
        {
            juce::Path highlight;
            const int segments = settings.quality == BubblegumSelectionSettings::Quality::BubblegumFull ? 18 : 10;
            const float y = bounds.getY() + juce::jmax(2.0f, settings.frameThickness * 0.45f);
            highlight.startNewSubPath(bounds.getX() + settings.frameThickness * 2.0f, y);
            for (int i = 1; i <= segments; ++i)
            {
                const float t = (float)i / (float)segments;
                const float x = juce::jmap(t, bounds.getX() + settings.frameThickness * 2.0f,
                                           bounds.getRight() - settings.frameThickness * 2.0f);
                const float wave = std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + timeSeconds * 1.4f) * 0.9f;
                highlight.lineTo(x, y + wave);
            }
            return highlight;
        }

    private:
        static juce::Path buildOuterPath(const juce::Rectangle<float>& bounds, float timeSeconds,
                                         const BubblegumSelectionSettings& settings)
        {
            std::vector<juce::Point<float>> points;
            const bool full = settings.quality == BubblegumSelectionSettings::Quality::BubblegumFull;
            const int topSegments = full ? 30 : 16;
            const int sideSegments = full ? 20 : 12;
            const int bottomSegments = full ? 80 : 32;
            points.reserve((size_t)(topSegments + sideSegments * 2 + bottomSegments + 16));

            const float x0 = bounds.getX();
            const float x1 = bounds.getRight();
            const float y0 = bounds.getY();
            const float y1 = bounds.getBottom();
            const float width = bounds.getWidth();
            const int dripCount = juce::jmax(3, (int)std::floor(width / 70.0f));

            for (int i = 0; i <= topSegments; ++i)
            {
                const float t = (float)i / (float)topSegments;
                points.emplace_back(juce::jmap(t, x0, x1), y0 + std::sin(t * juce::MathConstants<float>::twoPi * 3.0f + timeSeconds * 1.2f) * 1.1f);
            }

            for (int i = 1; i <= sideSegments; ++i)
            {
                const float t = (float)i / (float)sideSegments;
                points.emplace_back(x1 + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + timeSeconds * 1.1f) * 1.1f,
                                    juce::jmap(t, y0, y1));
            }

            for (int i = 1; i <= bottomSegments; ++i)
            {
                const float t = (float)i / (float)bottomSegments;
                const float x = juce::jmap(t, x1, x0);
                float y = y1 + std::sin(t * juce::MathConstants<float>::twoPi * 4.0f + timeSeconds * 1.5f) * 1.5f;

                for (int d = 0; d < dripCount; ++d)
                {
                    const float seed = (float)(d + 1);
                    const float center = x1 - ((float)d + 0.5f) * width / (float)dripCount;
                    const float halfWidth = 4.0f + std::abs(std::sin(seed * 3.0f)) * 5.5f;
                    const float dist = std::abs(x - center);
                    if (dist < halfWidth)
                    {
                        const float shape = 0.5f + 0.5f * std::cos((dist / halfWidth) * juce::MathConstants<float>::pi);
                        const float scale = 0.35f + std::abs(std::sin(seed * 7.3f)) * 0.45f;
                        const float wobble = std::sin(timeSeconds * 2.0f + seed * 2.1f) * 1.2f;
                        y += (11.0f * scale + wobble) * shape;
                    }
                }

                points.emplace_back(x, y);
            }

            for (int i = 1; i <= sideSegments; ++i)
            {
                const float t = (float)i / (float)sideSegments;
                points.emplace_back(x0 + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + timeSeconds * 1.3f) * 1.1f,
                                    juce::jmap(t, y1, y0));
            }

            return smoothClosedPath(points);
        }

        static juce::Path buildInnerPath(const juce::Rectangle<float>& bounds, float timeSeconds,
                                         const BubblegumSelectionSettings& settings)
        {
            std::vector<juce::Point<float>> points;
            const bool full = settings.quality == BubblegumSelectionSettings::Quality::BubblegumFull;
            const int topSegments = full ? 24 : 12;
            const int sideSegments = full ? 16 : 10;
            const int bottomSegments = full ? 24 : 12;
            points.reserve((size_t)(topSegments + sideSegments * 2 + bottomSegments + 4));

            const float x0 = bounds.getX();
            const float x1 = bounds.getRight();
            const float y0 = bounds.getY();
            const float y1 = bounds.getBottom();

            for (int i = 0; i <= topSegments; ++i)
            {
                const float t = (float)i / (float)topSegments;
                points.emplace_back(juce::jmap(t, x0, x1), y0 + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + timeSeconds) * 0.7f);
            }
            for (int i = 1; i <= sideSegments; ++i)
            {
                const float t = (float)i / (float)sideSegments;
                points.emplace_back(x1 + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + timeSeconds * 1.1f) * 0.7f, juce::jmap(t, y0, y1));
            }
            for (int i = 1; i <= bottomSegments; ++i)
            {
                const float t = (float)i / (float)bottomSegments;
                points.emplace_back(juce::jmap(t, x1, x0), y1 + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + timeSeconds * 1.2f) * 0.7f);
            }
            for (int i = 1; i <= sideSegments; ++i)
            {
                const float t = (float)i / (float)sideSegments;
                points.emplace_back(x0 + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + timeSeconds * 1.3f) * 0.7f, juce::jmap(t, y1, y0));
            }

            return smoothClosedPath(points);
        }

        static juce::Path smoothClosedPath(const std::vector<juce::Point<float>>& points)
        {
            juce::Path path;
            if (points.empty())
                return path;

            path.startNewSubPath(points.front());
            for (size_t i = 0; i < points.size(); ++i)
            {
                const auto& current = points[i];
                const auto& next = points[(i + 1) % points.size()];
                const auto midpoint = juce::Point<float>((current.x + next.x) * 0.5f, (current.y + next.y) * 0.5f);
                path.quadraticTo(current, midpoint);
            }
            path.closeSubPath();
            return path;
        }
    };
}
