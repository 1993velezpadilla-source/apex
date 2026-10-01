#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "BubbleOrchestrator.h"

namespace DAW {

/**
 * MixerBubble
 * Messenger-style draggable floating bubble dedicated to the mixer.
 * Appears when the mixer floating window is minimized or closed.
 * Click to restore the mixer; drag to reposition anywhere on screen.
 * Visually distinct from BubbleTaskbar with an accent-coloured fader icon.
 */
class MixerBubble : public juce::Component
{
public:
    static constexpr int kBubbleSize    = 56;
    static constexpr int kDragThreshold = 2;

    std::function<void()> onClicked;
    std::function<void(const juce::String& targetId)> onMergeTriggered;
    std::function<void(const juce::String& targetId)> onMergePreviewStart;
    std::function<void()>                              onMergePreviewEnd;

    void setOrchestrator(BubbleOrchestrator* o, const juce::String& id)
    { orchestrator_ = o; bubbleId_ = id; }

    juce::Point<float> getBubbleCenterInParent() const noexcept
    { return { bubbleX_ + kBubbleSize / 2.f, bubbleY_ + kBubbleSize / 2.f }; }

    bool wasDraggedByUser() const noexcept { return wasDraggedByUser_; }

    MixerBubble() {}

    void resized() override
    {
        if (bubbleX_ < 0.f)
        {
            bubbleX_ = (float)(getWidth() - kBubbleSize) - 10.f;
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

        // Accent gradient body (visually distinct from task bubble)
        juce::ColourGradient grad(t.apex.color.violetBright, bub.getCentreX(), bub.getY(),
                                  t.apex.color.violet,   bub.getCentreX(), bub.getBottom(), false);
        g.setGradientFill(grad);
        g.fillEllipse(bub);

        // Rim
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawEllipse(bub.reduced(1.f), 1.f);
        g.setColour(t.colors.border.withAlpha(0.8f));
        g.drawEllipse(bub, 1.f);

        // Merge zone glow — equal-pole magnet touching feedback
        if (inMergeZone_)
        {
            g.setColour(t.apex.color.violet.withAlpha(0.45f));
            g.drawEllipse(bub.expanded(6.f), 2.5f);
            g.setColour(t.apex.color.violet.withAlpha(0.12f));
            g.fillEllipse(bub.expanded(6.f));
        }
        drawMixerIcon(g, bub.reduced(14.f));
    }

    // ── mouse (drag + click) ──────────────────────────────────────────────────
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (bubbleRect().expanded(4.f).contains(e.position))
        {
            isDragging_ = false;
            dragStart_  = e.position;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragStart_.x < 0.f) return;

        auto delta = e.position - dragStart_;
        if (!isDragging_ && delta.getDistanceFromOrigin() > (float)kDragThreshold)
            isDragging_ = true;

        if (isDragging_)
        {
            wasDraggedByUser_ = true;
            bubbleX_ += delta.x;
            bubbleY_ += delta.y;
            clampBubble();
            dragStart_ = e.position;

            // Magnetic repulsion — equal poles can't overlap
            if (orchestrator_)
            {
                auto desired   = getBubbleCenterInParent();
                auto corrected = orchestrator_->resolveRepulsion(bubbleId_, desired, kBubbleSize / 2.f);
                bubbleX_ = corrected.x - kBubbleSize / 2.f;
                bubbleY_ = corrected.y - kBubbleSize / 2.f;
                clampBubble();

                bool          wasIn  = inMergeZone_;
                juce::String  target = orchestrator_->findDeepOverlapTarget(
                                           bubbleId_, getBubbleCenterInParent(), kBubbleSize / 2.f);
                inMergeZone_   = target.isNotEmpty();
                mergeTargetId_ = target;

                if (inMergeZone_ != wasIn)
                {
                    if (inMergeZone_  && onMergePreviewStart) onMergePreviewStart(mergeTargetId_);
                    if (!inMergeZone_ && onMergePreviewEnd)   onMergePreviewEnd();
                }
            }
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (dragStart_.x >= 0.f && !isDragging_)
        {
            if (bubbleRect().expanded(4.f).contains(e.position) && onClicked)
                onClicked();
        }
        // Release while in deep overlap zone → merge both into MasterBubble
        if (isDragging_ && inMergeZone_ && onMergeTriggered)
            onMergeTriggered(mergeTargetId_);
        if (onMergePreviewEnd) onMergePreviewEnd();

        dragStart_    = { -1.f, -1.f };
        isDragging_   = false;
        inMergeZone_  = false;
        repaint();
    }

    bool hitTest(int x, int y) override
    {
        return bubbleRect().expanded(4.f).contains(juce::Point<float>((float)x, (float)y));
    }

private:
    float bubbleX_        = -1.f;
    float bubbleY_        = -1.f;
    bool  isDragging_     = false;
    bool  inMergeZone_    = false;
    bool  wasDraggedByUser_ = false;
    juce::String        mergeTargetId_;
    juce::Point<float> dragStart_ { -1.f, -1.f };
    BubbleOrchestrator* orchestrator_ = nullptr;
    juce::String        bubbleId_;

    void clampBubble()
    {
        bubbleX_ = juce::jlimit(0.f, (float)juce::jmax(0, getWidth()  - kBubbleSize), bubbleX_);
        bubbleY_ = juce::jlimit(0.f, (float)juce::jmax(0, getHeight() - kBubbleSize), bubbleY_);
    }

    juce::Rectangle<float> bubbleRect() const
    {
        return juce::Rectangle<float>(bubbleX_, bubbleY_, (float)kBubbleSize, (float)kBubbleSize).reduced(4.f);
    }

    // Three vertical fader tracks at different levels
    void drawMixerIcon(juce::Graphics& g, juce::Rectangle<float> b)
    {
        float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
        float gap   = w / 4.f;
        float lineW = 2.5f;
        float levels[] = { 0.35f, 0.6f, 0.25f };

        for (int i = 0; i < 3; ++i)
        {
            float lx     = x + gap * (i + 0.5f);
            float thumbY = y + h * levels[i];

            // Track line
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.fillRoundedRectangle(lx - lineW * 0.5f, y, lineW, h, 1.f);

            // Thumb
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.fillRoundedRectangle(lx - 4.f, thumbY - 3.f, 8.f, 6.f, 2.f);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerBubble)
};

} // namespace DAW
