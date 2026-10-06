#pragma once
#include <JuceHeader.h>
#include "../Bubblegum/BubblegumV2System.h"
#include "../Bubblegum/BubblegumOffscreenDetectionCore.h"
#include "BubblegumOffscreenTargetPopup.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

class MixerPanel;

/**
 * BubblegumOffscreenEndpointComponent
 * 
 * Renders endpoint indicators for offscreen Bubblegum targets (left/right edges).
 * Shows preview on hover, opens multi-target picker popup on click.
 * Supports auto-scroll to selected target.
 */
class BubblegumOffscreenEndpointComponent : public juce::Component, private juce::Timer
{
public:
    static constexpr float kEndpointSize = 22.0f;
    static constexpr float kEdgeMargin = 12.0f;

    /**
     * Callback to scroll mixer to a specific track.
     * Receives the trackId to scroll to.
     */
    std::function<void(const TrackID&)> onScrollToTrack;
    std::function<void(const TrackID&)> onToggleSend;
    std::function<void(const TrackID&)> onDeleteSend;

    BubblegumOffscreenEndpointComponent()
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, true);
        setAlwaysOnTop(true);
        startTimerHz(60);
    }

    ~BubblegumOffscreenEndpointComponent() override
    {
        stopTimer();
    }

    /** Only capture mouse on real endpoint bubbles or the active popup. */
    bool hitTest (int x, int y) override
    {
        // Overlay is dead when the mixer is hidden.
        if (!mixerVisible_)
            return false;

        // Allow clicks through to active popup child
        if (activePopup_ && activePopup_->isVisible()
            && activePopup_->getBounds().contains(x, y))
            return true;

        // Only capture on actual endpoint bubble bounds
        juce::Point<float> p ((float) x, (float) y);
        for (const auto& group : offscreenGroups_)
        {
            auto endpointBounds = getEndpointBounds(group.direction);
            if (endpointBounds.expanded(4.0f).contains(p))
                return true;
        }

        return false;
    }

    void bind(BubblegumV2System* bgV2, MixerPanel* mixer)
    {
        bgV2_ = bgV2;
        mixerPanel_ = mixer;
    }

    /**
     * Master visibility gate driven by the mixer.
     * When the mixer is not visible, cables/bubbles belong to nothing, so:
     *   - endpoint bubbles must not render
     *   - endpoint bubbles must not capture mouse
     *   - offscreen popup (if open) must close immediately
     */
    void setMixerVisible(bool visible)
    {
        if (mixerVisible_ == visible)
            return;
        mixerVisible_ = visible;
        DBG("[BubblegumOffscreenEndpoint] setMixerVisible=" << (int)visible);
        if (!visible && activePopup_)
            closePopup();
        repaint();
    }

    bool isMixerVisible() const { return mixerVisible_; }

    /**
     * Set the Y center for endpoint placement (in local coords).
     * Called by parent after computing the routing zone position.
     */
    void setCableZoneCenterY(float centerY)
    {
        cableZoneCenterY_ = centerY;
    }

    /** Wire to settings: if true, popup closes after user clicks a target row to scroll. */
    void setAutoCloseOnTargetSelect(bool shouldClose)
    {
        autoClosePopupOnTargetSelect_ = shouldClose;
        if (activePopup_)
            activePopup_->autoCloseOnTargetSelect = shouldClose;
    }

    /** Wire to settings: if true, clicking a target row auto-scrolls the mixer to the target. */
    void setAutoScrollOnTargetSelect(bool shouldScroll)
    {
        autoScrollOnTargetSelect_ = shouldScroll;
    }

    /** Convenience: set both popup row-click behaviors at once. */
    void setPopupRowClickBehavior(bool autoScroll, bool autoClose)
    {
        setAutoScrollOnTargetSelect(autoScroll);
        // Auto-close is meaningful only when auto-scroll is enabled.
        setAutoCloseOnTargetSelect(autoScroll && autoClose);
    }

    /**
     * Set the mixer bounds in this component's local coordinate space.
     * Navigation bubbles are anchored to the mixer outer corners, not screen edges.
     */
    void setMixerBoundsInLocal(juce::Rectangle<float> bounds)
    {
        mixerBoundsLocal_ = bounds;
        updatePopupPosition();
    }

    /**
     * Update offscreen detection based on current viewport and target positions.
     */
    void updateOffscreenTargets(
        juce::Rectangle<float> viewportBounds,
        const std::map<TrackID, float>& targetPositions,
        const std::map<TrackID, juce::String>& targetNames,
        const std::map<TrackID, int>& targetNumbers,
        const std::map<TrackID, float>& sendLevels,
        const std::map<TrackID, bool>& sendActiveStates = {},
        const std::set<TrackID>& sendIds = {},
        const std::set<TrackID>& sidechainIds = {},
        const std::map<TrackID, bool>& sidechainActiveStates = {})
    {
        auto newGroups = detector_.detectOffscreenTargets(
            viewportBounds, targetPositions, targetNames, targetNumbers,
            sendLevels, sendActiveStates, sendIds, sidechainIds, sidechainActiveStates);

        // Compute a lightweight hash over the new groups to detect real changes.
        std::size_t newHash = 0;
        for (const auto& g : newGroups)
        {
            auto hashCombine = [](std::size_t& h, std::size_t v)
                { h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2); };
            hashCombine(newHash, std::hash<int>{}((int)g.direction));
            hashCombine(newHash, std::hash<std::size_t>{}(g.targets.size()));
            for (const auto& t : g.targets)
            {
                hashCombine(newHash, std::hash<juce::String>{}(t.trackId));
                hashCombine(newHash, std::hash<bool>{}(t.sendActive));
                hashCombine(newHash, std::hash<bool>{}(t.hasSend));
                hashCombine(newHash, std::hash<bool>{}(t.hasSidechain));
            }
        }

        const bool groupsChanged = (newHash != lastGroupsHash_);
        lastGroupsHash_ = newHash;
        offscreenGroups_ = std::move(newGroups);

        // Live-update open popup with fresh data
        if (activePopup_ && activePopupDirection_.has_value())
        {
            bool found = false;
            for (const auto& group : offscreenGroups_)
            {
                if (group.direction == *activePopupDirection_)
                {
                    if (groupsChanged)
                    {
                        DBG("[BubblegumOffscreenEndpoint] Popup context still valid");
                        activePopup_->updateGroup(group);
                        DBG("[BubblegumOffscreenEndpoint] Popup updated in place");
                    }
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                DBG("[BubblegumOffscreenEndpoint] Popup context vanished during refresh");

                // Auto-scroll/navigation must not dismiss the popup when auto-close is OFF.
                // Keep the same popup instance alive and simply preserve its last content and
                // anchor side so it can follow the indicator area across viewport movement.
                if (!autoClosePopupOnTargetSelect_)
                {
                    DBG("[BubblegumOffscreenEndpoint] Preserving popup across refresh (autoClose OFF)");
                }
                else
                {
                    closePopup();
                    repaint();
                    return;
                }
            }

            updatePopupPosition();
        }

        // Only repaint when something visible actually changed.
        if (groupsChanged)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (!mixerVisible_)
            return;

        if (offscreenGroups_.empty() && !activePopup_)
        {
            // No endpoints to draw
            return;
        }

        auto pink = juce::Colour(0xFFFF7DB8);

        // Draw visual tether from endpoint to active popup (if any)
        if (activePopup_ && activePopupDirection_.has_value() && popupOpenAnimation_ > 0.01f)
        {
            auto endpointBounds = getEndpointBounds(*activePopupDirection_);
            auto popupBounds = juce::Rectangle<float>(
                currentPopupPos_.x,
                currentPopupPos_.y,
                (float)activePopup_->getWidth(),
                (float)activePopup_->getHeight());

            juce::Path tether;
            auto startPoint = endpointBounds.getCentre();

            juce::Point<float> endPoint;
            if (*activePopupDirection_ == BubblegumOffscreenDetectionCore::Direction::Left)
                endPoint = { popupBounds.getX(), popupBounds.getCentreY() };
            else
                endPoint = { popupBounds.getRight(), popupBounds.getCentreY() };

            float ctrlOffset = (endPoint.x - startPoint.x) * 0.4f;
            auto ctrl1 = startPoint.translated(ctrlOffset, 0);
            auto ctrl2 = endPoint.translated(-ctrlOffset, 0);

            tether.startNewSubPath(startPoint);
            tether.cubicTo(ctrl1, ctrl2, endPoint);

            g.setColour(pink.withAlpha(0.3f * popupOpenAnimation_));
            g.strokePath(tether, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(pink.withAlpha(0.15f * popupOpenAnimation_));
            g.strokePath(tether, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }


        for (const auto& group : offscreenGroups_)
        {
            auto endpointBounds = getEndpointBounds(group.direction);
            bool hovered = hoveredDirection_ == group.direction;
            bool hasMultiple = group.targets.size() > 1;

            // Endpoint circle background
            juce::ColourGradient endpointGrad(
                pink.withAlpha(hovered ? 0.9f : 0.7f), endpointBounds.getCentreX(), endpointBounds.getY(),
                pink.darker(0.3f).withAlpha(hovered ? 0.8f : 0.6f), endpointBounds.getCentreX(), endpointBounds.getBottom(), false);
            g.setGradientFill(endpointGrad);
            g.fillEllipse(endpointBounds);

            // Outer glow rings
            g.setColour(pink.withAlpha(hovered ? 0.5f : 0.3f));
            g.drawEllipse(endpointBounds.expanded(4.0f), 2.5f);
            g.setColour(pink.withAlpha(hovered ? 0.3f : 0.15f));
            g.drawEllipse(endpointBounds.expanded(8.0f), 3.0f);

            // Inner highlight
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.fillEllipse(endpointBounds.getX() + endpointBounds.getWidth() * 0.25f,
                         endpointBounds.getY() + 3.0f,
                         endpointBounds.getWidth() * 0.4f,
                         endpointBounds.getHeight() * 0.25f);

            // Direction arrow (custom vector path — no font glyph dependency)
            {
                juce::Path arrow;
                auto ab = endpointBounds.reduced(kEndpointSize * 0.28f);
                if (group.direction == BubblegumOffscreenDetectionCore::Direction::Left)
                {
                    arrow.startNewSubPath(ab.getRight(), ab.getY());
                    arrow.lineTo(ab.getX(), ab.getCentreY());
                    arrow.lineTo(ab.getRight(), ab.getBottom());
                    arrow.closeSubPath();
                }
                else
                {
                    arrow.startNewSubPath(ab.getX(), ab.getY());
                    arrow.lineTo(ab.getRight(), ab.getCentreY());
                    arrow.lineTo(ab.getX(), ab.getBottom());
                    arrow.closeSubPath();
                }
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.fillPath(arrow);
            }

            // Count badges:
            //   Pink circle   = number of audio sends (original top-right position)
            //   Blue diamond  = number of SC connections, stacked UNDER the send badge
            // so it never clips above the endpoint during mixer movement.
            {
                int scCount  = 0, sndCount = 0;
                for (const auto& t : group.targets)
                {
                    if (t.hasSend) ++sndCount;
                    if (t.hasSidechain) ++scCount;
                }

                const float badgeSize = 20.0f;
                const float badgeGap  = 3.0f;
                const float badgeX    = endpointBounds.getRight() - badgeSize * 0.5f;
                const float topBadgeY = endpointBounds.getY()     - badgeSize * 0.5f;

                // Pink circle badge for sends — restored original placement
                if (sndCount > 0)
                {
                    auto bb = juce::Rectangle<float>(badgeX, topBadgeY, badgeSize, badgeSize);
                    g.setColour(juce::Colour(0xFFFF7DB8).withAlpha(0.92f));
                    g.fillEllipse(bb);
                    g.setColour(juce::Colours::black.withAlpha(0.35f));
                    g.drawEllipse(bb, 1.f);
                    g.setFont(juce::Font(10.0f, juce::Font::bold));
                    g.setColour(juce::Colours::white);
                    g.drawText(juce::String(sndCount), bb, juce::Justification::centred);
                }

                // Blue diamond badge for SC — bottom-right corner of the endpoint bubble.
                if (scCount > 0)
                {
                    const float scBadgeX = endpointBounds.getRight() - badgeSize * 0.5f;
                    const float scBadgeY = endpointBounds.getBottom() - badgeSize * 0.5f;
                    auto bb = juce::Rectangle<float>(scBadgeX, scBadgeY, badgeSize, badgeSize);
                    juce::Path diamond;
                    diamond.startNewSubPath(bb.getCentreX(), bb.getY());
                    diamond.lineTo(bb.getRight(),            bb.getCentreY());
                    diamond.lineTo(bb.getCentreX(),          bb.getBottom());
                    diamond.lineTo(bb.getX(),                bb.getCentreY());
                    diamond.closeSubPath();
                    g.setColour(juce::Colour(0xFF3A7BD5).withAlpha(0.92f));
                    g.fillPath(diamond);
                    g.setColour(juce::Colours::black.withAlpha(0.35f));
                    g.strokePath(diamond, juce::PathStrokeType(1.f));
                    g.setFont(juce::Font(10.0f, juce::Font::bold));
                    g.setColour(juce::Colours::white);
                    g.drawText(juce::String(scCount), bb, juce::Justification::centred);
                }
            }

            // Hover preview tooltip
            if (hovered)
            {
                auto previewText = BubblegumOffscreenDetectionCore::getPreviewText(group);
                if (previewText.isNotEmpty())
                {
                    g.setFont(juce::Font(11.0f, juce::Font::bold));
                    float textWidth = g.getCurrentFont().getStringWidth(previewText) + 16.0f;
                    
                    auto tooltipBounds = endpointBounds;
                    if (group.direction == BubblegumOffscreenDetectionCore::Direction::Left)
                        tooltipBounds = tooltipBounds.translated(kEndpointSize + 8.0f, -4.0f).withWidth(textWidth);
                    else
                        tooltipBounds = tooltipBounds.translated(-(textWidth + 8.0f), -4.0f).withWidth(textWidth);
                    
                    tooltipBounds.setHeight(24.0f);

                    // Tooltip background
                    g.setColour(juce::Colours::black.withAlpha(0.85f));
                    g.fillRoundedRectangle(tooltipBounds, 4.0f);
                    g.setColour(pink.withAlpha(0.6f));
                    g.drawRoundedRectangle(tooltipBounds, 4.0f, 1.5f);

                    // Tooltip text
                    g.setColour(juce::Colours::white.withAlpha(0.95f));
                    g.drawText(previewText, tooltipBounds, juce::Justification::centred);
                }
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        updateHoverState(e.position);
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoveredDirection_ = {};
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (const auto& group : offscreenGroups_)
        {
            auto endpointBounds = getEndpointBounds(group.direction);
            if (endpointBounds.contains(e.position.toFloat()))
            {
                handleEndpointClick(group, endpointBounds);
                return;
            }
        }
    }

private:
    BubblegumV2System* bgV2_ = nullptr;
    MixerPanel* mixerPanel_ = nullptr;
    BubblegumOffscreenDetectionCore detector_;
    std::vector<BubblegumOffscreenDetectionCore::OffscreenGroup> offscreenGroups_;
    std::optional<BubblegumOffscreenDetectionCore::Direction> hoveredDirection_;

    std::unique_ptr<BubblegumOffscreenTargetPopup> activePopup_;
    std::optional<BubblegumOffscreenDetectionCore::Direction> activePopupDirection_;

    // Settings
    bool autoClosePopupOnTargetSelect_ = false;
    bool autoScrollOnTargetSelect_ = true;
    bool mixerVisible_ = true;

    // Animation & Positioning
    juce::Point<float> currentPopupPos_;
    juce::Point<float> targetPopupPos_;
    float popupOpenAnimation_ = 0.0f; // 0.0 (closed) to 1.0 (open)

    float cableZoneCenterY_ = -1.f;
    juce::Rectangle<float> mixerBoundsLocal_;
    std::size_t lastGroupsHash_ = 0;

    void timerCallback() override
    {
        bool needsRepaint = false;

        // Animate popup opening/closing
        float targetAnim = activePopup_ ? 1.0f : 0.0f;
        if (std::abs(popupOpenAnimation_ - targetAnim) > 0.001f)
        {
            popupOpenAnimation_ += (targetAnim - popupOpenAnimation_) * 0.2f;
            needsRepaint = true;
        }

        if (activePopup_)
        {
            updatePopupPosition();

            // Magnetic follow for popup position
            if (currentPopupPos_.getDistanceFrom(targetPopupPos_) > 0.5f)
            {
                currentPopupPos_ = currentPopupPos_ + (targetPopupPos_ - currentPopupPos_) * 0.22f;
                activePopup_->setTopLeftPosition(currentPopupPos_.roundToInt());
                needsRepaint = true;
            }
        }

        if (needsRepaint)
            repaint();
    }

    juce::Rectangle<float> getEndpointBounds(BubblegumOffscreenDetectionCore::Direction dir) const
    {
        // Position bubbles at the outer bottom corners of the mixer, not the screen edges
        float y = mixerBoundsLocal_.isEmpty()
            ? (cableZoneCenterY_ >= 0.f
                ? cableZoneCenterY_ - kEndpointSize * 0.5f
                : (float)getHeight() * 0.5f - kEndpointSize * 0.5f)
            : mixerBoundsLocal_.getBottom() - kEndpointSize - kEdgeMargin;

        if (dir == BubblegumOffscreenDetectionCore::Direction::Left)
        {
            float x = mixerBoundsLocal_.isEmpty()
                ? kEdgeMargin
                : mixerBoundsLocal_.getX() - kEndpointSize - kEdgeMargin;
            return { x, y, kEndpointSize, kEndpointSize };
        }
        else
        {
            float x = mixerBoundsLocal_.isEmpty()
                ? (float)getWidth() - kEdgeMargin - kEndpointSize
                : mixerBoundsLocal_.getRight() + kEdgeMargin;
            return { x, y, kEndpointSize, kEndpointSize };
        }
    }

    void updateHoverState(juce::Point<float> mousePos)
    {
        std::optional<BubblegumOffscreenDetectionCore::Direction> newHover;

        for (const auto& group : offscreenGroups_)
        {
            if (getEndpointBounds(group.direction).contains(mousePos))
            {
                newHover = group.direction;
                break;
            }
        }

        if (newHover != hoveredDirection_)
        {
            hoveredDirection_ = newHover;
            repaint();
        }
    }

    void handleEndpointClick(const BubblegumOffscreenDetectionCore::OffscreenGroup& group,
                            juce::Rectangle<float> endpointBounds)
    {
        // Toggle: if popup is already open for this direction, close it
        if (activePopup_ && activePopupDirection_ == group.direction)
        {
            closePopup();
            return;
        }

        closePopup();

        if (group.targets.size() == 1)
        {
            // Single target: auto-scroll directly
            DBG("[BubblegumOffscreenEndpoint] Single target, auto-scrolling to: " + group.targets[0].trackId);
            if (onScrollToTrack)
                onScrollToTrack(group.targets[0].trackId);
        }
        else if (group.targets.size() > 1)
        {
            // Multiple targets: show picker popup
            DBG("[BubblegumOffscreenEndpoint] Multiple targets (" + juce::String((int)group.targets.size()) + "), showing picker");
            showTargetPicker(group, endpointBounds);
        }
    }

    void showTargetPicker(const BubblegumOffscreenDetectionCore::OffscreenGroup& group,
                         juce::Rectangle<float> anchorBounds)
    {
        activePopupDirection_ = group.direction;
        activePopup_ = std::make_unique<BubblegumOffscreenTargetPopup>(group);
        DBG("[BubblegumOffscreenEndpoint] Popup recreated");
        activePopup_->autoCloseOnTargetSelect = autoClosePopupOnTargetSelect_;

        activePopup_->onTargetSelected = [this](const TrackID& targetId)
        {
            DBG("[BubblegumOffscreenEndpoint] RowClick: autoScroll="
                + juce::String((int)autoScrollOnTargetSelect_)
                + " autoClose=" + juce::String((int)autoClosePopupOnTargetSelect_)
                + " target=" + targetId);
            if (autoScrollOnTargetSelect_ && onScrollToTrack)
                onScrollToTrack(targetId);
            // Popup close is handled inside the popup based on autoCloseOnTargetSelect.
        };

        activePopup_->onToggleSend = [this](const TrackID& targetId)
        {
            DBG("[BubblegumOffscreenEndpoint] Toggle send: " + targetId);
            if (onToggleSend)
                onToggleSend(targetId);
        };

        activePopup_->onDeleteSend = [this](const TrackID& targetId)
        {
            DBG("[BubblegumOffscreenEndpoint] Delete: " + targetId);
            // Check if this is a sidechain target — route to the correct remove API.
            bool isSC = false;
            for (const auto& group : offscreenGroups_)
                for (const auto& t : group.targets)
                    if (t.trackId == targetId) { isSC = t.hasSidechain; break; }

            if (bgV2_)
            {
                if (isSC)
                    bgV2_->removeSidechain(targetId);
                else if (onDeleteSend)
                    onDeleteSend(targetId);
            }
            else if (onDeleteSend)
            {
                onDeleteSend(targetId);
            }
        };

        activePopup_->onSetTapPoint = [this](const TrackID& targetId, TapPoint tap)
        {
            DBG("[BubblegumOffscreenEndpoint] SetTapPoint: " + targetId
                + " tap=" + juce::String((int)tap));
            if (bgV2_)
                bgV2_->setSidechainTapPoint(targetId, tap);
        };

        activePopup_->onClose = [this]()
        {
            closePopup();
        };

        updatePopupPosition();
        currentPopupPos_ = targetPopupPos_; // Snap instantly on open

        addAndMakeVisible(activePopup_.get());
        activePopup_->setTransform(juce::AffineTransform::scale(0.95f, 0.95f, targetPopupPos_.x, targetPopupPos_.y));
        activePopup_->setAlpha(0.0f);

        juce::Desktop::getInstance().getAnimator().animateComponent(
            activePopup_.get(),
            juce::Rectangle<int>(
                (int)targetPopupPos_.x,
                (int)targetPopupPos_.y,
                activePopup_->getWidth(),
                activePopup_->getHeight()),
            1.0f, 200, true, 1.0, 1.0);

        DBG("[BubblegumOffscreenEndpoint] Popup opened at (" + juce::String(targetPopupPos_.x) + ", " + juce::String(targetPopupPos_.y) + ")");
    }

    void updatePopupPosition()
    {
        if (!activePopup_ || !activePopupDirection_.has_value())
            return;

        auto anchorBounds = getEndpointBounds(*activePopupDirection_);
        int popupW = activePopup_->getWidth();
        int popupH = activePopup_->getHeight();

        float x, y;
        if (*activePopupDirection_ == BubblegumOffscreenDetectionCore::Direction::Left)
        {
            x = anchorBounds.getRight() + 12.0f;
            y = anchorBounds.getCentreY() - popupH * 0.5f;
        }
        else
        {
            x = anchorBounds.getX() - popupW - 12.0f;
            y = anchorBounds.getCentreY() - popupH * 0.5f;
        }
        targetPopupPos_ = {x, y};
    }

    void closePopup()
    {
        if (activePopup_)
        {
            DBG("[BubblegumOffscreenEndpoint] Popup closed explicitly");

            auto* popupPtr = activePopup_.get();
            juce::Desktop::getInstance().getAnimator().fadeOut(popupPtr, 150);

            // Transfer ownership to a shared_ptr so the lambda is copy-constructible
            // (required by std::function / juce::Timer::callAfterDelay).
            std::shared_ptr<BubblegumOffscreenTargetPopup> sharedPopup(activePopup_.release());
            juce::Timer::callAfterDelay(160, [sharedPopup]() mutable {
                DBG("[BubblegumOffscreenEndpoint] activePopup reset/destroyed");
                sharedPopup.reset();
            });

            activePopupDirection_.reset();
            repaint();
        }
    }

    TrackID getEffectiveSourceId() const
    {
        if (bgV2_)
        {
            auto src = bgV2_->sourceSync.getSourceTrackId();
            if (src.isNotEmpty())
                return src;
        }
        // Fallback to selected track if no explicit Bubblegum source
        // if (mixerPanel_)
        //     return mixerPanel_->getEngine().getUIState().getPrimarySelectedTrack();
        return {};
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumOffscreenEndpointComponent)
};

} // namespace DAW
