#pragma once
#include "BubblegumSelectionTypesCore.h"

namespace DAW::BubblegumSelection
{
    struct BubblegumSelectionStateCore
    {
        juce::Point<float> dragStart;
        juce::Point<float> dragEnd;
        bool isDragging = false;
        bool isFadingOut = false;
        float alpha = 0.0f;
        float animationTimeSeconds = 0.0f;

        void beginDrag(juce::Point<float> point) noexcept
        {
            dragStart = point;
            dragEnd = point;
            isDragging = true;
            isFadingOut = false;
            alpha = 0.0f;
            animationTimeSeconds = 0.0f;
        }

        void updateDrag(juce::Point<float> point) noexcept
        {
            dragEnd = point;
        }

        void endDrag() noexcept
        {
            isDragging = false;
            isFadingOut = alpha > 0.0f;
        }

        void cancel() noexcept
        {
            isDragging = false;
            isFadingOut = false;
            alpha = 0.0f;
        }

        bool isActive() const noexcept
        {
            return isDragging || isFadingOut || alpha > 0.0f;
        }

        bool shouldPaint(const BubblegumSelectionSettings& settings) const noexcept
        {
            return settings.enabled && alpha > 0.0f && getBounds().getWidth() >= 2.0f && getBounds().getHeight() >= 2.0f;
        }

        juce::Rectangle<float> getBounds() const noexcept
        {
            return juce::Rectangle<float>::leftTopRightBottom(
                juce::jmin(dragStart.x, dragEnd.x),
                juce::jmin(dragStart.y, dragEnd.y),
                juce::jmax(dragStart.x, dragEnd.x),
                juce::jmax(dragStart.y, dragEnd.y));
        }

        void advance(float deltaSeconds, const BubblegumSelectionSettings& settings) noexcept
        {
            animationTimeSeconds += settings.animate ? deltaSeconds : 0.0f;

            if (isDragging)
            {
                alpha = juce::jmin(1.0f, alpha + deltaSeconds * 8.0f);
                return;
            }

            if (isFadingOut)
            {
                const float fadeSeconds = juce::jmax(0.05f, settings.fadeOutMs * 0.001f);
                alpha = juce::jmax(0.0f, alpha - deltaSeconds / fadeSeconds);
                if (alpha <= 0.0f)
                    isFadingOut = false;
            }
        }
    };
}
