#pragma once
#include <JuceHeader.h>
#include "../Bubblegum/BubblegumOffscreenDetectionCore.h"
#include "../ThemeCore/Theme.h"
#include "ApexPresentationClock.h"

namespace DAW {

/**
 * BubblegumOffscreenTargetPopup
 * 
 * Premium Bubblegum-styled popup that displays offscreen send targets
 * with toggle dot (ON/OFF), track name, and delete button per row.
 * Scrollable when many items. Click row name to auto-scroll mixer.
 */
class BubblegumOffscreenTargetPopup : public juce::Component,
                                      public ApexPresentationClock::TickReceiver
{
public:
    static constexpr float kRowHeight   = 32.0f;
    static constexpr float kMaxHeight   = 220.0f;
    static constexpr float kMinWidth    = 200.0f;
    static constexpr float kMaxWidth    = 310.0f;
    static constexpr float kOuterPad    = 10.0f;
    static constexpr float kDotSize     = 10.0f;
    static constexpr float kDeleteSize  = 13.0f;
    static constexpr float kRowPadH     = 12.0f;
    static constexpr float kCloseSize   = 14.0f;
    static constexpr float kClosePad    = 10.0f;
    // Right-side padding inside a row (after the trash icon). The popup-level close X
    // lives in the top-right of the popup header, so we reserve a corner safety zone
    // large enough that the per-row trash icon is clearly separated from that close X
    // instead of hugging the same corner.
    static constexpr float kRowRightPad    = 10.0f;
    static constexpr float kRowCloseZoneW  = 32.0f;

    std::function<void(const TrackID&)> onTargetSelected;
    std::function<void(const TrackID&)> onToggleSend;
    std::function<void(const TrackID&)> onDeleteSend;
    std::function<void()> onClose;
    /** Called when the user picks a different tap-point on a sidechain row.
     *  Only fired for rows where target.isSidechain == true. */
    std::function<void(const TrackID&, TapPoint)> onSetTapPoint;

    bool autoCloseOnTargetSelect = false;

    BubblegumOffscreenTargetPopup(const BubblegumOffscreenDetectionCore::OffscreenGroup& group)
        : offscreenGroup_(group)
    {
        setOpaque(false);
        setAlwaysOnTop(true);
        setInterceptsMouseClicks(true, true);

        int numTargets = (int)group.targets.size();
        float contentH = numTargets * kRowHeight;
        float totalH   = juce::jmin(kMaxHeight, contentH + kOuterPad * 2.0f);
        float idealWidth = kMinWidth;

        // Grow the popup so the longest track name fits (clamped to kMaxWidth).
        // NOTE: earlier code used jmin here which collapsed the width and forced
        // every row to render as "...". Must use jmax to grow with content.
        juce::Font font(13.0f, juce::Font::bold);
        for (const auto& target : group.targets)
        {
            // text + dot column + trash column + corner close-zone + paddings
            float textWidth = font.getStringWidth(target.trackName)
                              + 90.0f + kRowCloseZoneW;
            idealWidth = juce::jmax(idealWidth, textWidth);
        }
        idealWidth = juce::jmin(idealWidth, kMaxWidth);

        // Internal content component for scrollable area
        contentComp_.setSize((int)idealWidth - (int)(kOuterPad * 2.0f), (int)contentH);
        contentComp_.setOpaque(false);
        contentComp_.setInterceptsMouseClicks(false, false);

        scrollViewport_.setViewedComponent(&contentComp_, false);
        scrollViewport_.setScrollBarsShown(true, false, true, false);
        scrollViewport_.setScrollBarThickness(4);
        addAndMakeVisible(scrollViewport_);

        setSize((int)idealWidth, (int)totalH);

        // Init animation alphas for all rows (start at 0 for fade-in)
        for (const auto& tgt : group.targets)
            rowAnimAlpha_[tgt.trackId] = 0.0f;
        ApexPresentationClock::instance().addReceiver(this);
    }

    ~BubblegumOffscreenTargetPopup() override
    {
        if (presentationUpdateActive_)
            ApexPresentationClock::instance().releaseContinuousUpdate(this);
        ApexPresentationClock::instance().removeReceiver(this);
        scrollViewport_.setViewedComponent(nullptr, false);
    }

    /** Live-update the group data (called when send graph changes while popup is open). */
    void updateGroup(const BubblegumOffscreenDetectionCore::OffscreenGroup& newGroup)
    {
        // Track newly added rows for fade-in animation
        for (const auto& nt : newGroup.targets)
        {
            bool existed = false;
            for (const auto& ot : offscreenGroup_.targets)
                if (ot.trackId == nt.trackId) { existed = true; break; }
            if (!existed)
                rowAnimAlpha_[nt.trackId] = 0.0f;
        }

        offscreenGroup_ = newGroup;

        float contentH = (float)offscreenGroup_.targets.size() * kRowHeight;
        contentComp_.setSize(contentComp_.getWidth(), (int)contentH);
        float newH = juce::jmin(kMaxHeight, contentH + kOuterPad * 2.0f);
        setSize(getWidth(), (int)newH);

        if (offscreenGroup_.targets.empty())
        {
            if (onClose) onClose();
            return;
        }
        updatePresentationDemand();
        repaint();
    }

    void resized() override
    {
        scrollViewport_.setBounds(
            (int)kOuterPad, (int)kOuterPad,
            getWidth() - (int)(kOuterPad * 2.0f),
            getHeight() - (int)(kOuterPad * 2.0f));
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        float corner = 14.0f;
        auto pink = juce::Colour(0xFFFF7DB8);

        // Soft drop shadow
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(b.translated(0, 4).expanded(2.0f), corner + 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.2f));
        g.fillRoundedRectangle(b.translated(0, 2), corner);

        // Background
        juce::ColourGradient bgGrad(
            juce::Colour(0xFF201820), b.getCentreX(), b.getY(),
            juce::Colour(0xFF0E0E0E), b.getCentreX(), b.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(b, corner);

        // Pink border
        g.setColour(pink.withAlpha(0.45f));
        g.drawRoundedRectangle(b.reduced(0.5f), corner, 1.5f);

        // Soft outer glow
        g.setColour(pink.withAlpha(0.15f));
        g.drawRoundedRectangle(b.expanded(3.0f), corner + 3.0f, 3.0f);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto pink = juce::Colour(0xFFFF7DB8);

        // Draw rows over the scroll content area
        auto vpBounds = scrollViewport_.getBounds().toFloat();
        int scrollY = scrollViewport_.getViewPositionY();

        float y = 0.f;
        for (const auto& target : offscreenGroup_.targets)
        {
            float rowScreenY = vpBounds.getY() + y - (float)scrollY;

            // Skip rows outside visible popup area
            if (rowScreenY + kRowHeight < vpBounds.getY() || rowScreenY > vpBounds.getBottom())
            {
                y += kRowHeight;
                continue;
            }

            // Clip to viewport area
            auto rowBounds = juce::Rectangle<float>(vpBounds.getX(), rowScreenY, vpBounds.getWidth(), kRowHeight);
            auto clippedRow = rowBounds.getIntersection(vpBounds);
            if (clippedRow.isEmpty()) { y += kRowHeight; continue; }

            // Per-row animation alpha
            float rowAlpha = 1.0f;
            auto ait = rowAnimAlpha_.find(target.trackId);
            if (ait != rowAnimAlpha_.end())
                rowAlpha = ait->second;

            // Vertical offset for slide-in (6px at alpha=0, 0 at alpha=1)
            float slideOff = (1.0f - rowAlpha) * 6.0f;
            rowBounds = rowBounds.translated(0.0f, slideOff);

            bool rowHovered = clippedRow.contains(hoverPosition_);

            // Row hover background
            if (rowHovered)
            {
                g.setColour(pink.withAlpha(0.08f * rowAlpha));
                g.fillRoundedRectangle(clippedRow.reduced(2.0f, 1.0f), 4.0f);
            }

            // --- Toggle dot (left) ---
            float dotX = rowBounds.getX() + kRowPadH;
            float dotY = rowBounds.getCentreY() - kDotSize * 0.5f;
            auto dotBounds = juce::Rectangle<float>(dotX, dotY, kDotSize, kDotSize);
            bool dotHovered = dotBounds.expanded(3.f).contains(hoverPosition_);
            float pulseBoost = (target.hasSend && target.sendActive) ? (0.05f * std::sin(dotPulse_)) : 0.0f;

            if (target.hasSend && target.sendActive)
            {
                // Filled pink — send is ON + subtle pulse glow
                g.setColour(pink.withAlpha((dotHovered ? 1.0f : 0.85f + pulseBoost) * rowAlpha));
                g.fillEllipse(dotBounds);
                g.setColour(pink.withAlpha((0.35f + pulseBoost * 2.0f) * rowAlpha));
                g.drawEllipse(dotBounds.expanded(3.0f), 1.5f);
            }
            else
            {
                // Hollow gray — either no send, or send exists but is OFF
                g.setColour(juce::Colours::white.withAlpha((dotHovered ? 0.45f : 0.25f) * rowAlpha));
                g.drawEllipse(dotBounds, 1.5f);
            }

            // --- Delete button (right) — trash bin icon ---
            // Popup's close X lives in the popup header, so each row reserves a
            // corner safety zone (kRowCloseZoneW) and then places the trash icon
            // to the LEFT of that zone for clear visual separation.
            float delX = rowBounds.getRight() - kRowCloseZoneW - kDeleteSize - kRowRightPad;
            float delY = rowBounds.getCentreY() - kDeleteSize * 0.5f;
            auto delBounds = juce::Rectangle<float>(delX, delY, kDeleteSize, kDeleteSize);
            bool delHovered = delBounds.expanded(4.f).contains(hoverPosition_);

            {
                auto col = delHovered ? pink.brighter(0.3f).withAlpha(0.9f * rowAlpha)
                                      : juce::Colours::white.withAlpha(0.3f * rowAlpha);
                g.setColour(col);

                // Trash bin: lid (horizontal bar with small knob)
                float lidY   = delBounds.getY() + 1.0f;
                float lidL   = delBounds.getX() + 1.5f;
                float lidR   = delBounds.getRight() - 1.5f;
                g.drawLine(lidL, lidY, lidR, lidY, 1.4f);
                // Lid handle
                float hW = 3.0f;
                float hX = delBounds.getCentreX() - hW * 0.5f;
                g.drawLine(hX, lidY - 1.5f, hX + hW, lidY - 1.5f, 1.2f);

                // Trash bin: body (tapered rectangle)
                float bodyTop = lidY + 2.0f;
                float bodyBot = delBounds.getBottom() - 0.5f;
                float bodyInset = 2.5f;
                float taper = 0.8f;
                juce::Path body;
                body.startNewSubPath(lidL + bodyInset, bodyTop);
                body.lineTo(lidR - bodyInset, bodyTop);
                body.lineTo(lidR - bodyInset - taper, bodyBot);
                body.lineTo(lidL + bodyInset + taper, bodyBot);
                body.closeSubPath();
                g.strokePath(body, juce::PathStrokeType(1.2f));

                // Trash bin: vertical lines inside body
                float lineTop = bodyTop + 2.0f;
                float lineBot = bodyBot - 1.5f;
                float cx = delBounds.getCentreX();
                g.drawLine(cx, lineTop, cx, lineBot, 0.8f);
                g.drawLine(cx - 2.5f, lineTop, cx - 2.5f + 0.3f, lineBot, 0.8f);
                g.drawLine(cx + 2.5f, lineTop, cx + 2.5f - 0.3f, lineBot, 0.8f);
            }

            // --- Track label area (center) ---
            float textLeft = dotX + kDotSize + 10.0f;
            if (target.hasSidechain)
                textLeft += 26.0f; // reserve room for the SC tag
            float textRight = delX - 8.0f;
            auto textBounds = juce::Rectangle<float>(textLeft, rowBounds.getY(), textRight - textLeft, kRowHeight);

            if (target.hasSidechain)
            {
                const auto scCol = juce::Colour(0xFF3A7BD5).withAlpha(0.92f * rowAlpha);
                const float tagW = 22.f, tagH = 13.f;
                const float tagX = dotX + kDotSize + 8.0f;
                const float tagY = rowBounds.getCentreY() - tagH * 0.5f - 5.0f;

                g.setColour(scCol.withAlpha(0.24f * rowAlpha));
                g.fillRoundedRectangle(tagX, tagY, tagW, tagH, 3.5f);
                g.setColour(scCol.withAlpha(0.85f * rowAlpha));
                g.drawRoundedRectangle(tagX, tagY, tagW, tagH, 3.5f, 1.0f);
                g.setColour(juce::Colours::white.withAlpha(0.95f * rowAlpha));
                g.setFont(juce::Font(8.2f, juce::Font::bold));
                g.drawText("SC", (int)tagX, (int)tagY, (int)tagW, (int)tagH,
                           juce::Justification::centred, false);
            }

            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.setColour((rowHovered ? juce::Colours::white : t.colors.text).withMultipliedAlpha(rowAlpha));
            g.drawText(target.trackName, textBounds, juce::Justification::centredLeft, true);

            // --- Tap-point label (sidechain rows only) — no background pill ---
            if (target.hasSidechain)
            {
                static const char* kTapLabels[] = { "Pre", "Post", "Mix" };
                const int tapIdx = (int)target.tapPoint;
                const juce::Colour pillFg = juce::Colour(0xFF3A7BD5).withAlpha(0.75f * rowAlpha);
                const juce::String tapLabel = kTapLabels[juce::jlimit(0, 2, tapIdx)];
                const float pillW = 26.f, pillH = 13.f;
                const float pillX = textBounds.getX();
                const float pillY = rowBounds.getBottom() - pillH - 3.f;
                g.setColour(pillFg);
                g.setFont(juce::Font(8.5f, juce::Font::bold));
                g.drawText(tapLabel, (int)pillX, (int)pillY, (int)pillW, (int)pillH,
                           juce::Justification::centredLeft, false);
            }

            y += kRowHeight;
        }

        // Close X button at top-right — properly centered
        {
            auto closeArea = juce::Rectangle<float>(
                (float)getWidth() - kClosePad - kCloseSize,
                kClosePad,
                kCloseSize, kCloseSize);
            bool closeHovered = closeArea.expanded(5.f).contains(hoverPosition_);

            // Subtle circular bg on hover
            if (closeHovered)
            {
                g.setColour(juce::Colours::white.withAlpha(0.08f));
                g.fillEllipse(closeArea.expanded(4.0f));
            }

            float inset = 2.5f;
            g.setColour(closeHovered ? juce::Colours::white.withAlpha(0.85f)
                                     : juce::Colours::white.withAlpha(0.35f));
            g.drawLine(closeArea.getX() + inset, closeArea.getY() + inset,
                       closeArea.getRight() - inset, closeArea.getBottom() - inset, 1.8f);
            g.drawLine(closeArea.getRight() - inset, closeArea.getY() + inset,
                       closeArea.getX() + inset, closeArea.getBottom() - inset, 1.8f);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        hoverPosition_ = e.position;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverPosition_ = {-1000, -1000};
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        isMouseButtonDown_ = true;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        // Close X button hit test
        {
            auto closeArea = juce::Rectangle<float>(
                (float)getWidth() - kClosePad - kCloseSize,
                kClosePad,
                kCloseSize, kCloseSize);
            if (closeArea.expanded(5.f).contains(e.position))
            {
                if (onClose) onClose();
                return;
            }
        }

        auto vpBounds = scrollViewport_.getBounds().toFloat();
        int scrollY = scrollViewport_.getViewPositionY();
        float y = 0.f;

        for (const auto& target : offscreenGroup_.targets)
        {
            float rowScreenY = vpBounds.getY() + y - (float)scrollY;
            auto rowBounds = juce::Rectangle<float>(vpBounds.getX(), rowScreenY, vpBounds.getWidth(), kRowHeight);

            if (rowBounds.contains(e.position))
            {
                // Right-click on a sidechain row = delete (mirrors trash-icon behaviour)
                if (e.mods.isRightButtonDown() && target.hasSidechain)
                {
                    auto deletedId = target.trackId;
                    offscreenGroup_.targets.erase(
                        std::remove_if(offscreenGroup_.targets.begin(), offscreenGroup_.targets.end(),
                            [&](const auto& t) { return t.trackId == deletedId; }),
                        offscreenGroup_.targets.end());
                    if (onDeleteSend)
                        onDeleteSend(deletedId);
                    if (offscreenGroup_.targets.empty())
                    {
                        if (onClose) onClose();
                        return;
                    }
                    float newContentH = (float)offscreenGroup_.targets.size() * kRowHeight;
                    contentComp_.setSize(contentComp_.getWidth(), (int)newContentH);
                    float newH = juce::jmin(kMaxHeight, newContentH + kOuterPad * 2.0f);
                    setSize(getWidth(), (int)newH);
                    repaint();
                    return;
                }

                // Check toggle dot hit
                float dotX = rowBounds.getX() + kRowPadH;
                float dotY = rowBounds.getCentreY() - kDotSize * 0.5f;
                auto dotHit = juce::Rectangle<float>(dotX, dotY, kDotSize, kDotSize).expanded(4.f);
                if (dotHit.contains(e.position))
                {
                    // Toggle local state immediately for visual feedback
                    for (auto& t : offscreenGroup_.targets)
                        if (t.trackId == target.trackId)
                            t.sendActive = !t.sendActive;
                    if (onToggleSend)
                        onToggleSend(target.trackId);
                    repaint();
                    return;
                }

                // Check tap-point pill hit (sidechain rows only) — cycles Pre→Post→Mix
                if (target.hasSidechain)
                {
                    float textLeft = dotX + kDotSize + 10.0f;
                    const float pillW = 26.f, pillH = 13.f;
                    const float pillX = textLeft;
                    const float pillY = rowBounds.getBottom() - pillH - 3.f;
                    auto pillHit = juce::Rectangle<float>(pillX, pillY, pillW, pillH).expanded(4.f);
                    if (pillHit.contains(e.position))
                    {
                        for (auto& tgt : offscreenGroup_.targets)
                        {
                            if (tgt.trackId == target.trackId)
                            {
                                tgt.tapPoint = (TapPoint)(((int)tgt.tapPoint + 1) % 3);
                                if (onSetTapPoint) onSetTapPoint(tgt.trackId, tgt.tapPoint);
                                break;
                            }
                        }
                        repaint();
                        return;
                    }
                }

                // Check delete hit (trash icon — always the only delete path for sends)
                float delX = rowBounds.getRight() - kRowCloseZoneW - kDeleteSize - kRowRightPad;
                float delY = rowBounds.getCentreY() - kDeleteSize * 0.5f;
                auto delHit = juce::Rectangle<float>(delX, delY, kDeleteSize, kDeleteSize).expanded(4.f);
                if (delHit.contains(e.position))
                {
                    auto deletedId = target.trackId;
                    // Remove row immediately
                    offscreenGroup_.targets.erase(
                        std::remove_if(offscreenGroup_.targets.begin(), offscreenGroup_.targets.end(),
                            [&](const auto& t) { return t.trackId == deletedId; }),
                        offscreenGroup_.targets.end());
                    if (onDeleteSend)
                        onDeleteSend(deletedId);
                    if (offscreenGroup_.targets.empty())
                    {
                        if (onClose) onClose();
                        return;
                    }
                    // Resize content and popup
                    float newContentH = (float)offscreenGroup_.targets.size() * kRowHeight;
                    contentComp_.setSize(contentComp_.getWidth(), (int)newContentH);
                    float newH = juce::jmin(kMaxHeight, newContentH + kOuterPad * 2.0f);
                    setSize(getWidth(), (int)newH);
                    repaint();
                    return;
                }

                // Row body click (non-dot, non-delete area):
                // Always scroll to track. For send rows also toggle the send active state.
                if (target.hasSend)
                {
                    for (auto& t : offscreenGroup_.targets)
                        if (t.trackId == target.trackId)
                            t.sendActive = !t.sendActive;
                    if (onToggleSend)
                        onToggleSend(target.trackId);
                    repaint();
                }

                if (onTargetSelected)
                    onTargetSelected(target.trackId);

                if (autoCloseOnTargetSelect)
                {
                    if (onClose)
                        onClose();
                }
                return;
            }

            y += kRowHeight;
        }
    }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().contains(x, y);
    }

    void focusLost(FocusChangeType) override
    {
        // Do not auto-close on focus loss — popup lifetime is controlled
        // exclusively by user interaction (close button, delete-all) and settings.
        DBG("[BubblegumOffscreenTargetPopup] Focus lost (ignored - no auto-close)");
    }

private:
    BubblegumOffscreenDetectionCore::OffscreenGroup offscreenGroup_;
    juce::Point<float> hoverPosition_{-1000, -1000};
    bool isMouseButtonDown_ = false;
    juce::Component contentComp_;
    juce::Viewport scrollViewport_;
    std::map<TrackID, float> rowAnimAlpha_;  // 0..1 fade per row
    float dotPulse_ = 0.0f;                 // subtle global pulse phase

    bool hasPendingRowAnimation() const noexcept
    {
        for (const auto& [id, alpha] : rowAnimAlpha_)
            if (alpha < 1.0f)
                return true;
        return false;
    }

    bool hasActiveSend() const noexcept
    {
        for (const auto& t : offscreenGroup_.targets)
            if (t.hasSend && t.sendActive)
                return true;
        return false;
    }

    void updatePresentationDemand()
    {
        const bool needsPresentation = isShowing()
            && (hasPendingRowAnimation() || hasActiveSend());

        if (needsPresentation && !presentationUpdateActive_)
        {
            ApexPresentationClock::instance().requestContinuousUpdate(this);
            presentationUpdateActive_ = true;
        }
        else if (!needsPresentation && presentationUpdateActive_)
        {
            ApexPresentationClock::instance().releaseContinuousUpdate(this);
            presentationUpdateActive_ = false;
        }
    }

    void visibilityChanged() override
    {
        updatePresentationDemand();
    }

    void onPresentationTick(double deltaSeconds) override
    {
        if (!isShowing())
        {
            updatePresentationDemand();
            return;
        }

        const float dt = (float) juce::jlimit(0.0, 0.25, deltaSeconds);
        bool needsRepaint = false;
        // Animate row alphas toward 1.0 (fade-in)
        for (auto& [id, alpha] : rowAnimAlpha_)
        {
            if (alpha < 1.0f)
            {
                alpha = juce::jmin(1.0f, alpha + dt * 4.8f);
                needsRepaint = true;
            }
        }
        // Dot pulse (subtle sin wave) — only drive repaints when a send-active
        // row is visible; idle popups with no active sends need no per-frame repaint.
        if (hasActiveSend())
        {
            dotPulse_ += dt * 2.4f;
            if (dotPulse_ > juce::MathConstants<float>::twoPi)
                dotPulse_ -= juce::MathConstants<float>::twoPi;
            needsRepaint = true;
        }

        if (needsRepaint)
            repaint();

        updatePresentationDemand();
    }

    bool presentationUpdateActive_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumOffscreenTargetPopup)
};

} // namespace DAW
