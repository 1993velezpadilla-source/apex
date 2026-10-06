#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../TrackCore/Track.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../PluginHostCore/PluginScannerCore.h"
#include "../ControlsCore/ModernControls.h"
#include "../CommandCore/CommandManager.h"
#include "../CommandCore/GeneralCommands.h"

namespace DAW {

/**
 * MixerPluginSidePanel
 *
 * Nucleus: FL Studio-style side panel on the mixer showing all plugins
 * on the selected track. Supports:
 *   - Unlimited plugins with scrollbar
 *   - Scroll wheel reorder (hover + scroll up/down)
 *   - Drag and drop to copy plugins to other tracks
 *   - Bypass / delete / open editor per plugin
 *   - Add new plugin button at bottom
 */
class MixerPluginSidePanel : public juce::Component,
                              public juce::DragAndDropContainer,
                              public juce::DragAndDropTarget,
                              public TrackManager::Listener,
                              private juce::Timer
{
public:
    static constexpr int kPreferredWidth = 240;
    static constexpr int kSlotHeight     = 56;
    static constexpr int kHeaderH        = 36;
    static constexpr int kAddBtnH        = 28;

    /** Callback: user dragged a plugin from slot srcIdx to a target track (cross-track copy). */
    std::function<void(const TrackID& srcTrack, int srcSlot,
                       const TrackID& destTrack)> onPluginDragCopy;

    /** Callback: user wants to browse and add a plugin. */
    std::function<void(const TrackID&)> onAddPluginRequested;

    /** Command manager bridge for plugin chain mutations. */
    std::function<void(PluginChainCore&, std::function<void()>, const juce::String&)> onPluginChainEditRequested;

    /** Callback: fired when any plugin editor is opened from this panel (for bubble wiring). */
    std::function<void(PluginInstanceCore* slot, const TrackID& trackId, int slotIndex)> onPluginEditorOpened;

    /** Callback: fired when user clicks a slot (provides slot index + modifiers) for multi-select wiring. */
    std::function<void(int slotIndex, const juce::ModifierKeys&)> onSlotClickedWithModifiers;

    /** Set selected visual state on a plugin slot by index. */
    void setSlotSelected(int index, bool selected)
    {
        for (auto* w : slotWidgets_)
        {
            if (auto* sw = dynamic_cast<SlotWidget*>(w))
                if (sw->getSlotIndex() == index)
                    sw->setSelected(selected);
        }
    }

    /** Clear all slot selected states. */
    void clearSlotSelection()
    {
        for (auto* w : slotWidgets_)
            if (auto* sw = dynamic_cast<SlotWidget*>(w))
                sw->setSelected(false);
    }

    explicit MixerPluginSidePanel(PluginScannerCore& scanner)
        : scanner_(scanner)
    {
        viewport_.setScrollBarsShown(true, false);
        viewport_.setViewedComponent(&content_, false);
        addAndMakeVisible(viewport_);

    }

    ~MixerPluginSidePanel() override
    {
        stopTimer();
        if (trackManager_)
            trackManager_->removeListener(this);
    }

    void setTrackManager(TrackManager* tm)
    {
        if (trackManager_)
            trackManager_->removeListener(this);
        trackManager_ = tm;
        if (trackManager_)
            trackManager_->addListener(this);
    }

    // TrackManager::Listener — clear immediately if our track is removed
    void trackRemoved(const TrackID& removedId) override
    {
        if (!track_ || track_->getID() != removedId) return;
        juce::Component::SafePointer<MixerPluginSidePanel> safeThis(this);
        juce::MessageManager::callAsync([safeThis]()
        {
            if (safeThis != nullptr)
                safeThis->clearTrack();
        });
    }

    void setTrack(Track* track, PluginChainCore* chain)
    {
        track_ = track;
        chain_ = chain;
        lastObservedSlotCount_ = -1;
        cachedVisibleStates_.clear();
        DBG("[SidePanel] Header updated to: " + (track_ ? track_->getName() : "none")
            + " isMaster=" + juce::String(track_ && track_->isMaster() ? 1 : 0));
        rebuildSlots();
        updatePollingState();
    }

    void clearTrack()
    {
        track_ = nullptr;
        chain_ = nullptr;
        lastObservedSlotCount_ = -1;
        cachedVisibleStates_.clear();
        updatePollingState();
        rebuildSlots();
    }

    void visibilityChanged() override
    {
        updatePollingState();
    }

    void parentHierarchyChanged() override
    {
        updatePollingState();
    }

    // ── Drag-and-Drop Target (receive plugins from other panels) ──────
    static constexpr const char* kPluginDragDesc = "MixerPluginSlotDrag";

    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        return details.description.toString().startsWith(kPluginDragDesc);
    }

    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        updateReorderIndicator(details);
        repaint();
    }

    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        updateReorderIndicator(details);
        repaint();
    }

    void itemDragExit(const juce::DragAndDropTarget::SourceDetails&) override
    {
        reorderDropIndex_ = -1;
        isCrossTrackDrag_ = false;
        repaint();
    }

    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        reorderDropIndex_ = -1;
        isCrossTrackDrag_ = false;

        // Parse: "MixerPluginSlotDrag:<trackId>:<slotIndex>"
        auto parts = juce::StringArray::fromTokens(details.description.toString(), ":", "");
        if (parts.size() < 3) return;
        auto srcTrackId = parts[1];
        int  srcSlot    = parts[2].getIntValue();
        if (!track_) return;

        if (srcTrackId == track_->getID())
        {
            // Same track → reorder by drop position
            if (!chain_) return;
            int targetSlot = juce::jlimit(0, juce::jmax(0, chain_->getNumSlots() - 1),
                                          (int)(details.localPosition.y - kHeaderH) / kSlotHeight);
            if (targetSlot != srcSlot)
            {
                chain_->moveSlot(srcSlot, targetSlot);
                DBG("[SidePanel] Same-track reorder success: slot " + juce::String(srcSlot)
                    + " -> " + juce::String(targetSlot)
                    + " track=" + track_->getName());
                rebuildSlots();
            }
            else
            {
                DBG("[SidePanel] Same-track reorder no-op (same position)");
            }
        }
        else
        {
            // Different track → copy plugin to this track
            DBG("[SidePanel] Cross-track drop: src=" + srcTrackId + " slot=" + juce::String(srcSlot)
                + " -> dest=" + track_->getID());
            if (onPluginDragCopy)
                onPluginDragCopy(srcTrackId, srcSlot, track_->getID());
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        g.fillAll(t.colors.backgroundDark.darker(0.1f));

        // Header background
        auto headerRect = getLocalBounds().removeFromTop(kHeaderH).toFloat();
        g.setColour(t.colors.surface.darker(0.1f));
        g.fillRect(headerRect);
        g.setColour(t.colors.border.withAlpha(0.5f));
        g.fillRect(headerRect.getX(), headerRect.getBottom() - 1.f, headerRect.getWidth(), 1.f);

        // Build title string
        juce::String title;
        if (track_)
        {
            if (track_->isMaster())
                title = "MASTER FX CHAIN";
            else
                title = "FX" + juce::String::charToString(0x2014) + " " + track_->getName();
        }
        else
        {
            title = "FX CHAIN";
        }

        // Draw title text with proper bounds — no re-consuming from getLocalBounds
        auto headerTextBounds = headerRect.reduced(10.f, 2.f);
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        g.setColour(t.colors.accent);
        auto controlsBounds = getHeaderControlsBounds(headerTextBounds.toNearestInt());
        g.drawText(title, headerTextBounds.withTrimmedRight((float) controlsBounds.getWidth() + 8.0f), juce::Justification::centredLeft, true);

        if (chain_)
        {
            auto bypassBounds = getHeaderBypassButtonBounds(controlsBounds).toFloat();
            const bool allBypassed = chain_->areAllActiveSlotsBypassed();
            const auto bypassBg = allBypassed ? juce::Colour(0xFFEF4444).withAlpha(0.22f)
                                              : t.colors.surface.brighter(0.08f);
            const auto bypassOutline = allBypassed ? juce::Colour(0xFFEF4444).withAlpha(0.85f)
                                                   : t.colors.border.withAlpha(0.55f);
            g.setColour(bypassBg);
            g.fillRoundedRectangle(bypassBounds, 4.f);
            g.setColour(bypassOutline);
            g.drawRoundedRectangle(bypassBounds, 4.f, 1.f);
            g.setColour(allBypassed ? juce::Colours::white : t.colors.text);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText(allBypassed ? "FX OFF" : "BYPASS", bypassBounds, juce::Justification::centred);

            auto chainKnobBounds = getHeaderMixKnobBounds(controlsBounds).toFloat();
            // "WET" label on the left, knob body occupies the right square
            auto wetLabelArea = chainKnobBounds.removeFromLeft(24.0f);
            g.setColour(t.colors.accent.withAlpha(0.85f));
            g.setFont(juce::Font(9.5f, juce::Font::bold));
            g.drawText("WET", wetLabelArea, juce::Justification::centred, false);
            paintMiniKnobNoLabel(g, chainKnobBounds, chainMixValue_, t.colors.accent);
        }

        // "No track selected" placeholder
        if (!chain_)
        {
            g.setColour(t.colors.textSecondary.withAlpha(0.4f));
            g.setFont(juce::Font(11.f));
            g.drawText("Select a track", getLocalBounds().withTrimmedTop(kHeaderH),
                       juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop(kHeaderH);
        viewport_.setBounds(b);
        layoutContent();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (chain_ == nullptr)
            return;

        auto headerArea = getLocalBounds().removeFromTop(kHeaderH);
        if (!headerArea.contains(e.position.toInt()))
            return;

        auto controlsBounds = getHeaderControlsBounds(headerArea.toFloat().reduced(10.f, 2.f).toNearestInt());
        if (getHeaderBypassButtonBounds(controlsBounds).contains(e.position.toInt()))
        {
            const bool allBypassed = chain_->areAllActiveSlotsBypassed();
            chain_->setAllActiveSlotsBypassed(!allBypassed);
            repaint(headerArea);
            repaintVisibleSlotWidgets();
            return;
        }

        if (getHeaderMixKnobBounds(controlsBounds).contains(e.position.toInt()))
        {
            draggingHeaderMix_ = true;
            dragStartPos_ = e.getScreenPosition();
            dragStartValue_ = chainMixValue_;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!draggingHeaderMix_ || chain_ == nullptr)
            return;

        const auto delta = dragStartPos_.y - e.getScreenPosition().y;
        const float newValue = juce::jlimit(0.0f, 1.0f, dragStartValue_ + (float) delta / 180.0f);
        applyChainMixValue(newValue);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingHeaderMix_ = false;
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();

        // ── Reorder placeholder indicator ───────────────────────────────
        if (reorderDropIndex_ >= 0 && !isCrossTrackDrag_)
        {
            // Draw an accent insertion line at the target slot position
            int indicatorY = kHeaderH + reorderDropIndex_ * kSlotHeight - viewport_.getViewPositionY();
            g.setColour(t.colors.accent);
            g.fillRect(4, indicatorY - 1, getWidth() - 8, 3);
            // Small arrow indicators
            float ly = (float)indicatorY;
            juce::Path arrowL;
            arrowL.addTriangle(0.f, ly - 5.f, 7.f, ly, 0.f, ly + 5.f);
            g.fillPath(arrowL);
            juce::Path arrowR;
            arrowR.addTriangle((float)getWidth(), ly - 5.f, (float)getWidth() - 7.f, ly, (float)getWidth(), ly + 5.f);
            g.fillPath(arrowR);
        }

        // ── Cross-track drop highlight ──────────────────────────────────
        if (isCrossTrackDrag_)
        {
            g.setColour(t.colors.accent.withAlpha(0.15f));
            g.fillRect(getLocalBounds().withTrimmedTop(kHeaderH));
            g.setColour(t.colors.accent.withAlpha(0.6f));
            g.drawRect(getLocalBounds().withTrimmedTop(kHeaderH).toFloat(), 2.f);
        }
    }

private:
    PluginScannerCore& scanner_;
    TrackManager*     trackManager_ = nullptr;
    Track*            track_ = nullptr;
    PluginChainCore*  chain_ = nullptr;
    juce::Viewport    viewport_;
    juce::Component   content_;
    juce::OwnedArray<juce::Component> slotWidgets_;

    struct SlotVisualState
    {
        bool hasPlugin = false;
        bool bypassed = false;
        bool selected = false;
        bool hovered = false;
        juce::String name;

        bool operator==(const SlotVisualState& other) const noexcept
        {
            return hasPlugin == other.hasPlugin
                && bypassed == other.bypassed
                && selected == other.selected
                && hovered == other.hovered
                && name == other.name;
        }

        bool operator!=(const SlotVisualState& other) const noexcept
        {
            return !(*this == other);
        }
    };

    int hoveredSlot_       = -1;
    int dragSlot_          = -1;
    int reorderDropIndex_  = -1;
    bool isCrossTrackDrag_ = false;
    int lastObservedSlotCount_ = -1;
    bool draggingHeaderMix_ = false;
    juce::Point<int> dragStartPos_;
    float dragStartValue_ = 1.0f;
    float chainMixValue_ = 1.0f;
    std::unordered_map<int, SlotVisualState> cachedVisibleStates_;

    static juce::Rectangle<int> getHeaderControlsBounds(juce::Rectangle<int> headerTextBounds)
    {
        const int controlsWidth = 160;
        return headerTextBounds.removeFromRight(controlsWidth);
    }

    static juce::Rectangle<int> getHeaderBypassButtonBounds(juce::Rectangle<int> controlsBounds)
    {
        auto area = controlsBounds;
        return area.removeFromLeft(56).reduced(0, 4);
    }

    static juce::Rectangle<int> getHeaderMixTextBounds(juce::Rectangle<int> controlsBounds)
    {
        auto area = controlsBounds;
        area.removeFromLeft(58);
        area.removeFromRight(40);
        return area.reduced(2, 4);
    }

    static juce::Rectangle<int> getHeaderMixKnobBounds(juce::Rectangle<int> controlsBounds)
    {
        // wider area: left portion used for "WET" label, right for the knob body
        return controlsBounds.removeFromRight(80).reduced(2, 2);
    }

    // Knob-only variant: no bottom label strip — used when the label is drawn externally
    static void paintMiniKnobNoLabel(juce::Graphics& g,
                                     juce::Rectangle<float> bounds,
                                     float normalized,
                                     juce::Colour accent)
    {
        const float value = juce::jlimit(0.0f, 1.0f, normalized);

        const float bodySize = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 4.0f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const auto bodyRect = juce::Rectangle<float>(cx - bodySize * 0.5f,
                                                     cy - bodySize * 0.5f,
                                                     bodySize, bodySize);

        const float strokeW  = 2.8f;
        const float arcRadius = bodySize * 0.5f + strokeW * 0.5f + 1.0f;

        const float startAngle = juce::MathConstants<float>::pi * 1.25f;
        const float endAngle   = juce::MathConstants<float>::pi * 2.75f;
        const float valueAngle = startAngle + (endAngle - startAngle) * value;

        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillEllipse(bodyRect.translated(0.0f, 1.5f));

        g.setColour(juce::Colour(0xFF1E2028));
        g.fillEllipse(bodyRect);

        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.fillEllipse(bodyRect.reduced(bodySize * 0.12f));

        juce::Path trackArc;
        trackArc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour(accent.withAlpha(0.15f));
        g.strokePath(trackArc, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        if (value > 0.001f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f, startAngle, valueAngle, true);
            g.setColour(accent.withAlpha(0.95f));
            g.strokePath(valueArc, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        const float pointerOuter = bodySize * 0.5f - 1.0f;
        const float pointerInner = bodySize * 0.5f * 0.22f;
        const auto pointOnKnob = [] (float centreX, float centreY, float radius, float angle)
        {
            return juce::Point<float>(centreX + std::sin(angle) * radius,
                                      centreY - std::cos(angle) * radius);
        };
        const auto pointerStart = pointOnKnob(cx, cy, pointerInner, valueAngle);
        const auto pointerEnd = pointOnKnob(cx, cy, pointerOuter, valueAngle);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.drawLine(pointerStart.x,
                   pointerStart.y,
                   pointerEnd.x,
                   pointerEnd.y,
                   1.8f);

        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillEllipse(cx - 1.5f, cy - 1.5f, 3.0f, 3.0f);

        // Percent value centred inside the knob body
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.drawText(juce::String(juce::roundToInt(value * 100.0f)) + "%",
                   bodyRect.translated(0.0f, bodySize * 0.72f).withHeight(14.0f),
                   juce::Justification::centred, false);
    }

    static void paintMiniKnob(juce::Graphics& g,
                              juce::Rectangle<float> bounds,
                              float normalized,
                              juce::Colour accent,
                              const juce::String& label)
    {
        const float value = juce::jlimit(0.0f, 1.0f, normalized);

        // Reserve bottom strip: percent on top half, name on bottom half
        auto labelStrip = bounds.removeFromBottom(20.0f);

        // Knob body — centred square inside remaining bounds, slightly inset
        const float bodySize = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 8.0f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const auto bodyRect = juce::Rectangle<float>(cx - bodySize * 0.5f,
                                                     cy - bodySize * 0.5f,
                                                     bodySize, bodySize);

        // Arc ring drawn OUTSIDE the body at its exact edge
        // arcRadius = half body + half stroke so the inner edge of the stroke
        // sits exactly at the body circumference
        const float strokeW  = 2.5f;
        const float arcRadius = bodySize * 0.5f + strokeW * 0.5f + 1.0f;

        // 7 o'clock → 5 o'clock  (225 °)
        const float startAngle = juce::MathConstants<float>::pi * 1.25f;
        const float endAngle   = juce::MathConstants<float>::pi * 2.75f;
        const float valueAngle = startAngle + (endAngle - startAngle) * value;

        // Drop shadow
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillEllipse(bodyRect.translated(0.0f, 1.5f));

        // Knob body fill
        g.setColour(juce::Colour(0xFF1E2028));
        g.fillEllipse(bodyRect);

        // Inner gloss
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.fillEllipse(bodyRect.reduced(bodySize * 0.12f));

        // Background track arc
        juce::Path trackArc;
        trackArc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour(accent.withAlpha(0.15f));
        g.strokePath(trackArc, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

        // Filled value arc
        if (value > 0.001f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f, startAngle, valueAngle, true);
            g.setColour(accent.withAlpha(0.95f));
            g.strokePath(valueArc, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        // Pointer: drawn inside the body, tip reaches the body edge (bodySize/2)
        // so it visually meets the arc ring
        const float pointerOuter = bodySize * 0.5f - 1.0f;   // just inside body edge
        const float pointerInner = bodySize * 0.5f * 0.22f;  // short tail near centre
        const auto pointOnKnob = [] (float centreX, float centreY, float radius, float angle)
        {
            return juce::Point<float>(centreX + std::sin(angle) * radius,
                                      centreY - std::cos(angle) * radius);
        };
        const auto pointerStart = pointOnKnob(cx, cy, pointerInner, valueAngle);
        const auto pointerEnd = pointOnKnob(cx, cy, pointerOuter, valueAngle);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.drawLine(pointerStart.x,
                   pointerStart.y,
                   pointerEnd.x,
                   pointerEnd.y,
                   1.8f);

        // Centre dot
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillEllipse(cx - 1.5f, cy - 1.5f, 3.0f, 3.0f);

        // Percent value — bold, clear
        auto pctBounds = labelStrip.removeFromTop(labelStrip.getHeight() * 0.52f);
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(juce::String(juce::roundToInt(value * 100.0f)) + "%",
                   pctBounds, juce::Justification::centred, false);

        // Name label
        g.setColour(juce::Colours::white.withAlpha(0.70f));
        g.setFont(juce::Font(9.0f, juce::Font::plain));
        g.drawText(label, labelStrip, juce::Justification::centred, false);
    }

    void repaintVisibleSlotWidgets()
    {
        for (auto* widget : slotWidgets_)
            if (auto* slotWidget = dynamic_cast<SlotWidget*>(widget))
                slotWidget->repaint();
    }

    void syncHeaderMixFromChain()
    {
        chainMixValue_ = chain_ ? chain_->getChainOutputMix() : 1.0f;
    }

    void applyChainMixValue(float newValue)
    {
        if (chain_ == nullptr)
            return;

        chainMixValue_ = juce::jlimit(0.0f, 1.0f, newValue);
        chain_->setChainOutputMix(chainMixValue_);
        repaint(getLocalBounds().removeFromTop(kHeaderH));
    }

    /** Parse drag details and compute reorder indicator position. */
    void updateReorderIndicator(const juce::DragAndDropTarget::SourceDetails& details)
    {
        auto parts = juce::StringArray::fromTokens(details.description.toString(), ":", "");
        if (parts.size() < 3 || !track_ || !chain_)
        {
            reorderDropIndex_ = -1;
            isCrossTrackDrag_ = false;
            return;
        }
        auto srcTrackId = parts[1];

        if (srcTrackId == track_->getID())
        {
            // Same track → show reorder indicator
            isCrossTrackDrag_ = false;
            int slot = juce::jlimit(0, chain_->getNumSlots(),
                                    (int)(details.localPosition.y - kHeaderH + viewport_.getViewPositionY()) / kSlotHeight);
            reorderDropIndex_ = slot;
            DBG("[SidePanel] Hover: same-track reorder area, placeholder=" + juce::String(slot));
        }
        else
        {
            // Cross-track → highlight the whole panel as a drop target
            isCrossTrackDrag_ = true;
            reorderDropIndex_ = -1;
            DBG("[SidePanel] Hover: cross-track drop from " + srcTrackId + " to " + track_->getID());
        }
    }

    /** Toggle a slot's editor: open if closed, close if open, restore if minimized. */
    void toggleSlotEditor(int slotIndex)
    {
        if (!chain_) return;
        auto* slot = chain_->getSlot(slotIndex);
        if (!slot)
        {
            DBG("[SidePanel] toggleSlotEditor: slot " + juce::String(slotIndex) + " is null");
            return;
        }

        juce::String pluginName = slot->getName();
        juce::String trackName = track_ ? track_->getName() : "?";

        if (slot->isEditorOpen())
        {
            DBG("[SidePanel] toggleSlotEditor: editor open for \"" + pluginName
                + "\" on track=\"" + trackName + "\" -> close");
            slot->closeEditor();
            return;
        }

        if (slot->isEditorMinimized())
        {
            DBG("[SidePanel] toggleSlotEditor: editor minimized for \"" + pluginName
                + "\" on track=\"" + trackName + "\" -> restore");
            slot->showEditor();
            return;
        }

        // Editor closed → open
        DBG("[SidePanel] toggleSlotEditor: editor closed for \"" + pluginName
            + "\" on track=\"" + trackName + "\" -> open");
        openSlotEditor(slotIndex);
    }

    /** Open a slot's editor (always opens, never closes). Used by context menu / add-plugin. */
    void openSlotEditor(int slotIndex)
    {
        if (!chain_) return;
        auto* slot = chain_->getSlot(slotIndex);
        if (!slot)
        {
            DBG("[SidePanel] openSlotEditor: slot " + juce::String(slotIndex) + " is null");
            return;
        }

        juce::String pluginName = slot->getName();
        juce::String trackName = track_ ? track_->getName() : "?";

        if (slot->isEditorOpen())
        {
            DBG("[SidePanel] openSlotEditor: already open for \"" + pluginName + "\" -> focus");
            slot->showEditor();
            return;
        }

        if (slot->isEditorMinimized())
        {
            DBG("[SidePanel] openSlotEditor: minimized for \"" + pluginName + "\" -> restore");
            slot->showEditor();
            return;
        }

        DBG("[SidePanel] openSlotEditor: closed for \"" + pluginName + "\" -> open");
        juce::Logger::writeToLog("[PluginOpenSource] source=SidePanel plugin=\"" + pluginName
            + "\" track=\"" + trackName + "\" trackId=\"" + (track_ ? track_->getID() : juce::String())
            + "\" slotIndex=" + juce::String(slotIndex)
            + " slotValid=" + juce::String(slot != nullptr ? 1 : 0));
        slot->openEditor();

        if (slot->isEditorCreated() && onPluginEditorOpened && track_)
            onPluginEditorOpened(slot, track_->getID(), slotIndex);
    }

    void rebuildSlots()
    {
        slotWidgets_.clear();
        content_.removeAllChildren();
        cachedVisibleStates_.clear();
        lastObservedSlotCount_ = chain_ ? chain_->getNumSlots() : -1;
        syncHeaderMixFromChain();

        if (!chain_) { layoutContent(); repaint(); return; }

        int count = chain_->getNumSlots();
        for (int i = 0; i < count; ++i)
        {
            auto* w = new SlotWidget(*this, i);
            slotWidgets_.add(w);
            content_.addAndMakeVisible(w);
        }
        // Add "+" button
        auto* addBtn = new AddButton(*this);
        slotWidgets_.add(addBtn);
        content_.addAndMakeVisible(addBtn);

        layoutContent();
        repaint();
    }

    void layoutContent()
    {
        int y = 0;
        for (auto* w : slotWidgets_)
        {
            w->setBounds(0, y, viewport_.getWidth() - (viewport_.isVerticalScrollBarShown() ? 10 : 0), kSlotHeight);
            y += kSlotHeight;
        }
        content_.setSize(viewport_.getWidth(), juce::jmax(y, viewport_.getHeight()));
    }

    void timerCallback() override
    {
        if (chain_ == nullptr)
        {
            stopTimer();
            return;
        }

        const int currentSlotCount = chain_->getNumSlots();
        if (currentSlotCount != lastObservedSlotCount_)
        {
            rebuildSlots();
            return;
        }

        const auto viewArea = viewport_.getViewArea();
        const int viewTop = viewArea.getY();
        const int viewBottom = viewArea.getBottom();
        const int visibleStart = juce::jlimit(0, juce::jmax(0, currentSlotCount - 1), viewTop / kSlotHeight);
        const int visibleEnd = juce::jlimit(0, juce::jmax(0, currentSlotCount - 1), juce::jmax(0, (viewBottom - 1) / kSlotHeight));

        for (auto it = cachedVisibleStates_.begin(); it != cachedVisibleStates_.end();)
        {
            const int index = it->first;
            if (index < visibleStart || index > visibleEnd)
                it = cachedVisibleStates_.erase(it);
            else
                ++it;
        }

        for (auto* widget : slotWidgets_)
        {
            auto* slotWidget = dynamic_cast<SlotWidget*>(widget);
            if (slotWidget == nullptr)
                continue;

            const int index = slotWidget->getSlotIndex();
            if (index < visibleStart || index > visibleEnd)
                continue;

            const auto currentState = slotWidget->captureVisualState();
            auto found = cachedVisibleStates_.find(index);
            if (found == cachedVisibleStates_.end() || found->second != currentState)
            {
                cachedVisibleStates_[index] = currentState;
                slotWidget->repaint();
            }
        }
    }

    void updatePollingState()
    {
        if (chain_ != nullptr && isShowing())
            startTimerHz(60);
        else
            stopTimer();
    }

    // ── Slot Widget ───────────────────────────────────────────────────────

    class SlotWidget : public juce::Component
    {
    public:
        SlotWidget(MixerPluginSidePanel& owner, int index)
            : owner_(owner), index_(index) {}

        int  getSlotIndex() const noexcept { return index_; }
        void setSelected(bool s) { if (selected_ != s) { selected_ = s; repaint(); } }
        bool isSelected()  const noexcept { return selected_; }

        SlotVisualState captureVisualState() const
        {
            SlotVisualState state;
            state.selected = selected_;
            state.hovered = hovered_;

            auto* chain = owner_.chain_;
            if (chain == nullptr)
                return state;

            if (auto* slot = chain->getSlot(index_))
            {
                state.hasPlugin = true;
                state.bypassed = slot->isBypassed();
                state.name = slot->getName();
            }

            return state;
        }

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto b = getLocalBounds().toFloat().reduced(2.f, 1.f);
            auto* chain = owner_.chain_;
            if (!chain) return;

            auto* slot = chain->getSlot(index_);
            bool hasPlugin = (slot != nullptr);
            bool bypassed = hasPlugin && slot->isBypassed();

            // Background
            g.setColour(hovered_ ? t.colors.surface.brighter(0.08f) : t.colors.surface);
            g.fillRoundedRectangle(b, 4.f);

            // Selection outline (penthouse-style, thin elegant pink)
            if (selected_)
            {
                g.setColour(juce::Colour(0xFFFF7DB8).withAlpha(0.75f));
                g.drawRoundedRectangle(b.reduced(0.5f), 4.f, 0.85f);
            }
            else
            {
                g.setColour(t.colors.border.withAlpha(0.3f));
                g.drawRoundedRectangle(b, 4.f, 1.f);
            }

            if (!hasPlugin) return;

            // Slot number
            g.setFont(juce::Font(8.f, juce::Font::bold));
            g.setColour(t.colors.textSecondary.withAlpha(0.5f));
            g.drawText(juce::String(index_ + 1), b.removeFromLeft(18.f), juce::Justification::centred);

            // Bypass indicator (green = active processing, orange = bypassed)
            auto bypassArea = b.removeFromLeft(14.f);
            g.setColour(bypassed ? juce::Colour(0xFFFF9500) : juce::Colour(0xFF22C55E));
            g.fillEllipse(bypassArea.getCentreX() - 4.f, bypassArea.getCentreY() - 4.f, 8.f, 8.f);

            // Plugin name
            auto rightControls = b.removeFromRight(68.f);
            auto nameArea = b.reduced(4.f, 0.f);
            g.setFont(juce::Font(10.f));
            g.setColour(bypassed ? t.colors.textDisabled : t.colors.text);
            g.drawText(slot->getName(), nameArea.removeFromTop(18.f), juce::Justification::centredLeft, true);

            const float wet = chain->getSlotMix(index_);
            auto mixKnob = rightControls.removeFromLeft(44.f).reduced(1.f, 2.f);
            paintMiniKnob(g, mixKnob, wet, t.colors.accent, "WET");
            // remaining rightControls acts as a spacer between knob and the X overlay

            // "X" delete button area (right edge)
            if (hovered_)
            {
                auto xArea = getLocalBounds().toFloat().removeFromRight(20.f).reduced(4.f, 6.f);
                g.setColour(juce::Colour(0xFFEF4444).withAlpha(0.7f));
                g.fillRoundedRectangle(xArea, 3.f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(9.f, juce::Font::bold));
                g.drawText("X", xArea, juce::Justification::centred);
            }
        }

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true;  repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovered_ = false; repaint(); }

        void mouseDown(const juce::MouseEvent& e) override
        {
            auto* chain = owner_.chain_;
            if (!chain || !chain->hasPlugin(index_)) return;

            dragMode_ = DragMode::None;

            // Delete button hit
            auto xArea = getLocalBounds().toFloat().removeFromRight(20.f).reduced(4.f, 6.f);
            if (xArea.contains(e.position))
            {
                if (owner_.onPluginChainEditRequested)
                    owner_.onPluginChainEditRequested(*chain, [chain, this]() { chain->removePlugin(index_); }, "Remove Plugin");
                else
                    chain->removePlugin(index_);
                owner_.rebuildSlots();
                return;
            }

            // Bypass button hit (the colored dot)
            auto bypassHit = getLocalBounds().toFloat().reduced(2.f, 1.f);
            bypassHit.removeFromLeft(18.f);
            bypassHit = bypassHit.removeFromLeft(14.f);
            if (bypassHit.contains(e.position))
            {
                chain->setSlotBypassed(index_, !chain->getSlot(index_)->isBypassed());
                repaint();
                return;
            }

            const auto knobZone = getKnobZone();
            if (knobZone.contains(e.position.toInt()))
            {
                dragMode_ = DragMode::Wet;
                dragStartPos_ = e.getScreenPosition();
                dragStartMix_ = chain->getSlotMix(index_);
                return;
            }

            // Right-click context menu
            if (e.mods.isPopupMenu())
            {
                showContextMenu();
                return;
            }

            // Notify owner for multi-select handling (Ctrl/Shift)
            if (owner_.onSlotClickedWithModifiers)
                owner_.onSlotClickedWithModifiers(index_, e.mods);

            // Mark for toggle on mouseUp (so drag doesn't also toggle)
            clickedForToggle_ = true;
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (dragMode_ != DragMode::None)
            {
                dragMode_ = DragMode::None;
                clickedForToggle_ = false;
                return;
            }

            if (clickedForToggle_ && e.getDistanceFromDragStart() < 8)
            {
                owner_.toggleSlotEditor(index_);
            }
            clickedForToggle_ = false;
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            auto* chain = owner_.chain_;
            if (dragMode_ != DragMode::None)
            {
                if (!chain || !chain->hasPlugin(index_))
                    return;

                const auto delta = dragStartPos_.y - e.getScreenPosition().y;
                float wet = juce::jlimit(0.0f, 1.0f, dragStartMix_ + (float) delta / 180.0f);

                chain->setSlotMix(index_, wet);
                repaint();
                clickedForToggle_ = false;
                return;
            }

            if (e.getDistanceFromDragStart() < 8) return;
            if (!chain || !chain->hasPlugin(index_)) return;
            if (!owner_.track_) return;

            auto* slot = chain->getSlot(index_);
            juce::String pluginName = slot ? slot->getName() : "Plugin";

            // Encode drag info: "MixerPluginSlotDrag:<trackId>:<slotIndex>"
            juce::String dragDesc = juce::String(kPluginDragDesc) + ":" +
                                   owner_.track_->getID() + ":" +
                                   juce::String(index_);

            DBG("[SidePanel] Drag start: plugin=\"" + pluginName + "\" id=" + juce::String(index_)
                + " srcTrack=\"" + owner_.track_->getName() + "\" (" + owner_.track_->getID() + ")");

            // Create a rich drag ghost showing plugin name
            int ghostW = juce::jmax(140, getWidth());
            int ghostH = kSlotHeight;
            juce::Image ghost(juce::Image::ARGB, ghostW, ghostH, true);
            {
                juce::Graphics gg(ghost);
                auto& t = Theme::getInstance();
                gg.setColour(t.colors.accent.withAlpha(0.85f));
                gg.fillRoundedRectangle(0.f, 0.f, (float)ghostW, (float)ghostH, 6.f);
                gg.setColour(juce::Colours::white);
                gg.setFont(juce::Font(11.f, juce::Font::bold));
                gg.drawText(pluginName, 8, 0, ghostW - 16, ghostH, juce::Justification::centredLeft, true);
                gg.setColour(juce::Colours::white.withAlpha(0.4f));
                gg.drawRoundedRectangle(0.5f, 0.5f, (float)ghostW - 1.f, (float)ghostH - 1.f, 6.f, 1.f);
            }
            DBG("[SidePanel] Ghost created for plugin=\"" + pluginName + "\"");
            owner_.startDragging(dragDesc, this, juce::ScaledImage(ghost), true);
        }

        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            auto* chain = owner_.chain_;
            if (!chain || !chain->hasPlugin(index_)) return;

            // Scroll wheel reorder
            int dir = (w.deltaY > 0.f) ? -1 : 1;
            int newIdx = index_ + dir;
            if (newIdx >= 0 && newIdx < chain->getNumSlots())
            {
                if (owner_.onPluginChainEditRequested)
                    owner_.onPluginChainEditRequested(*chain, [chain, this, newIdx]() { chain->moveSlot(index_, newIdx); }, "Move Plugin");
                else
                    chain->moveSlot(index_, newIdx);
                owner_.rebuildSlots();
            }
        }

    private:
        MixerPluginSidePanel& owner_;
        int  index_;
        bool hovered_ = false;
        bool clickedForToggle_ = false;
        bool selected_ = false;
        juce::Point<int> dragStartPos_;
        float dragStartMix_ = 1.0f;

        enum class DragMode { None, Wet };

        DragMode dragMode_ = DragMode::None;

        juce::Rectangle<int> getKnobZone() const
        {
            auto bounds = getLocalBounds().reduced(2, 1);
            bounds.removeFromLeft(18);
            bounds.removeFromLeft(14);
            auto rightControls = bounds.removeFromRight(56);
            return rightControls.removeFromLeft(52).reduced(1, 1);
        }

        void showContextMenu()
        {
            auto* chain = owner_.chain_;
            auto* slot = chain ? chain->getSlot(index_) : nullptr;
            if (!slot) return;

            juce::PopupMenu menu;
            menu.addItem(1, "Open Editor");
            menu.addItem(2, slot->isBypassed() ? "Enable" : "Bypass");
            menu.addSeparator();
            menu.addItem(3, "Move Up",   index_ > 0);
            menu.addItem(4, "Move Down", index_ < chain->getNumSlots() - 1);
            menu.addSeparator();
            menu.addItem(10, "Remove Plugin");

            juce::Component::SafePointer<SlotWidget> safeThis(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                [safeThis](int r)
                {
                    if (!safeThis) return;
                    // Re-read through safeThis: avoids stale chain/slot pointers if the
                    // track was deselected or plugins removed while the menu was open.
                    auto* chain = safeThis->owner_.chain_;
                    if (!chain) return;
                    auto* slot = chain->getSlot(safeThis->index_);

                    if (r == 1)
                    {
                        safeThis->owner_.openSlotEditor(safeThis->index_);
                    }
                    else if (r == 2)
                    {
                        if (slot)
                        {
                            if (safeThis->owner_.onPluginChainEditRequested)
                                safeThis->owner_.onPluginChainEditRequested(*chain, [chain, safeThis]() { chain->setSlotBypassed(safeThis->index_, !chain->getSlot(safeThis->index_)->isBypassed()); }, slot->isBypassed() ? "Enable Plugin" : "Bypass Plugin");
                            else
                                chain->setSlotBypassed(safeThis->index_, !slot->isBypassed());
                            safeThis->repaint();
                        }
                    }
                    else if (r == 3) { if (safeThis->owner_.onPluginChainEditRequested) safeThis->owner_.onPluginChainEditRequested(*chain, [chain, safeThis]() { chain->moveSlot(safeThis->index_, safeThis->index_ - 1); }, "Move Plugin"); else chain->moveSlot(safeThis->index_, safeThis->index_ - 1); safeThis->owner_.rebuildSlots(); }
                    else if (r == 4) { if (safeThis->owner_.onPluginChainEditRequested) safeThis->owner_.onPluginChainEditRequested(*chain, [chain, safeThis]() { chain->moveSlot(safeThis->index_, safeThis->index_ + 1); }, "Move Plugin"); else chain->moveSlot(safeThis->index_, safeThis->index_ + 1); safeThis->owner_.rebuildSlots(); }
                    else if (r == 10) { if (safeThis->owner_.onPluginChainEditRequested) safeThis->owner_.onPluginChainEditRequested(*chain, [chain, safeThis]() { chain->removePlugin(safeThis->index_); }, "Remove Plugin"); else chain->removePlugin(safeThis->index_); safeThis->owner_.rebuildSlots(); }
                });
        }
    };

    // ── Add Button ────────────────────────────────────────────────────────

    class AddButton : public juce::Component
    {
    public:
        AddButton(MixerPluginSidePanel& owner) : owner_(owner) {}

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto b = getLocalBounds().toFloat().reduced(4.f, 4.f);
            g.setColour(hovered_ ? t.colors.accent.withAlpha(0.2f) : t.colors.surface.withAlpha(0.5f));
            g.fillRoundedRectangle(b, 4.f);
            g.setColour(t.colors.accent.withAlpha(hovered_ ? 0.9f : 0.5f));
            g.drawRoundedRectangle(b, 4.f, 1.f);
            g.setFont(juce::Font(10.f, juce::Font::bold));
            g.drawText("+ Add Plugin", b, juce::Justification::centred);
        }

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true;  repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovered_ = false; repaint(); }

        void mouseDown(const juce::MouseEvent&) override
        {
            showPluginBrowser();
        }

    private:
        MixerPluginSidePanel& owner_;
        bool hovered_ = false;

        void showPluginBrowser()
        {
            if (!owner_.chain_)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon, "No Track Selected",
                    "Select a track first to add plugins.");
                return;
            }

            auto finalVisibleCount = owner_.scanner_.ensureBrowserVisibleDataReady("MixerPluginSidePanel::AddButton::showPluginBrowser");
            auto& known = owner_.scanner_.getKnownPlugins();
            const auto& list = known.getTypes();
            if (list.isEmpty())
            {
                PluginScanAuditLogCore::appendLine(
                    "plugin_ui_flow.log",
                    "MixerPluginSidePanel::AddButton::showPluginBrowser noPluginsPopup"
                        " scannerId=0x" + juce::String::toHexString((juce::int64) reinterpret_cast<uintptr_t>(&owner_.scanner_))
                        + " cacheCount=" + juce::String(owner_.scanner_.getCache().getCount())
                        + " knownCount=" + juce::String(owner_.scanner_.getKnownPlugins().getNumTypes())
                        + " browserFeedCount=" + juce::String(finalVisibleCount)
                        + " panelItemCount=unavailable"
                        + " scanRunning=" + juce::String(owner_.scanner_.isScanning() ? 1 : 0));
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon, "No Plugins",
                    "No plugins found. Go to Settings to scan for plugins.");
                return;
            }

            juce::PopupMenu menu;
            juce::StringArray manufacturers;
            for (auto& d : list)
                manufacturers.addIfNotAlreadyThere(d.manufacturerName);
            manufacturers.sort(true);

            int itemId = 1;
            std::vector<juce::PluginDescription> orderedDescs;

            for (auto& mfr : manufacturers)
            {
                juce::PopupMenu sub;
                for (auto& d : list)
                {
                    if (d.manufacturerName == mfr)
                    {
                        juce::String label = d.name + "  [" + d.pluginFormatName + "]";
                        sub.addItem(itemId++, label);
                        orderedDescs.push_back(d);
                    }
                }
                menu.addSubMenu(mfr, sub);
            }

            juce::Component::SafePointer<MixerPluginSidePanel> safePanel(&owner_);
            menu.showMenuAsync(
                juce::PopupMenu::Options().withTargetComponent(this),
                [safePanel, orderedDescs](int result)
                {
                    if (!safePanel) return;
                    if (result < 1 || result > (int)orderedDescs.size()) return;
                    const auto& desc = orderedDescs[(size_t)result - 1];
                    juce::String err;
                    if (safePanel->chain_)
                    {
                        int slotIndex = safePanel->chain_->appendPlugin(desc,
                            safePanel->scanner_.getFormatManager(), err);
                        safePanel->rebuildSlots();

                        if (slotIndex >= 0 && safePanel)
                            safePanel->openSlotEditor(slotIndex);
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::InfoIcon, "No Track Selected",
                            "Select a track first to add plugins.");
                        return;
                    }
                    if (err.isNotEmpty())
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon, "Error", err);
                });
        }
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPluginSidePanel)
};

} // namespace DAW
