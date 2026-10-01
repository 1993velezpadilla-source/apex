#pragma once
#include <JuceHeader.h>
#include <vector>

namespace DAW::BubblegumSelection
{
    struct BubblegumSelectionSettings
    {
        bool enabled = true;
        bool showGlow = true;
        bool showBubbles = true;
        bool animate = true;
        bool showHighlight = true;

        enum class Quality
        {
            Classic,
            BubblegumLite,
            BubblegumFull
        };

        Quality quality = Quality::BubblegumFull;

        float frameThickness = 2.0f;
        float minWidth = 30.0f;
        float minHeight = 30.0f;

        float fadeOutMs = 300.0f;
        float glowBlur = 7.0f;

        int bubbleCount = 14;
    };

    struct BubblegumSelectionGeometry
    {
        juce::Path outerPath;
        juce::Path innerPath;
        juce::Path framePath;

        bool hasInner = false;

        juce::Rectangle<float> outerBounds;
        juce::Rectangle<float> innerBounds;
    };

    struct BubblegumSelectionBubble
    {
        float relativeX = 0.0f;
        float relativeY = 0.0f;
        float size = 3.0f;
        float speed = 0.08f;
        float phase = 0.0f;
        float alpha = 0.0f;
    };
}
