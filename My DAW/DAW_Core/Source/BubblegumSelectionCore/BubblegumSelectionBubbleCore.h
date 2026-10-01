#pragma once
#include "BubblegumSelectionTypesCore.h"

namespace DAW::BubblegumSelection
{
    class BubblegumSelectionBubbleCore
    {
    public:
        void reset(const BubblegumSelectionSettings& settings)
        {
            bubbles_.clear();
            const int count = settings.quality == BubblegumSelectionSettings::Quality::BubblegumLite
                ? juce::jmin(8, settings.bubbleCount)
                : settings.bubbleCount;

            bubbles_.reserve((size_t)juce::jmax(0, count));
            for (int i = 0; i < count; ++i)
            {
                const float seed = (float)(i + 1);
                BubblegumSelectionBubble bubble;
                bubble.relativeX = fract(std::sin(seed * 12.9898f) * 43758.5453f);
                bubble.relativeY = fract(std::sin(seed * 78.233f) * 23454.123f);
                bubble.size = 2.0f + fract(std::sin(seed * 31.17f) * 9134.71f) * 4.0f;
                bubble.speed = 0.04f + fract(std::sin(seed * 19.73f) * 1234.91f) * 0.08f;
                bubble.phase = fract(std::sin(seed * 45.11f) * 4567.21f) * juce::MathConstants<float>::twoPi;
                bubble.alpha = 0.25f + fract(std::sin(seed * 8.91f) * 3333.77f) * 0.55f;
                bubbles_.push_back(bubble);
            }
        }

        void update(float deltaSeconds)
        {
            for (auto& bubble : bubbles_)
            {
                bubble.relativeY -= bubble.speed * deltaSeconds;
                bubble.relativeX += std::sin(bubble.phase + bubble.relativeY * 6.0f) * deltaSeconds * 0.018f;

                if (bubble.relativeY < 0.0f)
                    bubble.relativeY += 1.0f;
                if (bubble.relativeX < 0.0f)
                    bubble.relativeX += 1.0f;
                if (bubble.relativeX > 1.0f)
                    bubble.relativeX -= 1.0f;
            }
        }

        const std::vector<BubblegumSelectionBubble>& getBubbles() const noexcept { return bubbles_; }

    private:
        std::vector<BubblegumSelectionBubble> bubbles_;

        static float fract(float value) noexcept
        {
            return value - std::floor(value);
        }
    };
}
