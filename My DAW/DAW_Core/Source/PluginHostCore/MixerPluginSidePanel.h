#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../ThemeCore/ApexPrimitives.h"
#include "../ThemeCore/ApexLivingSeal.h"
#include "../TrackCore/Track.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../PluginHostCore/PluginHostingProductPolicyCore.h"
#include "../PluginHostCore/PluginScannerCore.h"
#include "../ControlsCore/ModernControls.h"
#include "../CommandCore/CommandManager.h"
#include "../CommandCore/GeneralCommands.h"
#include "../UICore/SlimeSidePanel.h"

namespace DAW {

namespace {

// Script typeface for the hand-signed "FX CHAIN" / "MASTER FX" wordmark.
// Scans the system font list ONCE (static cache) and prefers a cursive face,
// falling back to the default sans if none of the candidates exist.
const juce::String& getScriptTypeface()
{
    static const juce::String name = []()
    {
        const char* candidates[] = { "Brush Script MT", "Segoe Script",
                                     "Lucida Handwriting", "Edwardian Script ITC" };
        for (auto* c : candidates)
            if (juce::Font::findAllTypefaceNames().contains(juce::String(c)))
                return juce::String(c);
        return juce::String();
    }();
    return name;
}

} // namespace

class MixerPluginSidePanel;

class ScrollForwardingViewport : public juce::Viewport
{
public:
    using juce::Viewport::Viewport;

    std::function<void(const juce::MouseWheelDetails&)> onWheel;

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (onWheel)
        {
            onWheel(wheel);
            return; // do NOT call base class — we handle scrolling ourselves
        }
        juce::Viewport::mouseWheelMove(e, wheel);
    }
};

class ContentComponent : public juce::Component
{
public:
    using juce::Component::Component;

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // Forward wheel events from content children up to the viewport
        if (auto* viewport = findParentComponentOfClass<ScrollForwardingViewport>())
            viewport->mouseWheelMove(e, wheel);
    }
};

class MixerPluginSidePanel : public juce::Component,
                              public juce::DragAndDropContainer,
                              public juce::DragAndDropTarget,
                              public TrackManager::Listener,
                              private juce::Timer
{
public:
    static constexpr int kPreferredWidth  = 240;
    static constexpr int kSlotHeight      = 36;
    static constexpr int kHeaderH         = 40;
    static constexpr int kControlsH       = 30;
    static constexpr int kAddBtnH         = 32;
    static constexpr int kMaxVisibleSlots = 5;

    int getViewportTop()   const noexcept { return kHeaderH + kControlsH; }

    int getMaxViewportH()  const noexcept { return kMaxVisibleSlots * kSlotHeight + 8; }

    std::function<void(const TrackID& srcTrack, int srcSlot,
                       const TrackID& destTrack)> onPluginDragCopy;

    std::function<void(const TrackID&)> onAddPluginRequested;

    std::function<void(PluginChainCore&, std::function<void()>, const juce::String&)> onPluginChainEditRequested;
    std::function<void(PluginChainCore&, int, int)> onPluginSlotMoveRequested;

    void requestSlotMove(int fromIndex, int toIndex)
    {
        if (chain_ == nullptr) return;
        if (onPluginSlotMoveRequested)
            onPluginSlotMoveRequested(*chain_, fromIndex, toIndex);
        else
            chain_->moveSlot(fromIndex, toIndex);
    }

    std::function<void(PluginInstanceCore* slot, const TrackID& trackId, int slotIndex)> onPluginEditorOpened;

    std::function<void()> onCloseRequested;
    std::function<void()> onDetachToggleRequested;
    std::function<bool()> isSlimeAttached;

    std::function<void(int slotIndex, const juce::ModifierKeys&)> onSlotClickedWithModifiers;

    void setSlotSelected(int index, bool selected)
    {
        for (auto* w : slotWidgets_)
            if (auto* sw = dynamic_cast<SlotWidget*>(w))
                if (sw->getSlotIndex() == index)
                    sw->setSelected(selected);
    }

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
        viewport_.getVerticalScrollBar().setColour(juce::ScrollBar::ColourIds::trackColourId,
            juce::Colour(0xFF40E0A0).withAlpha(0.04f));
        viewport_.getVerticalScrollBar().setColour(juce::ScrollBar::ColourIds::thumbColourId,
            juce::Colour(0xFF40E0A0).withAlpha(0.18f));
        viewport_.setRepaintsOnMouseActivity(false);

        // Watermark mode: the Living Seal is attenuated to a subtle premium
        // watermark so plugin names, cards, bypass/active controls, Mix values
        // and buttons always stay the visual priority. Main structural shapes
        // land ~8-15%, secondary particles/lines ~4-10% (see ApexLivingSeal).
        livingSeal_.setProminence(0.12f);
        viewport_.onWheel = [this](const juce::MouseWheelDetails& wheel)
        {
            auto* viewed = viewport_.getViewedComponent();
            if (!viewed) return;
            const int contentH = viewed->getHeight();
            const int viewH = viewport_.getHeight();
            const int scrollable = contentH - viewH;
            if (scrollable > 0)
            {
                const int curPos = viewport_.getViewPositionY();
                const int newPos = juce::jlimit(0, scrollable,
                    curPos - juce::roundToInt(wheel.deltaY * 80.0f));
                viewport_.setViewPosition(0, newPos);
            }
        };
        addAndMakeVisible(viewport_);
    }

    ~MixerPluginSidePanel() override
    {
        stopTimer();
        if (trackManager_)
            trackManager_->removeListener(this);
        // JUCE unparents child Components during teardown but does not own or
        // delete raw child pointers. Match rebuildSlots() and release AddButton
        // while its parent is still alive.
        if (addBtn_)
        {
            removeChildComponent(addBtn_);
            delete addBtn_;
            addBtn_ = nullptr;
        }
    }

    void setTrackManager(TrackManager* tm)
    {
        if (trackManager_)
            trackManager_->removeListener(this);
        trackManager_ = tm;
        if (trackManager_)
            trackManager_->addListener(this);
    }

    void trackRemoved(const TrackID& removedId) override
    {
        if (!track_ || track_->getID() != removedId) return;

        // The Track and its PluginChain may be destroyed as soon as this
        // notification returns.  Null the non-owning pointers synchronously;
        // defer only child-component rebuilding so an initiating UI callback
        // cannot delete its own component stack.
        track_ = nullptr;
        chain_ = nullptr;
        lastObservedSlotCount_ = -1;
        cachedVisibleStates_.clear();
        updatePollingState();

        juce::Component::SafePointer<MixerPluginSidePanel> safeThis(this);
        juce::MessageManager::callAsync([safeThis]()
        {
            if (safeThis != nullptr)
                safeThis->rebuildSlots();
        });
    }

    void setTrack(Track* track, PluginChainCore* chain)
    {
        track_ = track;
        chain_ = chain;
        lastObservedSlotCount_ = -1;
        cachedVisibleStates_.clear();
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

    void visibilityChanged() override { updatePollingState(); }
    void parentHierarchyChanged() override { updatePollingState(); }

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

        auto parts = juce::StringArray::fromTokens(details.description.toString(), ":", "");
        if (parts.size() < 3) return;
        auto srcTrackId = parts[1];
        int  srcSlot    = parts[2].getIntValue();
        if (!track_) return;

        if (srcTrackId == track_->getID())
        {
            if (!chain_) return;
            int targetSlot = juce::jlimit(0, juce::jmax(0, chain_->getNumSlots() - 1),
                                          (int)(details.localPosition.y - kHeaderH) / kSlotHeight);
            if (targetSlot != srcSlot)
            {
                requestSlotMove(srcSlot, targetSlot);
                rebuildSlots();
            }
        }
        else
        {
            if (onPluginDragCopy)
                onPluginDragCopy(srcTrackId, srcSlot, track_->getID());
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto w = getWidth();

        // ── HEADER ──────────────────────────────────────────────────────
        auto headerArea = getLocalBounds().removeFromTop(kHeaderH);

        // Compact action group: [detach][close] — right side
        headerArea.removeFromRight(90); // push left so buttons sit inside the curved panel
        auto actionArea = headerArea.removeFromRight(34);

        // Close button — 16 px hit area, compact X icon
        closeBtnBounds_ = actionArea.removeFromRight(16).withSizeKeepingCentre(16, 16);
        {
            auto cb = closeBtnBounds_.toFloat();
            if (closeHovered_)
            {
                g.setColour(juce::Colour(0xFFEF4444).withAlpha(0.18f));
                g.fillRoundedRectangle(cb, 4.f);
            }
            float cx = cb.getCentreX(), cy = cb.getCentreY();
            g.setColour(juce::Colours::white.withAlpha(closeHovered_ ? 0.85f : 0.45f));
            float s = 3.5f;
            juce::Path xp;
            xp.startNewSubPath(cx - s, cy - s); xp.lineTo(cx + s, cy + s);
            xp.startNewSubPath(cx + s, cy - s); xp.lineTo(cx - s, cy + s);
            g.strokePath(xp, juce::PathStrokeType(1.3f));
        }

        // Detach button — matching 16 px
        detachBtnBounds_ = actionArea.removeFromRight(16).withSizeKeepingCentre(16, 16);
        {
            bool attached = isSlimeAttached ? isSlimeAttached() : true;
            auto db = detachBtnBounds_.toFloat();
            if (detachHovered_)
            {
                g.setColour(juce::Colour(0xFF40E0A0).withAlpha(0.16f));
                g.fillRoundedRectangle(db, 4.f);
            }
            float cx = db.getCentreX(), cy = db.getCentreY();
            g.setColour(juce::Colours::white.withAlpha(detachHovered_ ? 0.85f : 0.45f));
            float s = 3.5f;
            juce::Path ap;
            if (attached)
            {
                ap.startNewSubPath(cx - s, cy + s);
                ap.lineTo(cx + s, cy - s);
                ap.startNewSubPath(cx - s, cy - s);
                ap.lineTo(cx + s, cy - s);
                ap.lineTo(cx + s, cy + s);
            }
            else
            {
                ap.startNewSubPath(cx + s, cy - s);
                ap.lineTo(cx - s, cy + s);
                ap.startNewSubPath(cx + s, cy + s);
                ap.lineTo(cx - s, cy + s);
                ap.lineTo(cx - s, cy - s);
            }
            g.strokePath(ap, juce::PathStrokeType(1.3f));
        }

        // Title area
        auto titleArea = headerArea.reduced(6, 3);
        auto& a = t.apex;
        if (track_)
        {
            // "FX CHAIN" / "MASTER FX" — hand-signed script wordmark with a
            // soft glow (offset passes at low alpha, then the crisp wordmark).
            const juce::String scriptName = getScriptTypeface();
            juce::Font script;
            if (scriptName.isNotEmpty())
                script = juce::Font(juce::FontOptions(scriptName, 15.f, juce::Font::plain));
            else
                script = juce::Font(10.5f, juce::Font::bold);

            juce::String primary = track_->isMaster() ? "MASTER FX" : "FX CHAIN";
            auto primaryArea = titleArea.removeFromTop(16);

            g.setFont(script);
            g.setColour(a.color.magenta.withAlpha(0.28f));
            g.drawText(primary, primaryArea.translated(0, 1), juce::Justification::centredLeft, true);
            g.drawText(primary, primaryArea.translated(0, -1), juce::Justification::centredLeft, true);
            g.setColour(a.color.magenta);
            g.drawText(primary, primaryArea, juce::Justification::centredLeft, true);

            // Tiny violet micro-underline under the wordmark.
            g.setColour(a.color.violet.withAlpha(0.45f));
            g.fillRect(primaryArea.getX(), primaryArea.getBottom() - 1, 26, 1);

            g.setFont(juce::Font(9.f));
            g.setColour(a.color.textSecondary.withAlpha(0.85f));
            juce::String trackName = track_->getName();
            auto tnArea = titleArea;
            g.drawText(trackName, tnArea, juce::Justification::centredLeft, true);
        }
        else
        {
            // Same script wordmark treatment at reduced alpha when no track.
            const juce::String scriptName = getScriptTypeface();
            juce::Font script;
            if (scriptName.isNotEmpty())
                script = juce::Font(juce::FontOptions(scriptName, 15.f, juce::Font::plain));
            else
                script = juce::Font(10.5f, juce::Font::bold);

            auto primaryArea = titleArea.removeFromTop(16);
            g.setFont(script);
            g.setColour(a.color.magenta.withAlpha(0.28f));
            g.drawText("FX CHAIN", primaryArea.translated(0, 1), juce::Justification::centredLeft, true);
            g.drawText("FX CHAIN", primaryArea.translated(0, -1), juce::Justification::centredLeft, true);
            g.setColour(a.color.magenta.withAlpha(0.55f));
            g.drawText("FX CHAIN", primaryArea, juce::Justification::centredLeft, true);

            g.setColour(a.color.violet.withAlpha(0.30f));
            g.fillRect(primaryArea.getX(), primaryArea.getBottom() - 1, 26, 1);
        }

        // (Header sigil removed — the approved Living Seal owns this panel's
        //  processing/transformation identity. See the body decoration below.)

        // ── SEPARATOR ───────────────────────────────────────────────────
        int sepY = kHeaderH;
        g.setColour(a.color.violet.withAlpha(0.22f));
        g.fillRect(6, sepY, w - 12, 1);

        // ── GLOBAL CONTROLS ─────────────────────────────────────────────
        auto stripArea = getLocalBounds().removeFromTop(kHeaderH + kControlsH).withTrimmedTop(kHeaderH).reduced(4, 0);

        if (chain_)
        {
            // ACTIVE pill — compact
            bypassBtnBounds_ = stripArea.removeFromLeft(74).reduced(0, 4);
            const bool allBypassed = chain_->areAllActiveSlotsBypassed();
            {
                auto bb = bypassBtnBounds_.toFloat();
                g.setColour(allBypassed ? juce::Colour(0xFFEF4444).withAlpha(0.12f)
                                        : a.color.activeGreen.withAlpha(0.12f));
                g.fillRoundedRectangle(bb, 4.f);
                g.setColour(allBypassed ? juce::Colour(0xFFEF4444).withAlpha(0.30f)
                                        : a.color.activeGreen.withAlpha(0.35f));
                g.drawRoundedRectangle(bb, 4.f, 0.6f);
                g.setColour(allBypassed ? juce::Colour(0xFFEF4444) : a.color.activeGreen);
                g.fillEllipse(bb.getX() + 6.f, bb.getCentreY() - 3.f, 6.f, 6.f);
                g.setColour(allBypassed ? juce::Colour(0xFFEF4444).withAlpha(0.80f)
                                        : a.color.textPrimary);
                g.setFont(juce::Font(9.f, juce::Font::bold));
                g.drawText(allBypassed ? "BYPASS" : "ACTIVE",
                           bb.reduced(16, 0), juce::Justification::centredLeft);
            }

            // Single MIX knob — smaller to match slot knobs
            auto mixArea = stripArea.removeFromRight(46).reduced(2, 2);
            mixSliderBounds_ = mixArea;
            paintMixKnob(g, mixArea.toFloat().translated(-6, 0), chainMixValue_, a.color.violet);
        }
        else
        {
            bypassBtnBounds_ = {};
            mixSliderBounds_ = {};
        }

        // ── No-track placeholder ────────────────────────────────────────
        if (!chain_)
        {
            g.setColour(t.colors.textSecondary.withAlpha(0.25f));
            g.setFont(juce::Font(10.5f));
            g.drawText("Select a track", getLocalBounds().withTrimmedTop(getViewportTop()),
                       juce::Justification::centred);
        }

        // ── APEX Living Seal — the approved processing identity ─────────
        // Forged runic spine + integrated living eye, centred in the panel
        // body. Painted in paint() so every plugin row, knob, and label
        // renders ON TOP of it. Paint-only decoration — it is not a Component
        // and can never intercept plugin interaction.
        // Gaze follows the cursor (message-thread-relative, smoothed).
        livingSeal_.setGazeTarget(getMouseXYRelative().toFloat(), isMouseOver());

        // Layering rule: the artwork ALWAYS renders behind functional plugin
        // UI. Additionally, every OCCUPIED plugin slot masks the seal out of
        // its card area (excludeClipRegion) so plugin names, bypass dots, Mix
        // knobs, close/expand controls and slot borders stay perfectly clean.
        // Empty rows keep the watermark visible — empty areas retain the APEX
        // identity, occupied areas stay readable.
        if (chain_ != nullptr && !slotWidgets_.isEmpty())
        {
            juce::Graphics::ScopedSaveState sealSave(g);
            const auto vpPos = viewport_.getBounds().getPosition() - viewport_.getViewPosition();
            for (auto* widget : slotWidgets_)
            {
                auto* slotWidget = dynamic_cast<SlotWidget*>(widget);
                if (slotWidget == nullptr) continue;
                if (!chain_->hasPlugin(slotWidget->getSlotIndex())) continue;

                auto cardRect = widget->getBounds().translated(vpPos.x, vpPos.y)
                                    .getIntersection(livingSeal_.getBounds().toNearestInt());
                if (!cardRect.isEmpty())
                    g.excludeClipRegion(cardRect);
            }
            livingSeal_.paint(g);
        }
        else
        {
            livingSeal_.paint(g);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop(getViewportTop());
        b.removeFromBottom(kAddBtnH + 20); // reserve space for Add button in bottom curve
        b = b.withTrimmedRight(8); // keep scrollbar inside the panel's right curve
        if (b.getHeight() > getMaxViewportH())
            b = b.withSizeKeepingCentre(b.getWidth(), getMaxViewportH());
        viewport_.setBounds(b);
        layoutContent();
        positionAddButton();

        // Living Seal occupies the panel body between the controls strip and
        // the Add button — tall vertical composition, aspect-fitted internally.
        livingSeal_.setBounds(juce::Rectangle<float>(
            4.0f, (float) getViewportTop() + 2.0f,
            (float) getWidth() - 8.0f,
            juce::jmax(0.0f, (float) getHeight() - (float) getViewportTop()
                           - (float) kAddBtnH - 24.0f)));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto pt = e.position.toInt();

        auto headerArea = getLocalBounds().removeFromTop(kHeaderH);
        if (headerArea.contains(pt))
        {
            if (closeBtnBounds_.contains(pt))
            {
                if (onCloseRequested) onCloseRequested();
                return;
            }
            if (detachBtnBounds_.contains(pt))
            {
                if (onDetachToggleRequested) onDetachToggleRequested();
                return;
            }
            if (isSlimeAttached && !isSlimeAttached())
            {
                draggingHeader_ = true;
                if (auto* slime = findParentComponentOfClass<SlimeSidePanel>())
                    slime->getDragger().startDraggingComponent(slime, e);
            }
            return;
        }

        auto controlsArea = getLocalBounds().removeFromTop(getViewportTop()).withTrimmedTop(kHeaderH);
        if (controlsArea.contains(pt))
        {
            if (chain_ == nullptr) return;

            if (bypassBtnBounds_.contains(pt))
            {
                const bool allBypassed = chain_->areAllActiveSlotsBypassed();
                chain_->setAllActiveSlotsBypassed(!allBypassed);
                repaint(controlsArea);
                repaintVisibleSlotWidgets();
                return;
            }

            if (mixSliderBounds_.contains(pt))
            {
                draggingHeaderMix_ = true;
                dragStartPos_ = e.getScreenPosition();
                dragStartValue_ = chainMixValue_;
                return;
            }
            return;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingHeader_ && isSlimeAttached && !isSlimeAttached())
        {
            if (auto* slime = findParentComponentOfClass<SlimeSidePanel>())
                slime->getDragger().dragComponent(slime, e, nullptr);
            return;
        }
        if (!draggingHeaderMix_ || chain_ == nullptr) return;
        const auto delta = dragStartPos_.y - e.getScreenPosition().y;
        const float newValue = juce::jlimit(0.0f, 1.0f, dragStartValue_ + (float)delta / 180.0f);
        applyChainMixValue(newValue);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingHeader_ = false;
        draggingHeaderMix_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (chain_ && mixSliderBounds_.contains(e.position.toInt()))
        {
            float defaultValue = 1.0f;
            applyChainMixValue(defaultValue);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        bool newCloseHover = closeBtnBounds_.contains(e.position.toInt());
        bool newDetachHover = detachBtnBounds_.contains(e.position.toInt());
        if (newCloseHover != closeHovered_ || newDetachHover != detachHovered_)
        {
            closeHovered_ = newCloseHover;
            detachHovered_ = newDetachHover;
            repaint(getLocalBounds().removeFromTop(kHeaderH));
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (closeHovered_ || detachHovered_)
        {
            closeHovered_ = false;
            detachHovered_ = false;
            repaint(getLocalBounds().removeFromTop(kHeaderH));
        }
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (viewport_.onWheel)
            viewport_.onWheel(w);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto& a = t.apex;

        if (reorderDropIndex_ >= 0 && !isCrossTrackDrag_)
        {
            int indicatorY = getViewportTop() + reorderDropIndex_ * kSlotHeight - viewport_.getViewPositionY();
            g.setColour(a.color.magenta);
            g.fillRect(8, indicatorY - 1, getWidth() - 16, 2);
        }

        if (isCrossTrackDrag_)
        {
            g.setColour(a.color.magenta.withAlpha(0.12f));
            g.fillRect(getLocalBounds().withTrimmedTop(getViewportTop()));
            g.setColour(a.color.magenta.withAlpha(0.50f));
            g.drawRect(getLocalBounds().withTrimmedTop(getViewportTop()).toFloat(), 1.5f);
        }

        // (Sigil is painted in paint(), beneath child components.)
    }

private:
    PluginScannerCore& scanner_;
    TrackManager*     trackManager_ = nullptr;
    Track*            track_ = nullptr;
    PluginChainCore*  chain_ = nullptr;
    ScrollForwardingViewport viewport_;
    ContentComponent         content_;

    // Approved Living Seal (paint-only decoration; see ThemeCore/ApexLivingSeal.h)
    ApexLivingSeal livingSeal_;
    bool           sealWasVisible_ = false;
    juce::OwnedArray<juce::Component> slotWidgets_;
    juce::Component* addBtn_ = nullptr;

    struct SlotVisualState
    {
        bool hasPlugin = false;
        bool bypassed = false;
        bool sandboxed = false;
        bool selected = false;
        bool hovered = false;
        juce::String name;

        bool operator==(const SlotVisualState& other) const noexcept
        {
            return hasPlugin == other.hasPlugin
                && bypassed == other.bypassed
                && sandboxed == other.sandboxed
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
    bool draggingHeader_ = false;
    bool closeHovered_ = false;
    bool detachHovered_ = false;

    juce::Rectangle<int> closeBtnBounds_;
    juce::Rectangle<int> detachBtnBounds_;
    juce::Rectangle<int> bypassBtnBounds_;
    juce::Rectangle<int> mixSliderBounds_;
    juce::Point<int> dragStartPos_;
    float dragStartValue_ = 1.0f;
    float chainMixValue_ = 1.0f;
    std::unordered_map<int, SlotVisualState> cachedVisibleStates_;

    // ── Single MIX knob painter ─────────────────────────────────────────
    static void paintMixKnob(juce::Graphics& g, juce::Rectangle<float> bounds,
                              float normalized, juce::Colour accent)
    {
        const float value = juce::jlimit(0.0f, 1.0f, normalized);
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();

        const float bodyD = 16.f;
        const float bx = cx - bodyD * 0.5f;
        const float by = cy - bodyD * 0.5f;
        auto bodyRect = juce::Rectangle<float>(bx, by, bodyD, bodyD);

        const float arcR = bodyD * 0.5f + 0.6f;
        const float arcCx = bodyRect.getCentreX();
        const float arcCy = bodyRect.getCentreY();
        const float startA = juce::MathConstants<float>::pi * 1.25f;
        const float endA   = juce::MathConstants<float>::pi * 2.75f;
        const float valA   = startA + (endA - startA) * value;

        // Label — left side of knob
        g.setFont(juce::Font(7.5f, juce::Font::bold));
        g.setColour(accent.withAlpha(0.65f));
        g.drawText("MIX",
                   juce::Rectangle<float>(bounds.getX() - 16.f, bounds.getCentreY() - 5.f, 22.f, 10.f),
                   juce::Justification::centredRight, false);

        // Drop shadow
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillEllipse(bodyRect.translated(0, 1.f));

        // Body fill
        g.setColour(juce::Colour(0xFF1A1C24));
        g.fillEllipse(bodyRect);

        // Track arc
        juce::Path trackArc;
        trackArc.addCentredArc(arcCx, arcCy, arcR, arcR, 0.f, startA, endA, true);
        g.setColour(accent.withAlpha(0.12f));
        g.strokePath(trackArc, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        // Value arc
        if (value > 0.001f)
        {
            juce::Path valArc;
            valArc.addCentredArc(arcCx, arcCy, arcR, arcR, 0.f, startA, valA, true);
            g.setColour(accent.withAlpha(0.85f));
            g.strokePath(valArc, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Pointer
        const float pOut = bodyD * 0.5f - 0.5f;
        const float pIn  = bodyD * 0.25f;
        auto ptOn = [](float cx_, float cy_, float r, float a) {
            return juce::Point<float>(cx_ + std::sin(a) * r, cy_ - std::cos(a) * r);
        };
        auto pS = ptOn(arcCx, arcCy, pIn, valA);
        auto pE = ptOn(arcCx, arcCy, pOut, valA);
        g.setColour(juce::Colours::white.withAlpha(0.80f));
        g.drawLine(pS.x, pS.y, pE.x, pE.y, 1.2f);

        // Centre dot
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.fillEllipse(arcCx - 0.8f, arcCy - 0.8f, 1.6f, 1.6f);

        // Percentage text below
        g.setFont(juce::Font(7.5f));
        g.setColour(juce::Colours::white.withAlpha(0.60f));
        g.drawText(juce::String(juce::roundToInt(value * 100.f)) + "%",
                   bounds.withY(bounds.getBottom() - 10.f).withHeight(10.f),
                   juce::Justification::centred, false);
    }

    // ── Per-plugin mini MIX knob painter ────────────────────────────────
    static void paintSlotMixKnob(juce::Graphics& g, juce::Rectangle<float> bounds,
                                  float normalized, juce::Colour accent)
    {
        const float value = juce::jlimit(0.0f, 1.0f, normalized);
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();

        const float bodyD = 18.f;
        const float bx = cx - bodyD * 0.5f;
        const float by = cy - bodyD * 0.5f;
        auto bodyRect = juce::Rectangle<float>(bx, by, bodyD, bodyD);

        const float arcR = bodyD * 0.5f + 0.6f;
        const float arcCx = bodyRect.getCentreX();
        const float arcCy = bodyRect.getCentreY();
        const float startA = juce::MathConstants<float>::pi * 1.25f;
        const float endA   = juce::MathConstants<float>::pi * 2.75f;
        const float valA   = startA + (endA - startA) * value;

        g.setColour(juce::Colours::black.withAlpha(0.20f));
        g.fillEllipse(bodyRect.translated(0, 1.f));

        g.setColour(juce::Colour(0xFF1A1C24));
        g.fillEllipse(bodyRect);

        juce::Path trackArc;
        trackArc.addCentredArc(arcCx, arcCy, arcR, arcR, 0.f, startA, endA, true);
        g.setColour(accent.withAlpha(0.10f));
        g.strokePath(trackArc, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        if (value > 0.001f)
        {
            juce::Path valArc;
            valArc.addCentredArc(arcCx, arcCy, arcR, arcR, 0.f, startA, valA, true);
            g.setColour(accent.withAlpha(0.80f));
            g.strokePath(valArc, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        const float pOut = bodyD * 0.5f - 0.5f;
        const float pIn  = bodyD * 0.25f;
        auto ptOn = [](float cx_, float cy_, float r, float a) {
            return juce::Point<float>(cx_ + std::sin(a) * r, cy_ - std::cos(a) * r);
        };
        auto pS = ptOn(arcCx, arcCy, pIn, valA);
        auto pE = ptOn(arcCx, arcCy, pOut, valA);
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawLine(pS.x, pS.y, pE.x, pE.y, 1.0f);

        // Tiny percentage
        g.setFont(juce::Font(6.5f));
        g.setColour(juce::Colours::white.withAlpha(0.50f));
        g.drawText(juce::String(juce::roundToInt(value * 100.f)),
                   bounds.withY(bounds.getBottom() - 8.f).withHeight(8.f),
                   juce::Justification::centred, false);
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
        if (chain_ == nullptr) return;
        chainMixValue_ = juce::jlimit(0.0f, 1.0f, newValue);
        chain_->setChainOutputMix(chainMixValue_);
        repaint(getLocalBounds().removeFromTop(getViewportTop()));
    }

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
            isCrossTrackDrag_ = false;
            int slot = juce::jlimit(0, chain_->getNumSlots(),
                                    (int)(details.localPosition.y - kHeaderH + viewport_.getViewPositionY()) / kSlotHeight);
            reorderDropIndex_ = slot;
        }
        else
        {
            isCrossTrackDrag_ = true;
            reorderDropIndex_ = -1;
        }
    }

    void toggleSlotEditor(int slotIndex)
    {
        if (!chain_) return;
        auto* slot = chain_->getSlot(slotIndex);
        if (!slot) return;

        if (slot->isEditorOpen())
        {
            slot->closeEditor();
            return;
        }
        if (slot->isEditorMinimized())
        {
            slot->showEditor();
            return;
        }
        openSlotEditor(slotIndex);
    }

    void openSlotEditor(int slotIndex)
    {
        if (!chain_) return;
        auto* slot = chain_->getSlot(slotIndex);
        if (!slot) return;

        if (slot->isEditorOpen())
        {
            slot->showEditor();
            return;
        }
        if (slot->isEditorMinimized())
        {
            slot->showEditor();
            return;
        }

        slot->openEditor();
        if (slot->isEditorCreated() && onPluginEditorOpened && track_)
            onPluginEditorOpened(slot, track_->getID(), slotIndex);
    }

    void rebuildSlots()
    {
        slotWidgets_.clear();
        content_.removeAllChildren();
        if (addBtn_) { removeChildComponent(addBtn_); delete addBtn_; addBtn_ = nullptr; }
        cachedVisibleStates_.clear();
        lastObservedSlotCount_ = chain_ ? chain_->getNumSlots() : -1;
        syncHeaderMixFromChain();

        if (!chain_) { layoutContent(); positionAddButton(); repaint(); return; }

        int count = chain_->getNumSlots();
        for (int i = 0; i < count; ++i)
        {
            auto* w = new SlotWidget(*this, i);
            slotWidgets_.add(w);
            content_.addAndMakeVisible(w);
        }
        addBtn_ = new AddButton(*this);
        addAndMakeVisible(addBtn_);

        layoutContent();
        positionAddButton();
        repaint();
    }

    void layoutContent()
    {
        int y = 4;
        for (auto* w : slotWidgets_)
        {
            w->setBounds(0, y, viewport_.getWidth() - 4, kSlotHeight);
            y += kSlotHeight;
        }
        content_.setSize(viewport_.getWidth(), juce::jmax(y, viewport_.getHeight()));
    }

    void positionAddButton()
    {
        if (!addBtn_) return;
        auto b = getLocalBounds();
        b.removeFromTop(getViewportTop());
        auto bottomArea = b.removeFromBottom(kAddBtnH + 24);
        addBtn_->setBounds(bottomArea.reduced(30, 4));
    }

    void timerCallback() override
    {
        if (chain_ == nullptr)
        {
            stopTimer();
            return;
        }

        // Drive the Living Seal timeline from the existing 60 Hz poll.
        // Repaint is clipped to the seal's body area, not the whole panel.
        if (livingSeal_.needsRepaint())
        {
            livingSeal_.update(1.0f / 60.0f);
            repaint(livingSeal_.getContentBounds().toNearestInt()
                        .getIntersection(getLocalBounds()));
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
            if (slotWidget == nullptr) continue;

            const int index = slotWidget->getSlotIndex();
            if (index < visibleStart || index > visibleEnd) continue;

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
        const bool sealVisible = (chain_ != nullptr && isShowing());

        // Living Seal opening animation fires on EVERY hidden→visible
        // transition of the FX panel — never on track switches, repaints,
        // or plugin edits while the panel stays open.
        if (sealVisible && ! sealWasVisible_)
            livingSeal_.startOpening();
        sealWasVisible_ = sealVisible;

        if (sealVisible)
            startTimerHz(60);
        else
            stopTimer();
    }

    // ── Slot Widget ─────────────────────────────────────────────────────

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
            if (chain == nullptr) return state;

            if (auto* slot = chain->getSlot(index_))
            {
                state.hasPlugin = true;
                state.bypassed = slot->isBypassed();
                state.sandboxed = slot->isSandboxed();
                state.name = slot->getName();
            }
            return state;
        }

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto b = getLocalBounds();
            auto* chain = owner_.chain_;
            if (!chain) return;

            auto* slot = chain->getSlot(index_);
            bool hasPlugin = (slot != nullptr);
            bool bypassed = hasPlugin && slot->isBypassed();
            bool sandboxed = hasPlugin && slot->isSandboxed();

            auto& a = Theme::getInstance().apex;

            // Subtle separator
            g.setColour(a.color.borderSoftA.withAlpha(0.35f));
            g.fillRect((float)b.getX() + 4, (float)b.getY(), (float)b.getWidth() - 8, 1.f);

            // Hover glow — thin violet edge
            if (hovered_)
            {
                g.setColour(a.color.violet.withAlpha(0.06f));
                g.fillRect((float)b.getX() + 4, (float)b.getY(), (float)b.getWidth() - 8, (float)b.getHeight());
            }

            // Selection accent — thin pink left edge
            if (selected_)
            {
                g.setColour(a.color.pink.withAlpha(0.75f));
                g.fillRect(b.getX() + 4.f, b.getY() + 3.f, 2.f, b.getHeight() - 6.f);
            }

            if (!hasPlugin) return;

            const float cy = b.getCentreY();

            // Slot number — visible
            g.setFont(juce::Font(9.5f, juce::Font::bold));
            g.setColour(a.color.textMuted);
            g.drawText(juce::String(index_ + 1).paddedLeft('0', 2),
                       b.removeFromLeft(18).toFloat(), juce::Justification::centred);

            // Bypass dot — small
            g.setColour(bypassed ? juce::Colour(0xFFFF9500)
                                 : a.color.activeGreen);
            g.fillEllipse((float)(b.getX() + 5), cy - 3.f, 6.f, 6.f);
            b.removeFromLeft(14);

            // Right side: scrolltrack | knob | gap | X | pad
            const int scrollTrackW = 4;
            const int knobAreaW = 32;
            const int gapW = 6;
            const int xBtnW = 14;
            const int rightPad = 34;
            const int rightTotal = rightPad + xBtnW + gapW + knobAreaW + scrollTrackW;
            auto rightArea = b.removeFromRight(rightTotal);
            rightArea.removeFromRight(rightPad);
            auto xArea = rightArea.removeFromRight(xBtnW);
            rightArea.removeFromRight(gapW);
            auto knobRect = rightArea.removeFromRight(knobAreaW);
            rightArea.removeFromRight(scrollTrackW); // scrollbar track area

            // Plugin name — flexible space
            auto nameArea = b.reduced(2, 0);
            if (sandboxed)
            {
                auto badgeArea = nameArea.removeFromRight(34).reduced(1, 8);
                g.setColour(a.color.violet.withAlpha(0.22f));
                g.fillRoundedRectangle(badgeArea.toFloat(), 3.f);
                g.setColour(a.color.violet);
                g.setFont(juce::Font(7.5f, juce::Font::bold));
                g.drawText("SBX", badgeArea, juce::Justification::centred);
            }
            g.setFont(juce::Font(10.5f));
            g.setColour(bypassed ? a.color.textMuted : a.color.textPrimary);
            g.drawText(slot->getName(), nameArea.toFloat(),
                       juce::Justification::centredLeft, true);

            // Mini Mix knob
            const float wet = chain->getSlotMix(index_);
            paintSlotMixKnob(g, knobRect.toFloat(), wet, a.color.violet);

            // Delete X — on hover
            if (hovered_)
            {
                auto xHit = xArea.reduced(1, 8);
                g.setColour(juce::Colour(0xFFEF4444).withAlpha(0.40f));
                g.fillRoundedRectangle(xHit.toFloat(), 3.f);
                float cx = xHit.getCentreX(), cy2 = xHit.getCentreY(), s = 3.5f;
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                juce::Path xp;
                xp.startNewSubPath(cx - s, cy2 - s); xp.lineTo(cx + s, cy2 + s);
                xp.startNewSubPath(cx + s, cy2 - s); xp.lineTo(cx - s, cy2 + s);
                g.strokePath(xp, juce::PathStrokeType(1.5f));
            }
        }

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true;  repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovered_ = false; repaint(); }

        void mouseDown(const juce::MouseEvent& e) override
        {
            auto* chain = owner_.chain_;
            if (!chain || !chain->hasPlugin(index_)) return;

            dragMode_ = DragMode::None;

            // Right side layout constants (must match paint)
            const int scrollTrackW = 4;
            const int knobAreaW = 32;
            const int gapW = 6;
            const int xBtnW = 14;
            const int rightPad = 34;
            const int rightTotal = rightPad + xBtnW + gapW + knobAreaW + scrollTrackW;

            // Delete X hit — rightmost area
            auto rightArea = getLocalBounds().removeFromRight(rightTotal);
            rightArea.removeFromRight(rightPad);
            auto xArea = rightArea.removeFromRight(xBtnW).reduced(1, 8);
            if (xArea.contains(e.position.toInt()))
            {
                if (owner_.onPluginChainEditRequested)
                    owner_.onPluginChainEditRequested(*chain, [chain, this]() { chain->removePlugin(index_); }, "Remove Plugin");
                else
                    chain->removePlugin(index_);
                owner_.rebuildSlots();
                return;
            }

            // Bypass dot hit
            auto bypassHit = getLocalBounds().toFloat();
            bypassHit.removeFromLeft(18.f);
            bypassHit = bypassHit.removeFromLeft(14.f);
            if (bypassHit.contains(e.position))
            {
                chain->setSlotBypassed(index_, !chain->getSlot(index_)->isBypassed());
                repaint();
                return;
            }

            // Mini Mix knob hit — must match paint layout (left removals first)
            auto kb = getLocalBounds();
            kb.removeFromLeft(18); // slot number
            kb.removeFromLeft(14); // bypass
            auto knobRightArea = kb.removeFromRight(rightTotal);
            knobRightArea.removeFromRight(rightPad);
            knobRightArea.removeFromRight(xBtnW);
            knobRightArea.removeFromRight(gapW);
            auto mixHit = knobRightArea.removeFromRight(knobAreaW);
            if (mixHit.contains(e.position.toInt()))
            {
                dragMode_ = DragMode::Wet;
                dragStartPos_ = e.getScreenPosition();
                dragStartMix_ = chain->getSlotMix(index_);
                return;
            }

            if (e.mods.isPopupMenu())
            {
                showContextMenu();
                return;
            }

            if (owner_.onSlotClickedWithModifiers)
                owner_.onSlotClickedWithModifiers(index_, e.mods);

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
                owner_.toggleSlotEditor(index_);
            clickedForToggle_ = false;
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            auto* chain = owner_.chain_;
            if (dragMode_ != DragMode::None)
            {
                if (!chain || !chain->hasPlugin(index_)) return;
                const auto delta = dragStartPos_.y - e.getScreenPosition().y;
                float wet = juce::jlimit(0.0f, 1.0f, dragStartMix_ + (float)delta / 180.0f);
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

            juce::String dragDesc = juce::String(kPluginDragDesc) + ":" +
                                   owner_.track_->getID() + ":" +
                                   juce::String(index_);

            int ghostW = juce::jmax(140, getWidth());
            int ghostH = kSlotHeight;
            juce::Image ghost(juce::Image::ARGB, ghostW, ghostH, true);
            {
                juce::Graphics gg(ghost);
                auto& t = Theme::getInstance();
                gg.setColour(t.colors.accent.withAlpha(0.85f));
                gg.fillRoundedRectangle(0.f, 0.f, (float)ghostW, (float)ghostH, 6.f);
                gg.setColour(juce::Colours::white);
                gg.setFont(juce::Font(10.f, juce::Font::bold));
                gg.drawText(pluginName, 8, 0, ghostW - 16, ghostH, juce::Justification::centredLeft, true);
            }
            owner_.startDragging(dragDesc, this, juce::ScaledImage(ghost), true);
        }

        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            // Forward to viewport scroll
            if (owner_.viewport_.onWheel)
                owner_.viewport_.onWheel(w);
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
            const bool keepModeAllowed =
                PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled
                || slot->getExecutionMode() != PluginExecutionMode::Sandboxed;
            menu.addItem(20, "Replace (Keep Mode)", keepModeAllowed);
            menu.addItem(21, "Replace In Process");
            if (PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
                menu.addItem(22, "Replace Sandboxed");
            menu.addSeparator();
            menu.addItem(10, "Remove Plugin");

            juce::Component::SafePointer<SlotWidget> safeThis(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                [safeThis](int r)
                {
                     if (!safeThis) return;
                     auto* chain = safeThis->owner_.chain_;
                     if (!chain) return;
                     auto* slot = chain->getSlot(safeThis->index_);

                     if (r == 1) safeThis->owner_.openSlotEditor(safeThis->index_);
                     else if (r == 20)
                         safeThis->owner_.showPluginBrowserForSlot(
                             safeThis->index_, PluginExecutionMode::InProcess, true);
                     else if (r == 21)
                         safeThis->owner_.showPluginBrowserForSlot(
                             safeThis->index_, PluginExecutionMode::InProcess, false);
                      else if (r == 22
                               && PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
                      {
                          safeThis->owner_.showPluginBrowserForSlot(
                              safeThis->index_, PluginExecutionMode::Sandboxed, false);
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
                    else if (r == 3) { safeThis->owner_.requestSlotMove(safeThis->index_, safeThis->index_ - 1); safeThis->owner_.rebuildSlots(); }
                    else if (r == 4) { safeThis->owner_.requestSlotMove(safeThis->index_, safeThis->index_ + 1); safeThis->owner_.rebuildSlots(); }
                    else if (r == 10) { if (safeThis->owner_.onPluginChainEditRequested) safeThis->owner_.onPluginChainEditRequested(*chain, [chain, safeThis]() { chain->removePlugin(safeThis->index_); }, "Remove Plugin"); else chain->removePlugin(safeThis->index_); safeThis->owner_.rebuildSlots(); }
                });
        }
    };

    void showPluginBrowserForSlot(int capturedSlot,
                                  PluginExecutionMode requestedMode,
                                  bool preserveCurrentMode)
    {
        if (requestedMode == PluginExecutionMode::Sandboxed
            && ! PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Plugin Hosting Unavailable",
                PluginHostingProductPolicyCore::kSandboxDisabledMessage);
            return;
        }

        if (!chain_)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon, "No Track Selected",
                "Select a track first to add or replace effects.");
            return;
        }

        const auto finalVisibleCount = scanner_.ensureBrowserVisibleDataReady(
            "MixerPluginSidePanel::showPluginBrowserForSlot");
        auto& known = scanner_.getKnownPlugins();
        const auto& list = known.getTypes();
        if (list.isEmpty())
        {
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "MixerPluginSidePanel::showPluginBrowserForSlot noPluginsPopup"
                    " scannerId=0x"
                    + juce::String::toHexString(
                        (juce::int64) reinterpret_cast<uintptr_t>(&scanner_))
                    + " cacheCount=" + juce::String(scanner_.getCache().getCount())
                    + " knownCount=" + juce::String(scanner_.getKnownPlugins().getNumTypes())
                    + " browserFeedCount=" + juce::String(finalVisibleCount)
                    + " scanRunning=" + juce::String(scanner_.isScanning() ? 1 : 0));
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon, "No Plugins",
                "No plugins found. Go to Settings to scan for plugins.");
            return;
        }

        struct PluginOption
        {
            juce::PluginDescription description;
        };
        std::vector<PluginOption> options;
        juce::PopupMenu menu;
        juce::StringArray manufacturers;
        for (auto& description : list)
            manufacturers.addIfNotAlreadyThere(description.manufacturerName);
        manufacturers.sort(true);

        int itemId = 1;
        for (auto& manufacturer : manufacturers)
        {
            juce::PopupMenu subMenu;
            for (auto& description : list)
            {
                if (description.manufacturerName != manufacturer)
                    continue;
                subMenu.addItem(itemId++,
                                description.name + "  [" + description.pluginFormatName + "]");
                options.push_back({ description });
            }
            menu.addSubMenu(manufacturer, subMenu);
        }

        juce::Component::SafePointer<MixerPluginSidePanel> safePanel(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this),
            [safePanel, options, capturedSlot, requestedMode,
             preserveCurrentMode](int result)
            {
                if (!safePanel || result < 1 || result > static_cast<int>(options.size()))
                    return;

                auto* chain = safePanel->chain_;
                if (!chain)
                    return;

                PluginInsertOptions insertOptions;
                insertOptions.executionMode = requestedMode;
                if (preserveCurrentMode)
                    if (auto* current = chain->getSlot(capturedSlot))
                        insertOptions.executionMode = current->getExecutionMode();

                juce::String error;
                if (insertOptions.executionMode == PluginExecutionMode::Sandboxed
                    && ! PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Plugin Hosting Unavailable",
                        PluginHostingProductPolicyCore::kSandboxDisabledMessage);
                    return;
                }
                const auto& option = options[static_cast<std::size_t>(result - 1)];
                int slotIndex = -1;
                if (capturedSlot >= 0)
                {
                    if (chain->loadPlugin(capturedSlot, option.description, insertOptions,
                                          safePanel->scanner_.getFormatManager(), error))
                        slotIndex = capturedSlot;
                }
                else
                {
                    slotIndex = chain->appendPlugin(
                        option.description, insertOptions,
                        safePanel->scanner_.getFormatManager(), error);
                }

                safePanel->rebuildSlots();
                if (slotIndex >= 0)
                    if (auto* loaded = chain->getSlot(slotIndex); loaded != nullptr
                        && !loaded->isSandboxed())
                        safePanel->openSlotEditor(slotIndex);

                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Plugin Load Error", error);
            });
    }

    // ── Add Button ──────────────────────────────────────────────────────

    class AddButton : public juce::Component
    {
    public:
        AddButton(MixerPluginSidePanel& owner) : owner_(owner) {}

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto& a = t.apex;
            auto b = getLocalBounds().toFloat().reduced(16.f, 2.f);

            // Dark pill with magenta glyph — primary creation affordance
            g.setColour(a.color.panelC.withAlpha(hovered_ ? 1.0f : 0.75f));
            g.fillRoundedRectangle(b, a.metric.radiusControl);
            g.setColour(a.color.magenta.withAlpha(hovered_ ? 0.75f : 0.40f));
            g.drawRoundedRectangle(b, a.metric.radiusControl, a.metric.strokeThin);

            g.setFont(juce::Font(10.5f, juce::Font::bold));
            g.setColour(a.color.magenta.withAlpha(hovered_ ? 1.0f : 0.65f));
            g.drawText("+ ADD EFFECT", b, juce::Justification::centred);
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
                    "Select a track first to add effects.");
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
            struct AddOption
            {
                juce::PluginDescription description;
                PluginExecutionMode executionMode = PluginExecutionMode::InProcess;
            };
            std::vector<AddOption> orderedOptions;

            for (auto& mfr : manufacturers)
            {
                juce::PopupMenu sub;
                for (auto& d : list)
                {
                    if (d.manufacturerName == mfr)
                    {
                        juce::PopupMenu actions;
                        actions.addItem(itemId++, "Load");
                        orderedOptions.push_back({ d, PluginExecutionMode::InProcess });
                        if (PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
                        {
                            actions.addItem(itemId++, "Load Sandboxed");
                            orderedOptions.push_back({ d, PluginExecutionMode::Sandboxed });
                        }
                        sub.addSubMenu(d.name + "  [" + d.pluginFormatName + "]", actions);
                    }
                }
                menu.addSubMenu(mfr, sub);
            }

            juce::Component::SafePointer<MixerPluginSidePanel> safePanel(&owner_);
            menu.showMenuAsync(
                juce::PopupMenu::Options().withTargetComponent(this),
                [safePanel, orderedOptions](int result)
                {
                    if (!safePanel) return;
                    if (result < 1 || result > (int)orderedOptions.size()) return;
                    const auto& option = orderedOptions[(size_t)result - 1];
                    juce::String err;
                    if (safePanel->chain_)
                    {
                        PluginInsertOptions insertOptions;
                        insertOptions.executionMode = option.executionMode;
                        if (insertOptions.executionMode == PluginExecutionMode::Sandboxed
                            && ! PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::MessageBoxIconType::WarningIcon,
                                "Plugin Hosting Unavailable",
                                PluginHostingProductPolicyCore::kSandboxDisabledMessage);
                            return;
                        }
                        int slotIndex = safePanel->chain_->appendPlugin(
                            option.description, insertOptions,
                            safePanel->scanner_.getFormatManager(), err);
                        safePanel->rebuildSlots();

                        if (slotIndex >= 0 && safePanel)
                            safePanel->openSlotEditor(slotIndex);
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::InfoIcon, "No Track Selected",
                            "Select a track first to add effects.");
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
