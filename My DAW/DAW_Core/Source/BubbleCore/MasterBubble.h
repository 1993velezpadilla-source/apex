#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "BubbleOrchestrator.h"

namespace DAW {

/**
 * MasterBubble — the central hub bubble that owns stored "slot" bubbles.
 *
 * In the DAW Core workflow, individual feature bubbles (Mixer, Marker, etc.)
 * can be dragged onto the MasterBubble to "store" them. Clicking the
 * MasterBubble opens a radial/grid popup showing all stored slots.
 *
 * Design:
 *  - Larger than feature bubbles (64px vs 56px)
 *  - Distinctive gradient (dark-chrome metallic, not accent-coloured)
 *  - Always visible when any stored slot exists
 *  - Popup shows stored slots as labelled pill buttons arranged in rows
 *  - Slots can be popped out (restored to independent bubbles) by
 *    long-pressing or right-clicking
 */
class MasterBubble : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int kBubbleSize    = 64;
    static constexpr int kSlotSize      = 40;
    static constexpr int kSlotGap       = 6;
    static constexpr int kPanelPad      = 12;
    static constexpr int kPanelMaxCols  = 4;
    static constexpr int kDragThreshold = 2;

    void setOrchestrator(BubbleOrchestrator* o, const juce::String& id)
    { orchestrator_ = o; bubbleId_ = id; }

    juce::Point<float> getBubbleCenterInParent() const noexcept
    { return { bubbleX_ + kBubbleSize / 2.f, bubbleY_ + kBubbleSize / 2.f }; }

    /** Highlight the MasterBubble when a feature bubble is being dragged onto it. */
    void setMergeHighlight(bool on)
    {
        mergeHighlight_ = on;
        if (on && !isTimerRunning()) startTimerHz(60);
        else if (!on && !isTimerRunning()) repaint();
    }

    /** Reposition the bubble center (used to place it at merge midpoint). */
    void setBubblePosition(float cx, float cy)
    {
        bubbleX_ = cx - kBubbleSize / 2.f;
        bubbleY_ = cy - kBubbleSize / 2.f;
        if (isVisible()) clampBubble();
    }

    /** Call after absorbing a bubble — plays a scale-up pulse animation. */
    void triggerAbsorptionPulse()
    {
        absorptionPhase_ = 0.f;
        startTimerHz(60);
    }

    struct Slot
    {
        juce::String  id;
        juce::String  label;
        juce::String  group;     // "Transport", "View", "Marker", etc.
        juce::Colour  color;
        std::function<void()> onActivate;   // open/restore the feature
        std::function<void()> onDetach;     // pop back out to standalone bubble
    };

    MasterBubble() {}

    // ── Slot management ───────────────────────────────────────────────────────

    void addSlot(const Slot& slot)
    {
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(),
            [&](const Slot& s) { return s.id == slot.id; }), slots_.end());
        slots_.push_back(slot);
        if (!isVisible()) setVisible(true);
        repaint();
    }

    void removeSlot(const juce::String& id)
    {
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(),
            [&](const Slot& s) { return s.id == id; }), slots_.end());
        if (slots_.empty()) { popupOpen_ = false; setVisible(false); }
        repaint();
    }

    bool hasSlot(const juce::String& id) const
    {
        for (auto& s : slots_) if (s.id == id) return true;
        return false;
    }

    int getSlotCount() const { return (int)slots_.size(); }

    // ── paint ─────────────────────────────────────────────────────────────────

    void resized() override
    {
        if (bubbleX_ < 0.f)
        {
            bubbleX_ = (float)(getWidth() / 2 - kBubbleSize / 2);
            bubbleY_ = (float)(getHeight() - kBubbleSize - 10);
        }
        clampBubble();
    }

    void paint(juce::Graphics& g) override
    {
        if (slots_.empty()) return;

        auto& t  = Theme::getInstance();
        auto  bub = bubbleRect();

        // Absorption pulse: brief scale-up when a new slot arrives
        if (absorptionPhase_ > 0.f)
        {
            float pulse = std::sin(absorptionPhase_ * juce::MathConstants<float>::pi);
            bub = bub.expanded(pulse * 9.f);
        }

        // Shadow
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(bub.translated(0.f, 5.f).expanded(3.f));

        // Deep-wine Signal Core gradient (visually distinct hub)
        auto& a = t.apex;
        juce::ColourGradient grad(a.color.deepestB.interpolatedWith(a.color.magentaDeep, 0.35f),
                                  bub.getCentreX(), bub.getY(),
                                  a.color.deepestA, bub.getCentreX(), bub.getBottom(), false);
        g.setGradientFill(grad);
        g.fillEllipse(bub);

        // Inner highlight
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawEllipse(bub.reduced(1.f), 1.f);

        // Outer border — magenta rim
        g.setColour(a.color.magenta.withAlpha(0.65f));
        g.drawEllipse(bub, 1.2f);

        // Merge-target highlight — glows when a bubble is being dragged onto it
        if (mergeHighlight_)
        {
            float pulse = std::sin(absorptionPhase_ * juce::MathConstants<float>::twoPi * 3.f) * 0.5f + 0.5f;
            g.setColour(a.color.magenta.withAlpha(0.55f + pulse * 0.2f));
            g.drawEllipse(bub.expanded(7.f + pulse * 4.f), 3.f);
            g.setColour(a.color.magenta.withAlpha(0.15f + pulse * 0.1f));
            g.fillEllipse(bub.expanded(7.f + pulse * 4.f));
        }

        // Hub icon — four dots arranged in a 2×2 grid
        drawHubIcon(g, bub.reduced(16.f), a.color.magenta);

        // Slot count badge
        if (slots_.size() > 1)
        {
            auto badge = juce::Rectangle<float>(bub.getRight() - 16.f, bub.getY() - 4.f, 20.f, 20.f);
            g.setColour(a.color.magenta);
            g.fillEllipse(badge);
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawEllipse(badge, 1.f);
            g.setColour(juce::Colours::white);
            g.setFont(t.fonts.small);
            g.drawText(juce::String((int)slots_.size()), badge, juce::Justification::centred);
        }

        // Popup panel
        if (popupOpen_)
            drawPopup(g);
    }

    // ── mouse ─────────────────────────────────────────────────────────────────

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Hit-test popup slots first
        if (popupOpen_)
        {
            int hitSlot = hitTestSlot(e.position);
            if (hitSlot >= 0)
            {
                if (e.mods.isRightButtonDown())
                {
                    // Right-click = detach (pop out)
                    if (slots_[hitSlot].onDetach)
                        slots_[hitSlot].onDetach();
                }
                else
                {
                    // Left-click = activate
                    if (slots_[hitSlot].onActivate)
                        slots_[hitSlot].onActivate();
                }
                popupOpen_ = false;
                repaint();
                return;
            }
        }

        if (bubbleRect().expanded(4.f).contains(e.position))
        {
            isDragging_ = false;
            dragStart_  = e.position;
            return;
        }

        // Clicked outside everything — close popup
        if (popupOpen_) { popupOpen_ = false; repaint(); }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragStart_.x < 0.f) return;
        auto delta = e.position - dragStart_;
        if (!isDragging_ && delta.getDistanceFromOrigin() > (float)kDragThreshold)
            isDragging_ = true;
        if (isDragging_)
        {
            bubbleX_ += delta.x;
            bubbleY_ += delta.y;
            clampBubble();
            dragStart_ = e.position;
            popupOpen_ = false;

            // Repel from other bubbles (MasterBubble also obeys the physics)
            if (orchestrator_)
            {
                auto desired   = getBubbleCenterInParent();
                auto corrected = orchestrator_->resolveRepulsion(bubbleId_, desired, kBubbleSize / 2.f);
                bubbleX_ = corrected.x - kBubbleSize / 2.f;
                bubbleY_ = corrected.y - kBubbleSize / 2.f;
                clampBubble();
            }
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (dragStart_.x >= 0.f && !isDragging_)
        {
            if (bubbleRect().expanded(4.f).contains(e.position))
            {
                popupOpen_ = !popupOpen_;
                repaint();
            }
        }
        dragStart_ = { -1.f, -1.f };
        isDragging_ = false;
    }

    void mouseMove(const juce::MouseEvent&) override { if (popupOpen_) repaint(); }
    void mouseExit(const juce::MouseEvent&) override { if (popupOpen_) repaint(); }

    bool hitTest(int x, int y) override
    {
        auto pt = juce::Point<float>((float)x, (float)y);
        if (bubbleRect().expanded(4.f).contains(pt)) return true;
        if (popupOpen_ && popupRect().contains(pt)) return true;
        return false;
    }

private:
    std::vector<Slot> slots_;
    float bubbleX_         = -1.f;
    float bubbleY_         = -1.f;
    bool  popupOpen_       = false;
    bool  isDragging_      = false;
    float absorptionPhase_ =  0.f;  // 0..1, drives sin pulse in paint
    bool  mergeHighlight_  = false;
    juce::Point<float> dragStart_ { -1.f, -1.f };
    BubbleOrchestrator* orchestrator_ = nullptr;
    juce::String        bubbleId_;

    void clampBubble()
    {
        bubbleX_ = juce::jlimit(0.f, (float)juce::jmax(0, getWidth()  - kBubbleSize), bubbleX_);
        bubbleY_ = juce::jlimit(0.f, (float)juce::jmax(0, getHeight() - kBubbleSize), bubbleY_);
    }

    void timerCallback() override
    {
        absorptionPhase_ += 0.07f;
        repaint();
        if (absorptionPhase_ >= 1.f)
        {
            absorptionPhase_ = mergeHighlight_ ? 0.f : 0.f; // reset
            if (!mergeHighlight_) stopTimer();
        }
    }

    juce::Rectangle<float> bubbleRect() const
    {
        return juce::Rectangle<float>(bubbleX_, bubbleY_,
                                      (float)kBubbleSize, (float)kBubbleSize).reduced(4.f);
    }

    // ── Popup layout ──────────────────────────────────────────────────────────

    int popupCols() const { return juce::jmin(kPanelMaxCols, (int)slots_.size()); }
    int popupRows() const { return ((int)slots_.size() + popupCols() - 1) / popupCols(); }

    juce::Rectangle<float> popupRect() const
    {
        int cols = popupCols();
        int rows = popupRows();
        float w = (float)(kPanelPad * 2 + cols * kSlotSize + (cols - 1) * kSlotGap);
        float h = (float)(kPanelPad * 2 + rows * kSlotSize + (rows - 1) * kSlotGap + 22); // +22 for title

        float px = bubbleX_ + kBubbleSize * 0.5f - w * 0.5f;
        float py = bubbleY_ - h - 8.f; // above the bubble
        if (py < 4.f) py = bubbleY_ + (float)kBubbleSize + 8.f; // fallback below

        px = juce::jlimit(4.f, (float)juce::jmax(1, getWidth()) - w - 4.f, px);
        py = juce::jlimit(4.f, (float)juce::jmax(1, getHeight()) - h - 4.f, py);
        return { px, py, w, h };
    }

    juce::Rectangle<float> slotRect(int index) const
    {
        auto panel = popupRect().reduced((float)kPanelPad);
        panel.removeFromTop(22.f); // title area
        int cols = popupCols();
        int row = index / cols;
        int col = index % cols;
        float x = panel.getX() + (float)col * (kSlotSize + kSlotGap);
        float y = panel.getY() + (float)row * (kSlotSize + kSlotGap);
        return { x, y, (float)kSlotSize, (float)kSlotSize };
    }

    int hitTestSlot(juce::Point<float> pt) const
    {
        for (int i = 0; i < (int)slots_.size(); ++i)
            if (slotRect(i).contains(pt)) return i;
        return -1;
    }

    // ── Popup drawing ─────────────────────────────────────────────────────────

    void drawPopup(juce::Graphics& g)
    {
        auto& t = Theme::getInstance();
        auto panel = popupRect();

        // Panel shadow
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(panel.translated(0.f, 6.f).expanded(2.f), 14.f);

        // Panel body
        juce::ColourGradient panelGrad(t.colors.surface.withAlpha(0.97f), panel.getCentreX(), panel.getY(),
                                       t.colors.backgroundLight.withAlpha(0.985f), panel.getCentreX(), panel.getBottom(), false);
        g.setGradientFill(panelGrad);
        g.fillRoundedRectangle(panel, 14.f);

        // Border
        g.setColour(t.colors.border.withAlpha(0.9f));
        g.drawRoundedRectangle(panel, 14.f, 1.f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawRoundedRectangle(panel.reduced(1.f), 13.f, 1.f);

        // Title
        auto titleArea = panel.reduced((float)kPanelPad).removeFromTop(20.f);
        g.setColour(t.colors.textSecondary);
        g.setFont(t.fonts.small);
        g.drawText("STORED BUBBLES", titleArea, juce::Justification::centredLeft);

        // Slots
        auto mouse = getMouseXYRelative().toFloat();
        for (int i = 0; i < (int)slots_.size(); ++i)
        {
            auto sr = slotRect(i);
            bool hov = sr.contains(mouse);

            // Slot background
            g.setColour(hov ? t.colors.controlHover : t.colors.controlIdle);
            g.fillRoundedRectangle(sr, 8.f);

            // Colour accent dot
            g.setColour(slots_[i].color.withAlpha(0.9f));
            g.fillEllipse(sr.getCentreX() - 6.f, sr.getY() + 6.f, 12.f, 12.f);

            // Border
            g.setColour(hov ? slots_[i].color.withAlpha(0.5f) : t.colors.border);
            g.drawRoundedRectangle(sr, 8.f, 1.f);

            // Label
            g.setColour(t.colors.text);
            g.setFont(juce::Font(9.f));
            g.drawText(slots_[i].label, sr.withTrimmedTop(20.f), juce::Justification::centred, true);
        }
    }

    // ── Hub icon (2×2 dot grid) ───────────────────────────────────────────────

    void drawHubIcon(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        float cx = b.getCentreX(), cy = b.getCentreY();
        float off = b.getWidth() * 0.22f;
        float dotR = 3.f;

        g.setColour(c);
        g.fillEllipse(cx - off - dotR, cy - off - dotR, dotR * 2.f, dotR * 2.f);
        g.fillEllipse(cx + off - dotR, cy - off - dotR, dotR * 2.f, dotR * 2.f);
        g.fillEllipse(cx - off - dotR, cy + off - dotR, dotR * 2.f, dotR * 2.f);
        g.fillEllipse(cx + off - dotR, cy + off - dotR, dotR * 2.f, dotR * 2.f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterBubble)
};

} // namespace DAW
