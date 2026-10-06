#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <unordered_set>
#include "../ThemeCore/Theme.h"
#include "../ThemeCore/ApexLivingSeal.h"
#include "CursorThemeCore.h"
#include "../TrackCore/Track.h"
#include "../TrackCore/TrackReorderCore.h"
#include "../ControlsCore/ModernControls.h"
#include "TrackColorPalette.h"
#include "TrackSelectionVisualCore.h"
#include "ApexPresentationClock.h"
#include "InputTrimFloatingPanel.h"
#include "../AnalogVuMeterCore/CompactVuNeedle.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../AutomationCore/LastTouchedPluginParameterCore.h"
#include "../Automation/AutomationParameterKeyCore.h"
#include "../Automation/AutomationArrangementBridgeCore.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../DiagnosticsCore/TimelinePaintMetrics.h"

namespace DAW {

// Single track row
class TrackRow : public juce::Component,
                 public juce::TooltipClient,
                 public Track::Listener,
                 public juce::DragAndDropTarget
{
public:
    TrackRow(Track& track)
        : track_(track)
    {
        track_.addListener(this);
        setOpaque(true);
        setBufferedToImage(true);
        setSize(200, 72);
        // DISABLED (product decision): the per-row trim VU needle looked like
        // a non-interactive knob and confused users. The trim level remains
        // visible in the InputTrimFloatingPanel and the mixer strip.
        // Re-enable by restoring the two lines below.
        // vu_.setSource(&track_.getInputMeter());
        // addAndMakeVisible(vu_);
        if (track_.isMaster())
            DBG("[TrackRow] Master UI solo hidden = true");

        isFolderBus_ = (track_.getRole() == TrackRole::FolderBus);
        selectionVisual_.setAccentColour(track_.isMaster() ? masterAccentColour()
                                                            : track_.getColor());
    }

    /** MASTER identity accent — dark magenta foundation, not gold. */
    static juce::Colour masterAccentColour()
    {
        return Theme::getInstance().apex.color.magentaDeep;
    }

    juce::String getTooltip() override { return currentTooltip_; }

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
    std::function<void(const TrackID&)> onExclusiveMute;
    std::function<void(const TrackID&)> onToggleSolo;
    std::function<void(const TrackID&)> onToggleArm;
    std::function<void(const TrackID&, bool)> onSetMonitoring;
    std::function<void(const TrackID&, const juce::String&)> onRenameTrack;
    std::function<void(const TrackID&)> onOpenPianoRoll;
    std::function<void(const TrackID&)> onPreFaderToggleRequested;
    std::function<void(const TrackID&)> onOpenInputTrim;
    std::function<void(const TrackID&)> onConvertFolderToTrack;
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

    ~TrackRow() override
    {
        // DISABLED (product decision): see constructor — VU needle hidden.
        // vu_.clearSource();
        track_.removeListener(this);
    }

    // Instantly repaint when track color/name/state changes
    void trackPropertyChanged(Track*) override
    {
        isFolderBus_ = (track_.getRole() == TrackRole::FolderBus);
        selectionVisual_.setAccentColour(track_.isMaster() ? masterAccentColour()
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
        // While Quick Send mode is active, suppress the neon selection outline —
        // receiving rows are highlighted by their send-colour feedback instead.
        if (!quickSendModeActive_)
            selectionVisual_.setSelected(s);

        repaint();
        if (onPresentationDemandChanged)
            onPresentationDemandChanged();
    }
    bool isSelected() const { return selected_; }

    /** Quick Send mode source highlight (gold aura). */
    void setQuickSendSource(bool on)
    {
        if (quickSendSource_ == on)
            return;
        quickSendSource_ = on;
        repaint();
    }
    bool isQuickSendSource() const { return quickSendSource_; }

    /** Reflects the pre/post-fader summary of every send leaving this row's
     *  track: 0 = no sends ("—"), 1 = all post ("POST"), 2 = all pre ("PRE"),
     *  3 = mixed ("MIX"). The POST/PRE button label and fill follow it. */
    void setPreFaderState(int summary)
    {
        preFaderSummary_ = summary;
        repaint();
    }

    /** Quick Send mode is active somewhere in the app (any source row). */
    void setQuickSendModeActive(bool on)
    {
        if (quickSendModeActive_ == on)
            return;
        quickSendModeActive_ = on;
        // Restore the real selection outline when the mode exits.
        if (!on)
            selectionVisual_.setSelected(selected_);
        repaint();
        if (onPresentationDemandChanged)
            onPresentationDemandChanged();
    }
    bool isQuickSendModeActive() const { return quickSendModeActive_; }

    TrackID getTrackID() const { return track_.getID(); }
    TrackID getParentTrackID() const { return track_.getParentTrackID(); }
    juce::String getTrackName() const { return track_.getName(); }

    juce::Rectangle<float> getFolderDropTargetBounds() const
    {
        auto bounds = getLocalBounds().toFloat();
        if (indentLevel_ > 0 && !track_.isMaster())
            bounds.removeFromLeft((float) (indentLevel_ * kIndentStepPx));

        // Mirror the paint() layout: frame inset, then the 6 px color strip,
        // then the 96 px name strip that acts as the folder-drop target.
        bounds.removeFromLeft(kColorStripFrameInsetPx);
        bounds.removeFromLeft(6.0f);
        auto nameBounds = bounds.removeFromLeft(96.0f);
        return nameBounds.reduced(2.0f, juce::jmax(6.0f, nameBounds.getHeight() * 0.18f));
    }

    /** Set Bubblegum send feedback role for this track row. */
    void setBubblegumFeedback(bool isSource, bool isSendTarget, float sendLevel = 0.f,
                              juce::Colour colour = juce::Colour(0xFFFF2D78))
    {
        auto newFb = isSource ? BgFeedback::Source
                   : isSendTarget ? BgFeedback::SendTarget
                   : BgFeedback::None;
        if (newFb != bgFeedback_ || (isSendTarget && (sendLevel != bgFeedbackLevel_ || colour != sendFeedbackColour_)))
        {
            bgFeedback_ = newFb;
            bgFeedbackLevel_ = sendLevel;
            sendFeedbackColour_ = colour;
            repaint();
        }
    }

    std::function<void(TrackRow*)> onSelected;
    std::function<void(TrackRow*, const juce::ModifierKeys&)> onSelectedWithModifiers;
    std::function<void(TrackRow*, juce::Colour)> onColorChanged;
    std::function<void(TrackRow*, int deltaY)> onHeightDrag;
    std::function<void(TrackRow*, const juce::MouseEvent&)> onReorderDrag;
    std::function<void(TrackRow*)> onReorderDrop;
    /** Double-click on the row body (outside controls) — Quick Send mode toggle. */
    std::function<void(TrackRow*)> onQuickSendToggleRequested;
    /** Quick Send target actions (wired by TrackList from its own callbacks). */
    std::function<void(TrackRow*)> onQuickSendTargetClicked;
    std::function<void(TrackRow*)> onQuickSendDeleteTargetRequested;
    std::function<void()> onQuickSendExitRequested;
    std::function<void()> onPresentationDemandChanged;

    // Multi-selection context callbacks (set by TrackList)
    std::function<bool(const TrackID&)> onGetIsMultiSelected;
    std::function<void(const TrackID&)> onMultiRenameRequested;
    std::function<void(const TrackID&)> onMultiDeleteRequested;
    std::function<void(const TrackID&)> onMultiMuteRequested;
    std::function<void(const TrackID&)> onMultiSoloRequested;
    std::function<void(const TrackID&)> onMultiArmRequested;
    std::function<void(const TrackID&)> onMultiColorRequested;

    bool isMasterRow() const { return track_.isMaster(); }

    bool needsPresentationTick() const
    {
        return track_.isMaster()
            || selectionVisual_.needsAnimationTick()
            || (!automationBounds_.isEmpty() && hasTrackAutomation());
    }

    /** Present this row's visual-only animation from TrackList's shared clock.
        The caller has already restricted dispatch to the visible row area. */
    void presentationTick(double deltaSeconds)
    {
        if (!isShowing())
            return;

        const float dt = (float) juce::jlimit(0.0, 0.25, deltaSeconds);

        if (selectionVisual_.needsAnimationTick())
        {
            selectionVisual_.stepSweep(dt);
            selectionVisual_.getLavaCore().stepExternal(dt);
            // The selection material is the row background; keep this repaint
            // local to the row and never ask the whole TrackList to repaint.
            repaint(getLocalBounds());
        }

        if (!automationBounds_.isEmpty() && hasTrackAutomation())
        {
            autoPulsePhase_ += dt * 2.88f;
            if (autoPulsePhase_ > juce::MathConstants<float>::twoPi)
                autoPulsePhase_ -= juce::MathConstants<float>::twoPi;
            repaint(automationBounds_.expanded(7.0f).toNearestInt()
                        .getIntersection(getLocalBounds()));
        }

        if (track_.isMaster())
        {
            masterLivingEye_.update(dt);
            repaint(masterLivingEye_.getContentBounds().toNearestInt()
                        .getIntersection(getLocalBounds()));
        }
    }

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
        auto& a = t.apex;
        auto bounds = getLocalBounds().toFloat();
        const bool master = track_.isMaster();

        // Background — tracks stay dark; identity comes from thin accents.
        // MASTER gets a dark wine/magenta foundation with eye motif + splatter.
        if (master)
        {
            g.setColour(a.color.deepestB.interpolatedWith(a.color.magentaDeep, 0.14f));
            g.fillRect(bounds);

            // Creative-energy splatter along the header edges (cached, capped).
            ensureMasterDecor();
            ApexPrimitives::drawSplatter(g, masterSplatter_, a.color.magenta,
                                         a.decor.splatterOpacity * 0.8f);
            ApexPrimitives::drawSplatter(g, masterSplatter2_, a.color.violet,
                                         a.decor.splatterOpacity * 0.55f);

            // The living eye — same treatment as the FX panel seal:
            // dark iris, narrow pupil, follows the cursor, occasional blink.
            {
                const double nowMs = juce::Time::getMillisecondCounterHiRes();
                const double cyc = std::fmod(nowMs / 1000.0, 6.8) / 6.8;
                float blink = 0.0f;
                if (cyc > 0.910 && cyc < 0.965)
                    blink = (float) std::sin((cyc - 0.910) / 0.055 * juce::MathConstants<double>::pi);
                masterLivingEye_.paint(g, 1.0f, blink);
            }

            // Strong magenta top edge — master hierarchy reads immediately.
            g.setColour(a.color.magenta.withAlpha(0.9f));
            g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 3.f);
        }
        else
        {
            g.setColour(a.color.panelA);
            g.fillRect(bounds);
        }

        // Plugin drag highlight
        if (pluginDropHover_)
        {
            g.setColour(a.color.cyan.withAlpha(0.18f));
            g.fillRect(getLocalBounds().toFloat());
            g.setColour(a.color.cyan.withAlpha(0.7f));
            g.drawRect(getLocalBounds().toFloat().reduced(1.f), 2.f);
        }

        // Selection depth + one-shot sweep (no looping animation).
        selectionVisual_.paintInto(g, getLocalBounds().toFloat());

        // Master-selected seal — same palette as a regular selected track
        // (near-black body + magenta), so the master matches the rest of the
        // selection language instead of standing out in gold.
        if (track_.isMaster() && selected_)
        {
            const auto b = getLocalBounds().toFloat();
            juce::Rectangle<float> seal(b.getRight() - 82, 4, 76, 14);
            g.setColour(juce::Colour(0xFF1A0012).withAlpha(0.92f));
            g.fillRoundedRectangle(seal, 7.0f);
            g.setColour(Theme::getInstance().apex.color.magenta.withAlpha(0.95f));
            g.drawRoundedRectangle(seal, 7.0f, 1.0f);
            g.setFont(Theme::getInstance().fonts.bold.withHeight(8.5f));
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.drawText("MASTER SELECTED", seal, juce::Justification::centred);
        }

        // Bubblegum send feedback highlight (painted AFTER selection overlay
        // so the pink outline/glow isn’t washed out by the depth layers).
        if (bgFeedback_ == BgFeedback::Source)
        {
            auto pinkCol = a.color.pink;
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
            auto pinkCol = sendFeedbackColour_;
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

// Quick Send mode source — gold aura (distinct from the pink
         // Bubblegum source feedback; painted last so it reads on top).
         if (quickSendSource_)
         {
             auto goldCol = juce::Colour(0xFFFFC94D);
             auto rowBounds = getLocalBounds().toFloat();

             // Outer glow halo
             g.setColour(goldCol.withAlpha(0.10f));
             g.drawRect(rowBounds.expanded(2.f), 4.f);

             // Strong gold outline
             g.setColour(goldCol.withAlpha(0.75f));
             g.drawRect(rowBounds.reduced(0.5f), 2.f);

             // Soft internal tint
             g.setColour(goldCol.withAlpha(0.05f));
             g.fillRect(rowBounds.reduced(1.f));

             // Left accent bar (source indicator)
             g.setColour(goldCol.withAlpha(0.90f));
             g.fillRect(0.f, 0.f, 4.f, (float)getHeight());

             // "QUICK SEND" seal at the top-right of the row
             juce::Rectangle<float> seal(rowBounds.getRight() - 92, 4, 88, 14);
             g.setColour(juce::Colour(0xFF2A1F00).withAlpha(0.92f));
             g.fillRoundedRectangle(seal, 7.0f);
             g.setColour(goldCol.withAlpha(0.95f));
             g.drawRoundedRectangle(seal, 7.0f, 1.0f);
             g.setFont(Theme::getInstance().fonts.bold.withHeight(8.5f));
             g.setColour(juce::Colours::white.withAlpha(0.95f));
             g.drawText("QUICK SEND", seal, juce::Justification::centred);
         }

         // Quick Send receiving-target highlight — cyan outline
         // Indicates a valid receive target during Quick Send mode (distinct from selection).
         if (quickSendReceiveTarget_)
         {
             auto cyanCol = juce::Colour(0xFF00FFFF);
             auto rowBounds = getLocalBounds().toFloat();

             // Outer glow halo
             g.setColour(cyanCol.withAlpha(0.15f));
             g.drawRect(rowBounds.expanded(1.5f), 3.f);

             // Solid cyan outline
             g.setColour(cyanCol.withAlpha(0.8f));
             g.drawRect(rowBounds.reduced(0.5f), 2.f);

             // Subtle internal tint
             g.setColour(cyanCol.withAlpha(0.05f));
             g.fillRect(rowBounds.reduced(1.f));
         }

        // Quick Send receiving-track highlight (distinct from selection and send feedback)
        // Drawn as a cyan outline when the row is a valid receive target during Quick Send mode.
        if (quickSendReceiveTarget_)
        {
            auto cyanCol = juce::Colour(0xFF00FFFF);
            auto rowBounds = getLocalBounds().toFloat();

            // Outer glow halo
            g.setColour(cyanCol.withAlpha(0.15f));
            g.drawRect(rowBounds.expanded(1.5f), 3.f);

            // Solid cyan outline
            g.setColour(cyanCol.withAlpha(0.8f));
            g.drawRect(rowBounds.reduced(0.5f), 2.f);

            // Subtle internal tint
            g.setColour(cyanCol.withAlpha(0.05f));
            g.fillRect(rowBounds.reduced(1.f));
        }

        // Bottom separator
        g.setColour(a.color.borderSoftA.withAlpha(0.45f));
        g.fillRect(bounds.getX(), bounds.getBottom() - 1.f, bounds.getWidth(), 1.f);

        // Indent + nesting guides for child tracks
        const float indentPx = (float) (indentLevel_ * kIndentStepPx);
        if (indentLevel_ > 0 && !master)
        {
            auto indentArea = bounds.removeFromLeft(indentPx);

            // Subtle darker band under indent area to read as "inside folder"
            g.setColour(a.color.deepestA.withAlpha(0.55f));
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

        // Reserve a left inset so the (thickened) DAW window frame never
        // covers the color palette strip or the folder chevron.
        bounds.removeFromLeft(kColorStripFrameInsetPx);

        // Folder expand/collapse chevron — sits to the RIGHT of the color
        // strip with a small gap so the two click targets never overlap.
        // 16px hit target so the arrow is easy to see and click.
        if (isFolderBus_ && !master)
        {
            float chevX = bounds.getX() + 6.f + kColorStripChevronGapPx;
            float chevY = (getHeight() - 16.f) * 0.5f;
            auto chev = juce::Rectangle<float>(chevX, chevY, 16.f, 16.f);
            chevronBounds_ = chev;
        }
        else
        {
            chevronBounds_ = {};
        }

        // Track colour strip (clickable) — drawn for all non-master tracks
        colorStripBounds_ = bounds.removeFromLeft(6.f);
        g.setColour(master ? masterAccentColour() : track_.getColor());
        g.fillRect(colorStripBounds_);

        // Chevron drawn NEXT TO the color strip (folder buses only)
        if (isFolderBus_ && !master)
            drawFolderChevron(g, chevronBounds_, folderExpanded_, chevronHover_);

        // Track name — top half (folder buses skip the chevron lane)
        g.setColour(a.color.textPrimary);
        g.setFont(t.fonts.bold);
        auto nameBounds = bounds.removeFromLeft(bounds.getWidth());
        if (isFolderBus_ && !master)
            nameBounds.removeFromLeft(16.f);
        nameBounds = nameBounds.reduced(8, 0);
        nameBounds = nameBounds.removeFromTop(nameBounds.getHeight() * 0.5f);
        g.drawText(track_.getName(), nameBounds, juce::Justification::centredLeft);

        // M / S / Monitor / R / A / Trim / Target — bottom half
        drawMSRButton(g, muteBounds_, "M", track_.isMuted(), muteHover_, t.colors.warning);
        drawMSRButton(g, soloBounds_, "S", track_.isSoloed(), soloHover_, a.color.activeGreen);
        if (!master)
        {
            drawMonitorButton(g, monitorBounds_, track_.isMonitoring(), monitorHover_);
            drawMSRButton(g, armBounds_, "R", track_.isArmed(), armHover_, t.colors.transportRecord);
            const char* preLabel = preFaderSummary_ == 2 ? "PRE"
                                 : preFaderSummary_ == 3 ? "MIX"
                                 : preFaderSummary_ == 1 ? "POST" : "—";
            drawMSRButton(g, preFaderBounds_, preLabel, preFaderSummary_ >= 2, preFaderHover_, juce::Colour(0xFF00B3A4));
            drawAutomationButton(g, automationBounds_, track_.isAutomationVisible(), track_.isAutomationMuted(), automationHover_);
            drawTrimButton(g, trimBounds_, trimHover_);
            drawInputButton(g, inputBounds_, inputHover_);
            drawAutomationTargetButton(g, automationTargetBounds_, automationTargetHover_);
        }

        // Bottom resize handle (master is not resizable)
        if (!master)
        {
            g.setColour(resizeHover_ ? a.color.magenta.withAlpha(0.55f)
                                     : a.color.borderSoftA.withAlpha(0.25f));
            g.fillRect(0.f, (float)getHeight() - 2.f, (float)getWidth(), 2.f);
        }

        // Row frame so headers read as their own panel — restrained hairline.
        {
            auto r = getLocalBounds().toFloat().reduced(1.5f);
            g.setColour(master ? a.color.magenta.withAlpha(0.38f)
                               : a.color.borderSoftA.withAlpha(0.55f));
            g.drawRect(r, master ? 1.5f : 1.0f);
        }
    }

    /** Builds/caches the master-row decorative geometry (eye + splatter)
        only when master and only when bounds are (re)assigned. */
    void ensureMasterDecor()
    {
        if (! track_.isMaster())
            return;

        const auto bounds = getLocalBounds().toFloat();
        if (masterDecorBounds_ == bounds && ! masterSplatter_.empty())
            return; // cache valid

        masterDecorBounds_ = bounds;
        auto& a = Theme::getInstance().apex;

        // Living eye — right-hand side, vertically centred, modest size.
        const float eyeW = juce::jmin(76.0f, bounds.getWidth() * 0.36f);
        const float eyeH = juce::jmin(bounds.getHeight() * 0.60f, 40.0f);
        masterLivingEye_.setBounds(
            juce::Rectangle<float>(bounds.getRight() - eyeW - 10.0f,
                                   bounds.getCentreY() - eyeH * 0.5f,
                                   eyeW, eyeH));

        // Splatter hugging the top and bottom edges — away from the name text.
        auto topZone = bounds.withHeight(juce::jmin(14.0f, bounds.getHeight() * 0.30f));
        auto botZone = topZone.translated(0.0f, bounds.getHeight() - topZone.getHeight());
        masterSplatter_  = ApexPrimitives::buildSplatterPaths(topZone,  (juce::uint32) a.decor.splatterSeed, 34);
        masterSplatter2_ = ApexPrimitives::buildSplatterPaths(botZone,  (juce::uint32) (a.decor.splatterSeed ^ 0x5EED), 26);
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
        // DISABLED (product decision): the piano roll is a beat-making surface.
        const bool showPiano = false;
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
        if (!isMaster)    ++numBtns;    // POST/PRE tap point
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
            monitorBounds_ = {}; armBounds_ = {}; preFaderBounds_ = {};
            automationBounds_ = {}; trimBounds_ = {}; automationTargetBounds_ = {};
            inputBounds_ = {};
            return;
        }

        soloBounds_.setBounds(x, btnY, btnW, btnH);   x += btnW + kGap;
        monitorBounds_.setBounds(x, btnY, btnW, btnH); x += btnW + kGap;
        armBounds_.setBounds(x, btnY, btnW, btnH);     x += btnW + kGap;
        preFaderBounds_.setBounds(x, btnY, btnW, btnH); x += btnW + kGap;
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

        // DISABLED (product decision): per-row trim VU needle hidden — it
        // looked like a non-interactive knob. Trim metering still lives in
        // InputTrimFloatingPanel and the mixer strip.
        // Re-enable by restoring the block below.
        // if (!isMaster)
        // {
        //     vu_.setVisible(true);
        //     vu_.setBounds(getWidth() - 32, 4, 24,
        //                   juce::jmax(20, (int) (getHeight() * 0.44f)));
        // }
        // else
        // {
        //     vu_.setVisible(false);
        //     vu_.setBounds({});
        // }
        vu_.setVisible(false);
        vu_.setBounds({});
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

        if (e.mods.isPopupMenu() && !track_.isMaster() && muteBounds_.contains(e.position))
        {
            if (onExclusiveMute) onExclusiveMute(track_.getID());
            return;
        }

        // ── Right-click context menu ──────────────────────────────────────
        // A context click is still a selection gesture.  The canonical
        // selection callback preserves the complete set when this row is
        // already selected, and collapses only when the target was outside
        // the existing set.
        if (e.mods.isPopupMenu())
        {
            // While Quick Send mode is active, right-click toggles the
            // sidechain to/from this track (create if missing, remove if
            // exists) — the same rule as the mixer strip. The normal context
            // menu returns when the mode is inactive.
            if (!track_.isMaster() && isQuickSendModeActive())
            {
                if (onQuickSendDeleteTargetRequested)
                    onQuickSendDeleteTargetRequested(this);
                return;
            }
            notifySelectionForInteraction(e.mods);
            if (!track_.isMaster())
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
        else if (!track_.isMaster() && preFaderBounds_.contains(e.position))
        {
            if (onPreFaderToggleRequested) onPreFaderToggleRequested(track_.getID());
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
            notifySelectionForInteraction(e.mods);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (track_.isMaster())
            masterLivingEye_.setGazeTarget(e.position, true);

        bool wasHover = resizeHover_;
        resizeHover_ = (!track_.isMaster() && e.y >= getHeight() - 5);
        bool pm = muteHover_, pp = pianoHover_, ps = soloHover_, pmon = monitorHover_, pa = armHover_, ptr = trimHover_, paut = automationHover_, pat = automationTargetHover_;
        bool pc = chevronHover_;
        bool pin = inputHover_;
        bool pf = preFaderHover_;
        pianoHover_   = pianoRollBounds_.contains(e.position);
        muteHover_    = muteBounds_.contains(e.position);
        soloHover_    = soloBounds_.contains(e.position);
        monitorHover_ = !track_.isMaster() && monitorBounds_.contains(e.position);
        armHover_     = !track_.isMaster() && armBounds_.contains(e.position);
        preFaderHover_ = !track_.isMaster() && preFaderBounds_.contains(e.position);
        automationHover_ = !track_.isMaster() && automationBounds_.contains(e.position);
        automationTargetHover_ = !track_.isMaster() && automationTargetBounds_.contains(e.position);
        trimHover_    = !track_.isMaster() && trimBounds_.contains(e.position);
        inputHover_   = !track_.isMaster() && inputBounds_.contains(e.position);
        chevronHover_ = isFolderBus_ && !track_.isMaster() && chevronBounds_.contains(e.position);
        if (wasHover != resizeHover_ || pm != muteHover_ || pp != pianoHover_ || ps != soloHover_ || pmon != monitorHover_ || pa != armHover_ || ptr != trimHover_ || paut != automationHover_ || pat != automationTargetHover_ || pc != chevronHover_ || pin != inputHover_ || pf != preFaderHover_)
        {
            setMouseCursor(DAW::CursorThemeCore::getStandard(
                resizeHover_ ? juce::MouseCursor::UpDownResizeCursor
                             : automationTargetHover_ ? juce::MouseCursor::PointingHandCursor
                                                      : juce::MouseCursor::NormalCursor));
            repaint();
        }

        // Update tooltip based on what the cursor is over
        if (muteHover_)
            currentTooltip_ = "Mute\nMute this track. The track will produce no audio output during playback.";
        else if (soloHover_)
            currentTooltip_ = "Solo\nSolo this track. All other non-soloed tracks are muted.";
        else if (monitorHover_)
            currentTooltip_ = "Monitor\nToggle input monitoring. When enabled, you hear the live input through this track's FX chain.";
        else if (armHover_)
            currentTooltip_ = "Record Arm\nArm this track for recording. Right-click the row to choose Dry / Pre-Fader or Wet / Post-Fader.";
        else if (automationHover_)
            currentTooltip_ = "Automation Visibility\nToggle automation lane visibility on the arrangement timeline.";
        else if (trimHover_)
            currentTooltip_ = "Input Trim\nOpen the input trim panel to adjust gain and phase before the track.";
        else if (inputHover_)
            currentTooltip_ = "Input Routing\nOpen the input source selector to choose the audio or MIDI input for this track.";
        else if (preFaderHover_)
            currentTooltip_ = "Send Pre/Post-Fader\nToggle each send's tap point (PRE before the fader, POST after it). With one send it toggles directly; with several, pick the target from the menu.";
        else if (automationTargetHover_)
            currentTooltip_ = "Automation Target\nSelect which plugin parameter this track's automation lane controls.";
        else if (pianoHover_)
            currentTooltip_ = "Piano Roll\nOpen the piano roll editor for MIDI clips on this track.";
        else if (resizeHover_)
            currentTooltip_ = "Drag to resize\nChange the height of this track header.";
        else if (chevronHover_)
            currentTooltip_ = folderExpanded_ ? "Collapse folder\nHide tracks inside this folder." : "Expand folder\nShow tracks inside this folder.";
        else
            currentTooltip_ = {};
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
            // Custom rename overlay: text editor + checkmark (confirm) + X (cancel)
            struct RenameOverlay : public juce::Component
            {
                juce::TextEditor editor;
                juce::TextButton confirmBtn { "V" };
                juce::TextButton cancelBtn  { "X" };
                std::function<void(const juce::String&)> onConfirm;
                std::function<void()> onCancel;

                RenameOverlay()
                {
                    confirmBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2ECC71));
                    cancelBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE74C3C));
                    confirmBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
                    cancelBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

                    addAndMakeVisible(editor);
                    addAndMakeVisible(confirmBtn);
                    addAndMakeVisible(cancelBtn);

                    confirmBtn.onClick = [this]
                    {
                        auto name = editor.getText().trim();
                        if (onConfirm) onConfirm(name);
                    };
                    cancelBtn.onClick = [this]
                    {
                        if (onCancel) onCancel();
                    };
                    editor.onReturnKey = [this] { confirmBtn.triggerClick(); };
                    editor.onEscapeKey = [this] { cancelBtn.triggerClick(); };
                }

                void paint(juce::Graphics& g) override
                {
                    g.setColour(Theme::getInstance().colors.surface);
                    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);
                    g.setColour(Theme::getInstance().colors.accent);
                    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.f, 1.f);
                }

                void resized() override
                {
                    auto b = getLocalBounds();
                    const int btnH = juce::jmin(18, b.getHeight());
                    const int btnW = 18;
                    cancelBtn.setBounds(b.removeFromRight(btnW).withSizeKeepingCentre(btnW, btnH));
                    confirmBtn.setBounds(b.removeFromRight(btnW).withSizeKeepingCentre(btnW, btnH));
                    b.removeFromRight(2); // gap
                    editor.setBounds(b);
                }
            };

            auto* overlay = new RenameOverlay();
            auto editorBounds = nameArea.toNearestIntEdges().reduced(4, 2);
            // Widen to fit confirm + cancel buttons
            editorBounds = editorBounds.withTrimmedRight(-52);
            overlay->setBounds(editorBounds);
            overlay->editor.setText(track_.getName(), false);
            overlay->editor.setFont(Theme::getInstance().fonts.bold);
            overlay->editor.selectAll();
            overlay->editor.setColour(juce::TextEditor::backgroundColourId, Theme::getInstance().colors.surface);
            overlay->editor.setColour(juce::TextEditor::textColourId, Theme::getInstance().colors.text);
            overlay->editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            overlay->editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

            overlay->onConfirm = [this, overlay](const juce::String& name)
            {
                if (name.isNotEmpty()) track_.setName(name);
                delete overlay;
            };
            overlay->onCancel = [overlay] { delete overlay; };

            addAndMakeVisible(overlay);
            overlay->editor.grabKeyboardFocus();
        }
        else if (!track_.isMaster())
        {
            // Double-click on the row body (outside the name area and all
            // controls) toggles Quick Send mode with this track as source.
            const bool overControl =
                   muteBounds_.contains(e.position)
                || pianoRollBounds_.contains(e.position)
                || soloBounds_.contains(e.position)
                || (!track_.isMaster() && monitorBounds_.contains(e.position))
                || (!track_.isMaster() && armBounds_.contains(e.position))
                || (!track_.isMaster() && preFaderBounds_.contains(e.position))
                || (!track_.isMaster() && trimBounds_.contains(e.position))
                || (!track_.isMaster() && inputBounds_.contains(e.position))
                || colorStripBounds_.contains(e.position)
                || (!track_.isMaster() && automationBounds_.contains(e.position))
                || (!track_.isMaster() && automationTargetBounds_.contains(e.position))
                || (isFolderBus_ && !track_.isMaster() && chevronBounds_.contains(e.position));
            if (!overControl && onQuickSendToggleRequested)
                onQuickSendToggleRequested(this);
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (track_.isMaster())
            masterLivingEye_.setGazeTarget({}, false);

        resizeHover_ = false;
        pianoHover_ = false; muteHover_ = false; soloHover_ = false; monitorHover_ = false; armHover_ = false; automationHover_ = false; automationTargetHover_ = false; trimHover_ = false;
        preFaderHover_ = false;
        inputHover_ = false;
        chevronHover_ = false;
        currentTooltip_ = {};
        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::NormalCursor));
        repaint();
    }

private:
    Track& track_;
    TrackSelectionVisualCore selectionVisual_;
    juce::String currentTooltip_;
    CompactVuNeedle vu_;

    bool hasTrackAutomation() const
    {
        return hasAutomationData ? hasAutomationData(track_.getID()) : false;
    }

    void notifySelectionForInteraction(const juce::ModifierKeys& mods)
    {
        const bool hasModifier = mods.isCtrlDown() || mods.isCommandDown() || mods.isShiftDown();
        // A plain click on a member of an existing multi-selection is the
        // beginning of a possible drag. Do not collapse the set before the
        // drag target is known.
        if (!hasModifier && !mods.isPopupMenu() && onGetIsMultiSelected
            && onGetIsMultiSelected(track_.getID()))
            return;

        if (onSelectedWithModifiers)
            onSelectedWithModifiers(this, mods);
        else if (onSelected)
            onSelected(this);
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

        menu.addSectionHeader("Recording Mode");
        {
            juce::PopupMenu::Item dry("Record Dry / Pre-Fader (no effects printed)");
            dry.isTicked = track_.getRecordMode() == TrackRecordMode::Dry;
            dry.action = [safeThis]
            {
                if (!safeThis) return;
                safeThis->track_.setRecordMode(TrackRecordMode::Dry);
                safeThis->repaint();
            };
            menu.addItem(dry);
        }
        {
            juce::PopupMenu::Item postFader("Record Wet / Post-Fader (track FX + fader printed)");
            postFader.isTicked = track_.getRecordMode() == TrackRecordMode::PostFader;
            postFader.action = [safeThis]
            {
                if (!safeThis) return;
                safeThis->track_.setRecordMode(TrackRecordMode::PostFader);
                safeThis->repaint();
            };
            menu.addItem(postFader);
        }
        {
            juce::PopupMenu::Item recordingHint(
                "Monitor Off records dry; Auto/On prints FX. Playback runs FX again");
            recordingHint.isEnabled = false;
            menu.addItem(recordingHint);
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

        if (isFolderBus_ && onConvertFolderToTrack)
        {
            juce::PopupMenu::Item convert("Convert to Regular Track");
            convert.action = [safeThis]
            {
                if (safeThis && safeThis->onConvertFolderToTrack)
                    safeThis->onConvertFolderToTrack(safeThis->track_.getID());
            };
            menu.addItem(convert);
            menu.addSeparator();
        }

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
    bool preFaderHover_ = false;
    bool inputHover_ = false;
    bool automationHover_ = false;
    bool automationTargetHover_ = false;
    bool chevronHover_ = false;
    int  preFaderSummary_ = 0;   // 0=none, 1=all post, 2=all pre, 3=mixed

    // Nesting / folder expand state
    // kChevronWidthPx intentionally equals kIndentStepPx so a root folder bus
    // and its depth-1 children have their color strip at the same X offset.
    static constexpr int kIndentStepPx   = 14;
    static constexpr int kChevronWidthPx = 14;
    /** Left inset reserved for the (thickened) DAW window frame so the color
     *  palette strip and the folder chevron are never covered by it. */
    static constexpr float kColorStripFrameInsetPx = 4.0f;
    /** Gap between the color palette strip and the folder chevron. */
    static constexpr float kColorStripChevronGapPx = 5.0f;
    int  indentLevel_   = 0;
    bool isFolderBus_   = false;
    bool folderExpanded_ = true;
    juce::Rectangle<float> chevronBounds_;

// Bubblegum send feedback
      enum class BgFeedback { None, Source, SendTarget, QuickSendTarget };
      BgFeedback bgFeedback_       = BgFeedback::None;
      float      bgFeedbackLevel_  = 0.f;  // send level for intensity scaling (used for SendTarget)
      juce::Colour sendFeedbackColour_ = juce::Colour(0xFFFF2D78); // cable colour of the send

      bool       quickSendSource_  = false; // Quick Send mode source (gold aura)
      bool       quickSendModeActive_ = false; // Quick Send mode active (any source)
      bool       quickSendReceiveTarget_ = false; // Quick Send receive-target highlight (cyan outline)

      void setQuickSendTargetHighlight(bool on)
      {
          if (quickSendReceiveTarget_ == on)
              return;
          quickSendReceiveTarget_ = on;
          repaint();
      }

      void setQuickSendReceiveTarget(bool on)
      {
          setQuickSendTargetHighlight(on);
      }

    // MASTER decorative cache (built lazily by ensureMasterDecor)
    juce::Rectangle<float>     masterDecorBounds_ { -1.0f, -1.0f, -1.0f, -1.0f };
    ApexLivingEye              masterLivingEye_;
    std::vector<juce::Path>    masterSplatter_, masterSplatter2_;

    juce::Rectangle<float> pianoRollBounds_, muteBounds_, soloBounds_, monitorBounds_, armBounds_, trimBounds_;
    juce::Rectangle<float> preFaderBounds_;
    juce::Rectangle<float> inputBounds_;
    juce::Rectangle<float> automationBounds_;
    juce::Rectangle<float> automationTargetBounds_;
    float autoPulsePhase_ = 0.f;
    mutable juce::Rectangle<float> colorStripBounds_;

    void drawMSRButton(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                       const juce::String& text, bool isOn, bool hovered, juce::Colour color)
    {
        auto& t = Theme::getInstance();
        auto& a = t.apex;

        // Background: idle dark / hover lift / active semantic fill
        if (isOn)
            g.setColour(color.darker(0.15f));
        else if (hovered)
            g.setColour(a.color.panelC.brighter(a.state.hoverStrength * 4.0f));
        else
            g.setColour(a.color.panelC);
        g.fillRoundedRectangle(bounds, a.metric.radiusControl);

        // Border
        g.setColour(isOn ? color.withAlpha(0.55f) : a.color.borderSoftA);
        g.drawRoundedRectangle(bounds, a.metric.radiusControl, a.metric.strokeThin);

        // Text
        g.setColour(isOn ? a.color.textPrimary : a.color.textSecondary);
        g.setFont(t.fonts.bold);
        g.drawText(text, bounds, juce::Justification::centred);
    }

    void drawFolderChevron(juce::Graphics& g, juce::Rectangle<float> b, bool open, bool hovered)
    {
        if (b.isEmpty()) return;
        auto& t = Theme::getInstance();
        auto& a = t.apex;

        // Always-visible button backing so the arrow reads as a control even
        // before hover: violet-tinted fill + brighter border.
        g.setColour(hovered ? a.color.violet.withAlpha(0.45f)
                            : a.color.violet.withAlpha(0.22f));
        g.fillRoundedRectangle(b, a.metric.radiusControl);
        g.setColour((hovered ? a.color.violetBright : a.color.violet)
                        .withAlpha(hovered ? 0.95f : 0.65f));
        g.drawRoundedRectangle(b, a.metric.radiusControl, 1.4f);

        // Clean stroked chevron — ▼ when open, ▶ when closed.
        // Larger glyph + bright stroke so it is clearly visible on the dark
        // row even when the folder is collapsed.
        auto c = b.getCentre();
        const float s = 4.2f;
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
        g.setColour(hovered ? juce::Colours::white
                            : a.color.violetBright);
        g.strokePath(p, juce::PathStrokeType(2.2f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    void drawTrimButton(juce::Graphics& g, const juce::Rectangle<float>& bounds, bool hovered)
    {
        if (bounds.isEmpty()) return;
        auto& t = Theme::getInstance();
        auto& a = t.apex;
        const float db = track_.getInputTrim().getTargetGainDb();
        const bool active = (db < -0.1f || db > 0.1f);

        juce::Colour bg  = active  ? a.color.violet.withAlpha(0.75f)
                         : hovered ? a.color.panelC.brighter(a.state.hoverStrength * 4.0f)
                                   : a.color.panelC;
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, a.metric.radiusControl);
        g.setColour(active ? a.color.violetBright : a.color.borderSoftA);
        g.drawRoundedRectangle(bounds, a.metric.radiusControl, a.metric.strokeThin);

        g.setColour(active ? a.color.textPrimary : a.color.textSecondary);
        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.drawText("TRIM", bounds, juce::Justification::centred, false);
    }

    void drawInputButton(juce::Graphics& g, const juce::Rectangle<float>& bounds, bool hovered)
    {
        if (bounds.isEmpty()) return;
        auto& t = Theme::getInstance();
        auto& a = t.apex;
        const int  first = track_.getInputFirstChannel();
        const bool mono  = track_.isInputMono();
        const juce::String label = mono
            ? "In " + juce::String(first + 1)
            : "In " + juce::String(first + 1) + "/" + juce::String(first + 2);

        g.setColour(hovered ? a.color.panelC.brighter(a.state.hoverStrength * 4.0f)
                            : a.color.panelC);
        g.fillRoundedRectangle(bounds, a.metric.radiusControl);
        g.setColour(hovered ? a.color.cyan.withAlpha(0.65f) : a.color.borderSoftA);
        g.drawRoundedRectangle(bounds, a.metric.radiusControl, a.metric.strokeThin);

        g.setColour(hovered ? a.color.textPrimary : a.color.textSecondary);
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        auto textArea = bounds.withTrimmedRight(8.0f).reduced(2.0f, 0.0f);
        g.drawText(label, textArea.toNearestInt(), juce::Justification::centred, false);

        // dropdown arrow
        auto c = juce::Point<float>(bounds.getRight() - 5.0f, bounds.getCentreY());
        juce::Path arrow;
        arrow.startNewSubPath(c.x - 2.2f, c.y - 1.1f);
        arrow.lineTo(c.x, c.y + 1.4f);
        arrow.lineTo(c.x + 2.2f, c.y - 1.1f);
        g.setColour(a.color.cyan.withAlpha(hovered ? 0.95f : 0.55f));
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
        auto& a = t.apex;
        const auto accent = track_.getColor();
        const bool hasAutomation = hasTrackAutomation();
        const float pulse = hasAutomation ? 0.5f + 0.5f * std::sin(autoPulsePhase_) : 0.0f;

        if (hasAutomation)
        {
            const float glowAlpha = visible ? 0.18f : 0.10f + pulse * 0.22f;
            g.setColour((muted ? a.color.textMuted : accent).withAlpha(glowAlpha));
            g.fillRoundedRectangle(bounds.expanded(3.0f + pulse * 2.0f), 6.0f);
        }

        juce::Colour bg = visible ? accent.withAlpha(muted ? 0.25f : 0.78f)
                         : hasAutomation ? accent.withAlpha(muted ? 0.12f : 0.16f + pulse * 0.10f)
                         : hovered ? a.color.panelC.brighter(a.state.hoverStrength * 4.0f)
                                   : a.color.panelC;
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, a.metric.radiusControl);
        g.setColour(visible ? accent.brighter(0.35f).withAlpha(muted ? 0.35f : 0.75f)
                    : hasAutomation ? accent.withAlpha(muted ? 0.28f : 0.50f + pulse * 0.25f)
                    : a.color.borderSoftA);
        g.drawRoundedRectangle(bounds, a.metric.radiusControl, a.metric.strokeThin);

        g.setColour(muted ? a.color.textMuted
                          : (visible || hasAutomation ? a.color.textPrimary : a.color.textSecondary));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("A", bounds, juce::Justification::centred, false);
    }

    void drawMonitorButton(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                           bool isOn, bool hovered)
    {
        auto& t = Theme::getInstance();
        auto& a = t.apex;
        auto color = a.color.cyan;

        if (isOn)
            g.setColour(color.darker(0.15f));
        else if (hovered)
            g.setColour(a.color.panelC.brighter(a.state.hoverStrength * 4.0f));
        else
            g.setColour(a.color.panelC);
        g.fillRoundedRectangle(bounds, a.metric.radiusControl);

        g.setColour(isOn ? color.withAlpha(0.55f) : a.color.borderSoftA);
        g.drawRoundedRectangle(bounds, a.metric.radiusControl, a.metric.strokeThin);

        const auto iconColour = isOn ? a.color.textPrimary : a.color.textSecondary;
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

        auto& a = Theme::getInstance().apex;
        juce::Colour bg = hovered ? a.color.panelC.brighter(a.state.hoverStrength * 4.0f)
                                  : a.color.panelB.withAlpha(0.85f);
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, a.metric.radiusControl);
        g.setColour(track_.getColor().withAlpha(hovered ? 0.85f : 0.55f));
        g.drawRoundedRectangle(bounds.reduced(0.4f), a.metric.radiusControl, a.metric.strokeThin);

        g.setColour(a.color.textPrimary.withAlpha(0.90f));
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        auto textArea = bounds.withTrimmedRight(10.0f).reduced(2.0f, 0.0f);
        g.drawText(label, textArea.toNearestInt(), juce::Justification::centred, false);

        // dropdown arrow
        auto c = juce::Point<float>(bounds.getRight() - 6.0f, bounds.getCentreY());
        juce::Path arrow;
        arrow.startNewSubPath(c.x - 2.5f, c.y - 1.2f);
        arrow.lineTo(c.x, c.y + 1.5f);
        arrow.lineTo(c.x + 2.5f, c.y - 1.2f);
        g.setColour(a.color.textPrimary.withAlpha(hovered ? 0.95f : 0.60f));
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
                            track_.getID(), slot, pluginName, paramID,
                            plugin != nullptr ? plugin->getDescription().uniqueId : 0);
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
                  public TrackManager::Listener,
                  public ApexPresentationClock::TickReceiver,
                  private juce::Timer
{
public:
    // ── Pinned TRACKS header bar ──────────────────────────────────────────
    // A REAL fixed overlay child sitting ABOVE the scrolling track rows.
    // The Quick Track Builder "+" is a real juce::Button inside it, so:
    //   * its hit-test is component-exclusive — a row scrolled underneath
    //     can never receive the click (no painted-rect pass-through),
    //   * the header strip itself is painted here (not in paintOverChildren),
    //     so the button can never be covered by the parent's overlay paint.
    class TrackListHeaderBar final : public juce::Component
    {
    public:
        class QuickAddButton final : public juce::Button
        {
        public:
            QuickAddButton() : juce::Button("Quick Track Builder (TRACKS)") {}

            void paintButton(juce::Graphics& g, bool highlighted, bool down) override
            {
                auto& theme = Theme::getInstance();
                auto& a = theme.apex;
                const auto bounds = getLocalBounds().toFloat();
                g.setColour(down || highlighted ? a.color.panelC.brighter(0.10f)
                                                : a.color.panelC);
                g.fillRoundedRectangle(bounds, a.metric.radiusControl);
                g.setColour(a.color.borderSoftA);
                g.drawRoundedRectangle(bounds.reduced(0.5f), a.metric.radiusControl,
                                       a.metric.strokeThin);
                g.setColour(a.color.magenta);
                g.setFont(theme.fonts.bold.withHeight(18.0f));
                g.drawText("+", getLocalBounds(), juce::Justification::centred);
            }
        };

        TrackListHeaderBar()
        {
            addAndMakeVisible(addBtn_);
            addBtn_.onClick = [this]
            {
                if (onQuickAdd)
                    onQuickAdd();
            };
        }

        void paint(juce::Graphics& g) override
        {
            auto& theme = Theme::getInstance();
            auto& a = theme.apex;
            // Top 48px — matches the ArrangementViewCore toolbar height.
            g.setColour(a.color.deepestB);
            g.fillRect(0, 0, getWidth(), 48);
            g.setColour(a.color.textPrimary);
            g.setFont(theme.fonts.bold);
            g.drawText("TRACKS", 8, 0, getWidth() - 56, 48, juce::Justification::centredLeft);
            // Next 32px — matches the ruler height.
            g.setColour(a.color.deepestA);
            g.fillRect(0, 48, getWidth(), 32);
            g.setColour(a.color.borderSoftA.withAlpha(0.65f));
            g.fillRect(0, 79, getWidth(), 1);
        }

        void resized() override
        {
            addBtn_.setBounds(getWidth() - 44, 10, 34, 28);
        }

        std::function<void()> onQuickAdd;
        QuickAddButton addBtn_;
    };

    std::function<void(int deltaX)> onWidthDrag;  // callback for resize
    std::function<void(const TrackID&, const juce::ModifierKeys&)> onTrackSelectedWithModifiers;

    /** Screen anchor of the real TRACKS "+" button (Quick Track Builder
     *  popup anchoring). */
    juce::Rectangle<int> getQuickAddButtonScreenBounds() const
    {
        const auto local = headerBar_.addBtn_.getBounds() + headerBar_.getPosition();
        return local.isEmpty() ? local : local + getScreenPosition();
    }

    void setScrollOffset(int y)
    {
        y = juce::jmax(0, y);
        if (scrollOffsetY_ == y)
            return;
        scrollOffsetY_ = y;
        TimelinePaintMetrics::inc(TimelinePaintMetrics::trackListScrollEvents);
        positionRowsForScroll();
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
        ApexPresentationClock::instance().addReceiver(this);
        addAndMakeVisible(headerBar_);
        headerBar_.onQuickAdd = [this]
        {
            if (onQuickAddTrackRequested)
                onQuickAddTrackRequested();
        };
        rebuildTrackRows();
    }

    ~TrackList()
    {
        ApexPresentationClock::instance().removeReceiver(this);
        stopTimer();
        trackManager_.removeListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        auto& a = theme.apex;
        g.fillAll(a.color.deepestA);

        // The 80px pinned header strip (TRACKS label + ruler + the real "+"
        // button) is painted by the TrackListHeaderBar overlay child above
        // the scrolling rows — NOT here and NOT in paintOverChildren, so the
        // real button component can never be covered or re-hit-tested away.

        // Right edge resize handle
        g.setColour(dragHover_ ? a.color.magenta.withAlpha(0.55f)
                               : a.color.borderSoftA.withAlpha(0.4f));
        g.fillRect(getWidth() - 3, 0, 3, getHeight());
    }

    void resized() override
    {
        // Pinned header bar — a real fixed overlay above the scrolling rows.
        headerBar_.setBounds(0, 0, getWidth(), 80);
        headerBar_.toFront(false);

        // Direct Y calculation — mirrors ArrangementViewCore::arrangementContentTop() exactly
        int y = 80 - scrollOffsetY_;
        for (auto* row : trackRows_)
        {
            TimelinePaintMetrics::inc(TimelinePaintMetrics::trackListScrollRowBoundsAssignments);
            row->setBounds(0, y, getWidth(), row->getDesiredHeight());
            y += row->getDesiredHeight();
        }

        updatePresentationDemand();
    }

    void onPresentationTick(double deltaSeconds) override
    {
        if (!isShowing())
        {
            updatePresentationDemand();
            return;
        }

        const auto visibleArea = getLocalBounds().withTrimmedTop(80);
        for (auto* row : trackRows_)
        {
            if (row == nullptr || !row->isShowing()
                || !visibleArea.intersects(row->getBounds()))
                continue;

            row->presentationTick(deltaSeconds);
        }

        updatePresentationDemand();
    }

    void visibilityChanged() override
    {
        updatePresentationDemand();
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        auto& a = theme.apex;

        // NOTE: the pinned TRACKS header strip is NOT redrawn here anymore —
        // it lives in the TrackListHeaderBar overlay child (painted above
        // the rows naturally, and it owns the real "+" button). Redrawing it
        // here would cover the button component.

        g.setColour(dragHover_ ? a.color.magenta.withAlpha(0.55f)
                               : a.color.borderSoftA.withAlpha(0.4f));
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
                    const auto folderBrown = Theme::getInstance().apex.color.folderTimber;

                    // Outer glow halo
                    for (int i = 3; i >= 1; --i)
                    {
                        g.setColour(folderBrown.withAlpha(0.18f / (float)i));
                        g.drawRoundedRectangle(destBounds.expanded((float)i * 2.f), 4.f, 2.f);
                    }

                    // Inner tint + solid border
                    g.setColour(folderBrown.withAlpha(0.16f));
                    g.fillRect(destBounds);
                    g.setColour(folderBrown.withAlpha(0.9f));
                    g.drawRoundedRectangle(destBounds.reduced(0.5f), 4.f, 2.f);

                    // Gold top-edge band (mirrors folder-bus/master styling)
                    g.fillRect(destBounds.getX(), destBounds.getY(), destBounds.getWidth(), 3.f);

                    // Folder icon centered on the row
                    float fx = destBounds.getCentreX();
                    float fy = destBounds.getCentreY();
                    juce::Path folder;
                    folder.addRoundedRectangle(fx - 9.f, fy - 5.f, 18.f, 11.f, 2.f);
                    folder.addRectangle(fx - 9.f, fy - 9.f, 8.f, 5.f);
                    g.setColour(Theme::getInstance().apex.color.folderTimberBright);
                    g.fillPath(folder);

                    // Explicit release affordance. Adoption through a visible
                    // child highlights both the hovered child and its owning
                    // folder so the hierarchy change is never ambiguous.
                    if (folderDropHoverTrackId_.isNotEmpty()
                        && folderDropHoverTrackId_ != folderDropTargetTrackId_)
                    {
                        for (auto* hovered : trackRows_)
                            if (hovered != nullptr && hovered->getTrackID() == folderDropHoverTrackId_)
                            {
                                const auto child = juce::Rectangle<float>(0.0f, (float) hovered->getY(),
                                    (float) getWidth(), (float) hovered->getHeight()).reduced(3.0f);
                                g.setColour(folderBrown.withAlpha(0.13f));
                                g.fillRoundedRectangle(child, 4.0f);
                                g.setColour(folderBrown.withAlpha(0.62f));
                                g.drawRoundedRectangle(child, 4.0f, 1.2f);
                                break;
                            }
                    }

                    const auto actionText = folderDropIsAdoption_
                        ? "RELEASE TO ADD TO " + targetRow->getTrackName().toUpperCase()
                        : juce::String("RELEASE TO CREATE FOLDER");
                    auto actionBounds = destBounds.reduced(10.0f, 5.0f).withHeight(17.0f);
                    g.setColour(Theme::getInstance().apex.color.deepestA.withAlpha(0.86f));
                    g.fillRoundedRectangle(actionBounds, 4.0f);
                    g.setColour(Theme::getInstance().apex.color.folderTimberBright);
                    g.setFont(Theme::getInstance().fonts.bold.withHeight(10.5f));
                    g.drawText(actionText, actionBounds.toNearestInt(), juce::Justification::centred, true);

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

                        g.setColour(folderBrown.withAlpha(0.6f));
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
            setMouseCursor(DAW::CursorThemeCore::getStandard(
                dragHover_ ? juce::MouseCursor::LeftRightResizeCursor
                           : juce::MouseCursor::NormalCursor));
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // The TRACKS "+" is a REAL child component (TrackListHeaderBar::
        // QuickAddButton) and consumes its own pointer events — it can never
        // fall through to a track row scrolled underneath the pinned header.
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
        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::NormalCursor));
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
    std::function<void(const TrackID&)>          onExclusiveMuteRequested;
    std::function<void(const TrackID&)>          onConvertFolderToTrackRequested;
    std::function<void(const TrackID&)>          onOpenPianoRollRequested;
    std::function<void(const TrackID&)>          onPreFaderToggleRequested;
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

    /** Authoritative folder topology lookups (wired from MainComponent to
     *  FolderBusCore). Track::parentTrackID_ is a derived UI hint that can be
     *  empty/stale; these callbacks let the drop-target resolver consult the
     *  canonical childToParent_ map so dropping on a visible child of an open
     *  folder always resolves the owning folder as the adoption target. */
    std::function<TrackID(const TrackID&)> onGetParentFolderBus;
    std::function<bool(const TrackID&)>    onIsFolderBus;

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
    /** Returns true when the host advanced the Arranger viewport. */
    std::function<bool(int deltaY)> onReorderAutoScroll;
    /** Multi-track reorder: all selected track IDs moved as one block to the
     *  destination gap. Fired instead of onTrackReorderRequested when the
     *  dragged row is part of a multi-selection. The host applies the move in
     *  a single undo step. */
    TrackReorderCore::GroupReorderRequestedCallback onMultiTrackReorderRequested;
    /** Returns the full set of currently multi-selected track IDs (wired from
     *  MainComponent to the selection model). */
    std::function<std::vector<TrackID>()> onGetMultiSelectedTrackIds;
    /** Callback: a plugin was drag-dropped onto a track row from another track. */
    std::function<void(const TrackID& srcTrack, int srcSlot, const TrackID& destTrack)> onPluginDropReceived;
    /** Callback: returns all audio clips on a track (for automation menus). */
    std::function<juce::Array<Clip*>(const TrackID&)> onGetAudioClipsOnTrack;
    std::function<RoutingGraph*()> onGetRoutingGraph;

    // ── Quick Send mode callbacks (wired from MainComponent) ────────────────
    /** Row body double-click while Quick Send mode is inactive → enter mode
     *  with that row as source. While active → retarget or exit. */
    std::function<void(TrackRow*)> onQuickSendToggleRequested;
    /** Row body left-click while Quick Send mode is active → toggle send (create if missing, remove if exists) */
    std::function<void(TrackRow*)> onQuickSendToggleSend;
    /** Row body right-click while Quick Send mode is active → toggle sidechain (create if missing, remove if exists) */
    std::function<void(TrackRow*)> onQuickSendToggleSidechain;
    /** Right-click while Quick Send mode is active → show context menu (delete target, exit) */
    std::function<void()> onQuickSendExitRequested;

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
                                const std::vector<std::pair<TrackID, float>>& feedbackTargets,
                                const juce::Colour& cableColour = juce::Colour(0xFFFF2D78))
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
            row->setBubblegumFeedback(false, found, level, cableColour);
        }
    }

    /** Clear all Bubblegum feedback from track rows. */
    void clearBubblegumFeedback()
    {
        for (auto* row : trackRows_)
            row->setBubblegumFeedback(false, false);
    }

    /** Push Quick Send mode state to all rows (gold aura on the source row). */
    void setQuickSendMode(bool active, const TrackID& sourceId)
    {
        if (quickSendModeActive_ == active && quickSendSourceId_ == sourceId)
            return;
        quickSendModeActive_ = active;
        quickSendSourceId_   = sourceId;
        for (auto* row : trackRows_)
        {
            row->setQuickSendSource(active && row->getTrackID() == sourceId);
            row->setQuickSendModeActive(active);
        }
    }

    bool isQuickSendModeActive() const noexcept { return quickSendModeActive_; }
    TrackID getQuickSendSourceId() const noexcept { return quickSendSourceId_; }

    void setTrackQuickSendReceiveTarget(const TrackID& trackId, bool on)
    {
        for (auto* row : trackRows_)
        {
            if (row == nullptr)
                continue;
            if (row->getTrackID() == trackId)
            {
                row->setQuickSendReceiveTarget(on);
                break;
            }
        }
    }

/** Row body click while Quick Send mode is active → forward to host.
      *  Left-click toggles the send (create if missing, remove if exists).
      *  Right-click toggles the sidechain (create if missing, remove if exists).
      *  Modifier clicks (Ctrl/Cmd/Shift) are ignored to preserve multi-select semantics. */
    void handleQuickSendRowClick(TrackRow* clicked, const juce::ModifierKeys& mods)
    {
        if (!quickSendModeActive_ || clicked == nullptr)
            return;
        // Ignore modified clicks (Ctrl/Cmd/Shift) to preserve multi-select semantics
        if (mods.isCtrlDown() || mods.isCommandDown() || mods.isShiftDown())
            return;

        if (mods.isLeftButtonDown()) {
            if (onQuickSendToggleSend)
                onQuickSendToggleSend(clicked);
        } else if (mods.isRightButtonDown()) {
            if (onQuickSendToggleSidechain)
                onQuickSendToggleSidechain(clicked);
        }
        // Middle button or other clicks are ignored
    }

    /** Screen bounds of the row holding the Quick Send source (for popup
     *  positioning); empty if the source row is not visible. */
    juce::Rectangle<int> getQuickSendSourceRowScreenBounds() const
    {
        for (auto* row : trackRows_)
            if (row->getTrackID() == quickSendSourceId_)
                return row->getScreenBounds();
        return {};
    }

    /** Refresh every row's POST/PRE button from the routing graph.
     *  `query` returns the pre/post-fader summary for that track
     *  (0 = no sends, 1 = all post, 2 = all pre, 3 = mixed). */
    void refreshPreFaderStates(const std::function<int(const TrackID&)>& query)
    {
        for (auto* row : trackRows_)
            if (row != nullptr)
                row->setPreFaderState(query(row->getTrackID()));
    }

    void clearSelection()
    {
        selectedTrackIds_.clear();
        for (auto* row : trackRows_)
            row->setSelected(false);
        updatePresentationDemand();
    }

    /** Select a track by ID — updates visual state only (does not fire onTrackSelected). */
    void selectTrackById(const TrackID& id)
    {
        selectedTrackIds_.clear();
        if (id.isNotEmpty())
            selectedTrackIds_.insert(id);
        for (auto* row : trackRows_)
            row->setSelected(row->getTrackID() == id);
        updatePresentationDemand();
    }

    /** Multi-selection: mark all rows in the set as selected, others deselected. */
    void applyMultiSelectionVisual(const std::vector<juce::String>& selectedIds)
    {
        selectedTrackIds_.clear();
        selectedTrackIds_.insert(selectedIds.begin(), selectedIds.end());
        for (auto* row : trackRows_)
            row->setSelected(selectedTrackIds_.count(row->getTrackID()) > 0);
        updatePresentationDemand();
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
    TrackListHeaderBar headerBar_;   // pinned overlay with the real TRACKS "+"
    int  dragStartX_ = -1;
    bool dragHover_  = false;
    int  scrollOffsetY_ = 0;
    int  reorderPointerY_ = -1;
    TrackRow* reorderSource_ = nullptr;
    std::vector<TrackID> reorderSelectionAtDragStart_;
    int  dropIndicatorY_ = -1;
    int  ghostRowY_ = -1;
    TrackID folderDropTargetTrackId_;
    TrackID folderDropHoverTrackId_;
    bool folderDropIsAdoption_ = false;
    std::unordered_set<TrackID> collapsedFolders_;
    std::unordered_set<TrackID> selectedTrackIds_;
    bool   quickSendModeActive_ = false;
    TrackID quickSendSourceId_;

    /** Move existing rows for a pure viewport translation.  Width, height,
        child layout, selection, and folder state are all structural state and
        remain untouched until TrackList::resized() is required by a real
        geometry/topology change. */
    void positionRowsForScroll()
    {
        int y = 80 - scrollOffsetY_;
        for (auto* row : trackRows_)
        {
            if (row == nullptr)
                continue;

            TimelinePaintMetrics::inc(TimelinePaintMetrics::trackListScrollRowPositionUpdates);
            row->setTopLeftPosition(0, y);
            y += row->getDesiredHeight();
        }

        updatePresentationDemand();
    }

    bool hasVisiblePresentationWork() const
    {
        if (!isShowing())
            return false;

        const auto visibleArea = getLocalBounds().withTrimmedTop(80);
        for (auto* row : trackRows_)
        {
            if (row != nullptr && row->isShowing()
                && visibleArea.intersects(row->getBounds())
                && row->needsPresentationTick())
                return true;
        }
        return false;
    }

    void updatePresentationDemand()
    {
        const bool needsPresentation = hasVisiblePresentationWork();

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

    bool presentationUpdateActive_ = false;

    void timerCallback() override
    {
        if (reorderSource_ == nullptr)
        {
            stopTimer();
            return;
        }

        const auto screenPosition = juce::Desktop::getInstance().getMousePosition();
        if (!getScreenBounds().contains(screenPosition))
        {
            stopTimer();
            return;
        }

        reorderPointerY_ = getLocalPoint(nullptr, screenPosition).y;
        constexpr int kHeaderBottom = 80;
        constexpr int kEdgeZone = 64;
        const int bottom = getHeight();
        int delta = 0;

        if (reorderPointerY_ >= kHeaderBottom
            && reorderPointerY_ < kHeaderBottom + kEdgeZone)
        {
            const float proximity = (float) (kHeaderBottom + kEdgeZone - reorderPointerY_)
                                   / (float) kEdgeZone;
            delta = -juce::jmax(1, juce::roundToInt(22.0f * proximity * proximity));
        }
        else if (reorderPointerY_ >= bottom - kEdgeZone
                 && reorderPointerY_ < bottom)
        {
            const float proximity = (float) (reorderPointerY_ - (bottom - kEdgeZone))
                                   / (float) kEdgeZone;
            delta = juce::jmax(1, juce::roundToInt(22.0f * proximity * proximity));
        }

        if (delta == 0 || !onReorderAutoScroll || !onReorderAutoScroll(delta))
            return;

        // Scrolling changes row coordinates while the pointer stays still.
        // Recompute the insertion target against the new coordinates.
        if (folderDropTargetTrackId_.isEmpty())
        {
            updateDropIndicator(reorderPointerY_);
            ghostRowY_ = reorderPointerY_ - reorderSource_->getDesiredHeight() / 2;
        }
        repaint();
    }

    struct FolderDropTarget
    {
        TrackRow* semanticTarget = nullptr;
        TrackRow* hoveredRow = nullptr;
        bool adoption = false;
    };

    TrackRow* findRowByTrackId(const TrackID& id) const
    {
        for (auto* row : trackRows_)
            if (row != nullptr && row->getTrackID() == id)
                return row;
        return nullptr;
    }

    bool wouldCreateFolderCycle(const TrackID& draggedId, const TrackID& candidateFolderId) const
    {
        if (draggedId.isEmpty() || candidateFolderId.isEmpty() || draggedId == candidateFolderId)
            return true;

        auto ancestorId = candidateFolderId;
        for (int guard = 0; guard < 32 && ancestorId.isNotEmpty(); ++guard)
        {
            if (ancestorId == draggedId)
                return true;
            auto* ancestor = trackManager_.getTrack(ancestorId);
            if (ancestor == nullptr)
                break;
            ancestorId = ancestor->getParentTrackID();
        }
        return false;
    }

    FolderDropTarget findFolderDropTarget(juce::Point<int> localPos, TrackRow* dragSource) const
    {
        // ── Folder-bus drag & drop DISABLED (product decision) ────────────
        // Track-list drops never create or adopt folder buses: no folder
        // ghost appears while dragging and the drop falls through to a plain
        // reorder. The detection logic below is retained but unreachable so
        // the feature can be re-enabled later without rewriting.
        return {};

        // Is the row (or a track ID) a FolderBus? Uses the authoritative
        // FolderBusCore map via onIsFolderBus when the role is not FolderBus.
        const auto isFolderBusTrack = [this](const TrackID& id) -> bool
        {
            if (id.isEmpty())
                return false;
            if (auto* t = trackManager_.getTrack(id))
                if (t->getRole() == TrackRole::FolderBus)
                    return true;
            return onIsFolderBus != nullptr && onIsFolderBus(id);
        };

        // The dragged track's current owning folder (if any). Needed so a
        // track ALREADY inside a folder can still be reordered among its
        // siblings instead of being re-adopted (which the validator rejects).
        const auto draggedParentId = [this, dragSource]() -> TrackID
        {
            if (dragSource == nullptr)
                return {};
            const TrackID direct = dragSource->getParentTrackID();
            if (direct.isNotEmpty())
                return direct;
            if (onGetParentFolderBus != nullptr)
                return onGetParentFolderBus(dragSource->getTrackID());
            return {};
        }();

        for (auto* row : trackRows_)
        {
            if (row == nullptr || row == dragSource || row->isMasterRow())
                continue;

            auto rowLocalPos = row->getLocalPoint(this, localPos);
            if (!row->getLocalBounds().contains(rowLocalPos))
                continue;

            // ── 1) Child of an open folder: the WHOLE row adopts into the
            //       owning folder. Adoption outranks reorder here — dropping
            //       anywhere on the folder's visible child body puts the
            //       dragged track inside that folder. (Tracks already inside
            //       the same folder fall through to sibling reorder.)
            TrackID parentId = row->getParentTrackID();
            if (parentId.isEmpty() && onGetParentFolderBus != nullptr)
                parentId = onGetParentFolderBus(row->getTrackID());

            if (parentId.isNotEmpty() && parentId != draggedParentId)
            {
                if (auto* parentRow = findRowByTrackId(parentId);
                    parentRow != nullptr && isFolderBusTrack(parentId)
                        && !wouldCreateFolderCycle(dragSource->getTrackID(), parentId))
                {
                    return { parentRow, row, true };
                }
            }

            // ── 1b) Stale-link fallback: if the hovered row has NO recorded
            //        folder parent (child links lost / not yet synced) but the
            //        row directly above it in the visible list is an open
            //        folder, the row is part of that folder's child block.
            //        The child area of an open folder is ONE adoption zone —
            //        no non-folder track can be dropped there and stay outside.
            if (parentId.isEmpty())
            {
                const int rowIdx = trackRows_.indexOf(row);
                if (rowIdx > 0)
                {
                    auto* above = trackRows_[rowIdx - 1];
                    if (above != nullptr && above != dragSource
                        && isFolderBusTrack(above->getTrackID())
                        && !wouldCreateFolderCycle(dragSource->getTrackID(), above->getTrackID()))
                    {
                        return { above, row, true };
                    }
                }
            }

            // ── 2) Folder-bus row: the WHOLE row adopts into it.
            if (isFolderBusTrack(row->getTrackID())
                && !wouldCreateFolderCycle(dragSource->getTrackID(), row->getTrackID()))
            {
                return { row, row, true };
            }

            // ── 3) Regular (non-folder-child) row: only the CENTER band
            //       creates a folder. The top/bottom edge zones always mean
            //       "reorder before/after this row" — without this, dragging a
            //       track UP over another row's name strip is hijacked into the
            //       folder-drop path and the reorder never happens.
            if (parentId.isEmpty())
            {
                const float edgeBand = juce::jmax(10.0f, row->getHeight() * 0.28f);
                if (rowLocalPos.y < edgeBand
                    || rowLocalPos.y > (float) row->getHeight() - edgeBand)
                    continue;

                if (row->getFolderDropTargetBounds().toNearestInt().contains(rowLocalPos)
                    && !wouldCreateFolderCycle(dragSource->getTrackID(), row->getTrackID()))
                {
                    return { row, row, false };
                }
            }
        }

        return {};
    }
    
    void rebuildTrackRows()
    {
        stopTimer();
        reorderSource_ = nullptr;
        reorderSelectionAtDragStart_.clear();
        reorderPointerY_ = -1;
        trackRows_.clear();

        // Master track row always first (matches arrangement view)
        if (trackManager_.hasMasterTrack())
        {
            auto* masterRow = new TrackRow(*trackManager_.getMasterTrack());
            masterRow->setDesiredHeight(108);
            masterRow->setSelected(selectedTrackIds_.count(masterRow->getTrackID()) > 0);
            masterRow->onPresentationDemandChanged = [this]
            {
                updatePresentationDemand();
            };
            masterRow->onGetIsMultiSelected = [this](const TrackID& id) -> bool
            {
                return onGetIsMultiSelectedCallback
                    ? onGetIsMultiSelectedCallback(id) : false;
            };
            masterRow->onSelectedWithModifiers = [this](TrackRow* clicked, const juce::ModifierKeys& mods)
            {
                const bool preserve = !mods.isCtrlDown() && !mods.isCommandDown()
                    && !mods.isShiftDown() && onGetIsMultiSelectedCallback
                    && onGetIsMultiSelectedCallback(clicked->getTrackID());
                if (!preserve && !mods.isCtrlDown() && !mods.isCommandDown() && !mods.isShiftDown())
                    for (auto* r : trackRows_)
                        r->setSelected(r == clicked);
                else
                    clicked->setSelected(true);
                if (onTrackSelectedWithModifiers)
                    onTrackSelectedWithModifiers(clicked->getTrackID(), mods);
                else if (onTrackSelected)
                    onTrackSelected(clicked->getTrackID());
                handleQuickSendRowClick(clicked, mods);
            };
            masterRow->onQuickSendToggleRequested = [this](TrackRow* clicked)
            {
                if (onQuickSendToggleRequested) onQuickSendToggleRequested(clicked);
            };
            masterRow->onQuickSendTargetClicked = [this](TrackRow* clicked)
            {
                if (onQuickSendToggleSend) onQuickSendToggleSend(clicked);
            };
            masterRow->onQuickSendDeleteTargetRequested = [this](TrackRow* clicked)
            {
                if (onQuickSendToggleSidechain) onQuickSendToggleSidechain(clicked);
            };
            masterRow->onQuickSendExitRequested = [this]
            {
                if (onQuickSendExitRequested) onQuickSendExitRequested();
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
                row->setSelected(selectedTrackIds_.count(row->getTrackID()) > 0);
                row->setIndentLevel(depth);
                row->onPresentationDemandChanged = [this]
                {
                    updatePresentationDemand();
                };
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
                    const bool preserve = !mods.isCtrlDown() && !mods.isCommandDown()
                        && !mods.isShiftDown() && onGetIsMultiSelectedCallback
                        && onGetIsMultiSelectedCallback(clicked->getTrackID());
                    if (!preserve && !mods.isCtrlDown() && !mods.isCommandDown() && !mods.isShiftDown())
                        for (auto* r : trackRows_)
                            r->setSelected(r == clicked);
                    else
                        clicked->setSelected(true);
                    if (onTrackSelectedWithModifiers)
                        onTrackSelectedWithModifiers(clicked->getTrackID(), mods);
                    else if (onTrackSelected)
                        onTrackSelected(clicked->getTrackID());
                    handleQuickSendRowClick(clicked, mods);
                };
                row->onQuickSendToggleRequested = [this](TrackRow* clicked)
                {
                    if (onQuickSendToggleRequested) onQuickSendToggleRequested(clicked);
                };
                row->onQuickSendTargetClicked = [this](TrackRow* clicked)
                {
                    if (onQuickSendToggleSend) onQuickSendToggleSend(clicked);
                };
                row->onQuickSendDeleteTargetRequested = [this](TrackRow* clicked)
                {
                    if (onQuickSendToggleSidechain) onQuickSendToggleSidechain(clicked);
                };
                row->onQuickSendExitRequested = [this]
                {
                    if (onQuickSendExitRequested) onQuickSendExitRequested();
                };
                row->onPluginDropReceived = [this](const TrackID& src, int slot, const TrackID& dest)
                {
                    if (onPluginDropReceived) onPluginDropReceived(src, slot, dest);
                };
                row->onToggleMute = [this](const TrackID& id) { if (onToggleMuteRequested) onToggleMuteRequested(id); };
                row->onExclusiveMute = [this](const TrackID& id) { if (onExclusiveMuteRequested) onExclusiveMuteRequested(id); };
                row->onOpenPianoRoll = [this](const TrackID& id) { if (onOpenPianoRollRequested) onOpenPianoRollRequested(id); };
                row->onPreFaderToggleRequested = [this](const TrackID& id) { if (onPreFaderToggleRequested) onPreFaderToggleRequested(id); };
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
                     if (reorderSource_ != src)
                     {
                         reorderSelectionAtDragStart_.clear();
                         if (onGetIsMultiSelectedCallback
                             && onGetIsMultiSelectedCallback(src->getTrackID())
                             && onGetMultiSelectedTrackIds)
                             reorderSelectionAtDragStart_ = onGetMultiSelectedTrackIds();
                      }
                      reorderSource_ = src;
                      auto localPt = e.getEventRelativeTo(this).getPosition();
                      reorderPointerY_ = localPt.y;
                      // Semantic drag auto-scroll while the pointer is held at
                      // an edge. This is control-time behavior, not a visual
                      // animation scheduler; visual rows use the presentation
                      // clock below.
                      startTimerHz(60);
                     const auto target = findFolderDropTarget(localPt, src);
                    if (target.semanticTarget != nullptr)
                    {
                        folderDropTargetTrackId_ = target.semanticTarget->getTrackID();
                        folderDropHoverTrackId_ = target.hoveredRow != nullptr ? target.hoveredRow->getTrackID() : TrackID{};
                        folderDropIsAdoption_ = target.adoption;
                        dropIndicatorY_ = -1;
                        ghostRowY_ = target.hoveredRow != nullptr ? target.hoveredRow->getY() : target.semanticTarget->getY();
                    }
                    else
                    {
                        folderDropTargetTrackId_ = {};
                        folderDropHoverTrackId_ = {};
                        folderDropIsAdoption_ = false;
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
                row->onConvertFolderToTrack = [this](const TrackID& id)
                {
                    if (onConvertFolderToTrackRequested)
                        onConvertFolderToTrackRequested(id);
                };
                 row->onReorderDrop = [this](TrackRow* src)
                 {
                     stopTimer();
                     reorderPointerY_ = -1;
                     // Capture data before any rebuild can destroy src
                    TrackID srcId = src->getTrackID();
                    TrackID folderTargetId = folderDropTargetTrackId_;
                     int targetIdx = (dropIndicatorY_ >= 0) ? getDropTargetIndex(dropIndicatorY_) : -1;
                     int srcIdx = trackManager_.getTrackIndex(srcId);
                     auto selectedAtDragStart = reorderSelectionAtDragStart_;

                    // Clear visual state immediately
                    reorderSource_ = nullptr;
                    dropIndicatorY_ = -1;
                    ghostRowY_ = -1;
                     folderDropTargetTrackId_ = {};
                     folderDropHoverTrackId_ = {};
                     folderDropIsAdoption_ = false;
                     reorderSelectionAtDragStart_.clear();
                     repaint();

                    if (folderTargetId.isNotEmpty() && onFolderDropRequested)
                    {
                        juce::MessageManager::callAsync([cb = onFolderDropRequested, srcId, folderTargetId]()
                        {
                            if (cb) cb(srcId, folderTargetId);
                        });
                        return;
                    }

                    // Multi-track reorder: when the dragged row is part of a
                    // multi-selection, move the whole selection as one block
                    // in a single undo step (host-side).
                     if (srcIdx >= 0 && targetIdx >= 0
                         && onGetIsMultiSelectedCallback && onGetIsMultiSelectedCallback(srcId)
                         && onMultiTrackReorderRequested)
                     {
                         auto selectedIds = selectedAtDragStart;
                         if (selectedIds.empty() && onGetMultiSelectedTrackIds)
                             selectedIds = onGetMultiSelectedTrackIds();
                         if (selectedIds.size() > 1)
                         {
                            auto multiReorder = onMultiTrackReorderRequested;
                            juce::MessageManager::callAsync([multiReorder, selectedIds, targetIdx]()
                            {
                                if (multiReorder) multiReorder(selectedIds, targetIdx);
                            });
                            return;
                        }
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

    // Re-apply Quick Send mode state after the rebuild.
    for (auto* row : trackRows_)
    {
        row->setQuickSendSource(quickSendModeActive_ && row->getTrackID() == quickSendSourceId_);
        row->setQuickSendModeActive(quickSendModeActive_);
    }

    // The pinned header overlay (and its real "+" button) must always stay
    // above freshly-added rows so a scrolled row can never intercept the
    // Quick Track Builder click.
    headerBar_.toFront(false);

    resized();
}
    
    // TrackManager::Listener
    void trackAdded(Track*) override { if (!trackManager_.isRestoringState()) rebuildTrackRows(); }
    void trackRemoved(const TrackID&) override { if (!trackManager_.isRestoringState()) rebuildTrackRows(); }
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
        // The end of the normal-track list is an explicit gap after the final
        // track.  Master is a pinned presentation row and is never part of
        // this destination coordinate system.
        return trackManager_.getNumTracks();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackList)
};

} // namespace DAW
