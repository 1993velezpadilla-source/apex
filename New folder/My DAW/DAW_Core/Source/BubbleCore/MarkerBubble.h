#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../MarkerCore/MarkerManager.h"
#include "../ActionCore/ActionManager.h"
#include "../ActionCore/ActionID.h"
#include "BubbleOrchestrator.h"

namespace DAW {

/**
 * MarkerBubble — floating bubble for quick section navigation.
 *
 * Click to open a popup listing all markers. Click a marker row to jump
 * there. Drag to reposition.  Uses the same visual language as MixerBubble.
 */
class MarkerBubble : public juce::Component,
                     public MarkerManager::Listener
{
public:
    static constexpr int kBubbleSize    = 56;
    static constexpr int kItemH         = 34;
    static constexpr int kHeaderH       = 24;
    static constexpr int kPanelW        = 220;
    static constexpr int kPanelMaxH     = 340;
    static constexpr int kDragThreshold = 2;

    std::function<void(SamplePosition)>               onJumpToPosition;
    std::function<void(const juce::String& targetId)>  onMergeTriggered;
    std::function<void(const juce::String& targetId)>  onMergePreviewStart;
    std::function<void()>                               onMergePreviewEnd;

    void setOrchestrator(BubbleOrchestrator* o, const juce::String& id)
    { orchestrator_ = o; bubbleId_ = id; }

    juce::Point<float> getBubbleCenterInParent() const noexcept
    { return { bubbleX_ + kBubbleSize / 2.f, bubbleY_ + kBubbleSize / 2.f }; }

    explicit MarkerBubble(MarkerManager& markers)
        : markers_(markers)
    {
        markers_.addListener(this);
    }

    ~MarkerBubble() override { markers_.removeListener(this); }

    // ── paint ─────────────────────────────────────────────────────────────────

    void resized() override
    {
        if (bubbleX_ < 0.f)
        {
            bubbleX_ = (float)(getWidth() - kBubbleSize) - 80.f;
            bubbleY_ = (float)(getHeight() - kBubbleSize - 10);
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

        // Warm amber gradient — distinguishes from purple mixer bubble
        juce::ColourGradient grad(juce::Colour(0xfffbbf24).brighter(0.1f), bub.getCentreX(), bub.getY(),
                                  juce::Colour(0xffc88e0e),                bub.getCentreX(), bub.getBottom(), false);
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
            g.setColour(juce::Colour(0xfffbbf24).withAlpha(0.5f));
            g.drawEllipse(bub.expanded(6.f), 2.5f);
            g.setColour(juce::Colour(0xfffbbf24).withAlpha(0.12f));
            g.fillEllipse(bub.expanded(6.f));
        }

        // Flag icon
        drawFlagIcon(g, bub.reduced(14.f));

        // Marker count badge
        int mc = markers_.getNumMarkers();
        if (mc > 0)
        {
            auto badge = juce::Rectangle<float>(bub.getRight() - 16.f, bub.getY() - 4.f, 20.f, 20.f);
            g.setColour(t.colors.accent);
            g.fillEllipse(badge);
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawEllipse(badge, 1.f);
            g.setColour(juce::Colours::white);
            g.setFont(t.fonts.small);
            g.drawText(juce::String(mc), badge, juce::Justification::centred);
        }

        if (popupOpen_)
            drawPopup(g);
    }

    // ── mouse ─────────────────────────────────────────────────────────────────

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (popupOpen_)
        {
            int hitIdx = hitTestRow(e.position);
            if (hitIdx >= 0)
            {
                auto* m = markers_.getMarker(hitIdx);
                if (m && onJumpToPosition)
                    onJumpToPosition(m->position);
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
            if (bubbleRect().expanded(4.f).contains(e.position))
            {
                popupOpen_ = !popupOpen_;
                repaint();
            }
        }
        if (isDragging_ && inMergeZone_ && onMergeTriggered)
            onMergeTriggered(mergeTargetId_);
        if (onMergePreviewEnd) onMergePreviewEnd();

        dragStart_   = { -1.f, -1.f };
        isDragging_  = false;
        inMergeZone_ = false;
        repaint();
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

    // MarkerManager::Listener
    void markerAdded(SectionMarker*) override       { repaint(); }
    void markerRemoved(const MarkerID&) override     { repaint(); }
    void markerChanged(SectionMarker*) override      { repaint(); }
    void markersReloaded() override                  { repaint(); }

private:
    MarkerManager& markers_;
    float bubbleX_    = -1.f;
    float bubbleY_    = -1.f;
    bool  popupOpen_  = false;
    bool  isDragging_ = false;
    bool  inMergeZone_ = false;
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
        return juce::Rectangle<float>(bubbleX_, bubbleY_,
                                      (float)kBubbleSize, (float)kBubbleSize).reduced(4.f);
    }

    // ── Popup layout ──────────────────────────────────────────────────────────

    int getPanelHeight() const
    {
        return kHeaderH + 16 + markers_.getNumMarkers() * kItemH;
    }

    juce::Rectangle<float> popupRect() const
    {
        float ph = (float)juce::jmin(kPanelMaxH, getPanelHeight());
        float px = bubbleX_ + kBubbleSize + 8.f;
        if (px + kPanelW > (float)getWidth()) px = bubbleX_ - kPanelW - 8.f;
        float py = bubbleY_ + kBubbleSize * 0.5f - ph * 0.5f;
        py = juce::jlimit(4.f, (float)juce::jmax(1, getHeight()) - ph - 4.f, py);
        return { px, py, (float)kPanelW, ph };
    }

    juce::Rectangle<float> rowRect(int index) const
    {
        auto content = popupRect().reduced(10.f);
        content.removeFromTop((float)kHeaderH);
        for (int i = 0; i < index; ++i)
            content.removeFromTop((float)kItemH);
        return content.removeFromTop((float)kItemH).reduced(2.f, 2.f);
    }

    int hitTestRow(juce::Point<float> pt) const
    {
        for (int i = 0; i < markers_.getNumMarkers(); ++i)
            if (rowRect(i).contains(pt)) return i;
        return -1;
    }

    // ── Popup drawing ─────────────────────────────────────────────────────────

    void drawPopup(juce::Graphics& g)
    {
        auto& t = Theme::getInstance();
        auto panel = popupRect();

        // Shadow
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(panel.translated(0.f, 8.f).expanded(2.f), 14.f);

        // Body
        juce::ColourGradient panelGrad(t.colors.surface.withAlpha(0.97f), panel.getCentreX(), panel.getY(),
                                       t.colors.backgroundLight.withAlpha(0.985f), panel.getCentreX(), panel.getBottom(), false);
        g.setGradientFill(panelGrad);
        g.fillRoundedRectangle(panel, 14.f);

        // Border
        g.setColour(t.colors.border.withAlpha(0.9f));
        g.drawRoundedRectangle(panel, 14.f, 1.f);

        // Title
        auto content = panel.reduced(10.f);
        auto header = content.removeFromTop((float)kHeaderH);
        g.setColour(t.colors.text);
        g.setFont(t.fonts.bold);
        g.drawText("MARKERS", header, juce::Justification::centredLeft);
        g.setColour(t.colors.textSecondary);
        g.setFont(t.fonts.small);
        g.drawText(juce::String(markers_.getNumMarkers()) + " section(s)", header, juce::Justification::centredRight);

        // Rows
        auto mouse = getMouseXYRelative().toFloat();
        for (int i = 0; i < markers_.getNumMarkers(); ++i)
        {
            auto* m = markers_.getMarker(i);
            if (!m) continue;

            auto row = rowRect(i);
            bool hov = row.contains(mouse);

            // Row background
            g.setColour(hov ? t.colors.controlHover : t.colors.controlIdle);
            g.fillRoundedRectangle(row, 6.f);
            g.setColour(hov ? m->color.withAlpha(0.35f) : t.colors.border.withAlpha(0.3f));
            g.drawRoundedRectangle(row, 6.f, 1.f);

            // Colour dot
            auto dot = row.removeFromLeft(row.getHeight()).reduced(8.f);
            g.setColour(m->color);
            g.fillEllipse(dot);

            // Name
            g.setColour(t.colors.text);
            g.setFont(t.fonts.regular);
            g.drawText(m->name, row.reduced(6.f, 0.f), juce::Justification::centredLeft, true);
        }

        if (markers_.getNumMarkers() == 0)
        {
            g.setColour(t.colors.textSecondary);
            g.setFont(t.fonts.regular);
            g.drawText("No markers yet", content, juce::Justification::centred);
        }
    }

    // ── Flag icon ─────────────────────────────────────────────────────────────

    void drawFlagIcon(juce::Graphics& g, juce::Rectangle<float> b)
    {
        float x = b.getX() + b.getWidth() * 0.3f;
        float y = b.getY() + 2.f;
        float h = b.getHeight() - 4.f;

        // Pole
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.fillRect(x, y, 2.f, h);

        // Flag triangle
        juce::Path flag;
        flag.startNewSubPath(x + 2.f, y);
        flag.lineTo(x + b.getWidth() * 0.65f, y + h * 0.2f);
        flag.lineTo(x + 2.f, y + h * 0.4f);
        flag.closeSubPath();
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.fillPath(flag);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerBubble)
};

} // namespace DAW
