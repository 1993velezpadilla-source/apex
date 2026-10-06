#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <unordered_set>
#include "../ThemeCore/Theme.h"
#include "../TrackCore/Track.h"
#include "../ControlsCore/ModernControls.h"
#include "TrackColorPalette.h"
#include "TrackSelectionVisualCore.h"
#include "InputTrimFloatingPanel.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../AutomationCore/LastTouchedPluginParameterCore.h"
#include "../Automation/AutomationParameterKeyCore.h"
#include "../Automation/AutomationArrangementBridgeCore.h"
#include "../RoutingCore/RoutingGraph.h"

namespace DAW {

// Single track row
class TrackRow : public juce::Component,
                 public Track::Listener,
                 public juce::DragAndDropTarget,
                 private juce::Timer
{
public:
    TrackRow(Track& track)
        : track_(track)
    {
        track_.addListener(this);
        setOpaque(true);
        setBufferedToImage(true);
        setSize(200, 72);
        startTimerHz(24);
        if (track_.isMaster())
            DBG("[TrackRow] Master UI solo hidden = true");

        isFolderBus_ = (track_.getRole() == TrackRole::FolderBus);
        selectionVisual_.setAccentColour(track_.isMaster() ? juce::Colour(0xFFCC9900)
                                                            : track_.getColor());
    }

    void setIndentLevel(int level)
    {
        level = juce::jmax(0, level);
        if (indentLevel_ == level) return;
        indentLevel_ = level;
        resized();
        repaint();
    }
    int  getIndentLevel() const { return indentLevel_; }

    void setFolderExpanded(bool expanded)
    {
        if (folderExpanded_ == expanded) return;
        folderExpanded_ = expanded;
        repaint();
    }
    bool isFolderExpanded() const { return folderExpanded_; }
    bool isFolderBusRow()   const { return isFolderBus_; }

    /** Callback fired when the folder expand/collapse chevron is clicked. */
    std::function<void(const TrackID&, bool newExpanded)> onFolderToggled;
    std::function<void(const TrackID&)> onToggleMute;
    std::function<void(const TrackID&)> onToggleSolo;
    std::function<void(const TrackID&)> onToggleArm;
    std::function<void(const TrackID&, bool)> onSetMonitoring;
    std::function<void(const TrackID&, const juce::String&)> onRenameTrack;
    std::function<void(const TrackID&)> onOpenPianoRoll;
    std::function<void(const TrackID&)> onOpenInputTrim;
    /** Returns the active hardware input channel names for the input selector menu. */
    std::function<juce::StringArray()> onGetHardwareInputNames;
    std::function<void(const TrackID&, bool)> onAutomationMuteChanged;
    std::function<void(const TrackID&, const juce::String&, bool)> onAutomationTargetMuteChanged;
    std::function<void(const TrackID&, const juce::String&)> onAutomationTargetSoloRequested;
    std::function<void(const TrackID&)> onAutomationClearRequested;
    std::function<void(const TrackID&, const juce::String&)> onAutomationTargetClearRequested;
    std::function<void(const TrackID&, const juce::String&)> onCreateSequenceRequested;
    std::function<bool(const TrackID&)> hasAutomationData;
    /** Fired when the user toggles automation lane visibility or changes the active parameter. */
    std::function<void(const TrackID&)> onAutomationLaneStateChanged;
    std::function<juce::Array<Clip*>(const TrackID&)> onGetAudioClipsOnTrack;
    std::function<RoutingGraph*()> onGetRoutingGraph;

    ~TrackRow() override { track_.removeListener(this); }

    // Instantly repaint when track color/name/state changes
    void trackPropertyChanged(Track*) override
    {
        isFolderBus_ = (track_.getRole() == TrackRole::FolderBus);
        selectionVisual_.setAccentColour(track_.isMaster() ? juce::Colour(0xFFCC9900)
                                                            : track_.getColor());
        resized();
        repaint();
    }

    int getDesiredHeight() const { return height_; }
    void setDesiredHeight(int h) { height_ = h; }

    void setSelected(bool s)
    {
        if (selected_ == s)
            return;

        selected_ = s;
        selectionVisual_.setSelected(s);
        repaint();
    }
    bool isSelected() const { return selected_; }
    TrackID getTrackID() const { return track_.getID(); }

    juce::Rectangle<float> getFolderDropTargetBounds() const
    {
        auto bounds = getLocalBounds().toFloat();
        if (indentLevel_ > 0 && !track_.isMaster())
            bounds.removeFromLeft((float) (indentLevel_ * kIndentStepPx));

        bounds.removeFromLeft(6.0f);
        auto nameBounds = bounds.removeFromLeft(96.0f);
        return nameBounds.reduced(2.0f, juce::jmax(6.0f, nameBounds.getHeight() * 0.18f));
    }

    /** Set Bubblegum send feedback role for this track row. */
    void setBubblegumFeedback(bool isSource, bool isSendTarget, float sendLevel = 0.f)
    {
        auto newFb = isSource ? BgFeedback::Source
                   : isSendTarget ? BgFeedback::SendTarget
                   : BgFeedback::None;
        if (newFb != bgFeedback_ || (isSendTarget && sendLevel != bgFeedbackLevel_))
        {
            bgFeedback_ = newFb;
            bgFeedbackLevel_ = sendLevel;
            repaint();
        }
    }

    std::function<void(TrackRow*)> onSelected;
    std::function<void(TrackRow*, const juce::ModifierKeys&)> onSelectedWithModifiers;
    std::function<void(TrackRow*, juce::Colour)> onColorChanged;
    std::function<void(TrackRow*, int deltaY)> onHeightDrag;
    std::function<void(TrackRow*, const juce::MouseEvent&)> onReorderDrag;
    std::function<void(TrackRow*)> onReorderDrop;

    // Multi-selection context callbacks (set by TrackList)
    std::function<bool(const TrackID&)> onGetIsMultiSelected;
    std::function<void(const TrackID&)> onMultiRenameRequested;
    std::function<void(const TrackID&)> onMultiDeleteRequested;
    std::function<void(const TrackID&)> onMultiMuteRequested;
    std::function<void(const TrackID&)> onMultiSoloRequested;
    std::function<void(const TrackID&)> onMultiArmRequested;
    std::function<void(const TrackID&)> onMultiColorRequested;

    bool isMasterRow() const { return track_.isMaster(); }

    /** Callback: a plugin was drag-dropped onto this track row from another track. */
    std::function<void(const TrackID& srcTrack, int srcSlot, const TrackID& destTrack)> onPluginDropReceived;

    // ── DragAndDropTarget: highlight row when plugin is dragged over ──────
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        return details.description.toString().startsWith("MixerPluginSlotDrag");
    }

    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails&) override
    {
        pluginDropHover_ = true;
        DBG("[TrackRow] Hover target: " + track_.getName() + " id=" + track_.getID() + " region=timeline");
        repaint();
    }

    void itemDragExit(const juce::DragAndDropTarget::SourceDetails&) override
    {
        pluginDropHover_ = false;
        repaint();
    }

    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        pluginDropHover_ = false;
        repaint();
        auto parts = juce::StringArray::fromTokens(details.description.toString(), ":", "");
        if (parts.size() < 3) return;
        auto srcTrackId = parts[1];
        int  srcSlot    = parts[2].getIntValue();
        if (srcTrackId == track_.getID()) return; // same track = no-op
        DBG("[TrackRow] Cross-track drop success: src=" + srcTrackId + " slot=" + juce::String(srcSlot)
            + " -> dest=" + track_.getID() + " (" + track_.getName() + ")");
        if (onPluginDropReceived)
            onPluginDropReceived(srcTrackId, srcSlot, track_.getID());
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto bounds = getLocalBounds().toFloat();
        const bool master = track_.isMaster();

        // Background — master gets a distinct dark + gold top-edge.
        // Selection no longer tints the base fill; the depth overlay owns that.
        if (master)
        {
            g.setColour(t.colors.trackHeader.darker(0.25f));
            g.fillRect(bounds);
            g.setColour(juce::Colour(0xFFCC9900).withAlpha(0.9f));
            g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 3.f);
        }
        else
        {
            g.setColour(t.colors.trackHeader);
            g.fillRect(bounds);
        }

        // Plugin drag highlight
        if (pluginDropHover_)
        {
            g.setColour(juce::Colour(0xFF22D3EE).withAlpha(0.18f));
            g.fillRect(getLocalBounds().toFloat());
            g.setColour(juce::Colour(0xFF22D3EE).withAlpha(0.7f));
            g.drawRect(getLocalBounds().toFloat().reduced(1.f), 2.f);
        }

        // Selection depth + one-shot sweep (no looping animation).
        selectionVisual_.paintInto(g, getLocalBounds().toFloat());

        // Bubblegum send feedback highlight (painted AFTER selection overlay
        // so the pink outline/glow isn’t washed out by the depth layers).
        if (bgFeedback_ == BgFeedback::Source)
        {
            auto pinkCol = juce::Colour(0xFFFF7DB8);
            auto rowBounds = getLocalBounds().toFloat();

            // Source: outer glow halo
            g.setColour(pinkCol.withAlpha(0.08f));
            g.drawRect(rowBounds.expanded(1.f), 3.f);

            // Strong pink outline
            g.setColour(pinkCol.withAlpha(0.55f));
            g.drawRect(rowBounds.reduced(0.5f), 1.5f);

            // Subtle internal tint
            g.setColour(pinkCol.withAlpha(0.04f));
            g.fillRect(rowBounds.reduced(1.f));

            // Left accent bar (source indicator)
            g.setColour(pinkCol.withAlpha(0.75f));
            g.fillRect(0.f, 0.f, 3.f, (float)getHeight());
        }
        else if (bgFeedback_ == BgFeedback::SendTarget)
        {
            auto pinkCol = juce::Colour(0xFFFF7DB8);
            auto rowBounds = getLocalBounds().toFloat();
            float levelFactor = juce::jlimit(0.f, 1.f, bgFeedbackLevel_ * 0.5f);
            float intensity = 0.45f + levelFactor * 0.15f;

            // Soft outline
            g.setColour(pinkCol.withAlpha(0.25f * intensity));
            g.drawRect(rowBounds.reduced(0.5f), 1.f);

            // Left edge glow
            g.setColour(pinkCol.withAlpha(0.40f * intensity));
            g.fillRect(0.f, 0.f, 2.f, (float)getHeight());

            // Right edge glow
            g.setColour(pinkCol.withAlpha(0.15f * intensity));
            g.fillRect((float)getWidth() - 2.f, 0.f, 2.f, (float)getHeight());

            // Very subtle internal tint
            g.setColour(pinkCol.withAlpha(0.02f * intensity));
            g.fillRect(rowBounds.reduced(1.f));
        }

        // Bottom separator
        g.setColour(t.colors.separatorSoft);
        g.fillRect(bounds.getX(), bounds.getBottom() - 1.f, bounds.getWidth(), 1.f);

        // Indent + nesting guides for child tracks
        const float indentPx = (float) (indentLevel_ * kIndentStepPx);
        if (indentLevel_ > 0 && !master)
        {
            auto indentArea = bounds.removeFromLeft(indentPx);

            // Subtle darker band under indent area to read as "inside folder"
            g.setColour(t.colors.backgroundDark.withAlpha(0.35f));
            g.fillRect(indentArea);

            // Vertical guide lines for each ancestor level
            auto guide = track_.getColor().withAlpha(0.35f);
            g.setColour(guide);
            for (int i = 0; i < indentLevel_; ++i)
            {
                float gx = (float) (i * kIndentStepPx) + 8.f;
                g.fillRect(gx, 0.f, 1.f, (float) getHeight());
            }

            // Elbow connector into the row (horizontal stub)
            float stubY = bounds.getCentreY();
            float stubX = (float) ((indentLevel_ - 1) * kIndentStepPx) + 8.f;
            g.fillRect(stubX, stubY, (float) kIndentStepPx - 4.f, 1.f);
        }

        // Folder expand/collapse chevron — overlaid on the color strip area,
        // no layout shift so folder buses stay flush with normal tracks.
        if (isFolderBus_ && !master)
        {
            float chevX = bounds.getX();          // same X as color strip
            float chevY = (getHeight() - 12.f) * 0.5f;
            auto chev = juce::Rectangle<float>(chevX, chevY, 12.f, 12.f);
            chevronBounds_ = chev;
        }
        else
        {
            chevronBounds_ = {};
        }

        // Track colour strip (clickable) — drawn for all non-master tracks
        colorStripBounds_ = bounds.removeFromLeft(6.f);
        g.setColour(master ? juce::Colour(0xFFCC9900) : track_.getColor());
        g.fillRect(colorStripBounds_);

        // Chevron drawn on top of color strip (folder buses only)
        if (isFolderBus_ && !master)
            drawFolderChevron(g, chevronBounds_, folderExpanded_, chevronHover_);

        // Track name — top half
        g.setColour(t.colors.text);
        g.setFont(t.fonts.bold);
        auto nameBounds = bounds.removeFromLeft(bounds.getWidth()).reduced(8, 0);
        nameBounds = nameBounds.removeFromTop(nameBounds.getHeight() * 0.5f);
        g.drawText(track_.getName(), nameBounds, juce::Justification::centredLeft);

        // M / S / Monitor / R / A / Trim / Target — bottom half
        drawMSRButton(g, muteBounds_, "M", track_.isMuted(), muteHover_, juce::Colour(0xffe6c619));
        drawMSRButton(g, soloBounds_, "S", track_.isSoloed(), soloHover_, t.colors.transportPlay);
        if (!master)
        {
            drawMonitorButton(g, monitorBounds_, track_.isMonitoring(), monitorHover_);
            drawMSRButton(g, armBounds_, "R", track_.isArmed(), armHover_, t.colors.transportRecord);
            drawAutomationButton(g, automationBounds_, track_.isAutomationVisible(), track_.isAutomationMuted(), automationHover_);
            drawTrimButton(g, trimBounds_, trimHover_);
            drawInputButton(g, inputBounds_, inputHover_);
            drawAutomationTargetButton(g, automationTargetBounds_, automationTargetHover_);
        }

        // Bottom resize handle (master is not resizable)
        if (!master)
        {
            g.setColour(resizeHover_ ? t.colors.accent.withAlpha(0.5f)
                                     : t.colors.border.withAlpha(0.15f));
            g.fillRect(0.f, (float)getHeight() - 2.f, (float)getWidth(), 2.f);
        }

        // Stronger row frame so headers read as their own panel
        {
            auto r = getLocalBounds().toFloat().reduced(1.5f);
            g.setColour(t.colors.border.withAlpha(master ? 0.55f : 0.42f));
            g.drawRect(r, 2.0f);
        }
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().toFloat();
        if (indentLevel_ > 0 && !track_.isMaster())
            bounds.removeFromLeft((float)(indentLevel_ * kIndentStepPx));
        bounds.removeFromLeft(6.0f); // colour strip

        // Keep buttons comfortably inside the row so they don't kiss/overlap the edge.
        bounds = bounds.reduced(6.0f, 0.0f);

        const bool isMaster  = track_.isMaster();
        const bool showPiano = track_.getRole() == TrackRole::MIDI || track_.getRole() == TrackRole::Instrument;
        const bool showTarget = !isMaster && track_.isAutomationVisible();
        const bool showInput  = !isMaster && track_.getRole() == TrackRole::Audio;

        // Bottom half for buttons
        const float h = bounds.getHeight();
        const float btnRowY  = h * 0.5f;
        const float btnH     = juce::jlimit(18.0f, 24.0f, h * 0.44f);
        const float btnY     = btnRowY + (h * 0.5f - btnH) * 0.5f;

        // Optional stacked area (Input above Trim) lives in the right half
        // so the bottom row stays uncluttered.
        const float stackH = juce::jlimit(26.0f, 30.0f, h * 0.44f);
        const float stackTopY = bounds.getY() + 6.0f;
        const float kRightMargin = 10.0f;

        // Count buttons
        int numBtns = 1; // M
        if (showPiano)    ++numBtns;
        if (!isMaster)    numBtns += 4; // S Mon R A
        if (!isMaster)    ++numBtns;    // Trim counts as 1 slot
        if (showTarget)   ++numBtns;

        // Reserve a right-side stacked area when input selector is visible.
        const float stackW = showInput ? juce::jlimit(46.0f, 74.0f, bounds.getWidth() * 0.22f) : 0.0f;
        const float availW = bounds.getWidth() - kRightMargin - (showInput ? (stackW + 10.0f) : 0.0f);
        const float kGap   = 8.0f;
        const float totalGaps = kGap * (float)(numBtns - 1);
        const float btnW   = juce::jmax(20.0f, (availW - totalGaps) / (float)numBtns);
        const float trimW  = btnW * 1.3f; // Trim a bit wider

        float x = bounds.getX();

        if (showPiano)
        {
            pianoRollBounds_.setBounds(x, btnY, btnW, btnH);
            x += btnW + kGap;
        }
        else
        {
            pianoRollBounds_ = {};
        }

        muteBounds_.setBounds(x, btnY, btnW, btnH);
        x += btnW + kGap;

        if (isMaster)
        {
            soloBounds_.setBounds(x, btnY, btnW, btnH); x += btnW + kGap;
            monitorBounds_ = {}; armBounds_ = {};
            automationBounds_ = {}; trimBounds_ = {}; automationTargetBounds_ = {};
            inputBounds_ = {};
            return;
        }

        soloBounds_.setBounds(x, btnY, btnW, btnH);   x += btnW + kGap;
        monitorBounds_.setBounds(x, btnY, btnW, btnH); x += btnW + kGap;
        armBounds_.setBounds(x, btnY, btnW, btnH);     x += btnW + kGap;
        automationBounds_.setBounds(x, btnY, btnW, btnH); x += btnW + kGap;
        trimBounds_.setBounds(x, btnY, trimW, btnH);   x += trimW + kGap;

        // Input selector stacked above Trim on the right to avoid border collisions.
        if (showInput)
        {
            const float stackX = bounds.getRight() - kRightMargin - stackW;
            inputBounds_.setBounds(stackX, stackTopY, stackW, stackH);

            // Keep Trim from running into the stacked area.
            const float maxRight = stackX - 10.0f;
            if (trimBounds_.getRight() > maxRight)
                trimBounds_.setWidth(juce::jmax(18.0f, maxRight - trimBounds_.getX()));
        }
        else
        {
            inputBounds_ = {};
        }

        if (showTarget)
        {
            const float targetW = juce::jlimit(34.0f, 58.0f, btnW * 1.55f);
            const float targetX = bounds.getRight() - kRightMargin - targetW;
            automationTargetBounds_.setBounds(targetX, btnY, targetW, btnH);

            const float maxRight = targetX - kGap;
            if (!inputBounds_.isEmpty() && inputBounds_.getRight() > maxRight)
                inputBounds_.setWidth(juce::jmax(18.0f, maxRight - inputBounds_.getX()));
            if (trimBounds_.getRight() > maxRight)
                trimBounds_.setWidth(juce::jmax(18.0f, maxRight - trimBounds_.getX()));
        }
        else
        {
            automationTargetBounds_ = {};
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!track_.isMaster() && automationBounds_.contains(e.position))
        {
            if (e.mods.isPopupMenu())
                showAutomationMenu();
            else
                track_.setAutomationVisible(!track_.isAutomationVisible());
            if (onAutomationLaneStateChanged) onAutomationLaneStateChanged(track_.getID());
            resized();
            repaint();
            return;
        }

        if (!track_.isMaster() && automationTargetBounds_.contains(e.position))
        {
            showAutomationTargetMenu();
            repaint();
            return;
        }

        // ── Right-click context menu ──────────────────────────────────────
        if (e.mods.isPopupMenu() && !track_.isMaster())
        {
            // Right-click directly on the "R" arm button opens the per-track
            // record-mode menu (record dry vs. print plugin effects) instead
            // of the general track context menu.
            if (armBounds_.contains(e.position))
            {
                showRecordModeMenu();
                return;
            }
            showContextMenu();
            return;
        }

        if (!track_.isMaster() && e.y >= getHeight() - 5)
        {
            dragStartY_ = e.getScreenY();
            dragMode_ = DragMode::Resize;
            return;
        }
        if (isFolderBus_ && !track_.isMaster() && chevronBounds_.contains(e.position))
        {
            folderExpanded_ = !folderExpanded_;
            repaint();
            if (onFolderToggled)
                onFolderToggled(track_.getID(), folderExpanded_);
            return;
        }
        if (colorStripBounds_.contains(e.position))
        {
            if (onColorChanged)
            {
                TrackRow* self = this;
                TrackColorPalette::showWithCallback(track_, *this,
                    getScreenBounds().withWidth(8),
                    [self](juce::Colour c) { if (self->onColorChanged) self->onColorChanged(self, c); });
            }
            else
            {
                TrackColorPalette::show(track_, *this, getScreenBounds().withWidth(8));
            }
            return;
        }
        if (muteBounds_.contains(e.position))
        {
            if (onToggleMute) onToggleMute(track_.getID());
            else track_.setMuted(!track_.isMuted());
            repaint();
        }
        else if (pianoRollBounds_.contains(e.position))
        {
            if (onOpenPianoRoll) onOpenPianoRoll(track_.getID());
            repaint();
        }
        else if (soloBounds_.contains(e.position))
        {
            if (onToggleSolo) onToggleSolo(track_.getID());
            else track_.setSoloed(!track_.isSoloed());
            repaint();
        }
        else if (!track_.isMaster() && monitorBounds_.contains(e.position))
        {
            if (onSetMonitoring) onSetMonitoring(track_.getID(), !track_.isMonitoring());
            else track_.setMonitoring(!track_.isMonitoring());
            repaint();
        }
        else if (!track_.isMaster() && armBounds_.contains(e.position))
        {
            if (onToggleArm) onToggleArm(track_.getID());
            else track_.setArmed(!track_.isArmed());
            repaint();
        }
        else if (!track_.isMaster() && trimBounds_.contains(e.position))
        {
            if (onOpenInputTrim) onOpenInputTrim(track_.getID());
            else InputTrimFloatingPanel::showForTrack(track_, getTopLevelComponent());
        }
        else if (!track_.isMaster() && inputBounds_.contains(e.position))
        {
            showInputSourceMenu();
        }
        else
        {
            // Master is selectable but NOT reorderable
            if (!track_.isMaster())
            {
                dragStartY_ = e.getScreenY();
                dragMode_ = DragMode::PendingReorder;
            }
            if (onSelectedWithModifiers)
            {
                DBG("[TrackList] " + juce::String(track_.isMaster() ? "Master" : "Track") + " selected from timeline: " + track_.getName() + " id=" + track_.getID());
                onSelectedWithModifiers(this, e.mods);
            }
            else if (onSelected)
            {
                DBG("[TrackList] " + juce::String(track_.isMaster() ? "Master" : "Track") + " selected from timeline: " + track_.getName() + " id=" + track_.getID());
                onSelected(this);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        bool wasHover = resizeHover_;
        resizeHover_ = (!track_.isMaster() && e.y >= getHeight() - 5);
        bool pm = muteHover_, pp = pianoHover_, ps = soloHover_, pmon = monitorHover_, pa = armHover_, ptr = trimHover_, paut = automationHover_, pat = automationTargetHover_;
        bool pc = chevronHover_;
        bool pin = inputHover_;
        pianoHover_   = pianoRollBounds_.contains(e.position);
        muteHover_    = muteBounds_.contains(e.position);
        soloHover_    = soloBounds_.contains(e.position);
        monitorHover_ = !track_.isMaster() && monitorBounds_.contains(e.position);
        armHover_     = !track_.isMaster() && armBounds_.contains(e.position);
        automationHover_ = !track_.isMaster() && automationBounds_.contains(e.position);
        automationTargetHover_ = !track_.isMaster() && automationTargetBounds_.contains(e.position);
        trimHover_    = !track_.isMaster() && trimBounds_.contains(e.position);
        inputHover_   = !track_.isMaster() && inputBounds_.contains(e.position);
        chevronHover_ = isFolderBus_ && !track_.isMaster() && chevronBounds_.contains(e.position);
        if (wasHover != resizeHover_ || pm != muteHover_ || pp != pianoHover_ || ps != soloHover_ || pmon != monitorHover_ || pa != armHover_ || ptr != trimHover_ || paut != automationHover_ || pat != automationTargetHover_ || pc != chevronHover_ || pin != inputHover_)
        {
            setMouseCursor(resizeHover_ ? juce::MouseCursor::UpDownResizeCursor
                                        : automationTargetHover_ ? juce::MouseCursor::PointingHandCursor
                                                                 : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragMode_ == DragMode::Resize && dragStartY_ >= 0 && onHeightDrag)
        {
            int delta = e.getScreenY() - dragStartY_;
            dragStartY_ = e.getScreenY();
            onHeightDrag(this, delta);
        }
        else if (dragMode_ == DragMode::PendingReorder && dragStartY_ >= 0)
        {
            if (std::abs(e.getScreenY() - dragStartY_) > 6)
                dragMode_ = DragMode::Reorder;
        }
        else if (dragMode_ == DragMode::Reorder && onReorderDrag)
        {
            onReorderDrag(this, e);
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragMode_ == DragMode::Reorder && onReorderDrop)
            onReorderDrop(this);
        dragStartY_ = -1;
        dragMode_ = DragMode::None;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        // Double-click on name area — always renames this single track only.
        // Multi-track bulk rename is in the right-click context menu.
        auto nameArea = getLocalBounds().toFloat();
        nameArea.removeFromLeft(6.f);
        nameArea = nameArea.removeFromLeft(96.f);
        if (nameArea.contains(e.position))
        {
            auto* editor = new juce::TextEditor();
            editor->setBounds(nameArea.toNearestIntEdges().reduced(4, 2));
            editor->setText(track_.getName(), false);
            editor->setFont(Theme::getInstance().fonts.bold);
            editor->selectAll();
            editor->setColour(juce::TextEditor::backgroundColourId, Theme::getInstance().colors.surface);
            editor->setColour(juce::TextEditor::textColourId, Theme::getInstance().colors.text);
            editor->setColour(juce::TextEditor::outlineColourId, Theme::getInstance().colors.accent);
            editor->setColour(juce::TextEditor::focusedOutlineColourId, Theme::getInstance().colors.accent);
            addAndMakeVisible(editor);
            editor->grabKeyboardFocus();

            // Always rename this track only — use a direct lambda that does NOT
            // trigger the multi-selection rename dialog in MainComponent.
            editor->onReturnKey = [this, editor] {
                auto name = editor->getText().trim();
                if (name.isNotEmpty()) track_.setName(name);
                delete editor;
            };
            editor->onFocusLost = [this, editor] {
                auto name = editor->getText().trim();
                if (name.isNotEmpty()) track_.setName(name);
                delete editor;
            };
            editor->onEscapeKey = [editor] {
                delete editor;
            };
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        resizeHover_ = false;
        pianoHover_ = false; muteHover_ = false; soloHover_ = false; monitorHover_ = false; armHover_ = false; automationHover_ = false; automationTargetHover_ = false; trimHover_ = false;
        inputHover_ = false;
        chevronHover_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

private:
    Track& track_;
    TrackSelectionVisualCore selectionVisual_;

    void timerCallback() override
    {
        if (!automationBounds_.isEmpty() && hasTrackAutomation())
        {
            autoPulsePhase_ += 0.024f * 2.0f;  // ~24 Hz * phase step  ≈ 0.576 rad/tick
            if (autoPulsePhase_ > juce::MathConstants<float>::twoPi)
                autoPulsePhase_ -= juce::MathConstants<float>::twoPi;
            repaint(automationBounds_.expanded(7.0f).toNearestInt());
        }
    }

    bool hasTrackAutomation() const
    {
        return hasAutomationData ? hasAutomationData(track_.getID()) : false;
    }

    // Right-click menu for the "R" arm button: choose whether recording prints
    // the track's plugin effects into the file, or records the dry input while
    // still letting the performer monitor through the effects.
    void showRecordModeMenu()
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<TrackRow> safeThis(this);

        const bool hasPlugins = track_.getPluginChain() != nullptr
                             && track_.getPluginChain()->getNumActiveSlots() > 0;

        const auto currentMode = track_.getMonitoringState().getRecordingMode();
        const bool recordWet = (currentMode == RecordInputRouter::Mode::MonitorWetRecordWet);

        menu.addSectionHeader("Recording Mode");

        {
            // Record the dry vocal/input — plugins are heard while monitoring
            // but the recorded file stays clean (no printed effects).
            juce::PopupMenu::Item dry("Record Dry (effects monitored only)");
            dry.isTicked = !recordWet;
            dry.action = [safeThis]
            {
                if (!safeThis) return;
                // Keep wet monitoring if plugins exist, otherwise fully dry.
                const bool hasFx = safeThis->track_.getPluginChain() != nullptr
                                && safeThis->track_.getPluginChain()->getNumActiveSlots() > 0;
                safeThis->track_.getMonitoringState().setRecordingMode(
                    hasFx ? RecordInputRouter::Mode::MonitorWetRecordDry
                          : RecordInputRouter::Mode::MonitorDryRecordDry);
                safeThis->repaint();
            };
            menu.addItem(dry);
        }
        {
            // Print the track's plugin effects into the recorded file.
            juce::PopupMenu::Item wet("Record With Effects (printed)");
            wet.isTicked = recordWet;
            wet.isEnabled = hasPlugins;
            wet.action = [safeThis]
            {
                if (!safeThis) return;
                safeThis->track_.getMonitoringState().setRecordingMode(
                    RecordInputRouter::Mode::MonitorWetRecordWet);
                safeThis->repaint();
            };
            menu.addItem(wet);
        }

        if (!hasPlugins)
        {
            juce::PopupMenu::Item hint("(Add plugins to this track to print effects)");
            hint.isEnabled = false;
            menu.addItem(hint);
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

    void showContextMenu()
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<TrackRow> safeThis(this);

        const bool isMulti = onGetIsMultiSelected && onGetIsMultiSelected(track_.getID());

        // ── Multi-selection section (shown first when applicable) ──────────
        if (isMulti)
        {
            {
                juce::PopupMenu::Item r("Rename Selected Tracks (Numbered)...");
                r.action = [safeThis] { if (safeThis && safeThis->onMultiRenameRequested) safeThis->onMultiRenameRequested(safeThis->track_.getID()); };
                menu.addItem(r);
            }
            {
                juce::PopupMenu::Item c("Change Color for Selected Tracks...");
                c.action = [safeThis] { if (safeThis && safeThis->onMultiColorRequested) safeThis->onMultiColorRequested(safeThis->track_.getID()); };
                menu.addItem(c);
            }
            {
                juce::PopupMenu::Item m("Mute Selected Tracks");
                m.action = [safeThis] { if (safeThis && safeThis->onMultiMuteRequested) safeThis->onMultiMuteRequested(safeThis->track_.getID()); };
                menu.addItem(m);
            }
            {
                juce::PopupMenu::Item s("Solo Selected Tracks");
                s.action = [safeThis] { if (safeThis && safeThis->onMultiSoloRequested) safeThis->onMultiSoloRequested(safeThis->track_.getID()); };
                menu.addItem(s);
            }
            {
                juce::PopupMenu::Item a("Arm Selected Tracks");
                a.action = [safeThis] { if (safeThis && safeThis->onMultiArmRequested) safeThis->onMultiArmRequested(safeThis->track_.getID()); };
                menu.addItem(a);
            }
            {
                juce::PopupMenu::Item d("Delete Selected Tracks");
                d.action = [safeThis] { if (safeThis && safeThis->onMultiDeleteRequested) safeThis->onMultiDeleteRequested(safeThis->track_.getID()); };
                menu.addItem(d);
            }
            menu.addSeparator();
        }

        // ── Single-track section ───────────────────────────────────────────

        // Arm / Monitor toggles
        {
            juce::PopupMenu::Item arm("Record Arm");
            arm.isTicked = track_.isArmed();
            arm.action = [safeThis] { if (safeThis) { if (safeThis->onToggleArm) safeThis->onToggleArm(safeThis->track_.getID()); else safeThis->track_.setArmed(!safeThis->track_.isArmed()); safeThis->repaint(); } };
            menu.addItem(arm);
        }
        {
            juce::PopupMenu::Item mon("Input Monitor");
            mon.isTicked = track_.isMonitoring();
            mon.action = [safeThis] { if (safeThis) { if (safeThis->onSetMonitoring) safeThis->onSetMonitoring(safeThis->track_.getID(), !safeThis->track_.isMonitoring()); else safeThis->track_.setMonitoring(!safeThis->track_.isMonitoring()); safeThis->repaint(); } };
            menu.addItem(mon);
        }

        menu.addSeparator();

        // Mute / Solo
        {
            juce::PopupMenu::Item m("Mute");
            m.isTicked = track_.isMuted();
            m.action = [safeThis] { if (safeThis) { if (safeThis->onToggleMute) safeThis->onToggleMute(safeThis->track_.getID()); else safeThis->track_.setMuted(!safeThis->track_.isMuted()); safeThis->repaint(); } };
            menu.addItem(m);
        }
        {
            juce::PopupMenu::Item s("Solo");
            s.isTicked = track_.isSoloed();
            s.action = [safeThis] { if (safeThis) { if (safeThis->onToggleSolo) safeThis->onToggleSolo(safeThis->track_.getID()); else safeThis->track_.setSoloed(!safeThis->track_.isSoloed()); safeThis->repaint(); } };
            menu.addItem(s);
        }

        menu.addSeparator();

        // Rename (single track)
        {
            juce::PopupMenu::Item r("Rename...");
            r.action = [safeThis]
            {
                if (!safeThis) return;
                auto nameArea = safeThis->getLocalBounds().toFloat();
                nameArea.removeFromLeft(6.f);
                nameArea = nameArea.removeFromLeft(96.f);

                auto* editor = new juce::TextEditor();
                editor->setBounds(nameArea.toNearestIntEdges().reduced(4, 2));
                editor->setText(safeThis->track_.getName(), false);
                editor->setFont(Theme::getInstance().fonts.bold);
                editor->selectAll();
                editor->setColour(juce::TextEditor::backgroundColourId, Theme::getInstance().colors.surface);
                editor->setColour(juce::TextEditor::textColourId, Theme::getInstance().colors.text);
                editor->setColour(juce::TextEditor::outlineColourId, Theme::getInstance().colors.accent);
                safeThis->addAndMakeVisible(editor);
                editor->grabKeyboardFocus();
                editor->onReturnKey = [safeThis, editor] { if (safeThis) { if (safeThis->onRenameTrack) safeThis->onRenameTrack(safeThis->track_.getID(), editor->getText().trim()); else safeThis->track_.setName(editor->getText().trim()); } delete editor; };
                editor->onFocusLost = [safeThis, editor] { if (safeThis) { if (safeThis->onRenameTrack) safeThis->onRenameTrack(safeThis->track_.getID(), editor->getText().trim()); else safeThis->track_.setName(editor->getText().trim()); } delete editor; };
                editor->onEscapeKey = [editor] { delete editor; };
            };
            menu.addItem(r);
        }

        // Color (single track)
        {
            juce::PopupMenu::Item c("Change Color...");
            c.action = [safeThis] { if (safeThis) TrackColorPalette::show(safeThis->track_, *safeThis, safeThis->getScreenBounds().withWidth(8)); };
            menu.addItem(c);
        }

        menu.addSeparator();

        // Duplicate / Delete
        if (onDuplicateTrack)
        {
            juce::PopupMenu::Item d("Duplicate Track");
            d.action = [safeThis] { if (safeThis && safeThis->onDuplicateTrack) safeThis->onDuplicateTrack(safeThis->track_.getID()); };
            menu.addItem(d);
        }
        if (onDeleteTrack)
        {
            juce::PopupMenu::Item del("Delete Track");
            del.action = [safeThis] { if (safeThis && safeThis->onDeleteTrack) safeThis->onDeleteTrack(safeThis->track_.getID()); };
            menu.addItem(del);
        }

        menu.showMenuAsync(juce::PopupMenu::Options()
            .withTargetComponent(this));
    }

    void showAutomationMenu()
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<TrackRow> safeThis(this);
        const auto activeParamId = track_.getActiveAutomationParameterId().isNotEmpty()
            ? track_.getActiveAutomationParameterId()
            : juce::String(AutomationLaneCore::trackVolumeParameterId);

        juce::PopupMenu::Item muteCurrentItem("Mute Current Automation");
        muteCurrentItem.action = [safeThis, activeParamId]
        {
            if (!safeThis) return;
            const bool newMuted = !safeThis->track_.isAutomationMuted();
            if (safeThis->onAutomationTargetMuteChanged)
                safeThis->onAutomationTargetMuteChanged(safeThis->track_.getID(), activeParamId, newMuted);
            safeThis->repaint();
        };
        menu.addItem(muteCurrentItem);

        juce::PopupMenu::Item soloCurrentItem("Solo Current Automation");
        soloCurrentItem.action = [safeThis, activeParamId]
        {
            if (!safeThis) return;
            if (safeThis->onAutomationTargetSoloRequested)
                safeThis->onAutomationTargetSoloRequested(safeThis->track_.getID(), activeParamId);
            safeThis->repaint();
        };
        menu.addItem(soloCurrentItem);

        juce::PopupMenu::Item clearCurrentItem("Clear Current Automation");
        clearCurrentItem.action = [safeThis, activeParamId]
        {
            if (!safeThis) return;
            auto options = juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Clear Current Automation")
                .withMessage("Delete automation for the current target only?")
                .withButton("Delete")
                .withButton("Cancel")
                .withAssociatedComponent(safeThis.getComponent());

            juce::AlertWindow::showAsync(options, [safeThis, activeParamId](int result)
            {
                if (safeThis && result == 1 && safeThis->onAutomationTargetClearRequested)
                    safeThis->onAutomationTargetClearRequested(safeThis->track_.getID(), activeParamId);
            });
        };
        menu.addItem(clearCurrentItem);

        menu.addSeparator();

        juce::PopupMenu::Item muteItem("Mute All Automation");
        muteItem.isTicked = track_.isAutomationMuted();
        muteItem.action = [safeThis]
        {
            if (!safeThis) return;
            const bool newMuted = !safeThis->track_.isAutomationMuted();
            safeThis->track_.setAutomationMuted(newMuted);
            if (safeThis->onAutomationMuteChanged)
                safeThis->onAutomationMuteChanged(safeThis->track_.getID(), newMuted);
            safeThis->repaint();
        };
        menu.addItem(muteItem);

        juce::PopupMenu::Item clearItem("Clear All Automation");
        clearItem.action = [safeThis]
        {
            if (!safeThis) return;
            auto options = juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Clear Automation")
                .withMessage("Delete all automation for this track?")
                .withButton("Delete")
                .withButton("Cancel")
                .withAssociatedComponent(safeThis.getComponent());

            juce::AlertWindow::showAsync(options, [safeThis](int result)
            {
                if (safeThis && result == 1 && safeThis->onAutomationClearRequested)
                    safeThis->onAutomationClearRequested(safeThis->track_.getID());
            });
        };
        menu.addItem(clearItem);

        {
            if (activeParamId.isNotEmpty())
            {
                menu.addItem("Create Sequence...", [safeThis, activeParamId]()
                {
                    if (!safeThis) return;
                    if (safeThis->onCreateSequenceRequested)
                        safeThis->onCreateSequenceRequested(
                            safeThis->track_.getID(), activeParamId);
                });
            }
        }

        menu.addSeparator();
        juce::PopupMenu targets;
        const auto activeTarget = track_.getActiveAutomationParameterId();
        targets.addItem(101, "Volume", true, activeTarget == "track.volume");
        targets.addItem(102, "Pan", true, activeTarget == "track.pan");
        targets.addItem(104, "Tape Stop", true, activeTarget == "track.tape_stop");
        targets.addItem(103, "Instrument", true, activeTarget == "instrument.main");
        targets.addSeparator();
        juce::PopupMenu pluginTargets;
        bool hasPluginTargets = false;
        if (auto* chain = track_.getPluginChain())
        {
            for (int slot = 0; slot < chain->getNumSlots(); ++slot)
            {
                if (!chain->hasPlugin(slot)) continue;
                auto* plugin = chain->getSlot(slot);
                auto id = AutomationManagerCore::makePluginSlotMixId(slot);
                pluginTargets.addItem(1100 + slot,
                    juce::String(slot + 1) + ". " + (plugin ? plugin->getName() : juce::String("Plugin")) + " / Mix",
                    true,
                    activeTarget == id);
                hasPluginTargets = true;
            }
        }
        targets.addSubMenu("Plugin Slot Mix", pluginTargets, hasPluginTargets);
        targets.addSeparator();
        juce::PopupMenu clipTargets;
        bool hasClipTargets = false;
        std::vector<std::pair<juce::String,juce::String>> clipInfos; // {id, name}
        if (onGetAudioClipsOnTrack)
        {
            for (auto* clip : onGetAudioClipsOnTrack(track_.getID()))
            {
                if (clip == nullptr) continue;
                clipInfos.push_back({ clip->getID(), clip->getName().isNotEmpty() ? clip->getName() : ("Clip " + clip->getID().substring(0, 6)) });
            }
        }
        for (int ci = 0; ci < (int)clipInfos.size(); ++ci)
        {
            const auto& ce = clipInfos[(size_t)ci];
            const auto pitchId   = AutomationLaneCore::makeClipPitchParameterId(ce.first);
            const auto stretchId = AutomationLaneCore::makeClipStretchParameterId(ce.first);
            const int pitchItemId   = 8000 + ci * 2;
            const int stretchItemId = 8000 + ci * 2 + 1;
            if (clipInfos.size() == 1)
            {
                clipTargets.addItem(pitchItemId,   "Pitch Shift",  true, activeTarget == pitchId);
                // [FUTURE UPDATE] Time Stretch automation hidden until implemented:
                // clipTargets.addItem(stretchItemId, "Time Stretch", true, activeTarget == stretchId);
            }
            else
            {
                juce::PopupMenu perClip;
                perClip.addItem(pitchItemId,   "Pitch Shift",  true, activeTarget == pitchId);
                // [FUTURE UPDATE] Time Stretch automation hidden until implemented:
                // perClip.addItem(stretchItemId, "Time Stretch", true, activeTarget == stretchId);
                clipTargets.addSubMenu(ce.second, perClip);
            }
            hasClipTargets = true;
        }
        targets.addSubMenu("Clip Time / Pitch", clipTargets, hasClipTargets);
        menu.addSubMenu("Automation Target", targets);

        juce::PopupMenu reorder;
        auto order = track_.getAutomationParameterOrder();
        const int activeIndex = order.indexOf(activeTarget);
        reorder.addItem(201, "Move Current Up", activeIndex > 0);
        reorder.addItem(202, "Move Current Down", activeIndex >= 0 && activeIndex < order.size() - 1);
        menu.addSubMenu("Reorder Target List", reorder);

        menu.addSeparator();
        juce::PopupMenu::Item snapItem("Snap to Grid");
        snapItem.isTicked = track_.isAutomationSnapToGrid();
        snapItem.isEnabled = false;
        menu.addItem(snapItem);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis, order, clipInfos](int result)
        {
            if (!safeThis)
                return;

            if (result >= 8000 && result < 8000 + (int)clipInfos.size() * 2)
            {
                const int ci = (result - 8000) / 2;
                const bool isPitch = ((result - 8000) % 2) == 0;
                if (ci >= 0 && ci < (int)clipInfos.size())
                {
                    const auto& ce = clipInfos[(size_t)ci];
                    // [FUTURE UPDATE] Stretch selection disabled until implemented:
                    if (!isPitch) return; // stretch automation hidden for now
                    const auto paramId = AutomationLaneCore::makeClipPitchParameterId(ce.first);
                    safeThis->track_.setActiveAutomationParameterId(paramId);
                }
            }
            else if (result == 101) safeThis->track_.setActiveAutomationParameterId("track.volume");
            else if (result == 102) safeThis->track_.setActiveAutomationParameterId("track.pan");
            else if (result == 104) safeThis->track_.setActiveAutomationParameterId("track.tape_stop");
            else if (result == 103) safeThis->track_.setActiveAutomationParameterId("instrument.main");
            else if (result >= 1100 && result < 1200) safeThis->track_.setActiveAutomationParameterId(AutomationManagerCore::makePluginSlotMixId(result - 1100));
            else if (result == 201 || result == 202)
            {
                auto updated = order;
                const auto active = safeThis->track_.getActiveAutomationParameterId();
                const int idx = updated.indexOf(active);
                const int newIdx = result == 201 ? idx - 1 : idx + 1;
                if (idx >= 0 && newIdx >= 0 && newIdx < updated.size())
                {
                    updated.remove(idx);
                    updated.insert(newIdx, active);
                    safeThis->track_.setAutomationParameterOrder(updated);
                }
            }

            safeThis->repaint();
            if (safeThis->onAutomationLaneStateChanged)
                safeThis->onAutomationLaneStateChanged(safeThis->track_.getID());
        });
    }

public:
    std::function<void(const TrackID&)> onDuplicateTrack;
    std::function<void(const TrackID&)> onDeleteTrack;
    enum class DragMode { None, Resize, PendingReorder, Reorder };
    DragMode dragMode_ = DragMode::None;
    int  height_      = 72;
    int  dragStartY_  = -1;
    bool selected_    = false;
    bool resizeHover_ = false;
    bool pluginDropHover_ = false;
    bool pianoHover_ = false;
    bool muteHover_ = false, soloHover_ = false, monitorHover_ = false, armHover_ = false, trimHover_ = false;
    bool inputHover_ = false;
    bool automationHover_ = false;
    bool automationTargetHover_ = false;
    bool chevronHover_ = false;

    // Nesting / folder expand state
    // kChevronWidthPx intentionally equals kIndentStepPx so a root folder bus
    // and its depth-1 children have their color strip at the same X offset.
    static constexpr int kIndentStepPx   = 14;
    static constexpr int kChevronWidthPx = 14;
    int  indentLevel_   = 0;
    bool isFolderBus_   = false;
    bool folderExpanded_ = true;
    juce::Rectangle<float> chevronBounds_;

    // Bubblegum send feedback
    enum class BgFeedback { None, Source, SendTarget };
    BgFeedback bgFeedback_       = BgFeedback::None;
    float      bgFeedbackLevel_  = 0.f;  // send level for intensity scaling
    juce::Rectangle<float> pianoRollBounds_, muteBounds_, soloBounds_, monitorBounds_, armBounds_, trimBounds_;
    juce::Rectangle<float> inputBounds_;
    juce::Rectangle<float> automationBounds_;
    juce::Rectangle<float> automationTargetBounds_;
    float autoPulsePhase_ = 0.f;
    mutable juce::Rectangle<float> colorStripBounds_;

    void drawMSRButton(juce::Graphics& g, const juce::Rectangle<float>& bounds, 
                       const juce::String& text, bool isOn, bool hovered, juce::Colour color)
    {
        auto& t = Theme::getInstance();

        // Background: idle / hover / active
        if (isOn)
            g.setColour(color.darker(0.15f));
        else if (hovered)
            g.setColour(t.colors.controlHover);
        else
            g.setColour(t.colors.controlIdle);
        g.fillRoundedRectangle(bounds, 4.0f);

        // Border
        g.setColour(isOn ? color.withAlpha(0.5f) : t.colors.border);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        // Text
        g.setColour(isOn ? juce::Colours::white : t.colors.textSecondary);
        g.setFont(t.fonts.bold);
        g.drawText(text, bounds, juce::Justification::centred);
    }

    void drawFolderChevron(juce::Graphics& g, juce::Rectangle<float> b, bool open, bool hovered)
    {
        auto& t = Theme::getInstance();

        // Soft rounded backing so the hit target reads as a real button
        g.setColour(hovered ? t.colors.controlHover
                            : t.colors.controlIdle.withAlpha(0.55f));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(t.colors.border.withAlpha(hovered ? 0.65f : 0.35f));
        g.drawRoundedRectangle(b, 4.0f, 1.0f);

        // Clean stroked chevron — ▼ when open, ▶ when closed
        auto c = b.getCentre();
        const float s = 3.2f;
        juce::Path p;
        if (open)
        {
            p.startNewSubPath(c.x - s, c.y - s * 0.55f);
            p.lineTo(c.x,         c.y + s * 0.55f);
            p.lineTo(c.x + s,     c.y - s * 0.55f);
        }
        else
        {
            p.startNewSubPath(c.x - s * 0.55f, c.y - s);
            p.lineTo(c.x + s * 0.55f,          c.y);
            p.lineTo(c.x - s * 0.55f,          c.y + s);
        }
        g.setColour(hovered ? juce::Colours::white : t.colors.text);
        g.strokePath(p, juce::PathStrokeType(1.8f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    void drawTrimButton(juce::Graphics& g, const juce::Rectangle<float>& bounds, bool hovered)
    {
        if (bounds.isEmpty()) return;
        auto& t = Theme::getInstance();
        const float db = track_.getInputTrim().getTargetGainDb();
        const bool active = (db < -0.1f || db > 0.1f);

        juce::Colour bg  = active  ? t.colors.accent.withAlpha(0.75f)
                         : hovered ? t.colors.controlHover
                                   : t.colors.controlIdle;
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(active ? t.colors.accent : t.colors.border);
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

        g.setColour(active ? juce::Colours::white : t.colors.textSecondary);
        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.drawText("TRIM", bounds, juce::Justification::centred, false);
    }

    void drawInputButton(juce::Graphics& g, const juce::Rectangle<float>& bounds, bool hovered)
    {
        if (bounds.isEmpty()) return;
        auto& t = Theme::getInstance();
        const int  first = track_.getInputFirstChannel();
        const bool mono  = track_.isInputMono();
        const juce::String label = mono
            ? "In " + juce::String(first + 1)
            : "In " + juce::String(first + 1) + "/" + juce::String(first + 2);

        g.setColour(hovered ? t.colors.controlHover : t.colors.controlIdle);
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

        g.setColour(hovered ? juce::Colours::white : t.colors.textSecondary);
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        auto textArea = bounds.withTrimmedRight(8.0f).reduced(2.0f, 0.0f);
        g.drawText(label, textArea.toNearestInt(), juce::Justification::centred, false);

        // dropdown arrow
        auto c = juce::Point<float>(bounds.getRight() - 5.0f, bounds.getCentreY());
        juce::Path arrow;
        arrow.startNewSubPath(c.x - 2.2f, c.y - 1.1f);
        arrow.lineTo(c.x, c.y + 1.4f);
        arrow.lineTo(c.x + 2.2f, c.y - 1.1f);
        g.setColour(juce::Colours::white.withAlpha(hovered ? 0.9f : 0.55f));
        g.strokePath(arrow, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void showInputSourceMenu()
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<TrackRow> safeThis(this);

        juce::StringArray names;
        if (onGetHardwareInputNames) names = onGetHardwareInputNames();
        const int numIns = names.size();

        menu.addSectionHeader("Input Source");

        if (numIns <= 0)
        {
            juce::PopupMenu::Item none("(No audio inputs available)");
            none.isEnabled = false;
            menu.addItem(none);
        }
        else
        {
            const int  curFirst = track_.getInputFirstChannel();
            const bool curMono  = track_.isInputMono();

            auto shortName = [](const juce::String& n) -> juce::String
            {
                return n.length() > 24 ? n.substring(0, 22) + ".." : n;
            };

            for (int ch = 0; ch + 1 < numIns; ch += 2)
            {
                juce::PopupMenu::Item item("Stereo " + juce::String(ch + 1) + "/" + juce::String(ch + 2)
                                           + " \u2014 " + shortName(names[ch]));
                item.isTicked = (!curMono && curFirst == ch);
                const int first = ch;
                item.action = [safeThis, first]
                {
                    if (!safeThis) return;
                    safeThis->track_.setInputSource(first, false);
                    safeThis->repaint();
                };
                menu.addItem(item);
            }

            menu.addSeparator();

            for (int ch = 0; ch < numIns; ++ch)
            {
                juce::PopupMenu::Item item("Mono " + juce::String(ch + 1)
                                           + " \u2014 " + shortName(names[ch]));
                item.isTicked = (curMono && curFirst == ch);
                const int first = ch;
                item.action = [safeThis, first]
                {
                    if (!safeThis) return;
                    safeThis->track_.setInputSource(first, true);
                    safeThis->repaint();
                };
                menu.addItem(item);
            }
        }

        menu.showMenuAsync(juce::PopupMenu::Options()
            .withTargetComponent(this)
            .withTargetScreenArea(localAreaToGlobal(inputBounds_.toNearestInt())));
    }

    void drawAutomationButton(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                              bool visible, bool muted, bool hovered)
    {
        if (bounds.isEmpty()) return;
        auto& t = Theme::getInstance();
        const auto accent = track_.getColor();
        const bool hasAutomation = hasTrackAutomation();
        const float pulse = hasAutomation ? 0.5f + 0.5f * std::sin(autoPulsePhase_) : 0.0f;

        if (hasAutomation)
        {
            const float glowAlpha = visible ? 0.18f : 0.10f + pulse * 0.22f;
            g.setColour((muted ? juce::Colours::grey : accent).withAlpha(glowAlpha));
            g.fillRoundedRectangle(bounds.expanded(3.0f + pulse * 2.0f), 6.0f);
        }

        juce::Colour bg = visible ? accent.withAlpha(muted ? 0.25f : 0.78f)
                         : hasAutomation ? accent.withAlpha(muted ? 0.12f : 0.16f + pulse * 0.10f)
                         : hovered ? t.colors.controlHover
                                   : t.colors.controlIdle;
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(visible ? accent.brighter(0.35f).withAlpha(muted ? 0.35f : 0.75f)
                    : hasAutomation ? accent.withAlpha(muted ? 0.28f : 0.50f + pulse * 0.25f)
                    : t.colors.border);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        g.setColour(muted ? t.colors.textDisabled : (visible || hasAutomation ? juce::Colours::white : t.colors.textSecondary));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("A", bounds, juce::Justification::centred, false);
    }

    void drawMonitorButton(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                           bool isOn, bool hovered)
    {
        auto& t = Theme::getInstance();
        auto color = juce::Colour(0xFF06B6D4);

        if (isOn)
            g.setColour(color.darker(0.15f));
        else if (hovered)
            g.setColour(t.colors.controlHover);
        else
            g.setColour(t.colors.controlIdle);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(isOn ? color.withAlpha(0.5f) : t.colors.border);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        const auto iconColour = isOn ? juce::Colours::white : t.colors.textSecondary;
        auto r = bounds.reduced(5.0f);
        auto c = r.getCentre();
        g.setColour(iconColour);
        juce::Path ear;
        ear.startNewSubPath(c.x - 5.0f, c.y + 1.0f);
        ear.quadraticTo(c.x - 5.0f, c.y - 6.0f, c.x, c.y - 6.0f);
        ear.quadraticTo(c.x + 5.0f, c.y - 6.0f, c.x + 5.0f, c.y + 1.0f);
        g.strokePath(ear, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle(c.x - 6.0f, c.y - 0.5f, 2.5f, 5.5f, 1.2f);
        g.fillRoundedRectangle(c.x + 3.5f, c.y - 0.5f, 2.5f, 5.5f, 1.2f);
        g.drawLine(c.x - 1.8f, c.y + 4.2f, c.x + 2.0f, c.y + 2.2f, 1.3f);
        g.fillEllipse(c.x + 1.3f, c.y + 1.5f, 2.4f, 2.4f);
    }
    
    void drawAutomationTargetButton(juce::Graphics& g, const juce::Rectangle<float>& bounds, bool hovered)
    {
        if (bounds.isEmpty() || !track_.isAutomationVisible()) return;
        auto& t = Theme::getInstance();
        const auto label = track_.getActiveAutomationParameterId() == "track.pan" ? "Pan"
                         : track_.getActiveAutomationParameterId() == "track.tape_stop" ? "Tape"
                         : track_.getActiveAutomationParameterId() == "instrument.main" ? "Inst"
                         : track_.getActiveAutomationParameterId().startsWith("plugin.") ? "Plug"
                         : track_.getActiveAutomationParameterId().startsWith("clip.") && track_.getActiveAutomationParameterId().endsWith(".pitch_shift") ? "Pitch"
                         : track_.getActiveAutomationParameterId().startsWith("clip.") && track_.getActiveAutomationParameterId().endsWith(".time_stretch") ? "Strtch"
                         : "Vol";

        juce::Colour bg = hovered ? t.colors.controlHover : t.colors.surface.withAlpha(0.82f);
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 3.5f);
        g.setColour(track_.getColor().withAlpha(hovered ? 0.85f : 0.55f));
        g.drawRoundedRectangle(bounds.reduced(0.4f), 3.5f, 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        auto textArea = bounds.withTrimmedRight(10.0f).reduced(2.0f, 0.0f);
        g.drawText(label, textArea.toNearestInt(), juce::Justification::centred, false);

        // dropdown arrow
        auto c = juce::Point<float>(bounds.getRight() - 6.0f, bounds.getCentreY());
        juce::Path arrow;
        arrow.startNewSubPath(c.x - 2.5f, c.y - 1.2f);
        arrow.lineTo(c.x, c.y + 1.5f);
        arrow.lineTo(c.x + 2.5f, c.y - 1.2f);
        g.setColour(juce::Colours::white.withAlpha(hovered ? 0.95f : 0.60f));
        g.strokePath(arrow, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void showAutomationTargetMenu()
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<TrackRow> safeThis(this);
        const auto activeTarget = track_.getActiveAutomationParameterId();
        auto order = track_.getAutomationParameterOrder();

        menu.addItem(101, "Volume",     true, activeTarget == "track.volume");
        menu.addItem(102, "Pan",        true, activeTarget == "track.pan");
        menu.addItem(105, "Mute",       true, activeTarget == "track.mute");
        menu.addItem(106, "Solo",       true, activeTarget == "track.solo");
        menu.addItem(104, "Tape Stop",  true, activeTarget == "track.tape_stop");
        menu.addItem(103, "Instrument", true, activeTarget == "instrument.main");
        menu.addSeparator();
        juce::PopupMenu pluginMenu;
        bool hasPluginTargets = false;
        if (auto* chain = track_.getPluginChain())
        {
            for (int slot = 0; slot < chain->getNumSlots(); ++slot)
            {
                if (!chain->hasPlugin(slot)) continue;
                auto* plugin = chain->getSlot(slot);
                auto id = AutomationManagerCore::makePluginSlotMixId(slot);
                pluginMenu.addItem(1100 + slot,
                    juce::String(slot + 1) + ". " + (plugin ? plugin->getName() : juce::String("Plugin")) + " / Mix",
                    true,
                    activeTarget == id);
                hasPluginTargets = true;
            }
        }
        menu.addSubMenu("Plugin Slot Mix", pluginMenu, hasPluginTargets);

        juce::PopupMenu pluginParamsMenu;
        std::vector<juce::String> pluginParamKeys;
        bool hasPluginParamTargets = false;
        bool hasPluginSlotsForParams = false;
        if (auto* chain = track_.getPluginChain())
        {
            auto& bridge = apex::automation::AutomationArrangementBridge::getInstance();
            for (int slot = 0; slot < chain->getNumSlots(); ++slot)
            {
                if (!chain->hasPlugin(slot)) continue;
                hasPluginSlotsForParams = true;
                auto* plugin = chain->getSlot(slot);
                auto* processor = plugin ? plugin->getProcessor() : nullptr;

                juce::PopupMenu perPlugin;
                bool anyAutomatable = false;
                const auto pluginName = processor != nullptr ? processor->getName()
                                                             : (plugin ? plugin->getName() : juce::String("Plugin"));

                if (processor != nullptr)
                {
                    const auto& params = processor->getParameters();
                    for (int pi = 0; pi < params.size(); ++pi)
                    {
                        auto* p = params[pi];
                        if (p == nullptr || !p->isAutomatable()) continue;
                        anyAutomatable = true;

                        juce::String paramID;
                        if (auto* pwid = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                            paramID = pwid->paramID;
                        if (paramID.isEmpty())
                            paramID = "param" + juce::String(pi);

                        const auto fullKey = apex::automation::AutomationParameterKeyRegistry::pluginParamKey(
                            track_.getID(), slot, pluginName, paramID);
                        const bool recorded = bridge.hasPluginParamAutomation(track_.getID(), slot, pluginName, paramID);
                        pluginParamKeys.push_back(fullKey);
                        perPlugin.addItem(1500 + (int)pluginParamKeys.size() - 1,
                            p->getName(64) + (recorded ? "  [recorded]" : ""),
                            true,
                            activeTarget == fullKey);
                    }

                    if (!anyAutomatable)
                    {
                        const juce::String message = params.isEmpty()
                            ? "No parameters exposed by plugin"
                            : "No automatable parameters exposed";
                        perPlugin.addItem(1, message, false, false);
                    }
                }
                else
                {
                    perPlugin.addItem(1, "Processor unavailable", false, false);
                }

                pluginParamsMenu.addSubMenu(juce::String(slot + 1) + ". " + pluginName, perPlugin, anyAutomatable);
                hasPluginParamTargets = hasPluginParamTargets || anyAutomatable;
            }
        }
        if (!hasPluginSlotsForParams)
            pluginParamsMenu.addItem(1, "No loaded plugins", false, false);
        menu.addSubMenu("Plugin Parameters", pluginParamsMenu, hasPluginSlotsForParams && hasPluginParamTargets);

        juce::PopupMenu sendsMenu;
        std::vector<juce::String> sendKeys;
        bool hasSends = false;
        if (onGetRoutingGraph)
        {
            if (auto* graph = onGetRoutingGraph())
            {
                if (auto* sourceNode = graph->getNodeByTrackId(track_.getID()))
                {
                    auto& bridge = apex::automation::AutomationArrangementBridge::getInstance();
                    for (auto* conn : graph->getOutputConnections(sourceNode->id))
                    {
                        if (conn == nullptr || conn->type != ConnectionType::Send)
                            continue;
                        const auto fullKey = apex::automation::AutomationParameterKeyRegistry::trackSendLevelKey(track_.getID(), conn->id);
                        const auto bypassKey = apex::automation::AutomationParameterKeyRegistry::trackSendBypassKey(track_.getID(), conn->id);
                        const auto* destNode = graph->getNode(conn->destNodeId);
                        const juce::String destLabel = destNode != nullptr && destNode->name.isNotEmpty()
                            ? destNode->name
                            : conn->destNodeId;
                        const bool recorded = bridge.hasTrackSendLevelAutomation(track_.getID(), conn->id);
                        sendKeys.push_back(fullKey);
                        sendsMenu.addItem(1400 + (int)sendKeys.size() - 1,
                            juce::String(juce::CharPointer_UTF8("\xe2\x86\x92 ")) + destLabel + " Level" + (recorded ? "  [recorded]" : ""),
                            true,
                            activeTarget == fullKey);
                        const bool bypassRecorded = bridge.hasTrackSendBypassAutomation(track_.getID(), conn->id);
                        sendKeys.push_back(bypassKey);
                        sendsMenu.addItem(1400 + (int)sendKeys.size() - 1,
                            juce::String(juce::CharPointer_UTF8("\xe2\x86\x92 ")) + destLabel + " Bypass" + (bypassRecorded ? "  [recorded]" : ""),
                            true,
                            activeTarget == bypassKey);
                        hasSends = true;
                    }
                }
            }
        }
        if (!hasSends)
            sendsMenu.addItem(1, "No sends created", false, false);
        menu.addSubMenu("Sends", sendsMenu, hasSends);

        juce::PopupMenu clipMenu;
        bool hasClipTargets = false;
        std::vector<std::pair<juce::String,juce::String>> clipInfos; // {id, name}
        if (onGetAudioClipsOnTrack)
        {
            for (auto* clip : onGetAudioClipsOnTrack(track_.getID()))
            {
                if (clip == nullptr) continue;
                clipInfos.push_back({ clip->getID(), clip->getName().isNotEmpty() ? clip->getName() : ("Clip " + clip->getID().substring(0, 6)) });
            }
        }
        for (int ci = 0; ci < (int)clipInfos.size(); ++ci)
        {
            const auto& ce = clipInfos[(size_t)ci];
            const auto pitchId   = AutomationLaneCore::makeClipPitchParameterId(ce.first);
            const auto stretchId = AutomationLaneCore::makeClipStretchParameterId(ce.first);
            const int pitchItemId   = 8000 + ci * 2;
            const int stretchItemId = 8000 + ci * 2 + 1;
            if (clipInfos.size() == 1)
            {
                clipMenu.addItem(pitchItemId,   "Pitch Shift",  true, activeTarget == pitchId);
                // [FUTURE UPDATE] Time Stretch automation hidden until implemented:
                // clipMenu.addItem(stretchItemId, "Time Stretch", true, activeTarget == stretchId);
            }
            else
            {
                juce::PopupMenu perClip;
                perClip.addItem(pitchItemId,   "Pitch Shift",  true, activeTarget == pitchId);
                // [FUTURE UPDATE] Time Stretch automation hidden until implemented:
                // perClip.addItem(stretchItemId, "Time Stretch", true, activeTarget == stretchId);
                clipMenu.addSubMenu(ce.second, perClip);
            }
            hasClipTargets = true;
        }
        menu.addSubMenu("Clip Time / Pitch", clipMenu, hasClipTargets);
        const auto lastTouched = LastTouchedPluginParameterCore::getInstance().get();
        const bool canAddLastTouched = lastTouched.isValid() && lastTouched.trackId == track_.getID();
        menu.addItem(1300,
                     canAddLastTouched ? ("Add Lane: " + lastTouched.pluginDisplayName + " / " + lastTouched.parameterName)
                                       : "Add Lane: Last Touched Plugin Parameter",
                     canAddLastTouched,
                     false);
        menu.addSeparator();

        juce::PopupMenu reorder;
        const int activeIndex = order.indexOf(activeTarget);
        reorder.addItem(201, "Move Current Up",   activeIndex > 0);
        reorder.addItem(202, "Move Current Down", activeIndex >= 0 && activeIndex < order.size() - 1);
        menu.addSubMenu("Reorder List", reorder);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis, order, clipInfos, sendKeys, pluginParamKeys](int result)
        {
            if (!safeThis) return;
            if (result >= 8000 && result < 8000 + (int)clipInfos.size() * 2)
            {
                const int ci = (result - 8000) / 2;
                const bool isPitch = ((result - 8000) % 2) == 0;
                if (ci >= 0 && ci < (int)clipInfos.size())
                {
                    const auto& ce = clipInfos[(size_t)ci];
                    // [FUTURE UPDATE] Stretch selection disabled until implemented:
                    if (!isPitch) return; // stretch automation hidden for now
                    const auto paramId = AutomationLaneCore::makeClipPitchParameterId(ce.first);
                    safeThis->track_.setActiveAutomationParameterId(paramId);
                }
            }
            else if (result == 101) safeThis->track_.setActiveAutomationParameterId("track.volume");
            else if (result == 102) safeThis->track_.setActiveAutomationParameterId("track.pan");
            else if (result == 105) safeThis->track_.setActiveAutomationParameterId("track.mute");
            else if (result == 106) safeThis->track_.setActiveAutomationParameterId("track.solo");
            else if (result == 104) safeThis->track_.setActiveAutomationParameterId("track.tape_stop");
            else if (result == 103) safeThis->track_.setActiveAutomationParameterId("instrument.main");
            else if (result >= 1100 && result < 1200) safeThis->track_.setActiveAutomationParameterId(AutomationManagerCore::makePluginSlotMixId(result - 1100));
            else if (result >= 1500 && result < 1500 + (int)pluginParamKeys.size()) safeThis->track_.setActiveAutomationParameterId(pluginParamKeys[(size_t)(result - 1500)]);
            else if (result >= 1400 && result < 1400 + (int)sendKeys.size()) safeThis->track_.setActiveAutomationParameterId(sendKeys[(size_t)(result - 1400)]);
            else if (result == 1300)
            {
                const auto last = LastTouchedPluginParameterCore::getInstance().get();
                if (last.isValid() && last.trackId == safeThis->track_.getID())
                {
                    auto updated = safeThis->track_.getAutomationParameterOrder();
                    auto id = AutomationManagerCore::makePluginParameterId(last);
                    if (!updated.contains(id))
                    {
                        updated.add(id);
                        safeThis->track_.setAutomationParameterOrder(updated);
                    }
                    safeThis->track_.setActiveAutomationParameterId(id);
                    safeThis->track_.setAutomationVisible(true);
                }
            }
            else if (result == 201 || result == 202)
            {
                auto updated = order;
                const auto active = safeThis->track_.getActiveAutomationParameterId();
                const int idx = updated.indexOf(active);
                const int newIdx = result == 201 ? idx - 1 : idx + 1;
                if (idx >= 0 && newIdx >= 0 && newIdx < updated.size())
                {
                    updated.remove(idx);
                    updated.insert(newIdx, active);
                    safeThis->track_.setAutomationParameterOrder(updated);
                }
            }
            // Any lane selection should make automation visible
            if (result >= 101 && result != 201 && result != 202)
                safeThis->track_.setAutomationVisible(true);
            safeThis->repaint();
            if (safeThis->onAutomationLaneStateChanged)
                safeThis->onAutomationLaneStateChanged(safeThis->track_.getID());
        });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackRow)
};

// Track list panel with resizable right edge
class TrackList : public juce::Component,
                  public TrackManager::Listener
{
public:
    std::function<void(int deltaX)> onWidthDrag;  // callback for resize
    std::function<void(const TrackID&, const juce::ModifierKeys&)> onTrackSelectedWithModifiers;
    void setScrollOffset(int y)
    {
        y = juce::jmax(0, y);
        if (scrollOffsetY_ == y)
            return;
        scrollOffsetY_ = y;
        resized();
        repaint(0, 80, getWidth(), juce::jmax(0, getHeight() - 80));
    }
    void setMasterRowHeight(int h)
    {
        if (trackManager_.hasMasterTrack() && trackRows_.size() > 0)
            trackRows_[0]->setDesiredHeight(h);
    }
    int getNumRows() const { return trackManager_.hasMasterTrack()
                                   ? juce::jmax(0, trackRows_.size() - 1)
                                   : trackRows_.size(); }
    void setRowHeight(int index, int h)
    {
        // Offset by 1 to skip master row at index 0
        int actual = trackManager_.hasMasterTrack() ? index + 1 : index;
        if (actual >= 0 && actual < trackRows_.size())
            trackRows_[actual]->setDesiredHeight(h);
    }

    TrackList(TrackManager& trackManager)
        : trackManager_(trackManager)
    {
        trackManager_.addListener(this);
        rebuildTrackRows();
    }

    ~TrackList()
    {
        trackManager_.removeListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        g.fillAll(theme.colors.backgroundLight);

        // Top 48px — matches the ArrangementViewCore toolbar height
        g.setColour(theme.colors.backgroundDark);
        g.fillRect(0, 0, getWidth(), 48);
        g.setColour(theme.colors.text);
        g.setFont(theme.fonts.bold);
        g.drawText("Tracks", 8, 0, getWidth() - 56, 48,
                   juce::Justification::centredLeft);

        // Quick-add track button (top header)
        auto addBounds = juce::Rectangle<int>(getWidth() - 44, 10, 34, 28);
        g.setColour(theme.colors.controlIdle);
        g.fillRoundedRectangle(addBounds.toFloat(), 6.0f);
        g.setColour(theme.colors.border);
        g.drawRoundedRectangle(addBounds.toFloat(), 6.0f, 1.0f);
        g.setColour(theme.colors.text);
        g.setFont(theme.fonts.bold.withHeight(18.0f));
        g.drawText("+", addBounds, juce::Justification::centred);

        // Next 32px — matches the ruler height
        g.setColour(theme.colors.backgroundDark.darker(0.08f));
        g.fillRect(0, 48, getWidth(), 32);
        g.setColour(theme.colors.border.withAlpha(0.45f));
        g.fillRect(0, 79, getWidth(), 1); // bottom border matches ruler bottom

        // Right edge resize handle
        g.setColour(dragHover_ ? theme.colors.accent.withAlpha(0.5f)
                               : theme.colors.border.withAlpha(0.3f));
        g.fillRect(getWidth() - 3, 0, 3, getHeight());
    }

    void resized() override
    {
        // Direct Y calculation — mirrors ArrangementViewCore::arrangementContentTop() exactly
        int y = 80 - scrollOffsetY_;
        for (auto* row : trackRows_)
        {
            row->setBounds(0, y, getWidth(), row->getDesiredHeight());
            y += row->getDesiredHeight();
        }
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        g.setColour(theme.colors.backgroundDark);
        g.fillRect(0, 0, getWidth(), 48);
        g.setColour(theme.colors.text);
        g.setFont(theme.fonts.bold);
        g.drawText("Tracks", 8, 0, getWidth() - 56, 48, juce::Justification::centredLeft);

        // Quick-add track button (top header) - repaint-safe overlay
        {
            auto addBounds = juce::Rectangle<int>(getWidth() - 44, 10, 34, 28);
            g.setColour(theme.colors.controlIdle);
            g.fillRoundedRectangle(addBounds.toFloat(), 6.0f);
            g.setColour(theme.colors.border);
            g.drawRoundedRectangle(addBounds.toFloat(), 6.0f, 1.0f);
            g.setColour(theme.colors.text);
            g.setFont(theme.fonts.bold.withHeight(18.0f));
            g.drawText("+", addBounds, juce::Justification::centred);
        }
        g.setColour(theme.colors.backgroundDark.darker(0.08f));
        g.fillRect(0, 48, getWidth(), 32);
        g.setColour(theme.colors.border.withAlpha(0.45f));
        g.fillRect(0, 79, getWidth(), 1);
        g.setColour(dragHover_ ? theme.colors.accent.withAlpha(0.5f)
                               : theme.colors.border.withAlpha(0.3f));
        g.fillRect(getWidth() - 3, 0, 3, getHeight());

        // Drop indicator for track reorder / folder target
        if (reorderSource_ != nullptr
            && (dropIndicatorY_ >= 0 || folderDropTargetTrackId_.isNotEmpty()))
        {
            // Dim the source row to show it's being moved
            auto srcBounds = juce::Rectangle<int>(0, reorderSource_->getY(),
                getWidth(), reorderSource_->getHeight());
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRect(srcBounds);

            // ── Folder target mode (regular track → creates bus, folder → adopts)
            if (folderDropTargetTrackId_.isNotEmpty())
            {
                TrackRow* targetRow = nullptr;
                for (auto* r : trackRows_)
                    if (r != nullptr && r->getTrackID() == folderDropTargetTrackId_)
                    { targetRow = r; break; }

                if (targetRow != nullptr)
                {
                    auto destBounds = juce::Rectangle<int>(0, targetRow->getY(),
                        getWidth(), targetRow->getHeight()).toFloat();
                    auto gold = juce::Colour(0xFFFFB300);

                    // Outer glow halo
                    for (int i = 3; i >= 1; --i)
                    {
                        g.setColour(gold.withAlpha(0.18f / (float)i));
                        g.drawRoundedRectangle(destBounds.expanded((float)i * 2.f), 4.f, 2.f);
                    }

                    // Inner tint + solid border
                    g.setColour(gold.withAlpha(0.16f));
                    g.fillRect(destBounds);
                    g.setColour(gold.withAlpha(0.9f));
                    g.drawRoundedRectangle(destBounds.reduced(0.5f), 4.f, 2.f);

                    // Gold top-edge band (mirrors folder-bus/master styling)
                    g.fillRect(destBounds.getX(), destBounds.getY(), destBounds.getWidth(), 3.f);

                    // Folder icon centered on the row
                    float fx = destBounds.getCentreX();
                    float fy = destBounds.getCentreY();
                    juce::Path folder;
                    folder.addRoundedRectangle(fx - 9.f, fy - 5.f, 18.f, 11.f, 2.f);
                    folder.addRectangle(fx - 9.f, fy - 9.f, 8.f, 5.f);
                    g.setColour(juce::Colour(0xFFFFD54F));
                    g.fillPath(folder);

                    // Ghost row preview follows cursor
                    if (ghostRowY_ >= 0)
                    {
                        auto ghostBounds = juce::Rectangle<int>(0, ghostRowY_, getWidth(),
                            reorderSource_->getDesiredHeight());
                        g.saveState();
                        g.setOpacity(0.45f);
                        g.reduceClipRegion(ghostBounds);
                        g.setOrigin(0, ghostRowY_ - reorderSource_->getY());
                        reorderSource_->paint(g);
                        g.restoreState();

                        g.setColour(gold.withAlpha(0.6f));
                        g.drawRoundedRectangle(ghostBounds.toFloat(), 4.f, 1.5f);
                    }
                }
                return; // folder-target mode — skip the insertion-line UI below
            }

            // Highlight the full destination row slot
            int targetIdx = getDropTargetIndex(dropIndicatorY_);
            int masterOff = trackManager_.hasMasterTrack() ? 1 : 0;
            int targetRowIdx = targetIdx + masterOff;
            if (targetRowIdx >= 0 && targetRowIdx < (int)trackRows_.size())
            {
                auto* targetRow = trackRows_[targetRowIdx];
                auto destBounds = juce::Rectangle<int>(0, targetRow->getY(),
                    getWidth(), targetRow->getHeight());
                g.setColour(theme.colors.accent.withAlpha(0.18f));
                g.fillRect(destBounds);
                g.setColour(theme.colors.accent.withAlpha(0.7f));
                g.drawRect(destBounds.toFloat(), 2.f);
            }

            // Accent drop line at insertion point
            g.setColour(theme.colors.accent);
            g.fillRect(4, dropIndicatorY_ - 1, getWidth() - 8, 3);

            // Arrow indicators on both sides
            {
                float ly = (float)dropIndicatorY_;
                juce::Path arrow;
                arrow.addTriangle(0.f, ly - 6.f, 8.f, ly, 0.f, ly + 6.f);
                g.fillPath(arrow);
                juce::Path arrowR;
                arrowR.addTriangle((float)getWidth(), ly - 6.f,
                                   (float)getWidth() - 8.f, ly,
                                   (float)getWidth(), ly + 6.f);
                g.fillPath(arrowR);
            }

            // Ghost row preview
            if (ghostRowY_ >= 0)
            {
                auto ghostBounds = juce::Rectangle<int>(0, ghostRowY_, getWidth(),
                    reorderSource_->getDesiredHeight());
                g.saveState();
                g.setOpacity(0.45f);
                g.reduceClipRegion(ghostBounds);
                g.setOrigin(0, ghostRowY_ - reorderSource_->getY());
                reorderSource_->paint(g);
                g.restoreState();

                g.setColour(theme.colors.accent.withAlpha(0.5f));
                g.drawRoundedRectangle(ghostBounds.toFloat(), 4.f, 1.5f);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        bool wasHover = dragHover_;
        dragHover_ = (e.x >= getWidth() - 5);
        if (wasHover != dragHover_)
        {
            setMouseCursor(dragHover_ ? juce::MouseCursor::LeftRightResizeCursor
                                      : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.y < 48)
        {
            auto addBounds = juce::Rectangle<int>(getWidth() - 44, 10, 34, 28);
            if (addBounds.contains(e.getPosition()))
            {
                if (onQuickAddTrackRequested) onQuickAddTrackRequested();
                return;
            }
        }
        if (e.x >= getWidth() - 5)
            dragStartX_ = e.getScreenX();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        // Double-click in empty list space (below the last track row) adds a new track.
        if (e.y <= 80) return; // ignore header/ruler
        if (trackRows_.size() > 0)
        {
            auto bottom = trackRows_.getLast()->getBottom();
            if (e.y > bottom)
            {
                if (onQuickAddTrackRequested) onQuickAddTrackRequested();
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragStartX_ >= 0 && onWidthDrag)
        {
            int delta = e.getScreenX() - dragStartX_;
            dragStartX_ = e.getScreenX();
            onWidthDrag(delta);
        }
    }

    void mouseUp(const juce::MouseEvent&) override { dragStartX_ = -1; }

    void mouseExit(const juce::MouseEvent&) override
    {
        dragHover_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

    TrackRow* getRow(int index) const
    {
        int actual = trackManager_.hasMasterTrack() ? index + 1 : index;
        return (actual >= 0 && actual < trackRows_.size()) ? trackRows_[actual] : nullptr;
    }

    std::function<void(int index, int newHeight)> onRowHeightChanged;
    std::function<void(const TrackID&)>          onTrackSelected;
    std::function<void()>                       onQuickAddTrackRequested;
    std::function<void(const TrackID&)>          onDeleteTrackRequested;
    std::function<void(const TrackID&)>          onDuplicateTrackRequested;
    std::function<void(const TrackID&)>          onToggleMuteRequested;
    std::function<void(const TrackID&)>          onOpenPianoRollRequested;
    std::function<void(const TrackID&)>          onOpenInputTrimRequested;
    /** Returns the active hardware input channel names for per-track input selectors. */
    std::function<juce::StringArray()>           onGetHardwareInputNamesRequested;
    std::function<void(const TrackID&)>          onToggleSoloRequested;
    std::function<void(const TrackID&)>          onToggleArmRequested;
    std::function<void(const TrackID&, bool)>    onSetMonitoringRequested;
    std::function<void(const TrackID&, bool)>    onAutomationMuteChanged;
    std::function<void(const TrackID&, const juce::String&, bool)> onAutomationTargetMuteChanged;
    std::function<void(const TrackID&, const juce::String&)> onAutomationTargetSoloRequested;
    std::function<void(const TrackID&)>          onAutomationClearRequested;
    std::function<void(const TrackID&, const juce::String&)> onAutomationTargetClearRequested;
    std::function<void(const TrackID&, const juce::String&)> onCreateSequenceRequested;
    std::function<bool(const TrackID&)>          hasAutomationData;
    /** Fired when a track's automation lane visibility or active parameter changes. */
    std::function<void(const TrackID&)>          onAutomationLaneStateChanged;
    std::function<void(const TrackID&, const juce::String&)> onRenameTrackRequested;
    std::function<void(const TrackID&, juce::Colour)> onSetColorRequested;
    std::function<void(const TrackID& draggedTrackId, const TrackID& targetTrackId)> onFolderDropRequested;

    // Multi-selection bulk callbacks (wired from MainComponent)
    std::function<bool(const TrackID&)>  onGetIsMultiSelectedCallback;
    std::function<void(const TrackID&)>  onMultiRenameCallback;
    std::function<void(const TrackID&)>  onMultiDeleteCallback;
    std::function<void(const TrackID&)>  onMultiMuteCallback;
    std::function<void(const TrackID&)>  onMultiSoloCallback;
    std::function<void(const TrackID&)>  onMultiArmCallback;
    std::function<void(const TrackID&)>  onMultiColorCallback;
    std::function<void(const TrackID& folderTrackId, bool expanded)> onFolderCollapseChanged;
    std::function<void(const TrackID& trackId, int targetIndex)> onTrackReorderRequested;
    /** Callback: a plugin was drag-dropped onto a track row from another track. */
    std::function<void(const TrackID& srcTrack, int srcSlot, const TrackID& destTrack)> onPluginDropReceived;
    /** Callback: returns all audio clips on a track (for automation menus). */
    std::function<juce::Array<Clip*>(const TrackID&)> onGetAudioClipsOnTrack;
    std::function<RoutingGraph*()> onGetRoutingGraph;

    void setCollapsedFolders(const std::unordered_set<TrackID>& collapsedFolders)
    {
        if (collapsedFolders_ == collapsedFolders)
            return;

        collapsedFolders_ = collapsedFolders;
        rebuildTrackRows();
    }

    const std::unordered_set<TrackID>& getCollapsedFolders() const noexcept { return collapsedFolders_; }

    /** Push Bubblegum send feedback to all track rows.
     *  sourceId: currently active source, feedbackTargets: { trackId, sendLevel } pairs. */
    void applyBubblegumFeedback(const TrackID& sourceId,
                                const std::vector<std::pair<TrackID, float>>& feedbackTargets)
    {
        for (auto* row : trackRows_)
        {
            auto tid = row->getTrackID();
            if (tid == sourceId)
            {
                row->setBubblegumFeedback(true, false);
                continue;
            }
            bool found = false;
            float level = 0.f;
            for (auto& [ftId, ftLevel] : feedbackTargets)
            {
                if (ftId == tid) { found = true; level = ftLevel; break; }
            }
            row->setBubblegumFeedback(false, found, level);
        }
    }

    /** Clear all Bubblegum feedback from track rows. */
    void clearBubblegumFeedback()
    {
        for (auto* row : trackRows_)
            row->setBubblegumFeedback(false, false);
    }

    void clearSelection()
    {
        for (auto* row : trackRows_)
            row->setSelected(false);
    }

    /** Select a track by ID — updates visual state only (does not fire onTrackSelected). */
    void selectTrackById(const TrackID& id)
    {
        for (auto* row : trackRows_)
            row->setSelected(row->getTrackID() == id);
    }

    /** Multi-selection: mark all rows in the set as selected, others deselected. */
    void applyMultiSelectionVisual(const std::vector<juce::String>& selectedIds)
    {
        std::unordered_set<juce::String> idSet(selectedIds.begin(), selectedIds.end());
        for (auto* row : trackRows_)
            row->setSelected(idSet.count(row->getTrackID()) > 0);
    }

    /** Return ordered list of all visible track IDs (for Shift-click range). */
    std::vector<juce::String> getVisibleTrackIds() const
    {
        std::vector<juce::String> ids;
        ids.reserve((size_t)trackRows_.size());
        for (auto* row : trackRows_)
            ids.push_back(row->getTrackID());
        return ids;
    }

private:
    TrackManager& trackManager_;
    juce::OwnedArray<TrackRow> trackRows_;
    int  dragStartX_ = -1;
    bool dragHover_  = false;
    int  scrollOffsetY_ = 0;
    TrackRow* reorderSource_ = nullptr;
    int  dropIndicatorY_ = -1;
    int  ghostRowY_ = -1;
    TrackID folderDropTargetTrackId_;
    std::unordered_set<TrackID> collapsedFolders_;

    TrackRow* findFolderDropTargetRow(juce::Point<int> localPos, TrackRow* dragSource) const
    {
        for (auto* row : trackRows_)
        {
            if (row == nullptr || row == dragSource || row->isMasterRow())
                continue;

            auto rowLocalPos = row->getLocalPoint(this, localPos);
            if (!row->getLocalBounds().contains(rowLocalPos))
                continue;

            if (row->getFolderDropTargetBounds().toNearestInt().contains(rowLocalPos))
                return row;
        }

        return nullptr;
    }
    
    void rebuildTrackRows()
    {
        trackRows_.clear();

        // Master track row always first (matches arrangement view)
        if (trackManager_.hasMasterTrack())
        {
            auto* masterRow = new TrackRow(*trackManager_.getMasterTrack());
            masterRow->setDesiredHeight(108);
            masterRow->onSelectedWithModifiers = [this](TrackRow* clicked, const juce::ModifierKeys& mods)
            {
                if (!mods.isCtrlDown() && !mods.isCommandDown() && !mods.isShiftDown())
                    for (auto* r : trackRows_)
                        r->setSelected(r == clicked);
                else
                    clicked->setSelected(true);
                if (onTrackSelectedWithModifiers)
                    onTrackSelectedWithModifiers(clicked->getTrackID(), mods);
                else if (onTrackSelected)
                    onTrackSelected(clicked->getTrackID());
            };
            masterRow->onPluginDropReceived = [this](const TrackID& src, int slot, const TrackID& dest)
            {
                if (onPluginDropReceived) onPluginDropReceived(src, slot, dest);
            };
            masterRow->onToggleMute = [this](const TrackID& id) { if (onToggleMuteRequested) onToggleMuteRequested(id); };
            masterRow->onOpenPianoRoll = [this](const TrackID& id) { if (onOpenPianoRollRequested) onOpenPianoRollRequested(id); };
            masterRow->onOpenInputTrim = [this](const TrackID& id) { if (onOpenInputTrimRequested) onOpenInputTrimRequested(id); };
            masterRow->onRenameTrack = [this](const TrackID& id, const juce::String& name) { if (onRenameTrackRequested) onRenameTrackRequested(id, name); };
            masterRow->onAutomationMuteChanged = [this](const TrackID& id, bool muted) { if (onAutomationMuteChanged) onAutomationMuteChanged(id, muted); };
            masterRow->onAutomationTargetMuteChanged = [this](const TrackID& id, const juce::String& paramId, bool muted) { if (onAutomationTargetMuteChanged) onAutomationTargetMuteChanged(id, paramId, muted); };
            masterRow->onAutomationTargetSoloRequested = [this](const TrackID& id, const juce::String& paramId) { if (onAutomationTargetSoloRequested) onAutomationTargetSoloRequested(id, paramId); };
            masterRow->onAutomationClearRequested = [this](const TrackID& id) { if (onAutomationClearRequested) onAutomationClearRequested(id); };
            masterRow->onAutomationTargetClearRequested = [this](const TrackID& id, const juce::String& paramId) { if (onAutomationTargetClearRequested) onAutomationTargetClearRequested(id, paramId); };
            masterRow->onCreateSequenceRequested = [this](const TrackID& id, const juce::String& paramId) { if (onCreateSequenceRequested) onCreateSequenceRequested(id, paramId); };
            masterRow->hasAutomationData = [this](const TrackID& id) { return hasAutomationData ? hasAutomationData(id) : false; };
            masterRow->onAutomationLaneStateChanged = [this](const TrackID& id) { if (onAutomationLaneStateChanged) onAutomationLaneStateChanged(id); };
            masterRow->onGetAudioClipsOnTrack = [this](const TrackID& id) -> juce::Array<Clip*> { return onGetAudioClipsOnTrack ? onGetAudioClipsOnTrack(id) : juce::Array<Clip*>{}; };
            masterRow->onGetRoutingGraph = [this]() -> RoutingGraph* { return onGetRoutingGraph ? onGetRoutingGraph() : nullptr; };
            trackRows_.add(masterRow);
            addAndMakeVisible(masterRow);
        }

        for (int i = 0; i < trackManager_.getNumTracks(); ++i)
        {
            auto* track = trackManager_.getTrack(i);
            if (track == nullptr)
                continue;

            // Determine nesting depth + whether any ancestor folder is collapsed
            int depth = 0;
            bool hiddenByCollapsedAncestor = false;
            TrackID pid = track->getParentTrackID();
            while (pid.isNotEmpty())
            {
                if (collapsedFolders_.count(pid) > 0)
                    hiddenByCollapsedAncestor = true;
                ++depth;
                auto* parent = trackManager_.getTrack(pid);
                if (parent == nullptr) break;
                pid = parent->getParentTrackID();
                if (depth > 32) break; // safety
            }
            if (hiddenByCollapsedAncestor)
                continue;

            {
                const int visibleRowIndex = trackRows_.size() - (trackManager_.hasMasterTrack() ? 1 : 0);
                auto* row = new TrackRow(*track);
                row->setIndentLevel(depth);
                if (row->isFolderBusRow())
                    row->setFolderExpanded(collapsedFolders_.count(track->getID()) == 0);
                row->onHeightDrag = [this, visibleRowIndex](TrackRow* r, int delta)
                {
                    r->setDesiredHeight(r->getDesiredHeight() + delta);
                    if (onRowHeightChanged)
                        onRowHeightChanged(visibleRowIndex, r->getDesiredHeight());
                    resized();
                };
                row->onFolderToggled = [this](const TrackID& fid, bool expanded)
                {
                    if (expanded) collapsedFolders_.erase(fid);
                    else          collapsedFolders_.insert(fid);
                    if (onFolderCollapseChanged)
                        onFolderCollapseChanged(fid, expanded);
                    rebuildTrackRows();
                };
                row->onSelectedWithModifiers = [this](TrackRow* clicked, const juce::ModifierKeys& mods)
                {
                    if (!mods.isCtrlDown() && !mods.isCommandDown() && !mods.isShiftDown())
                        for (auto* r : trackRows_)
                            r->setSelected(r == clicked);
                    else
                        clicked->setSelected(true);
                    if (onTrackSelectedWithModifiers)
                        onTrackSelectedWithModifiers(clicked->getTrackID(), mods);
                    else if (onTrackSelected)
                        onTrackSelected(clicked->getTrackID());
                };
                row->onPluginDropReceived = [this](const TrackID& src, int slot, const TrackID& dest)
                {
                    if (onPluginDropReceived) onPluginDropReceived(src, slot, dest);
                };
                row->onToggleMute = [this](const TrackID& id) { if (onToggleMuteRequested) onToggleMuteRequested(id); };
                row->onOpenPianoRoll = [this](const TrackID& id) { if (onOpenPianoRollRequested) onOpenPianoRollRequested(id); };
                row->onOpenInputTrim = [this](const TrackID& id) { if (onOpenInputTrimRequested) onOpenInputTrimRequested(id); };
                row->onGetHardwareInputNames = [this]() -> juce::StringArray
                {
                    return onGetHardwareInputNamesRequested ? onGetHardwareInputNamesRequested()
                                                            : juce::StringArray();
                };
                row->onToggleSolo = [this](const TrackID& id) { if (onToggleSoloRequested) onToggleSoloRequested(id); };
                row->onToggleArm = [this](const TrackID& id) { if (onToggleArmRequested) onToggleArmRequested(id); };
                row->onSetMonitoring = [this](const TrackID& id, bool enabled) { if (onSetMonitoringRequested) onSetMonitoringRequested(id, enabled); };
                row->onRenameTrack = [this](const TrackID& id, const juce::String& name) { if (onRenameTrackRequested) onRenameTrackRequested(id, name); };
                row->onAutomationMuteChanged = [this](const TrackID& id, bool muted) { if (onAutomationMuteChanged) onAutomationMuteChanged(id, muted); };
                row->onAutomationTargetMuteChanged = [this](const TrackID& id, const juce::String& paramId, bool muted) { if (onAutomationTargetMuteChanged) onAutomationTargetMuteChanged(id, paramId, muted); };
                row->onAutomationTargetSoloRequested = [this](const TrackID& id, const juce::String& paramId) { if (onAutomationTargetSoloRequested) onAutomationTargetSoloRequested(id, paramId); };
                row->onAutomationClearRequested = [this](const TrackID& id) { if (onAutomationClearRequested) onAutomationClearRequested(id); };
                row->onAutomationTargetClearRequested = [this](const TrackID& id, const juce::String& paramId) { if (onAutomationTargetClearRequested) onAutomationTargetClearRequested(id, paramId); };
                row->onCreateSequenceRequested = [this](const TrackID& id, const juce::String& paramId) { if (onCreateSequenceRequested) onCreateSequenceRequested(id, paramId); };
                row->hasAutomationData = [this](const TrackID& id) { return hasAutomationData ? hasAutomationData(id) : false; };
                row->onAutomationLaneStateChanged = [this](const TrackID& id) { if (onAutomationLaneStateChanged) onAutomationLaneStateChanged(id); };
                row->onGetAudioClipsOnTrack = [this](const TrackID& id) -> juce::Array<Clip*> { return onGetAudioClipsOnTrack ? onGetAudioClipsOnTrack(id) : juce::Array<Clip*>{}; };
                row->onGetRoutingGraph = [this]() -> RoutingGraph* { return onGetRoutingGraph ? onGetRoutingGraph() : nullptr; };
                row->onColorChanged = [this](TrackRow* r, juce::Colour c)
                {
                    if (onSetColorRequested)
                        onSetColorRequested(r->getTrackID(), c);
                };
                row->onGetIsMultiSelected = [this](const TrackID& id) -> bool
                {
                    if (!onGetIsMultiSelectedCallback) return false;
                    return onGetIsMultiSelectedCallback(id);
                };
                row->onMultiRenameRequested  = [this](const TrackID& id) { if (onMultiRenameCallback)  onMultiRenameCallback(id); };
                row->onMultiDeleteRequested  = [this](const TrackID& id) { if (onMultiDeleteCallback)  onMultiDeleteCallback(id); };
                row->onMultiMuteRequested    = [this](const TrackID& id) { if (onMultiMuteCallback)    onMultiMuteCallback(id); };
                row->onMultiSoloRequested    = [this](const TrackID& id) { if (onMultiSoloCallback)    onMultiSoloCallback(id); };
                row->onMultiArmRequested     = [this](const TrackID& id) { if (onMultiArmCallback)     onMultiArmCallback(id); };
                row->onMultiColorRequested   = [this](const TrackID& id) { if (onMultiColorCallback)   onMultiColorCallback(id); };
                row->onReorderDrag = [this](TrackRow* src, const juce::MouseEvent& e)
                {
                    reorderSource_ = src;
                    auto localPt = e.getEventRelativeTo(this).getPosition();
                    if (auto* targetRow = findFolderDropTargetRow(localPt, src))
                    {
                        folderDropTargetTrackId_ = targetRow->getTrackID();
                        dropIndicatorY_ = -1;
                        ghostRowY_ = targetRow->getY();
                    }
                    else
                    {
                        folderDropTargetTrackId_ = {};
                        updateDropIndicator(localPt.y);
                        ghostRowY_ = localPt.y - src->getDesiredHeight() / 2;
                    }
                    repaint();
                };
                row->onDuplicateTrack = [this](const TrackID& id)
                {
                    if (onDuplicateTrackRequested)
                        onDuplicateTrackRequested(id);
                    else if (auto* t = trackManager_.getTrack(id))
                        trackManager_.createTrack(t->getName() + " (copy)");
                };
                row->onDeleteTrack = [this](const TrackID& id)
                {
                    if (onDeleteTrackRequested)
                        onDeleteTrackRequested(id);
                    else
                        trackManager_.deleteTrack(id);
                };
                row->onReorderDrop = [this](TrackRow* src)
                {
                    // Capture data before any rebuild can destroy src
                    TrackID srcId = src->getTrackID();
                    TrackID folderTargetId = folderDropTargetTrackId_;
                    int targetIdx = (dropIndicatorY_ >= 0) ? getDropTargetIndex(dropIndicatorY_) : -1;
                    int srcIdx = trackManager_.getTrackIndex(srcId);

                    // Clear visual state immediately
                    reorderSource_ = nullptr;
                    dropIndicatorY_ = -1;
                    ghostRowY_ = -1;
                    folderDropTargetTrackId_ = {};
                    repaint();

                    if (folderTargetId.isNotEmpty() && onFolderDropRequested)
                    {
                        juce::MessageManager::callAsync([cb = onFolderDropRequested, srcId, folderTargetId]()
                        {
                            if (cb) cb(srcId, folderTargetId);
                        });
                        return;
                    }

                    // Defer the move so mouseUp finishes before rows are rebuilt
                    if (srcIdx >= 0 && targetIdx >= 0 && srcIdx != targetIdx)
                    {
                        auto reorderRequested = onTrackReorderRequested;
                        auto& tm = trackManager_;
                        juce::MessageManager::callAsync([reorderRequested, &tm, srcId, targetIdx]()
                        {
                            if (reorderRequested)
                                reorderRequested(srcId, targetIdx);
                            else
                                tm.moveTrack(srcId, targetIdx);
                        });
                    }
                };
                trackRows_.add(row);
                addAndMakeVisible(row);
            }
        }
        
        resized();
    }
    
    // TrackManager::Listener
    void trackAdded(Track*) override { rebuildTrackRows(); }
    void trackRemoved(const TrackID&) override { rebuildTrackRows(); }
    void trackOrderChanged() override { rebuildTrackRows(); }

    // Reorder helpers
    void updateDropIndicator(int localY)
    {
        int masterOff = trackManager_.hasMasterTrack() ? 1 : 0;
        int bestY = -1;
        int bestDist = INT_MAX;
        // Snap to gaps between regular track rows (skip master)
        for (int i = masterOff; i <= (int)trackRows_.size(); ++i)
        {
            int edgeY = (i < (int)trackRows_.size()) ? trackRows_[i]->getY()
                                                      : (trackRows_.getLast()->getBottom());
            int dist = std::abs(localY - edgeY);
            if (dist < bestDist) { bestDist = dist; bestY = edgeY; }
        }
        dropIndicatorY_ = bestY;
    }

    int getDropTargetIndex(int indicatorY) const
    {
        int masterOff = trackManager_.hasMasterTrack() ? 1 : 0;
        for (int i = masterOff; i < (int)trackRows_.size(); ++i)
        {
            if (indicatorY <= trackRows_[i]->getY() + trackRows_[i]->getHeight() / 2)
                return trackManager_.getTrackIndex(trackRows_[i]->getTrackID());
        }
        return trackManager_.getNumTracks() - 1;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackList)
};

} // namespace DAW
