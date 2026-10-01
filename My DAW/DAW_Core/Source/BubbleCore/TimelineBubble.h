#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "BubbleOrchestrator.h"

namespace DAW {

/**
 * TimelineBubble
 * Messenger-style draggable floating bubble for the timeline/playlist.
 * Appears when the timeline window is minimized or closed.
 * Click to restore; drag to reposition.
 * Uses a waveform icon to visually distinguish from the mixer bubble.
 */
class TimelineBubble : public juce::Component
{
public:
    static constexpr int kBubbleSize    = 56;
    static constexpr int kDragThreshold = 2;

    std::function<void()> onClicked;

    TimelineBubble() {}

    void resized() override
    {
        if (bubbleX_ < 0.f)
        {
            bubbleX_ = (float)(getWidth() - kBubbleSize) - 80.f;
            bubbleY_ = (float)getHeight() - kBubbleSize - 10.f;
        }
        clampBubble();
    }

    void paint(juce::Graphics& g) override
    {
        auto& t  = Theme::getInstance();
        auto  bub = bubbleRect();

        // Shadow
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(bub.translated(0.f, 5.f).expanded(2.f));

        // Teal gradient body (distinct from mixer's accent purple)
        juce::ColourGradient grad(juce::Colour(0xff06b6d4).brighter(0.15f), bub.getCentreX(), bub.getY(),
                                  juce::Colour(0xff06b6d4).darker(0.35f),   bub.getCentreX(), bub.getBottom(), false);
        g.setGradientFill(grad);
        g.fillEllipse(bub);

        // Rim
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawEllipse(bub.reduced(1.f), 1.f);
        g.setColour(t.colors.border.withAlpha(0.8f));
        g.drawEllipse(bub, 1.f);

        // Waveform icon
        drawTimelineIcon(g, bub.reduced(14.f));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (bubbleRect().expanded(4.f).contains(e.position))
        {
            isDragging_ = false;
            dragStartX_ = e.position.x; dragStartY_ = e.position.y;
            dragOffX_   = e.position.x - bubbleX_;
            dragOffY_   = e.position.y - bubbleY_;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        float dx = e.position.x - dragStartX_, dy = e.position.y - dragStartY_;
        if (!isDragging_ && (dx * dx + dy * dy) > (float)(kDragThreshold * kDragThreshold))
            isDragging_ = true;
        if (isDragging_)
        {
            bubbleX_ = e.position.x - dragOffX_;
            bubbleY_ = e.position.y - dragOffY_;
            clampBubble(); repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!isDragging_ && bubbleRect().expanded(4.f).contains(e.position))
            if (onClicked) onClicked();
    }

    juce::Point<float> getBubbleCenterInParent() const noexcept
    { return { bubbleX_ + kBubbleSize / 2.f, bubbleY_ + kBubbleSize / 2.f }; }

    bool hitTest(int x, int y) override
    { return bubbleRect().expanded(4.f).contains((float)x, (float)y); }

private:
    float bubbleX_ = -1.f, bubbleY_ = -1.f;
    float dragStartX_ = 0, dragStartY_ = 0, dragOffX_ = 0, dragOffY_ = 0;
    bool  isDragging_ = false;

    juce::Rectangle<float> bubbleRect() const
    { return { bubbleX_, bubbleY_, (float)kBubbleSize, (float)kBubbleSize }; }

    void clampBubble()
    {
        const auto maxX = juce::jmax(0.0f, (float) getWidth()  - (float) kBubbleSize);
        const auto maxY = juce::jmax(0.0f, (float) getHeight() - (float) kBubbleSize);
        bubbleX_ = juce::jlimit(0.0f, maxX, bubbleX_);
        bubbleY_ = juce::jlimit(0.0f, maxY, bubbleY_);
    }

    void drawTimelineIcon(juce::Graphics& g, juce::Rectangle<float> r)
    {
        // Mini waveform icon
        float cx = r.getCentreX(), cy = r.getCentreY();
        float hw = r.getWidth() * 0.4f, hh = r.getHeight() * 0.35f;
        g.setColour(juce::Colours::white.withAlpha(0.9f));

        juce::Path wave;
        constexpr int pts = 12;
        float heights[] = { 0.2f, 0.5f, 0.8f, 1.0f, 0.7f, 0.4f, 0.6f, 0.9f, 0.6f, 0.3f, 0.5f, 0.2f };
        for (int i = 0; i < pts; ++i)
        {
            float x = cx - hw + (2.f * hw * i / (pts - 1));
            float h = hh * heights[i];
            g.drawVerticalLine((int)x, cy - h, cy + h);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineBubble)
};

} // namespace DAW
