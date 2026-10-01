#pragma once
#include <JuceHeader.h>
#include "../PluginScanCore/PluginScanAuditLogCore.h"
#include "../ThemeCore/Theme.h"
#include "../ThemeCore/ApexPrimitives.h"
#include "../TrackCore/Track.h"
#include "../ControlsCore/ModernControls.h"
#include "../ChannelStripCore/LevelMeter.h"
#include "../MixerScaleCore/FaderGradientRenderer.h"
#include "../MixerScaleCore/DbPositionMapper.h"
#include "../MixerScaleCore/FaderRangeCore.h"
#include "../MasterStripCore/MasterStripCeilingBinding.h"
#include "../MasterStripCore/MasterStripCeilingSection.h"
#include "../MasterStripCore/MasterStripDitherBinding.h"
#include "../MasterStripCore/MasterStripDitherSection.h"
#include "../MasterStripCore/MasterStripDripBackgroundRenderer.h"
#include "../MasterStripCore/MasterStripFlipState.h"
#include "../MasterStripCore/MasterStripMeteringSection.h"
#include "../MasterStripCore/MasterStripPhaseWidthSection.h"
#include "../MasterCore/MasterCeilingCore.h"
#include "../MasterCore/MasterDitherCore.h"
#include "../MeteringCore/PhaseWidthFacadeCore.h"
#include "../InputMonitorCore/TrackMonitoringStateModel.h"
#include "../PluginHostCore/PluginSlotUI.h"
#include "TrackSelectionVisualCore.h"
#include "InputTrimFloatingPanel.h"
#include "ApexPresentationClock.h"
#include "MixerUiFeaturePolicy.h"
#include "../TrackCore/TrackReorderCore.h"
#include "../AnalogVuMeterCore/CompactVuNeedle.h"
#include "../Bubblegum/BubblegumV2System.h"
#include "../Bubblegum/BubblegumAppearanceSettings.h"
#include "../Bubblegum/BubblegumCableStyleSettingsCore.h"
#include "../FolderBusCore/FolderBusCore.h"
#include "../FolderBusCore/FolderBusRoutingValidator.h"
#include "MixerDragStateMachine.h"
#include "../MixerFolderCore/MixerFolderIntegrationCore.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../Automation/AutomationSystemCore.h"
#include "../Automation/AutomationParameterKeyCore.h"
#include <unordered_map>
#include <unordered_set>

namespace DAW {

// ─── MixerStrip ──────────────────────────────────────────────────────────────

// ── Cache key for paintPremiumStripBase static layers ────────────────────────
// Only (width, height, selected) affect the output — the function reads nothing else.
struct PremiumBaseKey
{
    int  width    = 0;
    int  height   = 0;
    bool selected = false;

    bool operator==(const PremiumBaseKey& o) const noexcept
    {
        return width == o.width && height == o.height && selected == o.selected;
    }
    bool operator!=(const PremiumBaseKey& o) const noexcept { return !(*this == o); }
};

class MixerStrip : public juce::Component,
                   public Track::Listener,
                   public FaderRangeCore::Listener,
                   public apex::automation::AutomationParameter::Listener
{
public:
    enum class BgRole { None, Source, Target };

    MixerStrip(Track& track, FaderRangeCore& faderRange)
        : track_(track), faderRange_(faderRange)
    {
        setOpaque(false);
        // The Master is a pinned sibling inside the same viewed component.
        // Normal strips must remain clipped to their own bounds so their glow,
        // fader labels, and child controls cannot paint through the Master
        // boundary while the viewport is scrolled by a partial strip pitch.
        setPaintingIsUnclipped(false);
        track_.addListener(this);
        if (auto* c = FaderRangeCore::getGlobalInstance()) c->addListener(this);

        // ===== APEX Automation Phase 1C — register volume as automatable param =====
        {
            using KR = apex::automation::AutomationParameterKeyRegistry;
            const auto paramID = KR::getInstance().getOrCreateID (KR::trackVolumeKey (track_.getID()));

            apex::automation::ParameterRange range;
            range.minValue     = 0.0f;
            range.maxValue     = 1.0f;
            range.defaultValue = juce::jlimit (0.0f, 1.0f,
                faderRange_.dbToNorm (FaderRangeCore::gainToDb (track_.getVolume())));
            range.skew         = 1.0f;
            range.isStepped    = false;

            const juce::String displayName = track_.isMaster()
                ? juce::String ("Master Volume")
                : (track_.getName() + " Volume");

            bool createdVolumeParameter = false;
            {
                auto& sys = apex::automation::AutomationSystem::getInstance();
                createdVolumeParameter = (sys.getRegistry().find (paramID) == nullptr);
                volumeParameter_ = sys.createNativeParameter (paramID, displayName, range);
            }

            if (volumeParameter_ != nullptr)
            {
                if (createdVolumeParameter)
                    volumeParameter_->setValueProgrammatic (range.defaultValue);

                volumeParameter_->addListener (this);
            }
        }
        // ===== End Phase 1C registration =============================================

        registerAdditionalNativeAutomationParameters();

        initChildren();
    }

    ~MixerStrip() override
    {
        detachFromTrack();
    }

    /** Detach every external listener while track_ is still alive.  A strip
        may be retained until the current UI callback unwinds, so its destructor
        must not dereference a Track that TrackManager has already destroyed. */
    void detachFromTrack() noexcept
    {
        if (! listenersAttached_)
            return;

        vu_.clearSource();
        unregisterNativeAutomationParameters();
        if (auto* c = FaderRangeCore::getGlobalInstance())
            c->removeListener(this);
        if (flipObserver_ != nullptr)
            MasterStripFlipState::getGlobalInstance().removeListener(flipObserver_.get());
        track_.removeListener(this);
        listenersAttached_ = false;
    }

    void faderRangeChanged() override { invalidateFaderScaleCache(); repaint(); }

    // ===== AutomationParameter::Listener (Phase 1C) =======================
    void parameterValueChanged (apex::automation::AutomationParameter& p,
                                float                                  newNormalized,
                                apex::automation::ChangeSource         source) override
    {
        if (updatingFromAutomation_) return;
        juce::ignoreUnused (source);

        const juce::ScopedValueSetter<bool> guard (updatingFromAutomation_, true);

        const float clampedNorm = juce::jlimit (0.0f, 1.0f, newNormalized);
if (&p == volumeParameter_)
        {
            const float newDb       = faderRange_.normToDb (clampedNorm);
            const float newGain     = faderRange_.dbToGain (newDb);

            track_.setVolume (newGain);

            // During a fader drag the mouseDrag path already repaints the
            // fader lane; a full-strip repaint per mouse tick re-rasterizes
            // the entire strip and is the visible "fader lag". Repaint only
            // the fader lane while dragging; mouseUp does one final full
            // repaint when the gesture ends.
            if (draggingFader_)
                repaintFaderRegion();
            else
                repaint();
            return;
        }

        if (&p == panParameter_)
        {
            const float uiPan = clampedNorm * 2.0f - 1.0f;
            track_.setPan (uiPan);
            panKnob_.value = uiPan;
            panKnob_.repaint();
            return;
        }

        if (&p == muteParameter_)
        {
            const bool muted = clampedNorm >= 0.5f;
            track_.setMuted (muted);
            muteBtn_.active = muted;
            muteBtn_.repaint();
            return;
        }

        if (&p == soloParameter_)
        {
            const bool soloed = clampedNorm >= 0.5f;
            track_.setSoloed (soloed);
            soloBtn_.active = soloed;
            soloBtn_.repaint();
            return;
        }

        for (const auto& [routeID, sendParam] : sendParameters_)
        {
            if (&p != sendParam || routingGraph_ == nullptr)
                continue;

            if (auto* sourceNode = routingGraph_->getNodeByTrackId (track_.getID()))
            {
                for (auto* conn : routingGraph_->getOutputConnections (sourceNode->id))
                {
                    if (conn != nullptr && conn->id == routeID)
                    {
                        conn->gain.store (clampedNorm * 2.0f, std::memory_order_relaxed);
                        routingGraph_->publishSnapshotOnly();
                        break;
                    }
                }
            }
            return;
        }

        for (const auto& [routeID, bypassParam] : sendBypassParameters_)
        {
            if (&p != bypassParam || routingGraph_ == nullptr)
                continue;

            if (auto* sourceNode = routingGraph_->getNodeByTrackId (track_.getID()))
            {
                for (auto* conn : routingGraph_->getOutputConnections (sourceNode->id))
                {
                    if (conn != nullptr && conn->id == routeID)
                    {
                        conn->active.store (clampedNorm < 0.5f, std::memory_order_relaxed);
                        routingGraph_->publishSnapshotOnly();
                        break;
                    }
                }
            }
            return;
        }
    }

    void parameterGestureBegan (apex::automation::AutomationParameter&) override {}
    void parameterGestureEnded (apex::automation::AutomationParameter&) override {}
    // ===== End AutomationParameter::Listener ==============================

    Track& getTrack() const noexcept { return track_; }

    void bindRoutingGraph(RoutingGraph* graph)
    {
        routingGraph_ = graph;
        registerSendAutomationParameters();
    }

    /** Preferred width for this strip in the mixer layout.
     *  Master strips need extra width for the multi-section UI (ceiling,
     *  dither, metering, phase/width). Regular strips use the panel's
     *  standard kStripW. */
    int getPreferredWidth() const noexcept
    {
        return track_.isMaster() ? 170 : 80;
    }

    void setBubblegumRole(bool active, BgRole role)
    {
        bgActive_ = active;
        bgRole_   = role;
        repaint();
    }

BgRole getBubblegumRole() const noexcept   { return bgRole_; }
    bool   isBubblegumActive() const noexcept  { return bgActive_; }

    /** Reflects the pre/post-fader summary of every send leaving this strip:
     *  0 = no sends, 1 = all post-fader, 2 = all pre-fader, 3 = mixed.
     *  The button label flips between "—", "POST", "PRE" and "MIX". */
    void setPreFaderState(int summary)
    {
        switch (summary)
        {
            case 0:  preFaderBtn_.label  = "—";  preFaderBtn_.active = false; break;
            case 1:  preFaderBtn_.label  = "POST"; preFaderBtn_.active = false; break;
            case 2:  preFaderBtn_.label  = "PRE";  preFaderBtn_.active = true;  break;
            default: preFaderBtn_.label  = "MIX";  preFaderBtn_.active = true;  break;
        }
        preFaderBtn_.repaint();
    }

    void setSelected(bool sel)
    {
        if (lastSetSelectedValue_ == sel && lastSetSelectedInitialised_)
            return;
        lastSetSelectedValue_       = sel;
        lastSetSelectedInitialised_ = true;

        selectionVisual_.setSelected(sel);

        repaint();
    }

    // Wired by MixerPanel after construction
    std::function<bool(const TrackID&)>        onQueryHasSend;
    std::function<bool(const TrackID&)>        onQueryHasSidechain;
    std::function<juce::Colour(const TrackID&)> onQuerySendColour;
    std::function<juce::Colour(const TrackID&)> onQuerySidechainColour;

    /** Double-click on an empty part of this strip toggles Quick Send mode
     *  with this track as source (timeline-row parity). */
    std::function<void(const TrackID&)>        onQuickSendToggleRequested;

    /** POST/PRE button clicked — open the per-target pre/post-fader menu for
     *  this strip's sends (single send toggles directly). */
    std::function<void(const TrackID&)>        onPreFaderToggleRequested;

    bool stepLava(float dt)
    {
        if (useLegacyMixerSkin())
            return false;

        if (track_.isMaster() && selectionVisual_.getLavaCore().isAlwaysVisible())
            return false;

        if (!selectionVisual_.needsAnimationTick())
            return false;

        selectionVisual_.stepSweep(dt);

        return selectionVisual_.getLavaCore().stepExternal(dt);
    }

    /** Present visible Master-only sections from the owning MixerPanel clock.
        The sections themselves do not own a timer. */
    void presentationTick()
    {
        if (!track_.isMaster() || !isShowing())
            return;

        if (ceilingSection_ != nullptr && ceilingSection_->isShowing())
            ceilingSection_->presentationTick();
        if (meteringSection_ != nullptr && meteringSection_->isShowing())
            meteringSection_->presentationTick();
        if (phaseWidthSection_ != nullptr && phaseWidthSection_->isShowing())
            phaseWidthSection_->presentationTick();
    }

    void activateLava()
    {
        if (useLegacyMixerSkin())
            return;

        selectionVisual_.getLavaCore().setActive(true);
    }

    void setAccentColour(juce::Colour c)
    {
        selectionVisual_.setAccentColour(c);
    }

    void setBubbleColours(juce::Colour deep, juce::Colour near)
    {
        selectionVisual_.getLavaCore().setBubbleColours(deep, near);
    }

    void setSphereColour(juce::Colour c)
    {
        selectionVisual_.getLavaCore().setSphereColour(c);
    }

    void setParticleColour(juce::Colour c)
    {
        selectionVisual_.getLavaCore().setParticleColour(c);
    }

    void setSpheresPainted(bool on)
    {
        selectionVisual_.getLavaCore().setSpheresPainted(on);
    }

    void setAlwaysVisible(bool always)
    {
        selectionVisual_.getLavaCore().setAlwaysVisible(always);
    }

    void setRestBubbleColours(juce::Colour deep, juce::Colour near)
    {
        selectionVisual_.setRestBubbleColours(deep, near);
    }

    void setFolderVisualState(bool isFolderBus, bool expanded, int depth,
                              float openAmount = 1.0f, float shimmerProgress = -1.0f,
                              juce::Colour parentColour = juce::Colours::transparentBlack)
    {
        isFolderBus_ = isFolderBus;
        folderExpanded_ = expanded;
        depth_ = juce::jmax(0, depth);
        folderOpenAmount_ = juce::jlimit(0.0f, 1.0f, openAmount);
        folderShimmerProgress_ = shimmerProgress;
        folderParentColour_ = parentColour;
        folderToggleBtn_.setVisible(isFolderBus_);
        updateTrackLabelPresentation();
        folderToggleBtn_.expandedAmount = folderOpenAmount_;
        if (! getLocalBounds().isEmpty())
            resized();
        repaint();
    }

    // Callbacks wired by MixerPanel
    std::function<void()>      onClicked;
    std::function<void(const TrackID&, const juce::ModifierKeys&)> onSelectedWithModifiers;
    std::function<bool(const TrackID&)> onQueryIsMultiSelected;
    std::function<void(bool)>  onMuteToggled;
    std::function<void()>      onExclusiveMuteRequested;
    std::function<void(bool)>  onSoloToggled;
    std::function<void(bool)>  onArmToggled;
    std::function<void()>      onPianoRollRequested;
    std::function<void(float, float)> onVolumeChanged;   // (beforeGain, afterGain)
    std::function<void(float, float)> onPanChanged;      // (oldPan, newPan)
    std::function<void()>             onInputPanelRequested;
    std::function<void()>             onConvertFolderToTrack;
    std::function<void()>             onDeleteRequested;
    std::function<void()>             onMultiDeleteRequested;
    std::function<void(const TrackID&, juce::Point<int>)> onBodyDragBegin;
    std::function<void(juce::Point<int>)>                 onBodyDragMove;
    std::function<void(juce::Point<int>)>                 onBodyDragEnd;
    std::function<void(bool)>                             onFaderDragStateChanged;
    std::function<void(const TrackID&, bool)>             onFolderToggleRequested;

    void tickMeter()
    {
        meter_.setLevels(track_.getPeakLevelLeft(), track_.getPeakLevelRight());
        meter_.tick();
        if (vu_.isVisible())
            vu_.tick();
    }

    bool needsMeterPresentationTick() const noexcept
    {
        return meter_.needsAnimationTick();
    }

    bool needsLavaPresentationTick() const noexcept
    {
        if (useLegacyMixerSkin())
            return false;

        if (track_.isMaster() && selectionVisual_.getLavaCore().isAlwaysVisible())
            return false;

        return selectionVisual_.needsAnimationTick();
    }

    bool isFaderDragging() const noexcept { return draggingFader_; }

    void trackPropertyChanged(Track*) override
    {
        label_.setText(track_.getName(), juce::dontSendNotification);
        muteBtn_.active = track_.isMuted();
        soloBtn_.active = track_.isSoloed();
        armBtn_.active  = track_.isArmed();
        autoModeBtn_.label  = automationModeLabel (currentAutoMode_);
        autoModeBtn_.active = (currentAutoMode_ != apex::automation::AutomationMode::Off);
        panKnob_.value  = track_.getPan();
        isFolderBus_    = (track_.getRole() == TrackRole::FolderBus);
        folderToggleBtn_.setVisible(isFolderBus_);
        updateTrackLabelPresentation();
        selectionVisual_.setAccentColour(track_.isMaster() ? juce::Colour(0xFFE91572) // APEX master magentaDeep
                                                           : track_.getColor());

        // During a fader drag every volume change reaches this listener. A
        // full resized() invalidates the baked fader-scale cache and a full
        // repaint re-rasterizes the entire strip per mouse tick — that is the
        // visible fader "lag". Bounds cannot change mid-drag, so repaint only
        // the fader lane and keep the cached scale valid.
        if (draggingFader_)
        {
            repaintFaderRegion();
            return;
        }

        resized();
        repaint();
    }

    /** Repaint only the vertical fader lane (thumb + fill + ticks + the live
     *  dB readout pill below the track), not the whole strip. Used by the
     *  drag path to keep fader response smooth. */
    void repaintFaderRegion()
    {
        const auto ft = faderTrackBounds_;
        if (ft.isEmpty())
        {
            repaint();
            return;
        }
        // The dB readout is painted at ft.getBottom()+2 .. +14, so the dirty
        // region must extend ~16 px below the track to stay live during drag.
        repaint(ft.expanded(2.0f, 16.0f).toNearestInt());
    }

    void paint(juce::Graphics& g) override
    {
        // Master mode takes a completely different paint path:
        // drip background + header overlay + persistent fader.
        if (track_.isMaster())
        {
            paintMasterMode(g);
            return;
        }

        auto& t = Theme::getInstance();
        auto b  = getLocalBounds();
        const bool selected = selectionVisual_.isSelected();

        auto stripBounds = getLocalBounds().toFloat().reduced(1.0f);

        if (useLegacyMixerSkin())
        {
            g.fillAll(t.colors.backgroundDark);
            paintLegacyStripBase(g, stripBounds, selected);
        }
        else
        {
            // Premium black-glass base — paints its own background, no fillAll wipe
            paintPremiumStripBase(g, stripBounds, selected);

            // Obsidian spheres + lava (selected strip)
            selectionVisual_.paintInto(g, stripBounds);

            // Purple selection language — additive accent, stays purple per project rules
            if (selected)
                paintSelectedPurpleAccent(g, stripBounds);
        }

        // Folder bus identity system — SPINE + CAP BAR
        if (isFolderBus_)
        {
            paintFolderGroupSpine(g, stripBounds);
            paintFolderCapBar(g, stripBounds, selected);
        }
        // Child track hierarchy accent
        else if (depth_ > 0)
        {
            paintChildHierarchyAccent(g, stripBounds);
        }
// Regular track color bar
        else if (!isFolderBus_)
        {
            // Track color bar at top — 4px for premium visual weight
            // Master gets a 2-tone gold gradient bar
            if (track_.isMaster())
            {
                juce::ColourGradient goldBar(
                    juce::Colour(0xFFFFD966), (float)b.getX(), (float)b.getY(),
                    juce::Colour(0xFFB8860B), (float)b.getRight(), (float)b.getY(), false);
                g.setGradientFill(goldBar);
                g.fillRect(b.removeFromTop(4));
            }
            else
            {
                g.setColour(track_.getColor());
                g.fillRect(b.removeFromTop(4));
            }
            // Gloss sheen over color bar
            g.setColour(juce::Colours::white.withAlpha(0.28f));
            g.fillRect(juce::Rectangle<int>(b.getX(), b.getY() - 4, b.getWidth(), 2));
        }

        // Quick Send mode — gold aura on the source strip (same language as
        // TrackRow's quick-send gold aura, faithfully replicated on the mixer).
        if (quickSendSource_ && !track_.isMaster())
        {
            auto goldCol  = juce::Colour(0xFFFFC94D);
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

            // "QUICK SEND" seal at the top-right of the strip
            juce::Rectangle<float> seal(rowBounds.getRight() - 92, 22, 88, 14);
            g.setColour(juce::Colour(0xFF2A1F00).withAlpha(0.92f));
            g.fillRoundedRectangle(seal, 7.0f);
            g.setColour(goldCol.withAlpha(0.95f));
            g.drawRoundedRectangle(seal, 7.0f, 1.0f);
            g.setFont(juce::Font(8.5f, juce::Font::bold));
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.drawText("QUICK SEND", seal, juce::Justification::centred);
        }

        // Quick Send receiving-target highlight — cyan outline
        // Indicates a valid receive target during Quick Send mode (distinct from selection).
        if (quickSendReceiveTarget_ && !track_.isMaster())
        {
            auto cyanCol = juce::Colour(0xFF00FFFF);
            auto stripBounds = getLocalBounds().toFloat();

            // Outer glow halo
            g.setColour(cyanCol.withAlpha(0.15f));
            g.drawRect(stripBounds.expanded(1.5f), 3.f);

            // Solid cyan outline
            g.setColour(cyanCol.withAlpha(0.8f));
            g.drawRect(stripBounds.reduced(0.5f), 2.f);

            // Subtle internal tint
            g.setColour(cyanCol.withAlpha(0.05f));
            g.fillRect(stripBounds.reduced(1.f));
        }

        // Border
        if (useLegacyMixerSkin())
        {
            g.setColour(t.colors.border.withAlpha(0.35f));
            g.drawRect(getLocalBounds());
        }

        // Fader track + thumb
        {
        if (faderTrackBounds_.getHeight() > 8.f)
        {
            auto ft = faderTrackBounds_;
            float cx = ft.getCentreX();
            float tw = 5.f; // machined slot width
            const auto thumbR = getFaderThumbBounds();

            if (useLegacyMixerSkin())
            {
                g.setColour(t.colors.graphite);
                g.fillRoundedRectangle(cx - tw * 0.5f, ft.getY(), tw, ft.getHeight(), tw * 0.5f);
            }
            else
            {
                // ── Clean fader slot: thin dark inset pill ─────────────────
                // Deep near-black well
                g.setColour(juce::Colour(0xFF050608));
                g.fillRoundedRectangle(cx - tw * 0.5f, ft.getY(), tw, ft.getHeight(), tw * 0.5f);
                // Subtle white border
                g.setColour(juce::Colours::white.withAlpha(0.07f));
                g.drawRoundedRectangle(cx - tw * 0.5f, ft.getY(), tw, ft.getHeight(), tw * 0.5f, 0.7f);
                // Left inner catch-light
                g.setColour(juce::Colours::white.withAlpha(0.10f));
                g.drawLine(cx - tw * 0.5f + 0.8f, ft.getY() + 2.f,
                           cx - tw * 0.5f + 0.8f, ft.getBottom() - 2.f, 0.8f);
            }

            // Filled portion — crisp white (pro monochrome standard)
            const float currentDb = FaderRangeCore::gainToDb(track_.getVolume());
            float norm = faderRange_.dbToNorm(currentDb);
            if (useLegacyMixerSkin())
                FaderGradientRenderer::drawFaderFill(g, ft, norm, tw);
            else
            {
                // APEX: energised rail in the track's own colour — signal level
                // reads per-channel without flooding the strip.
                float fillH = norm * ft.getHeight();
                g.setColour(track_.getColor().withAlpha(0.68f));
                g.fillRoundedRectangle(cx - tw * 0.5f + 1.f,
                                       ft.getBottom() - fillH,
                                       tw - 2.f, fillH, (tw - 2.f) * 0.4f);
            }

            const float db = currentDb;

            // ── Thumb: slim console fader cap ────────────────────────────
            constexpr float thCR = 2.5f;                         // small radius — rectangular feel
            const float thumbY = thumbR.getCentreY();

            if (useLegacyMixerSkin())
            {
                g.setColour(juce::Colours::white.withAlpha(0.92f));
                g.fillRoundedRectangle(thumbR, thCR);
                g.setColour(t.colors.border.withAlpha(0.4f));
                g.drawRoundedRectangle(thumbR, thCR, 0.7f);
            }
            else
            {
                // Soft drop shadow
                g.setColour(juce::Colours::black.withAlpha(0.55f));
                g.fillRoundedRectangle(thumbR.expanded(1.f, 2.f).translated(0.f, 1.5f), thCR + 1.f);

                // ── APEX illuminated cap — track-colour energy ─────────
                // Bright lit top falling to the saturated track colour.
                const auto trackCol = track_.getColor();
                if (!thumbCapGradientBuilt_) { cachedThumbCapGradient_ = juce::ColourGradient(juce::Colours::white, 0.f, 0.f, juce::Colours::black, 0.f, 1.f, false); thumbCapGradientBuilt_ = true; } auto cap = cachedThumbCapGradient_; cap = juce::ColourGradient(trackCol.brighter(0.85f), thumbR.getCentreX(), thumbR.getY(), trackCol.darker(0.15f), thumbR.getCentreX(), thumbR.getBottom(), false); cap.addColour(0.5, trackCol.brighter(0.30f)); g.setGradientFill(cap); g.fillRoundedRectangle(thumbR, thCR);

                // Top-edge catch light (single bright line)
                g.setColour(juce::Colours::white.withAlpha(0.55f));
                g.drawLine(thumbR.getX() + thCR, thumbR.getY() + 0.6f,
                           thumbR.getRight() - thCR, thumbR.getY() + 0.6f, 0.9f);

                // Bottom-edge deep shadow line
                g.setColour(juce::Colours::black.withAlpha(0.50f));
                g.drawLine(thumbR.getX() + thCR, thumbR.getBottom() - 0.6f,
                           thumbR.getRight() - thCR, thumbR.getBottom() - 0.6f, 0.8f);

                // Outer rim
                g.setColour(juce::Colours::white.withAlpha(0.28f));
                g.drawRoundedRectangle(thumbR, thCR, 0.8f);
            }

            const juce::String dbText = currentDb <= faderRange_.getMinDb() + 0.1f
                ? juce::String("-inf")
                : juce::String(currentDb, 1);
            auto valuePill = juce::Rectangle<float>(0.0f, 0.0f, 30.0f, 11.0f)
                .withCentre({ thumbR.getCentreX(), thumbR.getCentreY() });
            valuePill = valuePill.getIntersection(getLocalBounds().toFloat().reduced(2.0f));

            g.setColour(juce::Colours::black.withAlpha(0.68f));
            g.fillRoundedRectangle(valuePill, 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.22f));
            g.drawRoundedRectangle(valuePill, 4.0f, 0.6f);
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.setFont(juce::Font(7.0f, juce::Font::bold));
            g.drawFittedText(dbText, valuePill.toNearestInt(), juce::Justification::centred, 1, 0.85f);

            juce::ignoreUnused(db, thumbY);
        }
        }
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        // Master strip uses different fader visuals; suppress the regular
        // fader scale overlay (numeric ticks beside the rail) for master.
        if (track_.isMaster()) return;

        paintFaderScaleOverlay(g);
    }

    void resized() override
    {
        {
            auto _b = getLocalBounds();
            if (_b.getWidth() < 50 || _b.getHeight() < 50)
            {
                DBG("[MixerStrip] resized() skipped - bounds too small w="
                    + juce::String(_b.getWidth()) + " h=" + juce::String(_b.getHeight()));
                return;
            }
        }
        invalidateFaderScaleCache();  // bounds changed — re-bake ticks at new size
        cachedDripBg_ = {};           // master drip bg: re-bake at new size
        // Master mode uses its own dedicated layout path.
        if (track_.isMaster())
        {
            layoutMasterMode();
            return;
        }

        auto b = getLocalBounds();
        b.removeFromTop(4); // color bar

        // Track name label area at top (painted by label_)
        auto titleRow = b.removeFromTop(18);
        const int indentPx = juce::jmin(12, depth_ * 4);
        titleRow.removeFromLeft(indentPx);
        if (isFolderBus_)
        {
            folderToggleBtn_.setBounds(titleRow.removeFromLeft(14).reduced(1));
            titleRow.removeFromLeft(2);
        }
        else
        {
            folderToggleBtn_.setBounds({});
        }
        // Folder titles are painted once by paintFolderCapBar(). Keeping the
        // ordinary Label alive here paints the same name over the arrow/cap.
        label_.setBounds(isFolderBus_ ? juce::Rectangle<int>() : titleRow);

// Mute / Solo / Arm row
        auto btnRow = b.removeFromTop(18).reduced(4, 1);
        const bool isMaster = track_.isMaster();
        // DISABLED (product decision): the piano roll is a beat-making surface.
        const bool showPiano = false;
        const int buttonCount = isMaster ? 2 : 4;
        const int buttonGap = 2;
        const int buttonWidth = juce::jmax(10, (btnRow.getWidth() - buttonGap * (buttonCount - 1)) / buttonCount);

        auto nextButtonBounds = [&btnRow, buttonWidth, buttonGap]() mutable
        {
            auto bounds = btnRow.removeFromLeft(buttonWidth);
            btnRow.removeFromLeft(buttonGap);
            return bounds;
        };

        pianoBtn_.setVisible(showPiano);
        if (showPiano)
            pianoBtn_.setBounds(nextButtonBounds());
        else
            pianoBtn_.setBounds({});

muteBtn_.setBounds(nextButtonBounds());
        soloBtn_.setBounds(nextButtonBounds());
        armBtn_.setVisible(!isMaster);
        if (!isMaster)
            armBtn_.setBounds(nextButtonBounds());
        inputBtn_.setVisible(!isMaster);
        if (!isMaster)
            inputBtn_.setBounds(nextButtonBounds());

        // Automation mode button — one row below M/S/R, full strip width
        {
            auto autoRow = b.removeFromTop(16).reduced(4, 1);
            autoModeBtn_.setBounds (autoRow);
        }

// Input button + pan knob
        if (!isMaster)
        {
            auto inputRow = b.removeFromTop(18).reduced(6, 1);
            preFaderBtn_.setVisible(true);
            // Compact PRE/POST send button — leaves the rest of the row
            // empty so Quick Send double-clicks have more room to land.
            preFaderBtn_.setBounds(inputRow.removeFromLeft(14).withSizeKeepingCentre(14, 14));
        }
        else
        {
            preFaderBtn_.setVisible(false);
            preFaderBtn_.setBounds({});
        }

        auto panRow = b.removeFromTop(30);
        auto panArea = panRow.reduced(6, 1);
        auto tlBtnArea = panArea.removeFromRight(12);
        trackLensBtn_.setBounds(tlBtnArea.withSizeKeepingCentre(12, 12));

        // Clip indicator: the empty right side of the input row, directly
        // above the TL button. The in-meter box is always disabled — the
        // meter lane is 6 px wide, so its text was ellipsized to "..." and
        // the hit target was unusable. Every strip (master included) uses
        // this box; clicking it clears the latch until the next clip.
        meter_.setClipIndicatorVisible(false);
        clipIndicator_.setMeter(&meter_);
        clipIndicator_.setEnabledForStrip(true);
        clipIndicator_.setBounds(tlBtnArea.getX(),
                                 panRow.getY() - 17,
                                 12,
                                 12);
        if (clipIndicator_.getParentComponent() == nullptr)
            addAndMakeVisible(clipIndicator_);

        panKnob_.setBounds(panArea);

        // Meter lives in a more central lane inside the strip instead of hugging the far-right edge
        auto faderArea = b.reduced(0, 4);
        faderTrackBounds_ = faderArea.toFloat();

        const int meterW = 6;
        if (faderArea.getWidth() < meterW + 4)
            return;
        const int meterX = juce::jlimit(faderArea.getX() + 2,
                                        faderArea.getRight() - meterW - 2,
                                        faderArea.getCentreX() + faderArea.getWidth() / 6 - meterW / 2);
        meter_.setBounds(meterX,
                         faderArea.getY(),
                         meterW,
                         faderArea.getHeight());

        // The strip no longer hosts a personal trim VU needle — the needle
        // now lives only inside the Input Trim panel (opened via the Trim
        // button), so the strip keeps more empty space for Quick Send.
        vu_.setVisible(false);
        vu_.setBounds({});
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Quick Send mode takes priority over the normal strip context menu:
        // left-click toggles the send, right-click toggles the sidechain on a
        // non-source strip. When the mode is inactive the regular right-click
        // context menu (and all other strip behaviour) resumes unchanged.
        if (quickSendModeActive_ && !quickSendSource_)
        {
            if (e.mods.isLeftButtonDown())
            {
                if (onQuickSendToggleSendRequested)
                    onQuickSendToggleSendRequested(track_.getID());
                return;
            }
            else if (e.mods.isRightButtonDown())
            {
                if (onQuickSendToggleSidechainRequested)
                    onQuickSendToggleSidechainRequested(track_.getID());
                return;
            }
        }

        if (e.mods.isPopupMenu())
        {
            notifySelectionForInteraction(e.mods);
            showStripContextMenu();
            return;
        }
        if (e.mods.isRightButtonDown()) return;
if (pianoBtn_.isVisible() && pianoBtn_.getBounds().contains(e.getPosition()))
        {
            if (onClicked) onClicked();
            if (onPianoRollRequested) onPianoRollRequested();
            return;
        }

        // Notify panel that this strip was clicked (for selection).  A plain
        // click on an existing multi-selected strip is intentionally ignored
        // here so a drag can begin without collapsing the canonical set.
        notifySelectionForInteraction(e.mods);
        // Fader drag
        if (getFaderThumbBounds().contains(e.position))
        {
            dragStartY_           = e.getScreenPosition().y;
            dragStartVolumeDb_     = FaderRangeCore::gainToDb(track_.getVolume());
            dragStartVolumeGain_   = track_.getVolume();
            setFaderDragging(true);
            if (volumeParameter_ != nullptr)
            {
                volumeParameter_->beginGesture();
            }
        }
        else if (!track_.isMaster())
        {
            bodyDragging_ = true;
            if (onBodyDragBegin)
                onBodyDragBegin(track_.getID(), e.getEventRelativeTo(getParentComponent()).getPosition());
        }
    }

    void showStripContextMenu()
    {
        juce::Component::SafePointer<MixerStrip> safeThis(this);
        juce::PopupMenu menu;

        const bool isMulti = onQueryIsMultiSelected
            && onQueryIsMultiSelected(track_.getID());

        if (isMulti && onMultiDeleteRequested)
        {
            juce::PopupMenu::Item delSelected("Delete Selected Tracks");
            delSelected.action = [safeThis]
            {
                if (safeThis != nullptr && safeThis->onMultiDeleteRequested)
                    safeThis->onMultiDeleteRequested();
            };
            menu.addItem(delSelected);
            menu.addSeparator();
        }

        if (isFolderBus_)
        {
            {
                juce::PopupMenu::Item toggle(folderExpanded_ ? "Collapse Folder" : "Expand Folder");
                toggle.action = [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->requestFolderToggle();
                };
                menu.addItem(toggle);
            }
            {
                juce::PopupMenu::Item convert("Convert to Regular Track");
                convert.action = [safeThis]
                {
                    if (safeThis != nullptr && safeThis->onConvertFolderToTrack)
                        safeThis->onConvertFolderToTrack();
                };
                menu.addItem(convert);
            }
            menu.addSeparator();
        }

        {
            juce::PopupMenu::Item mute("Mute");
            mute.isTicked = track_.isMuted();
            mute.action = [safeThis]
            {
                if (safeThis != nullptr && safeThis->onMuteToggled)
                    safeThis->onMuteToggled(!safeThis->track_.isMuted());
            };
            menu.addItem(mute);
        }
        {
            juce::PopupMenu::Item solo("Solo");
            solo.isTicked = track_.isSoloed();
            solo.action = [safeThis]
            {
                if (safeThis != nullptr && safeThis->onSoloToggled)
                    safeThis->onSoloToggled(!safeThis->track_.isSoloed());
            };
            menu.addItem(solo);
        }
        if (onDeleteRequested)
        {
            juce::PopupMenu::Item del("Delete Track");
            del.action = [safeThis]
            {
                if (safeThis != nullptr && safeThis->onDeleteRequested)
                    safeThis->onDeleteRequested();
            };
            menu.addItem(del);
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingFader_)
        {
            int dy = dragStartY_ - e.getScreenPosition().y; // up = positive
            float delta = (float)dy / faderTrackBounds_.getHeight();
            float newNorm = juce::jlimit(0.0f, 1.0f,
                faderRange_.dbToNorm(dragStartVolumeDb_) + delta);
            float newDb = faderRange_.normToDb(newNorm);
            if (volumeParameter_ != nullptr)
            {
                volumeParameter_->setValueFromUser (juce::jlimit (0.0f, 1.0f, newNorm));
            }
            else
            {
                track_.setVolume(faderRange_.dbToGain(newDb));
            }
            repaintFaderRegion();
        }
        else if (bodyDragging_ && onBodyDragMove)
        {
            onBodyDragMove(e.getEventRelativeTo(getParentComponent()).getPosition());
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
            return;

        notifySelectionForInteraction(e.mods);

        // Double-click on the fader thumb keeps its reset-to-0dB behaviour.
        if (getFaderThumbBounds().contains(e.position))
        {
            const float oldGain = track_.getVolume();
            const float resetDb = 0.0f;
            const float resetGain = faderRange_.dbToGain(resetDb);

            dragStartVolumeGain_ = oldGain;

            if (volumeParameter_ != nullptr)
            {
                // Reset through the automation parameter — the same value authority
                // the drag path uses. Writing track_.setVolume() directly here was
                // overwritten on the next parameter refresh, snapping the fader back
                // to the pre-reset value the moment it reached 0 dB.
                volumeParameter_->beginGesture();
                volumeParameter_->setValueFromUser(juce::jlimit(0.0f, 1.0f, faderRange_.dbToNorm(resetDb)));
                // The parameter listener is dispatched asynchronously; keep
                // the canonical Track value current for the one-shot commit.
                track_.setVolume(resetGain);
                volumeParameter_->endGesture();
            }
            else
            {
                track_.setVolume(resetGain);
            }

            commitFaderVolumeGesture();

            setFaderDragging(false);
            repaint();
            return;
        }

        // Double-click on any other EMPTY area of the strip toggles Quick Send
        // mode with this track as source — same rule as the timeline row:
        // only the empty parts of the strip respond (child buttons such as
        // mute/solo/arm/trim/pan own their own mouse events).
        if (!track_.isMaster() && onQuickSendToggleRequested)
            onQuickSendToggleRequested(track_.getID());
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        // Guard against this strip being destroyed during the drag-end callback
        // (folder creation/reorder can trigger rebuildStrips() synchronously)
        juce::Component::SafePointer<MixerStrip> safeThis(this);

        if (bodyDragging_ && onBodyDragEnd)
            onBodyDragEnd(e.getEventRelativeTo(getParentComponent()).getPosition());

        // If the callback destroyed this strip (via rebuildStrips), bail out immediately
        if (safeThis == nullptr)
            return;

        bodyDragging_ = false;

        const bool wasFaderDragging = draggingFader_;
        if (wasFaderDragging)
        {
            // AutomationParameter writes its value immediately, while its
            // listener notification is delivered by the UI dispatcher. Read
            // the latest normalized value here so mouse-up always commits the
            // final canonical Track gain.
            syncTrackVolumeFromParameter();
            setFaderDragging(false);
            if (volumeParameter_ != nullptr)
                volumeParameter_->endGesture();
            commitFaderVolumeGesture();
        }

        if (safeThis == nullptr)
            return;

        // One final full repaint at gesture end: the drag path only repainted
        // the fader lane, so the rest of the strip (labels, meters, buttons)
        // may have stale visuals that need a single refresh.
        repaint();
    }

private:
    void notifySelectionForInteraction(const juce::ModifierKeys& mods)
    {
        const bool hasModifier = mods.isCtrlDown() || mods.isCommandDown() || mods.isShiftDown();
        if (!hasModifier && !mods.isPopupMenu() && onQueryIsMultiSelected
            && onQueryIsMultiSelected(track_.getID()))
            return;

        if (onSelectedWithModifiers)
            onSelectedWithModifiers(track_.getID(), mods);
        else if (onClicked)
            onClicked();
    }

    void setFaderDragging(bool dragging)
    {
        if (draggingFader_ == dragging)
            return;

        draggingFader_ = dragging;
        if (onFaderDragStateChanged)
            onFaderDragStateChanged(dragging);
    }

    void syncTrackVolumeFromParameter()
    {
        if (volumeParameter_ != nullptr)
            track_.setVolume (faderRange_.normToGain (volumeParameter_->getNormalizedValue()));
    }

    void commitFaderVolumeGesture()
    {
        const float afterGain = track_.getVolume();
        if (onVolumeChanged && dragStartVolumeGain_ != afterGain)
            onVolumeChanged (dragStartVolumeGain_, afterGain);
    }

    // ── Small button helper ──────────────────────────────────────────────
    struct SmallButton : public juce::Component,
                         public juce::TooltipClient
    {
        juce::String label;
        juce::String tooltipText;
        bool         active   = false;
        juce::Colour activeCol;
        std::function<void()> onClick;
        std::function<void(const juce::MouseEvent&)> onRightClick;

        SmallButton(const juce::String& lbl, juce::Colour ac)
            : label(lbl), activeCol(ac) {}

        juce::String getTooltip() override { return tooltipText; }

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto b  = getLocalBounds().reduced(1).toFloat();

            if (useLegacyMixerSkin())
            {
                g.setColour(active ? activeCol : t.colors.backgroundDark);
                g.fillRoundedRectangle(b, 2.f);
                g.setColour(active ? juce::Colours::black : t.colors.text.withAlpha(0.7f));
                g.setFont(juce::Font(7.5f, juce::Font::bold));
                g.drawText(label, b, juce::Justification::centred, false);
                g.setColour(t.colors.border.withAlpha(0.4f));
                g.drawRoundedRectangle(b, 2.f, 0.7f);
                return;
            }

            // Inactive: deep recessed dark well
            // Active: glowing colour fill with bright rim
            if (active)
            {
                juce::ColourGradient fill(
                    activeCol.brighter(0.05f).withAlpha(0.78f), b.getCentreX(), b.getY(),
                    activeCol.darker(0.45f).withAlpha(0.72f),   b.getCentreX(), b.getBottom(), false);
                fill.addColour(0.48, activeCol.withAlpha(0.76f));
                g.setGradientFill(fill);
                g.fillRoundedRectangle(b, 2.0f);

                g.setColour(juce::Colours::white.withAlpha(0.32f));
                g.drawRoundedRectangle(b, 2.0f, 0.8f);
                g.setColour(juce::Colours::black.withAlpha(0.28f));
                g.drawLine(b.getX() + 1.5f, b.getBottom() - 0.6f, b.getRight() - 1.5f, b.getBottom() - 0.6f, 0.8f);
            }
            else
            {
                auto& a = Theme::getInstance().apex;
                juce::ColourGradient fill(
                    a.color.panelC,   b.getCentreX(), b.getY(),
                    a.color.panelA,   b.getCentreX(), b.getBottom(), false);
                fill.addColour(0.45, a.color.panelB);
                g.setGradientFill(fill);
                g.fillRoundedRectangle(b, 2.0f);

                g.setColour(a.color.textPrimary.withAlpha(0.10f));
                g.drawLine(b.getX() + 1.5f, b.getY() + 0.6f, b.getRight() - 1.5f, b.getY() + 0.6f, 0.8f);
                g.setColour(a.color.borderSoftA.withAlpha(0.55f));
                g.drawRoundedRectangle(b, 2.0f, 0.8f);
            }

            juce::ColourGradient gloss(
                juce::Colours::white.withAlpha(active ? 0.14f : 0.08f), b.getCentreX(), b.getY(),
                juce::Colours::transparentWhite, b.getCentreX(), b.getBottom(), false);
            g.setGradientFill(gloss);
            g.fillRoundedRectangle(b.withHeight(b.getHeight() * 0.42f), 2.0f);

            g.setColour(active ? juce::Colours::white
                               : Theme::getInstance().apex.color.textSecondary);
            g.setFont(juce::Font(7.5f, juce::Font::bold));
            g.drawText(label, b, juce::Justification::centred, false);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.mods.isRightButtonDown())
            {
                if (onRightClick) onRightClick(e);
                return;
            }
            active = !active;
            if (onClick) onClick();
            repaint();
        }
    };

    struct FolderToggleButton : public juce::Component
    {
        float expandedAmount = 1.0f;
        std::function<void()> onClick;

        void paint(juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced(1.0f);
            if (b.isEmpty())
                return;

            const auto centre = b.getCentre();
            const float size = juce::jmin(b.getWidth(), b.getHeight()) * 0.34f;
            juce::Path p;
            p.startNewSubPath(centre.x - size * 0.7f, centre.y - size);
            p.lineTo(centre.x + size * 0.9f, centre.y);
            p.lineTo(centre.x - size * 0.7f, centre.y + size);
            p.closeSubPath();

            const float rotation = juce::MathConstants<float>::halfPi * expandedAmount;
            auto transform = juce::AffineTransform::rotation(rotation, centre.x, centre.y);
            g.setColour(Theme::getInstance().apex.color.violetBright.withAlpha(0.92f));
            g.fillPath(p, transform);
        }

        void mouseDown(const juce::MouseEvent&) override
        {
            if (onClick) onClick();
        }
    };

    // ── Pan knob (vertical drag) ─────────────────────────────────────────
    struct PanKnob : public juce::Component
    {
        float value = 0.0f;  // -1..+1
        int   dragStartY_ = 0;
        float dragStartVal_ = 0.0f;
        std::function<void(float,float)> onChanged;
        std::function<void()> onGestureBegin;
        std::function<void()> onGestureEnd;

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto b  = getLocalBounds().toFloat().reduced(1.f);
            float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
            r = juce::jmax(0.0f, r - 1.5f);
            auto  c = b.getCentre();

            if (useLegacyMixerSkin())
            {
                g.setColour(t.colors.graphite);
                g.fillEllipse(c.x - r, c.y - r, r * 2.f, r * 2.f);
                g.setColour(t.colors.border.withAlpha(0.5f));
                g.drawEllipse(c.x - r, c.y - r, r * 2.f, r * 2.f, 0.8f);

                float angle = value * juce::MathConstants<float>::pi * 0.75f;
                float lx = c.x + (r - 3.f) * std::sin(angle);
                float ly = c.y - (r - 3.f) * std::cos(angle);
                g.setColour(juce::Colours::white);
                g.drawLine(c.x, c.y, lx, ly, 1.5f);

                g.setColour(t.colors.text.withAlpha(0.6f));
                g.setFont(juce::Font(6.0f));
                g.drawText("PAN", getLocalBounds(), juce::Justification::centredBottom, false);
                return;
            }

            auto outer = juce::Rectangle<float>(c.x - r, c.y - r, r * 2.f, r * 2.f);
            auto& a = Theme::getInstance().apex;

            // ── APEX trim knob: dark body, thin luminous ring, cyan pointer ──
            g.setColour(juce::Colours::black.withAlpha(0.65f));
            g.fillEllipse(outer.expanded(1.5f).translated(0.f, 2.f));

            // Restrained collar
            {
                juce::ColourGradient ring(
                    a.color.borderSoftB, c.x, outer.getY(),
                    a.color.panelA,      c.x, outer.getBottom(), false);
                ring.addColour(0.42, a.color.panelC);
                g.setGradientFill(ring);
                g.fillEllipse(outer.expanded(1.0f));
            }

            // Body: deep navy glass
            {
                juce::ColourGradient body(
                    a.color.panelC,     c.x, outer.getY(),
                    a.color.deepestA,   c.x, outer.getBottom(), false);
                body.addColour(0.55, a.color.panelA);
                g.setGradientFill(body);
                g.fillEllipse(outer);
            }

            g.setColour(a.color.textPrimary.withAlpha(0.14f));
            g.drawEllipse(outer.reduced(2.5f), 0.8f);

            // Thin luminous pan arc (cyan = signal precision)
            {
                const float panA = value * juce::MathConstants<float>::pi * 0.75f;
                juce::Path arc;
                arc.addCentredArc(c.x, c.y, r - 1.2f, r - 1.2f, 0.0f,
                                  juce::MathConstants<float>::halfPi,
                                  juce::MathConstants<float>::halfPi + panA, true);
                g.setColour(a.color.cyan.withAlpha(0.75f));
                g.strokePath(arc, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }

            // Pointer with cyan tip
            float angle = value * juce::MathConstants<float>::pi * 0.75f;
            {
                float pr = juce::jmax(2.0f, r - 4.5f);
                float lx = c.x + pr * std::sin(angle);
                float ly = c.y - pr * std::cos(angle);
                // Soft glow behind tip
                g.setColour(a.color.cyan.withAlpha(0.22f));
                g.fillEllipse(lx - 3.f, ly - 3.f, 6.f, 6.f);
                // Bright cyan tip
                g.setColour(a.color.cyan.withAlpha(0.95f));
                g.fillEllipse(lx - 1.6f, ly - 1.6f, 3.2f, 3.2f);
                // Pointer line
                g.setColour(a.color.textPrimary.withAlpha(0.72f));
                g.drawLine(c.x, c.y, lx, ly, 1.4f);
            }

            // "PAN" label
            g.setColour(juce::Colours::white.withAlpha(0.42f));
            g.setFont(juce::Font(5.8f));
            g.drawText("PAN", getLocalBounds(), juce::Justification::centredBottom, false);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            dragStartY_   = e.getScreenPosition().y;
            dragStartVal_ = value;
            if (onGestureBegin) onGestureBegin();
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            int dy = dragStartY_ - e.getScreenPosition().y;
            float newVal = juce::jlimit(-1.0f, 1.0f,
                dragStartVal_ + (float)dy / 80.f);
            float old = value;
            value = newVal;
            if (onChanged) onChanged(old, value);
            repaint();
        }

        void mouseDoubleClick(const juce::MouseEvent&) override
        {
            if (onGestureBegin) onGestureBegin();
            float old = value;
            value = 0.0f;
            if (onChanged) onChanged(old, value);
            if (onGestureEnd) onGestureEnd();
            repaint();
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            if (onGestureEnd) onGestureEnd();
        }
    };

    void registerAdditionalNativeAutomationParameters()
    {
        using KR = apex::automation::AutomationParameterKeyRegistry;
        auto& sys = apex::automation::AutomationSystem::getInstance();

        {
            const auto id = KR::getInstance().getOrCreateID (KR::trackPanKey (track_.getID()));
            apex::automation::ParameterRange range;
            range.minValue = 0.0f;
            range.maxValue = 1.0f;
            range.defaultValue = 0.5f;
            range.skew = 1.0f;
            range.isStepped = false;

            panParameter_ = sys.createNativeParameter (id, "Pan: " + track_.getName(), range);
            if (panParameter_ != nullptr)
            {
                panParameter_->setValueProgrammatic (juce::jlimit (0.0f, 1.0f, (track_.getPan() + 1.0f) * 0.5f));
                panParameter_->addListener (this);
            }
        }

        {
            const auto id = KR::getInstance().getOrCreateID (KR::trackMuteKey (track_.getID()));
            apex::automation::ParameterRange range;
            range.minValue = 0.0f;
            range.maxValue = 1.0f;
            range.defaultValue = 0.0f;
            range.skew = 1.0f;
            range.isStepped = true;
            range.numSteps = 2;

            muteParameter_ = sys.createNativeParameter (id, "Mute: " + track_.getName(), range);
            if (muteParameter_ != nullptr)
            {
                muteParameter_->setValueProgrammatic (track_.isMuted() ? 1.0f : 0.0f);
                muteParameter_->addListener (this);
            }
        }

        {
            const auto id = KR::getInstance().getOrCreateID (KR::trackSoloKey (track_.getID()));
            apex::automation::ParameterRange range;
            range.minValue = 0.0f;
            range.maxValue = 1.0f;
            range.defaultValue = 0.0f;
            range.skew = 1.0f;
            range.isStepped = true;
            range.numSteps = 2;

            soloParameter_ = sys.createNativeParameter (id, "Solo: " + track_.getName(), range);
            if (soloParameter_ != nullptr)
            {
                soloParameter_->setValueProgrammatic (track_.isSoloed() ? 1.0f : 0.0f);
                soloParameter_->addListener (this);
            }
        }
    }

    void registerSendAutomationParameters()
    {
        if (routingGraph_ == nullptr)
            return;

        using KR = apex::automation::AutomationParameterKeyRegistry;
        auto& sys = apex::automation::AutomationSystem::getInstance();
        auto* sourceNode = routingGraph_->getNodeByTrackId (track_.getID());
        if (sourceNode == nullptr)
            return;

        for (auto* conn : routingGraph_->getOutputConnections (sourceNode->id))
        {
            if (conn == nullptr || conn->type != ConnectionType::Send)
                continue;
            if (sendParameters_.find (conn->id) != sendParameters_.end())
                continue;

            const auto id = KR::getInstance().getOrCreateID (KR::trackSendLevelKey (track_.getID(), conn->id));
            apex::automation::ParameterRange range;
            range.minValue = 0.0f;
            range.maxValue = 1.0f;
            range.defaultValue = 0.5f;
            range.skew = 1.0f;
            range.isStepped = false;

            auto* sendParam = sys.createNativeParameter (id, "Send " + conn->id + ": " + track_.getName(), range);
            if (sendParam != nullptr)
            {
                const float currentGain = juce::jlimit (0.0f, 2.0f, conn->gain.load (std::memory_order_relaxed));
                sendParam->setValueProgrammatic (currentGain * 0.5f);
                sendParam->addListener (this);
                sendParameters_.emplace (conn->id, sendParam);
            }

            const auto bypassId = KR::getInstance().getOrCreateID (KR::trackSendBypassKey (track_.getID(), conn->id));
            apex::automation::ParameterRange bypassRange;
            bypassRange.minValue = 0.0f;
            bypassRange.maxValue = 1.0f;
            bypassRange.defaultValue = 0.0f;
            bypassRange.skew = 1.0f;
            bypassRange.isStepped = true;
            bypassRange.numSteps = 2;

            auto* bypassParam = sys.createNativeParameter (bypassId, "Send " + conn->id + " Bypass: " + track_.getName(), bypassRange);
            if (bypassParam != nullptr)
            {
                const bool active = conn->active.load (std::memory_order_relaxed);
                bypassParam->setValueProgrammatic (active ? 0.0f : 1.0f);
                bypassParam->addListener (this);
                sendBypassParameters_.emplace (conn->id, bypassParam);
            }
        }
    }

    void unregisterNativeAutomationParameters()
    {
        auto& sys = apex::automation::AutomationSystem::getInstance();

        unregisterNativeAutomationParameter (sys, volumeParameter_);
        unregisterNativeAutomationParameter (sys, panParameter_);
        unregisterNativeAutomationParameter (sys, muteParameter_);
        unregisterNativeAutomationParameter (sys, soloParameter_);

        for (auto& [routeID, sendParam] : sendParameters_)
            unregisterNativeAutomationParameter (sys, sendParam);
        sendParameters_.clear();

        for (auto& [routeID, bypassParam] : sendBypassParameters_)
            unregisterNativeAutomationParameter (sys, bypassParam);
        sendBypassParameters_.clear();
    }

    void unregisterNativeAutomationParameter (apex::automation::AutomationSystem& sys,
                                              apex::automation::AutomationParameter*& parameter)
    {
        if (parameter == nullptr)
            return;

        const auto id = parameter->getID();
        parameter->removeListener (this);
        sys.removeNativeParameter (id);
        parameter = nullptr;
    }

    Track&          track_;
    FaderRangeCore& faderRange_;
    bool            listenersAttached_ = true;
    RoutingGraph*   routingGraph_ = nullptr;
    BgRole          bgRole_   = BgRole::None;
    bool            bgActive_ = false;

    // ===== Phase 1C: automation volume parameter ==========================
    apex::automation::AutomationParameter* volumeParameter_       = nullptr;
    apex::automation::AutomationParameter* panParameter_          = nullptr;
    apex::automation::AutomationParameter* muteParameter_         = nullptr;
    apex::automation::AutomationParameter* soloParameter_         = nullptr;
    std::unordered_map<juce::String, apex::automation::AutomationParameter*> sendParameters_;
    std::unordered_map<juce::String, apex::automation::AutomationParameter*> sendBypassParameters_;
    bool                                   updatingFromAutomation_ = false;
    apex::automation::AutomationMode       currentAutoMode_        =
        apex::automation::AutomationMode::Off;

    static juce::String automationModeLabel (apex::automation::AutomationMode m) noexcept
    {
        using M = apex::automation::AutomationMode;
        switch (m)
        {
            case M::Off:   return "OFF";
            case M::Read:  return "READ";
            case M::Touch: return "TCH";
            case M::Latch: return "LTCH";
            case M::Write: return "WRT";
            case M::Trim:  return "TRIM";
            default:       return "OFF";
        }
    }

    static apex::automation::AutomationMode nextAutoMode (apex::automation::AutomationMode m) noexcept
    {
        using M = apex::automation::AutomationMode;
        switch (m)
        {
            case M::Off:   return M::Read;
            case M::Read:  return M::Touch;
            case M::Touch: return M::Latch;
            case M::Latch: return M::Write;
            case M::Write: return M::Off;
            default:       return M::Off;
        }
    }
    // ===== End Phase 1C ===================================================

    TrackSelectionVisualCore selectionVisual_;

    // Child components
    juce::Label  label_;
    juce::TextButton pianoBtn_;
SmallButton  muteBtn_ { "M", juce::Colour(0xFFFF3333) };
    SmallButton  soloBtn_ { "S", juce::Colour(0xFFD4AF37) };
    SmallButton  armBtn_  { "R", juce::Colour(0xFF8B0000) };
    SmallButton  preFaderBtn_ { "POST", juce::Colour(0xFF00B3A4) };  // POST/PRE send tap point
    SmallButton  inputBtn_ { "T", juce::Colour(0xFF7C3AED) };  // compact square trim button
    SmallButton  autoModeBtn_ { "OFF", juce::Colour(0xFF00BFFF) };
    FolderToggleButton folderToggleBtn_;
    PanKnob      panKnob_;
    // Clip indicator drawn above the TL button on regular strips (the meter
    // lane is only 6 px wide, too narrow for a readable clip box). Polls the
    // meter's latch and repaints on change; clicking clears the latch until
    // the track clips again.
    class MixerClipIndicator final : public juce::Component,
                                     private juce::Timer
    {
    public:
        MixerClipIndicator() { startTimerHz(15); }

        void setMeter(LevelMeter* meter) { meter_ = meter; }

        void setEnabledForStrip(bool shouldBeEnabled)
        {
            enabled_ = shouldBeEnabled;
            setVisible(shouldBeEnabled);
        }

        void paint(juce::Graphics& g) override
        {
            if (meter_ == nullptr || ! meter_->isClipLatched())
                return;

            const auto b = getLocalBounds().toFloat();
            g.setColour(juce::Colour(0xFFFF3B30));
            g.fillRoundedRectangle(b, 2.0f);
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(8.5f, juce::Font::bold));
            g.drawText("+" + juce::String(meter_->getClipOverDb(), 1),
                       getLocalBounds(), juce::Justification::centred);
        }

        void mouseDown(const juce::MouseEvent&) override
        {
            if (meter_ != nullptr)
                meter_->clearClip();
            repaint();
        }

    private:
        void timerCallback() override
        {
            const bool latched = meter_ != nullptr && meter_->isClipLatched();
            if (latched != lastLatched_)
            {
                lastLatched_ = latched;
                repaint();
            }
        }

        LevelMeter* meter_ = nullptr;
        bool enabled_ = true;
        bool lastLatched_ = false;
    };

    SmallButton  trackLensBtn_ { "TL", juce::Colour(0xFFE84393) };
    LevelMeter   meter_;
    MixerClipIndicator clipIndicator_;
    CompactVuNeedle vu_;

    juce::Rectangle<float> faderTrackBounds_;
    bool  draggingFader_   = false;
    bool  bodyDragging_    = false;
    bool  isFolderBus_     = false;
    bool  folderExpanded_  = true;
    bool  lastSetSelectedValue_       = false;
    bool  lastSetSelectedInitialised_ = false;
    float folderOpenAmount_ = 1.0f;
    float folderShimmerProgress_ = -1.0f;
    juce::Colour folderParentColour_ = juce::Colours::transparentBlack;
    int   depth_           = 0;
    int   dragStartY_      = 0;
    float dragStartVolumeDb_ = 0.f;
    float dragStartVolumeGain_ = 1.f;

    // Cached fader thumb cap gradient � colours are hardcoded constants, built once.
    mutable juce::ColourGradient cachedThumbCapGradient_;
    mutable bool                 thumbCapGradientBuilt_ = false;

    // Cached folder cap bar gradient -- keyed on familyColour + selected + cap bounds.
    mutable juce::ColourGradient   cachedCapBarGradient_;
    mutable juce::Colour           cachedCapBarColour_;
    mutable bool                   cachedCapBarSelected_  = false;
    mutable juce::Rectangle<float> cachedCapBarBounds_;

    // ── Master mode (only used when track_.isMaster()) ─────────────────
    // Backgrounds and bindings are value members (lightweight, always present).
    // Sections are unique_ptrs so they exist only on the master strip; regular
    // strips pay zero memory cost beyond the pointer slots.
    MasterStripDripBackgroundRenderer             dripRenderer_;
    MasterStripCeilingBinding                     ceilingBinding_;
    MasterStripDitherBinding                      ditherBinding_;
    std::unique_ptr<MasterStripCeilingSection>    ceilingSection_;
    std::unique_ptr<MasterStripDitherSection>     ditherSection_;
    std::unique_ptr<MasterStripMeteringSection>   meteringSection_;
    std::unique_ptr<MasterStripPhaseWidthSection> phaseWidthSection_;

    // Header buttons (only positioned/visible when isMaster())
    juce::TextButton utilityBtn_ { "UTIL" };
    juce::TextButton flipBtn_  { "Flip" };
    juce::TextButton fxBtn_    { "FX" };
    juce::TextButton optionsBtn_ { "-" };
    bool masterOptionsCollapsed_ { false };
    juce::TextButton meteringTabBtn_ { "METERING" };
    juce::TextButton phaseTabBtn_    { "PHASE" };
    juce::TextButton ceilingTabBtn_  { "CEILING" };
    juce::TextButton ditherTabBtn_   { "DITHER" };
    bool meteringCollapsed_ { false };
    bool phaseCollapsed_    { false };
    bool ceilingCollapsed_  { false };
    bool ditherCollapsed_   { false };

    // Concrete listener wired to MasterStripFlipState global instance.
    // Nested class so MixerStrip itself does not change its inheritance.
    struct FlipObserver : public MasterStripFlipState::Listener
    {
        explicit FlipObserver(MixerStrip& owner) : owner_(owner) {}
        void masterStripFlipChanged(MasterStripFlipState::Side) override
        {
            owner_.resized();
            owner_.repaint();
        }
        MixerStrip& owner_;
    };
    std::unique_ptr<FlipObserver> flipObserver_;

    // Public callback set by MixerPanel::rebuildStrips() on master strip.
    // Body-drag callbacks already documented elsewhere; this is a master-only
    // callback for the FX button.
public:
    std::function<void()> onMasterFxRequested;
    std::function<void(MixerStrip&)> onMasterUtilityRequested;
    std::function<void(const TrackID&)> onTrackLensRequested;

    /** Wire the master strip's UI sections to the live audio engines.
     *
     *  Called by MixerPanel::applyMasterEnginesToExistingStrip() after
     *  bindMasterEngines() has been called on the panel. Safe to call
     *  multiple times; later calls overwrite earlier wiring.
     *
     *  No-op if track_ is not the master track. */
    void setMasterEngines(MasterCeilingCore*    ceiling,
                           MasterDitherCore*     dither,
                           MeteringFacadeCore*   postMeter,
                          PhaseWidthFacadeCore* phaseWidth)
    {
        if (!track_.isMaster()) return;

        ceilingBinding_.setCeiling(ceiling);
        ditherBinding_ .setDither(dither);

        if (meteringSection_ != nullptr)
            meteringSection_->setPostMeter(postMeter);

        if (phaseWidthSection_ != nullptr && phaseWidth != nullptr)
        {
            phaseWidthSection_->setSources(&phaseWidth->getCorrelation(),
                                            &phaseWidth->getWidth(),
                                            &phaseWidth->getMonoCheck());
        }
        else if (phaseWidthSection_ != nullptr)
        {
            phaseWidthSection_->setSources(nullptr, nullptr, nullptr);
        }

        repaint();
    }

private:

    // Broadcasts the given automation mode to every parameter belonging to this track:
    // native track parameters (volume, pan, mute, solo, sends) AND plugin parameters on
    // every loaded plugin slot. Matches Logic/Ableton/Pro Tools track-wide mode semantics.
    void broadcastModeToAllTrackParameters (apex::automation::AutomationMode mode)
    {
        auto& keys = apex::automation::AutomationParameterKeyRegistry::getInstance();
        auto& sys  = apex::automation::AutomationSystem::getInstance();
        const auto trackID = track_.getID();

        // "track.<id>." covers volume, pan, mute, solo, and all send keys.
        // "plugin.<id>." covers every plugin parameter on every loaded slot.
        for (const auto& [key, paramID] : keys.findAllKeysWithPrefix (juce::String ("track.")  + trackID + "."))
            sys.setMode (paramID, mode);
        for (const auto& [key, paramID] : keys.findAllKeysWithPrefix (juce::String ("plugin.") + trackID + "."))
            sys.setMode (paramID, mode);
    }

static constexpr bool useLegacyMixerSkin() noexcept { return false; }

public:
    // ── Quick Send mode ───────────────────────────────────────────────────
    void setQuickSendSource(bool on)
    {
        if (quickSendSource_ == on)
            return;
        quickSendSource_ = on;
        repaint();
    }
    bool isQuickSendSource() const noexcept { return quickSendSource_; }

    void setQuickSendModeActive(bool on)
    {
        if (quickSendModeActive_ == on)
            return;
        quickSendModeActive_ = on;
        repaint();
    }
    bool isQuickSendModeActive() const noexcept { return quickSendModeActive_; }

    /** Left-click on a strip during Quick Send mode toggles the send (create if missing, remove if exists). */
    std::function<void(const TrackID&)> onQuickSendToggleSendRequested;
    /** Right-click on a strip during Quick Send mode toggles the sidechain (create if missing, remove if exists). */
    std::function<void(const TrackID&)> onQuickSendToggleSidechainRequested;

// ── Quick Send mode state ─────────────────────────────────────────────
      bool       quickSendSource_         = false;
      bool       quickSendModeActive_     = false;
      bool       quickSendReceiveTarget_ = false;

      void setQuickSendReceiveTarget(bool on)
      {
          if (quickSendReceiveTarget_ == on)
              return;
          quickSendReceiveTarget_ = on;
          repaint();
      }

private:
    juce::Rectangle<float> getFaderThumbBounds() const
    {
        if (faderTrackBounds_.getHeight() <= 8.0f)
            return {};

        const auto ft = faderTrackBounds_;
        const float cx = ft.getCentreX();
        const float norm = faderRange_.dbToNorm(FaderRangeCore::gainToDb(track_.getVolume()));
        const float thW = juce::jmin(ft.getWidth() - 4.0f, 34.0f);
        const float thH = 13.0f;
        const float thumbHalfH = thH * 0.5f;
        const float unclampedY = ft.getBottom() - norm * ft.getHeight();
        const float thumbY = juce::jlimit(ft.getY() + thumbHalfH,
                                          ft.getBottom() - thumbHalfH,
                                          unclampedY);
        return juce::Rectangle<float>(cx - thW * 0.5f, thumbY - thumbHalfH, thW, thH);
    }

    juce::Rectangle<float> getFolderCapBounds() const
    {
        if (!isFolderBus_)
            return {};

        return getLocalBounds().toFloat().reduced(1.0f).withHeight(18.0f);
    }

    void updateTrackLabelPresentation()
    {
        label_.setVisible(!isFolderBus_);

        juce::String displayName = track_.getName();
        if (!isFolderBus_ && depth_ > 0)
        {
            juce::String hierarchyPrefix;
            for (int i = 0; i < juce::jmin(depth_, 3); ++i)
                hierarchyPrefix += "\xE2\x86\xB3"; // ↳
            displayName = hierarchyPrefix + " " + displayName;
            label_.setColour(juce::Label::textColourId,
                             (folderParentColour_.isTransparent() ? track_.getColor() : folderParentColour_)
                                 .brighter(0.65f).withAlpha(0.96f));
            label_.setFont(juce::Font(11.5f, juce::Font::bold));
        }
        else if (!track_.isMaster())
        {
            label_.setColour(juce::Label::textColourId,
                             Theme::getInstance().apex.color.textPrimary);
            label_.setFont(juce::Font(13.0f, juce::Font::plain));
        }

        label_.setText(displayName, juce::dontSendNotification);
    }

    juce::Colour getFolderFamilyColour() const
    {
        auto colour = track_.getColor();
        if (depth_ > 0)
            colour = colour.withRotatedHue(0.12f * (float) depth_)
                           .withMultipliedSaturation(1.15f)
                           .brighter(0.18f);

        return colour;
    }

    juce::Colour getFolderFamilyColour(juce::Colour baseColour, int depth) const
    {
        auto colour = baseColour;
        if (depth > 0)
            colour = colour.withRotatedHue(0.12f * (float) depth)
                           .withMultipliedSaturation(1.15f)
                           .brighter(0.18f);

        return colour;
    }

    void requestFolderToggle()
    {
        folderExpanded_ = !folderExpanded_;
        folderToggleBtn_.expandedAmount = folderExpanded_ ? 1.0f : 0.0f;
        // Copy the callback before dispatch. The panel-side handler defers
        // rebuilds, but any handler may still destroy this strip, so never
        // touch members after the callback returns (use-after-free guard).
        auto callback = onFolderToggleRequested;
        if (callback)
            callback(track_.getID(), folderExpanded_);
    }

    void paintLegacyStripBase(juce::Graphics& g, juce::Rectangle<float> bounds, bool selected)
    {
        if (bounds.isEmpty())
            return;

        auto& t = Theme::getInstance();
        constexpr float cr = 3.0f;

        juce::Colour top = selected
            ? t.colors.mixerChannelHover.interpolatedWith(t.colors.champagne, 0.22f)
            : t.colors.mixerChannelHover;
        juce::Colour bottom = selected
            ? t.colors.mixerChannel.interpolatedWith(t.colors.copper, 0.12f)
            : t.colors.mixerChannel;

        juce::ColourGradient body(top, bounds.getCentreX(), bounds.getY(),
                                  bottom, bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(body);
        g.fillRoundedRectangle(bounds, cr);

        if (selected)
        {
            g.setColour(t.colors.champagne.withAlpha(0.12f));
            g.fillRoundedRectangle(bounds.reduced(1.0f), juce::jmax(1.5f, cr - 1.0f));

            auto highlightBand = bounds.removeFromTop(bounds.getHeight() * 0.18f);
            juce::ColourGradient champagneGloss(
                t.colors.champagne.withAlpha(0.16f), highlightBand.getCentreX(), highlightBand.getY(),
                juce::Colours::transparentWhite, highlightBand.getCentreX(), highlightBand.getBottom(), false);
            g.setGradientFill(champagneGloss);
            g.fillRect(highlightBand);
        }

        g.setColour(juce::Colours::white.withAlpha(selected ? 0.05f : 0.035f));
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.0f);

        g.setColour(selected ? t.colors.champagne.withAlpha(0.80f)
                             : t.colors.border.withAlpha(0.45f));
        g.drawRoundedRectangle(bounds, cr, 1.0f);
    }

    void paintFolderBusHierarchyAccent(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (bounds.isEmpty())
            return;

        auto crown = bounds.reduced(4.0f, 0.0f).withHeight(22.0f);
        juce::ColourGradient crownFill(
            juce::Colour(0x2238322A), crown.getCentreX(), crown.getY(),
            juce::Colour(0x2C120F0C), crown.getCentreX(), crown.getBottom(), false);
        crownFill.addColour(0.18, juce::Colour(0x30FFF4E5));
        crownFill.addColour(0.52, juce::Colour(0x24D7B98A));
        g.setGradientFill(crownFill);
        g.fillRoundedRectangle(crown, 8.0f);

        g.setColour(juce::Colour(0x7AF8E7C8));
        g.drawRoundedRectangle(crown, 8.0f, 1.0f);

        auto innerCrown = crown.reduced(1.5f, 1.5f);
        juce::ColourGradient innerGlow(
            juce::Colours::white.withAlpha(0.08f), innerCrown.getCentreX(), innerCrown.getY(),
            juce::Colours::transparentWhite, innerCrown.getCentreX(), innerCrown.getBottom(), false);
        g.setGradientFill(innerGlow);
        g.fillRoundedRectangle(innerCrown, 6.5f);

        const float braceY = crown.getBottom() + 6.0f;
        juce::ColourGradient braceGrad(
            juce::Colour(0x90F2D8AB), bounds.getX() + 10.0f, braceY,
            juce::Colour(0x58A37B49), bounds.getRight() - 10.0f, braceY, false);
        g.setGradientFill(braceGrad);
        g.drawLine(bounds.getX() + 10.0f, braceY, bounds.getRight() - 10.0f, braceY, 1.5f);
        g.setColour(juce::Colour(0x66F6EAD6));
        g.drawLine(bounds.getX() + 10.0f, braceY, bounds.getX() + 10.0f, braceY + 10.0f, 1.0f);
        g.drawLine(bounds.getRight() - 10.0f, braceY, bounds.getRight() - 10.0f, braceY + 10.0f, 1.0f);
    }

    void paintChildHierarchyAccent(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (bounds.isEmpty())
            return;

        // Membership colour comes from the parent folder — instantly readable
        auto family = folderParentColour_.isTransparent()
                          ? getFolderFamilyColour()
                          : folderParentColour_;

        // ── 0. Child keeps its own colour identity — slim top bar ─────────
        g.setColour(track_.getColor());
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.0f);

        // ── 1. Whisper-quiet colour-wash so children read as "inside" ──────
        juce::ColourGradient wash(
            family.withAlpha(0.055f), bounds.getX(), bounds.getY(),
            juce::Colours::transparentBlack, bounds.getX() + bounds.getWidth() * 0.55f, bounds.getY(), false);
        g.setGradientFill(wash);
        g.fillRoundedRectangle(bounds, 6.0f);

        // ── 2. Thin membership ribbon under the colour bar, parent colour ──
        auto ribbon = juce::Rectangle<float>(bounds.getX() + 1.0f, bounds.getY() + 4.0f,
                                             bounds.getWidth() - 2.0f, 2.0f);
        juce::ColourGradient ribbonGrad(
            family.withAlpha(0.85f), ribbon.getX(), ribbon.getCentreY(),
            family.withAlpha(0.15f), ribbon.getRight(), ribbon.getCentreY(), false);
        g.setGradientFill(ribbonGrad);
        g.fillRect(ribbon);

        // ── 3. Slim indent rail on the left, one per depth level ───────────
        const float railTop    = bounds.getY() + 8.0f;
        const float railBottom = bounds.getBottom() - 6.0f;
        if (railBottom > railTop + 12.0f)
        {
            for (int d = 0; d < juce::jmin(depth_, 3); ++d)
            {
                const float railX = bounds.getX() + 2.5f + (float) d * 4.0f;
                const float alpha = (d == depth_ - 1) ? 0.55f : 0.22f;
                juce::ColourGradient railGrad(
                    family.withAlpha(alpha), railX, railTop,
                    family.withAlpha(alpha * 0.25f), railX, railBottom, false);
                g.setGradientFill(railGrad);
                g.fillRoundedRectangle(railX, railTop, 1.6f, railBottom - railTop, 0.8f);
            }
        }

        // ── 4. Small elbow connector — ties the strip to its parent rail ───
        const float innerRailX = bounds.getX() + 2.5f + (float) (juce::jmin(depth_, 3) - 1) * 4.0f;
        const float elbowY = bounds.getY() + 14.0f;
        g.setColour(family.withAlpha(0.60f));
        g.drawLine(innerRailX + 1.6f, elbowY, innerRailX + 8.0f, elbowY, 1.2f);
        g.fillEllipse(innerRailX + 7.2f, elbowY - 1.5f, 3.0f, 3.0f);
    }

    void paintFolderGroupSpine(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (!isFolderBus_ || bounds.isEmpty() || bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        auto familyColour = getFolderFamilyColour();

        // Safety check - ensure we have valid color
        if (familyColour.isTransparent())
            return;

        // Slim, elegant 3px spine — one soft halo layer, no billboard glow
        const float spineWidth = 3.0f;

        g.setColour(familyColour.withAlpha(0.16f));
        g.fillRect(bounds.getX() - 1.5f, bounds.getY(), spineWidth + 3.0f, bounds.getHeight());

        // Main spine — vertical fade keeps it refined
        juce::ColourGradient spineGrad(
            familyColour.brighter(0.25f).withAlpha(0.95f), bounds.getX(), bounds.getY(),
            familyColour.withAlpha(0.55f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(spineGrad);
        g.fillRect(bounds.getX(), bounds.getY(), spineWidth, bounds.getHeight());

        // Single hairline catch-light
        if (bounds.getHeight() > 2.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.30f));
            g.fillRect(bounds.getX(), bounds.getY() + 1.0f, 1.0f, bounds.getHeight() - 2.0f);
        }
    }

    void paintFolderCapBar(juce::Graphics& g, juce::Rectangle<float> bounds, bool selected)
    {
        auto cap = getFolderCapBounds();
        if (cap.isEmpty() || cap.getWidth() <= 0.0f || cap.getHeight() < 10.0f)
            return;

        auto familyColour = getFolderFamilyColour();

        // Safety check
        if (familyColour.isTransparent())
            return;

        // Refined muted-glass cap — family colour stays present but composed
        auto capBase = familyColour.withMultipliedSaturation(0.85f)
                                    .withMultipliedBrightness(selected ? 0.52f : 0.40f);
        if (cachedCapBarBounds_ != cap || cachedCapBarColour_ != familyColour || cachedCapBarSelected_ != selected) { cachedCapBarGradient_ = juce::ColourGradient(capBase.brighter(0.20f), cap.getCentreX(), cap.getY(), capBase.darker(0.25f), cap.getCentreX(), cap.getBottom(), false); cachedCapBarGradient_.addColour(0.24, capBase.brighter(0.10f)); cachedCapBarBounds_ = cap; cachedCapBarColour_ = familyColour; cachedCapBarSelected_ = selected; } g.setGradientFill(cachedCapBarGradient_); g.fillRoundedRectangle(cap, 10.0f);

        auto capBody = cap.withTrimmedLeft(3.0f);
        if (capBody.isEmpty() || capBody.getWidth() <= 0.0f)
            return;

        // Hairline border — definition without shouting
        g.setColour(familyColour.brighter(0.30f).withAlpha(0.45f));
        g.drawRoundedRectangle(capBody, 8.0f, 1.0f);

        // Top highlight for depth
        if (capBody.getWidth() > 18.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.14f));
            g.drawLine(capBody.getX() + 9.0f, capBody.getY() + 1.2f,
                       capBody.getRight() - 9.0f, capBody.getY() + 1.2f, 1.0f);
        }

        // Bottom accent line
        if (capBody.getWidth() > 18.0f)
        {
            g.setColour(familyColour.brighter(0.25f).withAlpha(0.35f));
            g.drawLine(capBody.getX() + 9.0f, capBody.getBottom() - 0.7f,
                       capBody.getRight() - 9.0f, capBody.getBottom() - 0.7f, 1.0f);
        }

        const float arrowRight = (float) folderToggleBtn_.getRight() + 4.0f;
        const float labelRight = cap.getRight() - 7.0f;
        const float iconW = 10.0f;
        const float labelX = arrowRight + iconW + 4.0f;
        const float labelWidth = juce::jmax(12.0f, labelRight - labelX);
        auto nameBounds = juce::Rectangle<float>(labelX, cap.getY() + 1.0f, labelWidth, cap.getHeight() - 2.0f);

        // Persistent folder-tab glyph: identifies the strip as a FolderBus
        // even when its name is short or the family colour is subtle.
        juce::Path folderGlyph;
        const float glyphY = cap.getCentreY() - 3.5f;
        folderGlyph.addRoundedRectangle(arrowRight, glyphY + 1.5f, iconW, 6.0f, 1.4f);
        folderGlyph.addRoundedRectangle(arrowRight, glyphY, 5.0f, 3.0f, 1.0f);
        g.setColour(familyColour.brighter(0.45f).withAlpha(0.95f));
        g.fillPath(folderGlyph);

        // Track name with text shadow for contrast
        g.setColour(juce::Colours::black.withAlpha(0.60f));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(track_.getName(), nameBounds.toNearestInt().translated(0, 1), juce::Justification::centredLeft, true);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(track_.getName(), nameBounds.toNearestInt(), juce::Justification::centredLeft, true);

        if (folderShimmerProgress_ >= 0.0f && folderShimmerProgress_ <= 1.0f)
        {
            juce::Graphics::ScopedSaveState save(g);
            juce::Path clip;
            clip.addRoundedRectangle(capBody, 8.0f);
            g.reduceClipRegion(clip);

            const float sweepWidth = juce::jmax(28.0f, capBody.getWidth() * 0.42f);
            const float sweepStart = capBody.getX() - sweepWidth;
            const float sweepX = sweepStart + (capBody.getWidth() + sweepWidth * 2.0f) * folderShimmerProgress_;
            juce::ColourGradient shimmer(
                juce::Colours::transparentWhite, sweepX - sweepWidth * 0.5f, capBody.getCentreY(),
                juce::Colours::white.withAlpha(0.35f), sweepX, capBody.getCentreY(), false);
            shimmer.addColour(0.50, juce::Colours::white.withAlpha(0.48f));
            shimmer.addColour(1.00, juce::Colours::transparentWhite);
            g.setGradientFill(shimmer);
            g.fillRect(capBody.getX(), capBody.getY(), capBody.getWidth(), capBody.getHeight());
        }

    }

    void invalidateFaderScaleCache() const noexcept { faderScaleCacheValid_ = false; }

    void ensureFaderScaleCacheBuilt() const
    {
        const auto ft = faderTrackBounds_;
        const int w = getWidth();
        const int h = getHeight();
        if (faderScaleCacheValid_ && cachedFaderScale_.isValid()
            && cachedFaderScale_.getWidth() == w && cachedFaderScale_.getHeight() == h)
            return;

        cachedFaderScale_ = juce::Image(juce::Image::ARGB, juce::jmax(1, w), juce::jmax(1, h), true);
        faderScaleCacheValid_ = true;

        juce::Graphics ig(cachedFaderScale_);
        const float tw = 5.0f;
        const float cx = ft.getCentreX();
        const float tickX0  = track_.isMaster() ? cx + tw * 0.5f + 7.f : cx + tw * 0.5f + 2.f;
        const float tickX1M = tickX0 + (track_.isMaster() ? 14.f : 8.f);
        const float tickX1m = tickX0 + (track_.isMaster() ? 7.f : 4.f);
        const float labelX  = tickX1M + 3.f;
        const float labelRight = (float)w - (track_.isMaster() ? 8.f : 4.f);
        const float labelW  = juce::jmax(0.0f, labelRight - labelX);
        const float fontH = juce::jlimit(7.0f, 9.5f, ft.getHeight() * 0.028f);
        const float labelH = juce::jmax(10.0f, fontH + 3.0f);

        const auto displayTicks = faderRange_.getDisplayTicks(ft.getHeight());
        for (const auto& tick : displayTicks)
        {
            const float yN  = 1.0f - faderRange_.dbToNorm(tick.db);
            const float yPx = ft.getY() + yN * ft.getHeight();
            const bool  isUnity = (std::abs(tick.db) < 0.1f);

            if (tick.isMajor)
            {
                juce::Colour col = isUnity ? Theme::getInstance().apex.color.magentaBright
                                           : juce::Colours::white;
                ig.setColour(col);
                ig.drawLine(tickX0, yPx,
                            isUnity ? tickX1M + 3.f : tickX1M, yPx,
                            isUnity ? 1.8f : 1.3f);

                if (labelW > 4.f)
                {
                    juce::String lbl(tick.label);
                    if (lbl.isNotEmpty())
                    {
                        ig.setColour(col);
                        ig.setFont(juce::Font(fontH, isUnity ? juce::Font::bold : juce::Font::plain));
                        ig.drawText(lbl,
                                    juce::Rectangle<int>((int)labelX,
                                                         juce::roundToInt(yPx - labelH * 0.5f),
                                                         juce::roundToInt(labelW),
                                                         juce::roundToInt(labelH)),
                                    juce::Justification::centredLeft, false);
                    }
                }
            }
            else
            {
                ig.setColour(juce::Colours::white.withAlpha(0.42f));
                ig.drawLine(tickX0, yPx, tickX1m, yPx, 0.9f);
            }
        }
    }

    void paintFaderScaleOverlay(juce::Graphics& g)
    {
        if (useLegacyMixerSkin() || faderTrackBounds_.getHeight() <= 8.0f)
            return;

        ensureFaderScaleCacheBuilt();
        if (cachedFaderScale_.isValid())
            g.drawImageAt(cachedFaderScale_, 0, 0, false);

        // Live dB readout (changes with volume — painted directly, not cached)
        const auto ft = faderTrackBounds_;
        const float currentDb = FaderRangeCore::gainToDb(track_.getVolume());
        juce::String dbStr = (currentDb <= -96.f) ? "-inf" : juce::String(currentDb, 1) + " dB";
        g.setColour(juce::Colours::white.withAlpha(0.88f));
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.drawText(dbStr, (int)ft.getX(), (int)ft.getBottom() + 2, (int)ft.getWidth(), 12,
                   juce::Justification::centred, false);
    }

    void ensurePremiumBaseCacheBuilt(juce::Rectangle<float> bounds, const PremiumBaseKey& key) const
    {
        const int w = (int) std::ceil(bounds.getWidth());
        const int h = (int) std::ceil(bounds.getHeight());
        if (w <= 0 || h <= 0) return;

        if (premiumBaseCacheValid_
            && cachedPremiumBase_.isValid()
            && cachedPremiumBaseKey_ == key
            && cachedPremiumBase_.getWidth()  == w
            && cachedPremiumBase_.getHeight() == h)
            return;

        cachedPremiumBase_     = juce::Image(juce::Image::ARGB, w, h, true);
        cachedPremiumBaseKey_  = key;
        premiumBaseCacheValid_ = true;
        ++premiumBaseBakeCount_;

        juce::Graphics ig(cachedPremiumBase_);
        const juce::Rectangle<float> b(0.0f, 0.0f, (float)w, (float)h);
        const bool selected = key.selected;
        constexpr float cr = 14.0f;

        // Outer drop shadow
        ig.setColour(juce::Colours::black.withAlpha(0.55f));
        ig.fillRoundedRectangle(b.expanded(0.f, 1.5f).translated(0.f, 3.f), cr + 1.f);
        ig.setColour(juce::Colours::black.withAlpha(0.28f));
        ig.fillRoundedRectangle(b.expanded(0.f, 3.f).translated(0.f, 6.f), cr + 2.f);

        juce::Path clip;
        clip.addRoundedRectangle(b, cr);
        juce::Graphics::ScopedSaveState save(ig);
        ig.reduceClipRegion(clip);

        // Strip glass body
        juce::ColourGradient base(
            juce::Colour(0x7A2A3140), b.getCentreX(), b.getY(),
            juce::Colour(0x96101218), b.getCentreX(), b.getBottom(), false);
        base.addColour(0.22, juce::Colour(0x6E323A4A));
        base.addColour(0.58, juce::Colour(0x7A181B24));
        ig.setGradientFill(base);
        ig.fillRoundedRectangle(b, cr);

        {
            juce::ColourGradient coolTint(
                juce::Colour(0x165FA8D3), b.getX() + b.getWidth() * 0.20f, b.getY() + b.getHeight() * 0.12f,
                juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(), true);
            ig.setGradientFill(coolTint);
            ig.fillRoundedRectangle(b, cr);
        }
        {
            juce::ColourGradient warmTint(
                juce::Colour(0x12D39B6A), b.getRight() - b.getWidth() * 0.18f, b.getY() + b.getHeight() * 0.28f,
                juce::Colours::transparentBlack, b.getCentreX(), b.getBottom(), true);
            ig.setGradientFill(warmTint);
            ig.fillRoundedRectangle(b, cr);
        }

        // Top gloss sheen
        {
            auto topZone = b.withHeight(b.getHeight() * 0.40f);
            juce::ColourGradient gloss(
                juce::Colours::white.withAlpha(selected ? 0.10f : 0.05f),
                topZone.getCentreX(), topZone.getY(),
                juce::Colours::transparentWhite,
                topZone.getCentreX(), topZone.getBottom(), false);
            ig.setGradientFill(gloss);
            ig.fillRoundedRectangle(topZone, cr);
        }
        {
            auto glassBand = b.reduced(2.0f).withHeight(b.getHeight() * 0.24f);
            juce::ColourGradient reflection(
                juce::Colours::white.withAlpha(selected ? 0.09f : 0.06f), glassBand.getCentreX(), glassBand.getY(),
                juce::Colours::transparentWhite, glassBand.getCentreX(), glassBand.getBottom(), false);
            ig.setGradientFill(reflection);
            ig.fillRoundedRectangle(glassBand, cr - 3.0f);
        }

        // Left catch-light streak
        {
            juce::ColourGradient streak(
                juce::Colours::white.withAlpha(0.28f), b.getX() + 5.5f, b.getY(),
                juce::Colours::transparentWhite,       b.getX() + 5.5f, b.getBottom(), false);
            streak.addColour(0.35f, juce::Colours::white.withAlpha(0.04f));
            ig.setGradientFill(streak);
            ig.fillRect(b.getX() + 5.f, b.getY() + cr, 1.f, b.getHeight() - cr * 2.f);
        }

        // Outer border
        ig.setColour(juce::Colours::white.withAlpha(selected ? 0.28f : 0.13f));
        ig.drawRoundedRectangle(b, cr, 1.0f);

        // Inset top catch-light
        ig.setColour(juce::Colours::white.withAlpha(0.35f));
        ig.drawLine(b.getX() + cr * 0.6f, b.getY() + 0.7f,
                    b.getRight() - cr * 0.6f, b.getY() + 0.7f, 0.8f);

        // Inset right depth shadow
        {
            juce::ColourGradient rc(
                juce::Colours::black.withAlpha(0.22f), b.getRight() - 1.f, b.getCentreY(),
                juce::Colours::transparentBlack,       b.getRight() - 3.f, b.getCentreY(), false);
            ig.setGradientFill(rc);
            ig.fillRect(b.getRight() - 2.5f, b.getY() + cr, 2.5f, b.getHeight() - cr * 2.f);
        }
    }

    void paintPremiumStripBase(juce::Graphics& g, juce::Rectangle<float> bounds, bool selected)
    {
        if (bounds.isEmpty())
            return;

        PremiumBaseKey key;
        key.width    = (int) std::ceil(bounds.getWidth());
        key.height   = (int) std::ceil(bounds.getHeight());
        key.selected = selected;

        ensurePremiumBaseCacheBuilt(bounds, key);

        if (cachedPremiumBase_.isValid())
        {
            g.drawImageAt(cachedPremiumBase_,
                          (int) std::round(bounds.getX()),
                          (int) std::round(bounds.getY()),
                          false);
        }
    }

    void paintSimplePurpleSelection(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (bounds.isEmpty())
            return;

        constexpr float cr = 6.0f;

        // Clean purple background - professional DAW style
        juce::ColourGradient purpleGrad(
            juce::Colour(0xFF6A4C93).withAlpha(0.85f), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0xFF4A306D).withAlpha(0.95f), bounds.getCentreX(), bounds.getBottom(), false);

        g.setGradientFill(purpleGrad);
        g.fillRoundedRectangle(bounds, cr);

        // Purple border
        g.setColour(juce::Colour(0xFF8B6FB8).withAlpha(0.9f));
        g.drawRoundedRectangle(bounds, cr, 1.5f);

        // Subtle inner highlight
        g.setColour(juce::Colour(0xFFB19CD9).withAlpha(0.4f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), cr - 1.0f, 1.0f);
    }

    void paintSelectedPurpleAccent(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (bounds.isEmpty())
            return;

        constexpr float cr = 14.0f;

        constexpr float pulse = 0.5f;
        auto& a = Theme::getInstance().apex;

        // Outer neon magenta bloom — wide diffuse halo
        for (int i = 1; i <= 3; ++i)
        {
            const float expand = (float)i * 1.4f;
            const float alpha = (0.18f + pulse * 0.10f) / (float)i;
            g.setColour(a.color.magenta.withAlpha(alpha));
            g.drawRoundedRectangle(bounds.expanded(expand), cr + expand * 0.3f, 1.2f);
        }

        // Main neon rim — top brighter, fades to pure magenta at bottom
        juce::ColourGradient rim(
            a.color.pink.withAlpha(0.95f + pulse * 0.05f),    bounds.getCentreX(), bounds.getY(),
            a.color.magenta.withAlpha(0.80f + pulse * 0.10f), bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(rim);
        g.drawRoundedRectangle(bounds, cr, 1.6f);

        // Inset bevel — subtle white for glass depth
        g.setColour(juce::Colours::white.withAlpha(0.12f + pulse * 0.04f));
        g.drawRoundedRectangle(bounds.reduced(1.2f), cr - 1.0f, 0.7f);
    }

    void paintSelectedPurpleBackground(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (bounds.isEmpty())
            return;

        constexpr float cr = 6.0f;

        // Purple background for selected tracks
        juce::ColourGradient purpleGrad(
            juce::Colour(0xFF8A2BE2).withAlpha(0.7f), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0xFF4B0082).withAlpha(0.9f), bounds.getCentreX(), bounds.getBottom(), false);
        purpleGrad.addColour(0.3f, juce::Colour(0xFF9932CC).withAlpha(0.8f));

        g.setGradientFill(purpleGrad);
        g.fillRoundedRectangle(bounds, cr);

        // Purple glow/border
        for (int i = 1; i <= 3; ++i)
        {
            const float expand = (float) i * 1.2f;
            const float alpha = 0.4f / (float) i;
            g.setColour(juce::Colour(0xFF8A2BE2).withAlpha(alpha));
            g.drawRoundedRectangle(bounds.expanded(expand), cr + expand * 0.3f, 1.0f);
        }

        // Inner purple rim
        g.setColour(juce::Colour(0xFFBA55D3).withAlpha(0.6f));
        g.drawRoundedRectangle(bounds, cr, 1.5f);
    }

    void paintUnselectedMotion(juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (bounds.isEmpty())
            return;

        juce::Graphics::ScopedSaveState save(g);
        g.setOpacity(0.22f);
        selectionVisual_.paintLavaOnly(g, bounds.withTrimmedTop(bounds.getHeight() * 0.14f));

        g.setOpacity(0.16f);
        selectionVisual_.paintParticlesOnly(g, bounds);
    }

    static float stripBoundsHeightPortion(juce::Rectangle<float> bounds, float portion)
    {
        return bounds.getHeight() * portion;
    }

    void initChildren()
    {
        // Label — APEX typography: panel title role, primary text
        label_.setText(track_.getName(), juce::dontSendNotification);
        {
            juce::Font f(13.f, juce::Font::plain);
            if (track_.isMaster())
            {
                f = juce::Font(14.f, juce::Font::plain);
                label_.setColour(juce::Label::textColourId,
                                 Theme::getInstance().apex.color.textPrimary);
            }
            else
            {
                label_.setColour(juce::Label::textColourId,
                                 Theme::getInstance().apex.color.textPrimary);
            }
            label_.setFont(f);
        }
        label_.setJustificationType(juce::Justification::centred);
        label_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label_);

        // Buttons
        muteBtn_.active = track_.isMuted();
        soloBtn_.active = track_.isSoloed();
        armBtn_.active  = track_.isArmed();

        // Tooltip text for mixer channel strip buttons
        muteBtn_.tooltipText   = "Mute (M)\nMute this track. The track will produce no audio output during playback.";
        soloBtn_.tooltipText   = "Solo (S)\nSolo this track. All other non-soloed tracks are muted. Hold Ctrl for latch-solo.";
        armBtn_.tooltipText    = "Record Arm (R)\nArm this track for recording. When armed, incoming audio or MIDI will be captured on record.";
        autoModeBtn_.tooltipText = "Automation Mode\nClick to cycle: Off -> Read -> Touch -> Latch -> Write.\nOff: no automation.\nRead: plays existing automation.\nTouch: writes only while dragging.\nLatch: overwrites until playback stops.\nWrite: writes continuously.";
        inputBtn_.tooltipText  = "Input Trim\nOpen the input trim panel to adjust gain, phase, and routing before the track.";
        preFaderBtn_.tooltipText = "Send Pre/Post-Fader\nShows the pre/post-fader state of sends leaving this track.\n- = no sends, POST = all post-fader, PRE = all pre-fader, MIX = mixed.\nClick to toggle (per-target menu when multiple sends exist).";
        trackLensBtn_.tooltipText = "Track Lens\nToggle the track lens view for advanced clip inspection.";

        muteBtn_.onClick = [this]
        {
            const bool newState = muteBtn_.active;
            if (muteParameter_ != nullptr)
            {
                muteParameter_->beginGesture();
                muteParameter_->setValueFromUser (newState ? 1.0f : 0.0f);
                muteParameter_->endGesture();
            }
            if (onMuteToggled) onMuteToggled(newState);
        };
        muteBtn_.onRightClick = [this](const juce::MouseEvent&)
        {
            if (onExclusiveMuteRequested)
                onExclusiveMuteRequested();
        };
        soloBtn_.onClick = [this]
        {
            const bool newState = soloBtn_.active;
            if (soloParameter_ != nullptr)
            {
                soloParameter_->beginGesture();
                soloParameter_->setValueFromUser (newState ? 1.0f : 0.0f);
                soloParameter_->endGesture();
            }
            if (onSoloToggled) onSoloToggled(newState);
        };
        armBtn_.onClick  = [this] { if (onArmToggled)  onArmToggled(armBtn_.active); };
        armBtn_.onRightClick = [this](const juce::MouseEvent& e)
        {
            juce::PopupMenu menu;
            menu.addSectionHeader("Recording Mode");

            // Only show "Record Dry" option, hide "Record With Effects (printed)"
            constexpr int kRecordDry = 1;

            menu.addItem(kRecordDry, "Record Dry (effects monitored only)", true, true);

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(getParentComponent()),
                               [this, kRecordDry](int result)
                               {
                                   if (result == kRecordDry)
                                   {
                                       // Record dry mode selected
                                       if (onArmToggled) onArmToggled(armBtn_.active);
                                   }
                               });
        };
        autoModeBtn_.onClick = [this]
        {
            currentAutoMode_ = nextAutoMode (currentAutoMode_);
            autoModeBtn_.label  = automationModeLabel (currentAutoMode_);
            autoModeBtn_.active = (currentAutoMode_ != apex::automation::AutomationMode::Off);
            broadcastModeToAllTrackParameters (currentAutoMode_);
            autoModeBtn_.repaint();
        };
inputBtn_.onClick = [this]
        {
            inputBtn_.active = false;
            if (onInputPanelRequested) onInputPanelRequested();
            else InputTrimFloatingPanel::showForTrack(track_, getTopLevelComponent());
        };
        preFaderBtn_.onClick = [this]
        {
            // SmallButton toggles `active` before firing onClick; the real
            // pre/post state is re-synced from the routing graph by
            // refreshPreFaderStates after the toggle, so the transient flip
            // here is harmless.
            if (onPreFaderToggleRequested) onPreFaderToggleRequested(track_.getID());
        };

        addAndMakeVisible(muteBtn_);
        addAndMakeVisible(soloBtn_);
        addAndMakeVisible(armBtn_);
        addAndMakeVisible(inputBtn_);
        addAndMakeVisible(preFaderBtn_);
        addAndMakeVisible(autoModeBtn_);

        folderToggleBtn_.setVisible(false);
        folderToggleBtn_.onClick = [safeStrip = juce::Component::SafePointer<MixerStrip>(this)]
        {
            // The strip may have been destroyed by a rebuild before this
            // queued click is dispatched; never dereference a raw 'this'.
            if (safeStrip != nullptr)
                safeStrip->requestFolderToggle();
        };
        addAndMakeVisible(folderToggleBtn_);

        // Pan knob
        panKnob_.value = track_.getPan();
        panKnob_.onGestureBegin = [this]
        {
            if (panParameter_ != nullptr)
                panParameter_->beginGesture();
        };
        panKnob_.onGestureEnd = [this]
        {
            if (panParameter_ != nullptr)
                panParameter_->endGesture();
        };
        panKnob_.onChanged = [this](float oldV, float newV)
        {
            if (panParameter_ != nullptr)
                panParameter_->setValueFromUser (juce::jlimit (0.0f, 1.0f, (newV + 1.0f) * 0.5f));
            else
                track_.setPan(newV);
            if (onPanChanged) onPanChanged(oldV, newV);
        };
        addAndMakeVisible(panKnob_);

        // TrackLens toggle (tiny button to the right of pan)
        trackLensBtn_.setInterceptsMouseClicks(true, true);
        trackLensBtn_.onClick = [this]
        {
            if (onTrackLensRequested) onTrackLensRequested(track_.getID());
        };
        addAndMakeVisible(trackLensBtn_);

        // Meter
        meter_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(meter_);

        // Personal trim VU needle — bound to the SAME per-track InputMeterCore
        // as the trim panel so mixer, timeline row and panel agree by
        // construction. Ticked by the MixerPanel's presentation-clock path.
        vu_.setSource(&track_.getInputMeter());
        vu_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(vu_);

        // Master-only init (sections, header buttons, flip observer)
        if (track_.isMaster())
            initMasterModeChildren();
    }

    /** Lazy-init for master-only UI sections, header buttons, and flip observer.
     *  Called from initChildren() at the end, only when track_.isMaster().
     *
     *  Sections are created UNBOUND — engine pointers will be wired in a
     *  later batch via setMasterEngines(). Until then, they render their
     *  "not bound" placeholder visuals safely. */
    void initMasterModeChildren()
    {
        // ── Sections (lazy-instantiated) ─────────────────────────────
        ceilingSection_    = std::make_unique<MasterStripCeilingSection>();
        ditherSection_     = std::make_unique<MasterStripDitherSection>();
        meteringSection_   = std::make_unique<MasterStripMeteringSection>();
        phaseWidthSection_ = std::make_unique<MasterStripPhaseWidthSection>();

        // Bind sections to their (still-unbound) bindings so they can render
        // even before engine pointers are wired.
        ceilingSection_->setBinding(&ceilingBinding_);
        ditherSection_ ->setBinding(&ditherBinding_);

        addAndMakeVisible(*ceilingSection_);
        addAndMakeVisible(*ditherSection_);
        addAndMakeVisible(*meteringSection_);
        addAndMakeVisible(*phaseWidthSection_);

        // ── Header buttons ───────────────────────────────────────────
        utilityBtn_.onClick = [this]
        {
            if (MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled
                && onMasterUtilityRequested)
                onMasterUtilityRequested(*this);
        };
        flipBtn_.onClick = []
        {
            MasterStripFlipState::getGlobalInstance().flip();
        };
        fxBtn_.onClick = [this]
        {
            if (onMasterFxRequested) onMasterFxRequested();
            else                     DBG("[MixerStrip] master FX button clicked, no callback wired");
        };

        utilityBtn_.setButtonText(juce::String::fromUTF8("\xE2\x8A\x9E"));  // ⊞ panel
        flipBtn_.setButtonText(juce::String::fromUTF8("\xE2\x87\x84"));    // ⇄ flip
        fxBtn_.setButtonText("FX");
        optionsBtn_.setButtonText("-");

        utilityBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF0F1219));
        utilityBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFF4F8A));
        utilityBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFFFB3D0));
        utilityBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

        // Style the header buttons minimally
        flipBtn_.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xFF0F1219));
        flipBtn_.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xFFFF4F8A));
        flipBtn_.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xFFFFB3D0));
        flipBtn_.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);

        fxBtn_.setColour(juce::TextButton::buttonColourId,      juce::Colour(0xFF0F1219));
        fxBtn_.setColour(juce::TextButton::buttonOnColourId,    juce::Colour(0xFFFF4F8A));
        fxBtn_.setColour(juce::TextButton::textColourOffId,     juce::Colour(0xFFFFB3D0));
        fxBtn_.setColour(juce::TextButton::textColourOnId,      juce::Colours::white);

        optionsBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xFF0F1219));
        optionsBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFF4F8A));
        optionsBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFFFB3D0));
        optionsBtn_.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);

        auto styleTabButton = [](juce::TextButton& b)
        {
            b.setColour(juce::TextButton::buttonColourId,     juce::Colour(0xFF0F1219));
            b.setColour(juce::TextButton::buttonOnColourId,   juce::Colour(0xFFFF4F8A));
            b.setColour(juce::TextButton::textColourOffId,    juce::Colour(0xFFFFB3D0));
            b.setColour(juce::TextButton::textColourOnId,     juce::Colours::white);
        };
        styleTabButton(meteringTabBtn_);
        styleTabButton(phaseTabBtn_);
        styleTabButton(ceilingTabBtn_);
        styleTabButton(ditherTabBtn_);

        addChildComponent(utilityBtn_);
        utilityBtn_.setEnabled(MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled);
        utilityBtn_.setInterceptsMouseClicks(
            MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled,
            MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled);
        utilityBtn_.setTooltip({});
        utilityBtn_.setVisible(MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled);
        if (!MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled)
            utilityBtn_.setBounds({});
        addAndMakeVisible(flipBtn_);
        addAndMakeVisible(fxBtn_);
        addAndMakeVisible(optionsBtn_);
        addAndMakeVisible(meteringTabBtn_);
        addAndMakeVisible(phaseTabBtn_);
        addAndMakeVisible(ceilingTabBtn_);
        addAndMakeVisible(ditherTabBtn_);

        // ── Flip state observer ──────────────────────────────────────
        flipObserver_ = std::make_unique<FlipObserver>(*this);
        MasterStripFlipState::getGlobalInstance().addListener(flipObserver_.get());

        // Apply initial flip state (visibility of front/back sections will be
        // set in layoutMasterMode() — SUB-PATCH 4)
    }

    /** Paints just the vertical fader track + thumb at faderTrackBounds_.
     *
     *  Extracted from the existing paint() body (lines 208-297) so that the
     *  master-mode paint path can reuse the same fader visual without
     *  duplicating code or breaking the normal-strip paint flow.
     *
     *  Both regular strips and master strip use this helper. Regular strip's
     *  paint() will call it at the end; master strip's paintMasterMode()
     *  will call it after painting its own background+header. */
    void paintFader(juce::Graphics& g)
    {
        if (faderTrackBounds_.getHeight() <= 8.f) return;

        auto& t = Theme::getInstance();
        auto ft = faderTrackBounds_;
        float cx = ft.getCentreX();
        float tw = 5.f;
        const auto thumbR = getFaderThumbBounds();

        if (useLegacyMixerSkin())
        {
            g.setColour(t.colors.graphite);
            g.fillRoundedRectangle(cx - tw * 0.5f, ft.getY(), tw, ft.getHeight(), tw * 0.5f);
        }
        else
        {
            g.setColour(juce::Colour(0xFF050608));
            g.fillRoundedRectangle(cx - tw * 0.5f, ft.getY(), tw, ft.getHeight(), tw * 0.5f);
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawRoundedRectangle(cx - tw * 0.5f, ft.getY(), tw, ft.getHeight(), tw * 0.5f, 0.7f);
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.drawLine(cx - tw * 0.5f + 0.8f, ft.getY() + 2.f,
                       cx - tw * 0.5f + 0.8f, ft.getBottom() - 2.f, 0.8f);
        }

        const float currentDb = FaderRangeCore::gainToDb(track_.getVolume());
        float norm = faderRange_.dbToNorm(currentDb);
        if (useLegacyMixerSkin())
            FaderGradientRenderer::drawFaderFill(g, ft, norm, tw);
        else
        {
            float fillH = norm * ft.getHeight();
            g.setColour(juce::Colours::white.withAlpha(0.72f));
            g.fillRoundedRectangle(cx - tw * 0.5f + 1.f,
                                   ft.getBottom() - fillH,
                                   tw - 2.f, fillH, (tw - 2.f) * 0.4f);
        }

        constexpr float thCR = 2.5f;

        if (useLegacyMixerSkin())
        {
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.fillRoundedRectangle(thumbR, thCR);
            g.setColour(t.colors.border.withAlpha(0.4f));
            g.drawRoundedRectangle(thumbR, thCR, 0.7f);
        }
        else
        {
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.fillRoundedRectangle(thumbR.expanded(1.f, 2.f).translated(0.f, 1.5f), thCR + 1.f);

            juce::ColourGradient cap(
                juce::Colour(0xFFCECBC4), thumbR.getCentreX(), thumbR.getY(),
                juce::Colour(0xFF303030), thumbR.getCentreX(), thumbR.getBottom(), false);
            cap.addColour(0.45, juce::Colour(0xFF888680));
            cap.addColour(0.55, juce::Colour(0xFF505050));
            g.setGradientFill(cap);
            g.fillRoundedRectangle(thumbR, thCR);

            g.setColour(juce::Colours::white.withAlpha(0.70f));
            g.drawLine(thumbR.getX() + thCR, thumbR.getY() + 0.6f,
                       thumbR.getRight() - thCR, thumbR.getY() + 0.6f, 0.9f);

            g.setColour(juce::Colours::black.withAlpha(0.50f));
            g.drawLine(thumbR.getX() + thCR, thumbR.getBottom() - 0.6f,
                       thumbR.getRight() - thCR, thumbR.getBottom() - 0.6f, 0.8f);

            g.setColour(juce::Colours::white.withAlpha(0.30f));
            g.drawRoundedRectangle(thumbR, thCR, 0.8f);
        }

        const juce::String dbText = currentDb <= faderRange_.getMinDb() + 0.1f
            ? juce::String("-inf")
            : juce::String(currentDb, 1);
        auto valuePill = juce::Rectangle<float>(0.0f, 0.0f,
                                                track_.isMaster() ? 34.0f : 30.0f,
                                                11.0f)
            .withCentre({ thumbR.getCentreX(), thumbR.getCentreY() });
        valuePill = valuePill.getIntersection(getLocalBounds().toFloat().reduced(2.0f));

        g.setColour(juce::Colours::black.withAlpha(0.68f));
        g.fillRoundedRectangle(valuePill, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawRoundedRectangle(valuePill, 4.0f, 0.6f);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(juce::Font(7.0f, juce::Font::bold));
        g.drawFittedText(dbText, valuePill.toNearestInt(), juce::Justification::centred, 1, 0.85f);
    }

    /** Layout when track_.isMaster(). Composes a vertical stack:
     *
         *    [ header: label + utility + fx ]
     *    [ persistent: mute/solo/fader/mini-meter ]
     *
     *  Visibility of front/back sections is driven by MasterStripFlipState.
     *
     *  Hides regular-strip-only children: pianoBtn_, armBtn_, inputBtn_,
     *  panKnob_, folderToggleBtn_. */
    void layoutMasterMode()
    {
        auto b = getLocalBounds();

// Hide all regular-strip-only children
        pianoBtn_.setVisible(false);  pianoBtn_.setBounds({});
        armBtn_.setVisible(false);    armBtn_.setBounds({});
        inputBtn_.setVisible(false);  inputBtn_.setBounds({});
        preFaderBtn_.setVisible(false); preFaderBtn_.setBounds({});
        panKnob_.setBounds({});
        trackLensBtn_.setVisible(false);
        trackLensBtn_.setBounds({});
        folderToggleBtn_.setVisible(false);  folderToggleBtn_.setBounds({});

        // Skip top color bar area (4px) — drip header replaces it
        b.removeFromTop(4);

        // ── Header row (40px): custom text + panel button right-aligned ───
        auto headerRow = b.removeFromTop(40).reduced(6, 6);

        const int btnW = 32;
        const int btnH = 20;
        const int btnGap  = 4;

        // Right side: the Master Utility control is product-hidden for now.
        // Do not reserve its slot when hidden; the backend panel remains alive
        // for state, persistence, and a future UI re-enable.
        if (MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled)
        {
            auto rightSlot = headerRow.removeFromRight(btnW);
            auto centredBtn = rightSlot.withSizeKeepingCentre(btnW, btnH);
            utilityBtn_.setBounds(centredBtn);
            utilityBtn_.setVisible(true);
        }
        else
        {
            utilityBtn_.setVisible(false);
            utilityBtn_.setEnabled(false);
            utilityBtn_.setInterceptsMouseClicks(false, false);
            utilityBtn_.setTooltip({});
            utilityBtn_.setBounds({});
        }
        fxBtn_.setVisible(false);
        fxBtn_.setBounds({});
        flipBtn_.setVisible(false);
        flipBtn_.setBounds({});
        optionsBtn_.setVisible(false);
        optionsBtn_.setBounds({});

        // Master title is painted manually for better typography.
        label_.setVisible(false);
        label_.setBounds({});

        // Use the full remaining master strip for the main controls now that
        // master special features live in the adjacent utility panel.
        const int persistentMuteSoloH = 22;
        ceilingSection_   ->setVisible(false);
        ditherSection_    ->setVisible(false);
        meteringSection_  ->setVisible(false);
        phaseWidthSection_->setVisible(false);
        meteringTabBtn_.setVisible(false);
        phaseTabBtn_.setVisible(false);
        ceilingTabBtn_.setVisible(false);
        ditherTabBtn_.setVisible(false);

        // ── Persistent block: mute/solo + fader+mini-meter ───────────
        auto persistArea = b;

        // Mute/Solo row at top of persistent block
        auto muteSoloRow = persistArea.removeFromTop(persistentMuteSoloH).reduced(8, 1);
        const int msButtonGap = 4;
        const int msButtonW = juce::jmax(20, (muteSoloRow.getWidth() - msButtonGap) / 2);
        muteBtn_.setVisible(true);
        soloBtn_.setVisible(true);
        muteBtn_.setBounds(muteSoloRow.removeFromLeft(msButtonW));
        muteSoloRow.removeFromLeft(msButtonGap);
        soloBtn_.setBounds(muteSoloRow);

        // Automation mode button on master
        {
            auto autoRow = persistArea.removeFromTop(18).reduced(8, 1);
            autoModeBtn_.setBounds (autoRow);
        }

        persistArea.removeFromTop(4);

        // Fader area: mini-meter on left, fader rail on right of persist.
        // Keep this large when sections are minimized so dB ticks are readable.
        auto faderArea = persistArea.reduced(8, 4);

        // Mini stereo meter on left edge (16 wide for L+R bars)
        const int miniMeterW = 16;
        meter_.setVisible(true);
        meter_.setBounds(faderArea.removeFromLeft(miniMeterW));
        faderArea.removeFromLeft(6);

        // Fader rail: fixed narrow lane like regular strips, with right-side
        // space left empty for the standard dB tick overlay.
        auto railLane = faderArea.removeFromLeft(52);
        faderTrackBounds_ = railLane.toFloat();
    }

    void paintMasterFaderTicks(juce::Graphics& g)
    {
        if (faderTrackBounds_.getHeight() <= 8.0f)
            return;

        const auto ft = faderTrackBounds_;
        const float railX = ft.getCentreX();
        const float tickLeft = railX - 24.0f;
        const float tickRight = railX - 7.0f;
        const float labelRight = tickLeft - 3.0f;

        struct Tick { float db; const char* label; bool major; };
        static constexpr Tick ticks[] = {
            {  6.0f, "+6", true }, {  3.0f, "+3", false }, {  0.0f, "0", true },
            { -3.0f, "-3", true }, { -6.0f, "-6", true }, { -9.0f, "-9", true },
            {-12.0f, "-12", true }, {-18.0f, "-18", true }, {-24.0f, "-24", true },
            {-36.0f, "-36", true }, {-48.0f, "-48", true }
        };

        g.setFont(juce::Font(8.0f, juce::Font::plain));
        for (const auto& tick : ticks)
        {
            const float y = ft.getBottom() - faderRange_.dbToNorm(tick.db) * ft.getHeight();
            if (y < ft.getY() || y > ft.getBottom())
                continue;

            const float alpha = tick.major ? 0.74f : 0.34f;
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.drawLine(tick.major ? tickLeft : tickLeft + 8.0f, y, tickRight, y, tick.major ? 1.0f : 0.6f);

            if (tick.major)
            {
                g.setColour(juce::Colours::white.withAlpha(0.86f));
                g.drawText(tick.label,
                           juce::Rectangle<int>((int) ft.getX(), (int) y - 5, (int) (labelRight - ft.getX()), 10),
                           juce::Justification::centredRight, false);
            }
        }

        g.setColour(juce::Colours::white.withAlpha(0.70f));
        g.drawText("-inf",
                   juce::Rectangle<int>((int) ft.getX(), (int) ft.getBottom() - 12,
                                        (int) (labelRight - ft.getX()), 10),
                   juce::Justification::centredRight, false);
    }

    /** Paint the master-mode visual layers in correct order.
     *
     *  Replaces the normal-strip painting path (color bar, premium base,
     *  selection visuals, fader) for the master strip ONLY. Sections paint
     *  themselves as child components, so this method only handles:
     *    1. Drop shadow (drawn before strip body but visible below)
     *    2. Drip background (body + drips + border)
     *    3. Header text overlay (subtitle "mixing view" / "mastering view")
     *    4. Selected purple accent if selected
     *    5. The fader rail+thumb via paintFader() helper
     *
     *  The flip and FX buttons paint themselves as child TextButtons. */
    void paintMasterMode(juce::Graphics& g)
    {
        auto& a = Theme::getInstance().apex;
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);

        // 1. Deep-wine drip background + APEX decorative geometry — one cached
        //    image, rebuilt only on resize (body, drips, sigil, eye, splatter).
        {
            const int w = juce::jmax(1, (int)std::ceil(bounds.getWidth()));
            const int h = juce::jmax(1, (int)std::ceil(bounds.getHeight()));
            if (!cachedDripBg_.isValid() || cachedDripBg_.getWidth() != w || cachedDripBg_.getHeight() != h)
            {
                cachedDripBg_ = juce::Image(juce::Image::ARGB, w, h, true);
                juce::Graphics ig(cachedDripBg_);
                const auto local = juce::Rectangle<float>(0.f, 0.f, (float)w, (float)h);
                dripRenderer_.paint(ig, local, false);

                // APEX slash / flow — abstract signal-energy streaks in the
                // deep-wine body. No emblem: the Master strip stays clean,
                // premium and focused. Baked once per resize behind all controls.
                {
                    const auto flowZone = local.withTrimmedTop(48.0f)
                                               .withTrimmedBottom(6.0f)
                                               .reduced(4.0f, 0.0f);
                    const auto streaks = ApexPrimitives::buildSlashFlowPaths(
                        flowZone, (juce::uint32) (a.decor.splatterSeed ^ 0x51A5), 16);
                    ApexPrimitives::drawSlashFlow(ig, streaks,
                                                  a.color.magentaDeep, a.color.violet,
                                                  0.30f);
                    const auto streaks2 = ApexPrimitives::buildSlashFlowPaths(
                        flowZone, (juce::uint32) (a.decor.splatterSeed ^ 0xF10A), 10);
                    ApexPrimitives::drawSlashFlow(ig, streaks2,
                                                  a.color.pink, a.color.magenta,
                                                  0.16f);
                }

                // Seeded splatter hugging the left/right edges — creative
                // energy escaping structure, kept away from the header text.
                {
                    auto leftZone  = local.withWidth(juce::jmin(16.0f, local.getWidth() * 0.22f))
                                          .withTrimmedTop(48.0f);
                    auto rightZone = leftZone.translated(local.getWidth() - leftZone.getWidth(), 0.0f);
                    const auto splL = ApexPrimitives::buildSplatterPaths(leftZone,  (juce::uint32) a.decor.splatterSeed, 30);
                    const auto splR = ApexPrimitives::buildSplatterPaths(rightZone, (juce::uint32) (a.decor.splatterSeed ^ 0x5EED), 30);
                    ApexPrimitives::drawSplatter(ig, splL, a.color.magenta, a.decor.splatterOpacity * 0.85f);
                    ApexPrimitives::drawSplatter(ig, splR, a.color.violet,  a.decor.splatterOpacity * 0.65f);
                }
            }
            g.drawImageAt(cachedDripBg_, (int)std::round(bounds.getX()), (int)std::round(bounds.getY()), false);
        }

        // Master-selected identity: static seal using the same near-black/
        // magenta palette as a regular selected track.
        if (selectionVisual_.isSelected())
        {
            const auto seal = juce::Rectangle<float>(bounds.getRight() - 106, 4, 100, 15);
            g.setColour(juce::Colour(0xFF1A0012).withAlpha(0.92f));
            g.fillRoundedRectangle(seal, 7.5f);
            g.setColour(Theme::getInstance().apex.color.magenta.withAlpha(0.95f));
            g.drawRoundedRectangle(seal, 7.5f, 1.0f);
            g.setFont(Theme::getInstance().fonts.bold.withHeight(8.5f));
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.drawText("MASTER SELECTED", seal, juce::Justification::centred);
        }

        // 2. (Removed: floating glass spheres — replaced by the cached APEX
        //    signal geometry above, which carries identity without visual noise.)

        // 3. Header chrome — magenta glow + premium title text
        {
            const bool isFront = MasterStripFlipState::getGlobalInstance().isFront();
            auto header = bounds.withHeight(43.0f).reduced(8.0f, 5.0f);
            header.removeFromRight(40.0f);

            juce::ColourGradient headerGlow(
                a.color.magenta.withAlpha(0.16f), header.getCentreX(), header.getY(),
                juce::Colours::transparentBlack, header.getCentreX(), header.getBottom(), false);
            g.setGradientFill(headerGlow);
            g.fillRoundedRectangle(header.expanded(4.0f, 2.0f), 7.0f);

            auto titleArea = header.withHeight(16.0f).translated(0.0f, 2.0f);
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawText("MASTER", titleArea.toNearestInt().translated(0, 1), juce::Justification::centred, false);
            g.setColour(a.color.textPrimary);
            g.drawText("MASTER", titleArea.toNearestInt(), juce::Justification::centred, false);

            auto subtitleArea = header.withTrimmedTop(16.0f).withHeight(12.0f);
            g.setFont(juce::Font(8.5f, juce::Font::italic));
            g.setColour(a.color.pink.withAlpha(0.88f));
            g.drawText(isFront ? "mixing view" : "mastering view",
                       subtitleArea.toNearestInt(), juce::Justification::centred, false);
        }

        // 4. Selected purple accent (only when selected)
        {
            const bool selected = selectionVisual_.isSelected();
            if (selected)
                paintSelectedPurpleAccent(g, bounds);
        }

        // 5. Fader rail + thumb + dB tick overlay
        {
            paintFader(g);
            paintFaderScaleOverlay(g);
        }
    }

    // ── Premium base image cache ────────────────────────────────────────────
    mutable juce::Image      cachedPremiumBase_;
    mutable PremiumBaseKey   cachedPremiumBaseKey_;
    mutable bool             premiumBaseCacheValid_ = false;
    mutable int              premiumBaseBakeCount_  = 0;
    mutable int              premiumBaseBlitCount_  = 0;

    // ── Fader scale overlay cache (static ticks+labels, blit each paintOverChildren) ──
    mutable juce::Image      cachedFaderScale_;
    mutable bool             faderScaleCacheValid_ = false;

    // ── Master drip background cache ────────────────────────────────
    mutable juce::Image      cachedDripBg_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerStrip)
};

class MasterUtilityPanel : public juce::Component,
                           public MasterStripFlipState::Listener
{
public:
    MasterUtilityPanel()
    {
        MasterStripFlipState::getGlobalInstance().addListener(this);
        ceilingSection_.setBinding(&ceilingBinding_);
        ditherSection_.setBinding(&ditherBinding_);

        flipBtn_.setButtonText(juce::String::fromUTF8("\xE2\x87\x84"));
        closeBtn_.setButtonText(juce::String::fromUTF8("\xC3\x97"));
        meteringTabBtn_.setButtonText("METERING");
        phaseTabBtn_.setButtonText("PHASE");
        ceilingTabBtn_.setButtonText("CEILING");
        ditherTabBtn_.setButtonText("DITHER");

        auto styleButton = [](juce::TextButton& b)
        {
            b.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFF0F1219));
            b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFFF4F8A));
            b.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xFFFFB3D0));
            b.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        };
        styleButton(flipBtn_);
        styleButton(closeBtn_);
        styleButton(meteringTabBtn_);
        styleButton(phaseTabBtn_);
        styleButton(ceilingTabBtn_);
        styleButton(ditherTabBtn_);

        flipBtn_.onClick = [this]
        {
            MasterStripFlipState::getGlobalInstance().flip();
            resized();
            repaint();
        };
        closeBtn_.onClick = [this]
        {
            setVisible(false);
        };

        addAndMakeVisible(flipBtn_);
        addAndMakeVisible(closeBtn_);
        addAndMakeVisible(meteringTabBtn_);
        addAndMakeVisible(phaseTabBtn_);
        addAndMakeVisible(ceilingTabBtn_);
        addAndMakeVisible(ditherTabBtn_);
        addAndMakeVisible(meteringSection_);
        addAndMakeVisible(phaseWidthSection_);
        addAndMakeVisible(ceilingSection_);
        addAndMakeVisible(ditherSection_);

        setVisible(false);
    }

    ~MasterUtilityPanel() override
    {
        MasterStripFlipState::getGlobalInstance().removeListener(this);
    }

    void setMasterEngines(MasterCeilingCore*    ceiling,
                          MasterDitherCore*     dither,
                          MeteringFacadeCore*   postMeter,
                          PhaseWidthFacadeCore* phaseWidth)
    {
        ceilingBinding_.setCeiling(ceiling);
        ditherBinding_ .setDither(dither);
        meteringSection_.setPostMeter(postMeter);

        if (phaseWidth != nullptr)
            phaseWidthSection_.setSources(&phaseWidth->getCorrelation(),
                                          &phaseWidth->getWidth(),
                                          &phaseWidth->getMonoCheck());
        else
            phaseWidthSection_.setSources(nullptr, nullptr, nullptr);

        repaint();
    }

    /** Present visible utility sections from the owning MixerPanel clock. */
    void presentationTick()
    {
        if (!isShowing())
            return;

        if (meteringSection_.isShowing())
            meteringSection_.presentationTick();
        if (phaseWidthSection_.isShowing())
            phaseWidthSection_.presentationTick();
        if (ceilingSection_.isShowing())
            ceilingSection_.presentationTick();
    }

    bool needsPresentationTick() const noexcept
    {
        return isShowing() && (meteringSection_.isShowing()
            || phaseWidthSection_.isShowing()
            || ceilingSection_.isShowing());
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        if (b.isEmpty())
        {
            return;
        }

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(b.translated(0.0f, 3.0f).expanded(0.0f, 1.0f), 10.0f);

        juce::ColourGradient fill(juce::Colour(0xFF251524), b.getCentreX(), b.getY(),
                                  juce::Colour(0xFF0A0D13), b.getCentreX(), b.getBottom(), false);
        fill.addColour(0.32, juce::Colour(0xFF3A1830));
        g.setGradientFill(fill);
        g.fillRoundedRectangle(b, 10.0f);

        g.setColour(juce::Colour(0xFFFF4F8A).withAlpha(0.55f));
        g.drawRoundedRectangle(b, 10.0f, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.16f));
        g.drawRoundedRectangle(b.reduced(1.0f), 9.0f, 0.8f);

        auto header = getLocalBounds().reduced(10, 5).removeFromTop(24);
        const bool isFront = MasterStripFlipState::getGlobalInstance().isFront();
        g.setColour(juce::Colour(0xFFFFE3B0));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        auto titleArea = header.withTrimmedRight(76).removeFromTop(12);
        g.drawFittedText("MASTER UTIL", titleArea, juce::Justification::centredLeft, 1, 0.85f);
        g.setColour(juce::Colour(0xFFFFB3D0).withAlpha(0.80f));
        g.setFont(juce::Font(7.0f, juce::Font::italic));
        g.drawText(isFront ? "mixing view" : "mastering view",
                   header.withTrimmedRight(76).translated(0, 12).withHeight(9),
                   juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(8, 6);
        auto header = b.removeFromTop(28);
        closeBtn_.setBounds(header.removeFromRight(22).withHeight(22));
        header.removeFromRight(4);
        flipBtn_.setBounds(header.removeFromRight(34).withHeight(22));
        b.removeFromTop(4);

        auto tabRow = b.removeFromTop(22).reduced(0, 2);
        const bool isFront = MasterStripFlipState::getGlobalInstance().isFront();
        meteringTabBtn_.setVisible(isFront);
        phaseTabBtn_.setVisible(isFront);
        ceilingTabBtn_.setVisible(!isFront);
        ditherTabBtn_.setVisible(!isFront);

        auto placeTwoTabs = [](juce::Rectangle<int> area, juce::TextButton& a, juce::TextButton& b)
        {
            const int gap = 4;
            const int w = (area.getWidth() - gap) / 2;
            a.setBounds(area.removeFromLeft(w));
            area.removeFromLeft(gap);
            b.setBounds(area);
        };
        if (isFront)
            placeTwoTabs(tabRow, meteringTabBtn_, phaseTabBtn_);
        else
            placeTwoTabs(tabRow, ceilingTabBtn_, ditherTabBtn_);

        b.removeFromTop(4);
        auto content = b.reduced(2, 0);
        if (isFront)
        {
            ceilingSection_.setVisible(false);
            ditherSection_.setVisible(false);
            meteringSection_.setVisible(true);
            phaseWidthSection_.setVisible(true);
            meteringSection_.setBounds(content.removeFromTop(145));
            content.removeFromTop(5);
            phaseWidthSection_.setBounds(content.removeFromTop(86));
        }
        else
        {
            meteringSection_.setVisible(false);
            phaseWidthSection_.setVisible(false);
            ceilingSection_.setVisible(true);
            ditherSection_.setVisible(true);
            ceilingSection_.setBounds(content.removeFromTop(74));
            content.removeFromTop(5);
            ditherSection_.setBounds(content.removeFromTop(54));
        }
    }

private:
    void masterStripFlipChanged(MasterStripFlipState::Side newSide) override
    {
        lastFront_ = newSide == MasterStripFlipState::Side::Front;
        resized();
        repaint();
    }

    MasterStripCeilingBinding ceilingBinding_;
    MasterStripDitherBinding ditherBinding_;
    MasterStripMeteringSection meteringSection_;
    MasterStripPhaseWidthSection phaseWidthSection_;
    MasterStripCeilingSection ceilingSection_;
    MasterStripDitherSection ditherSection_;
    juce::TextButton flipBtn_ { juce::String::fromUTF8("\xE2\x87\x84") };
    juce::TextButton closeBtn_ { juce::String::fromUTF8("\xC3\x97") };
    juce::TextButton meteringTabBtn_ { "METERING" };
    juce::TextButton phaseTabBtn_ { "PHASE" };
    juce::TextButton ceilingTabBtn_ { "CEILING" };
    juce::TextButton ditherTabBtn_ { "DITHER" };
    bool lastFront_ = MasterStripFlipState::getGlobalInstance().isFront();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterUtilityPanel)
};

// ─── MixerPanel ──────────────────────────────────────────────────────────────

class MixerPanel : public juce::Component,
                   public juce::DragAndDropTarget,
                   public ApexPresentationClock::TickReceiver,
                   private TrackManager::Listener
{
public:
    static constexpr int kStripW   = 80;
    static constexpr int kStripGap = 2;
    static constexpr int kDefaultVisibleNormalTracks = 6;

    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        return details.description.toString().startsWith("MixerPluginSlotDrag:");
    }

    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        pluginDropActive_ = true;
        pluginDropHoverTrackId_ = resolvePluginDropTarget(details.localPosition);
        updatePresentationDemand();
        repaint();
    }

    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        const auto target = resolvePluginDropTarget(details.localPosition);
        if (target == pluginDropHoverTrackId_)
            return;
        pluginDropHoverTrackId_ = target;
        updatePresentationDemand();
        repaint();
    }

    void itemDragExit(const juce::DragAndDropTarget::SourceDetails&) override
    {
        if (pluginDropHoverTrackId_.isNotEmpty())
        {
            pluginDropHoverTrackId_.clear();
            repaint();
        }
        pluginDropActive_ = false;
        updatePresentationDemand();
    }

    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        const auto targetId = resolvePluginDropTarget(details.localPosition);
        pluginDropHoverTrackId_.clear();
        pluginDropActive_ = false;
        updatePresentationDemand();
        repaint();

        if (targetId.isEmpty() || !onPluginDropCopy)
            return;

        const auto parts = juce::StringArray::fromTokens(details.description.toString(), ":", "");
        if (parts.size() < 3)
            return;

        const auto sourceId = parts[1];
        const int sourceSlot = parts[2].getIntValue();
        if (sourceId.isEmpty() || sourceId == targetId || sourceSlot < 0)
            return;

        onPluginDropCopy(sourceId, sourceSlot, targetId);
    }

    /**
     * Computes the content width used by the floating Mixer's default size.
     * The Master is a separate pinned strip, so its width and separating gap
     * are included explicitly instead of treating it like a normal track.
     * The caller supplies the number of available normal tracks; the default
     * never grows beyond six merely because a project contains more tracks.
     */
    static int computeDefaultVisibleContentWidth(int availableNormalTracks,
                                                  int masterWidth) noexcept
    {
        const int normalTracks = juce::jlimit(0, kDefaultVisibleNormalTracks,
                                              availableNormalTracks);
        int width = normalTracks > 0
            ? normalTracks * kStripW + (normalTracks - 1) * kStripGap
            : 0;

        if (masterWidth > 0)
            width += (width > 0 ? kStripGap : 0) + masterWidth;

        return width;
    }

    /**
     * Returns the portion of a viewport that is safe for normal strips.  The
     * pinned Master occupies the remaining right-hand portion and must not be
     * treated as available reveal space.
     */
    static int computeSafeNormalViewportWidth(int viewportWidth,
                                              int masterWidth,
                                              int separatingGap = kStripGap) noexcept
    {
        if (viewportWidth <= 0)
            return 1;

        if (masterWidth <= 0)
            return viewportWidth;

        return juce::jmax(1, viewportWidth - masterWidth
                              - juce::jmax(0, separatingGap));
    }

    /** Returns the pinned Master x position in viewed-component coordinates. */
    static int computePinnedMasterX(int scrollX,
                                    int viewportWidth,
                                    int masterWidth,
                                    int panelWidth) noexcept
    {
        if (masterWidth <= 0 || panelWidth <= 0)
            return juce::jmax(0, panelWidth - juce::jmax(0, masterWidth));

        const int desiredX = scrollX + juce::jmax(0, viewportWidth) - masterWidth;
        return juce::jlimit(0, juce::jmax(0, panelWidth - masterWidth), desiredX);
    }

    /**
     * Clips one normal strip to the safe side of the pinned Master.  The
     * returned width is expressed in the strip's own component coordinate
     * space; stripX is the component's actual x position.
     */
    static int computeNormalStripVisibleWidth(int stripX,
                                              int preferredWidth,
                                              int masterX,
                                              int separatingGap = kStripGap) noexcept
    {
        if (preferredWidth <= 0)
            return 0;

        const int safeRight = masterX - juce::jmax(0, separatingGap);
        return juce::jlimit(0, preferredWidth, safeRight - stripX);
    }

    /**
     * Computes the horizontal reorder auto-scroll delta from the safe normal
     * strip viewport.  The pinned Master is deliberately absent from this
     * coordinate contract: callers pass the safe right edge before Master.
     */
    static int computeReorderAutoScrollDelta(int cursorX,
                                             int safeLeft,
                                             int safeRight,
                                             int edge = 64) noexcept
    {
        const int effectiveEdge = juce::jmax(1, edge);
        int delta = 0;

        if (cursorX >= safeLeft && cursorX < safeLeft + effectiveEdge)
        {
            const float proximity = (float) (safeLeft + effectiveEdge - cursorX)
                                  / (float) effectiveEdge;
            delta = -juce::jmax(1, juce::roundToInt(24.0f * proximity * proximity));
        }
        else if (cursorX >= safeRight - effectiveEdge && cursorX < safeRight)
        {
            const float proximity = (float) (cursorX - (safeRight - effectiveEdge))
                                  / (float) effectiveEdge;
            delta = juce::jmax(1, juce::roundToInt(24.0f * proximity * proximity));
        }

        return delta;
    }

    /** Lay out normal strips while preserving their preferred sequence x. */
    void layoutNormalStripBounds(int masterX)
    {
        int x = 0;
        for (auto* strip : stripPtrs_)
        {
            if (strip == nullptr)
                continue;

            if (strip == masterStrip_)
                continue;

            const int depth = getTrackDepth(strip->getTrack());
            const bool isChildTrack = depth > 0 && !strip->getTrack().isMaster();
            const int childInsetX = isChildTrack ? juce::jmin(14, 8 + depth * 2) : 0;
            const int childTopInset = isChildTrack ? juce::jmin(14, 8 + depth * 2) : 0;
            const int childBottomLift = isChildTrack ? juce::jmin(18, 10 + depth * 3) : 0;
            const int stripW = strip->getPreferredWidth();
            const int fullW = juce::jmax(0, stripW - childInsetX - (isChildTrack ? 4 : 0));
            const int actualX = x + childInsetX;
            const int visibleW = masterStrip_ != nullptr
                ? computeNormalStripVisibleWidth(actualX, fullW, masterX)
                : fullW;

            strip->setBounds(actualX,
                             childTopInset,
                             visibleW,
                             juce::jmax(108, getHeight() - childTopInset - childBottomLift));
            x += stripW + kStripGap;
        }
    }

    // Debug fields (set by MainComponent for diagnostics)
    juce::String debugLastLauncher_;
    juce::String debugLastSendToggle_;

    /** Reposition the master strip to the right edge of the viewport,
     *  keeping it visible regardless of scroll position. */
    void updateMasterStripPosition()
    {
        if (!masterStrip_)
        {
            layoutNormalStripBounds(-1);
            return;
        }
        int masterW = masterStrip_->getPreferredWidth();
        int masterH = juce::jmax(108, getHeight());
        int panelW = getWidth();

        if (!viewport_)
        {
            const int masterX = juce::jmax(0, panelW - masterW);
            masterStrip_->setBounds(masterX, 0, masterW, masterH);
            layoutNormalStripBounds(masterX);
            masterStrip_->toFront(false);
            return;
        }

        auto vp = viewport_->getViewPosition();
        int vw = viewport_->getViewWidth();
        const int masterX = computePinnedMasterX(vp.x, vw, masterW, panelW);
        masterStrip_->setBounds(masterX, 0, masterW, masterH);
        layoutNormalStripBounds(masterX);
        masterStrip_->toFront(false);
    }

    // Callbacks wired by MainComponent
    std::function<void(const TrackID&)>                      onTrackSelected;
    std::function<void(const TrackID&, const juce::ModifierKeys&)> onTrackSelectedWithModifiers;
    std::function<bool(const TrackID&)>                       onGetIsMultiSelected;
    std::function<void(const TrackID&)>                      onTrackLensRequested;
    std::function<void(const TrackID&)>                      onOpenTrackPianoRoll;
    std::function<void(const TrackID&)>                      onOpenInputTrimPanel;
    std::function<void(int)>                                 onResizeDrag;
    std::function<void(int)>                                 onContentWidthChanged;
    std::function<void()>                                    onUndockRequested;
    std::function<void()>                                    onDockRequested;
    std::function<void(bool)>                                onToggleSidePanel;
    std::function<void(bool)>                                onTogglePluginBrowser;
    std::function<void()>                                    onCreateBus;
    std::function<void()>                                    onCreateFolderBus;
std::function<void(const TrackID&)>                      onConvertFolderToTrack;
    std::function<void(const TrackID&)>                      onDeleteTrack;
    std::function<void(const TrackID&)>                      onMultiDeleteTrack;
    std::function<void(const TrackID&, bool)>                onToggleTrackMute;
    std::function<void(const TrackID&)>                      onExclusiveTrackMute;
    /** Double-click on an empty part of a strip toggles Quick Send mode. */
    std::function<void(const TrackID&)>                      onStripQuickSendToggleRequested;
    /** POST/PRE button on a strip — toggle every send from that strip between
     *  post-fader and pre-fader. */
    std::function<void(const TrackID&)>                      onStripPreFaderToggled;
    /** Left-click on a strip during Quick Send mode toggles a send (create if missing, remove if exists). */
    std::function<void(const TrackID&)>                      onQuickSendToggleSendRequested;
    /** Right-click on a strip during Quick Send mode toggles a sidechain (create if missing, remove if exists). */
    std::function<void(const TrackID&)>                      onQuickSendToggleSidechainRequested;

    // ── Quick Send mode state & control ──────────────────────────────────
    void setQuickSendMode(bool active, const TrackID& sourceId)
    {
        if (quickSendModeActive_ == active && quickSendSourceId_ == sourceId)
            return;
        quickSendModeActive_ = active;
        quickSendSourceId_   = sourceId;
        for (auto* strip : stripPtrs_)
        {
            if (!strip) continue;
            const bool isSource = active && strip->getTrack().getID() == sourceId;
            strip->setQuickSendSource(isSource);
            strip->setQuickSendModeActive(active);
        }
    }
    bool isQuickSendModeActive() const noexcept { return quickSendModeActive_; }
    TrackID getQuickSendSourceId() const noexcept { return quickSendSourceId_; }

    /** Push the Quick Send receive-target highlight to the matching strip
     *  (cyan outline on tracks that are valid receive targets). */
    void setStripQuickSendReceiveTarget(const TrackID& trackId, bool on)
    {
        for (auto* strip : stripPtrs_)
        {
            if (strip == nullptr)
                continue;
            if (strip->getTrack().getID() == trackId)
            {
                strip->setQuickSendReceiveTarget(on);
                break;
            }
        }
    }

    bool   quickSendModeActive_ = false;
    TrackID quickSendSourceId_;

    std::function<void(const TrackID&, bool)>                onToggleTrackSolo;
    std::function<void(const TrackID&)>                      onToggleTrackArm;
    std::function<void(const TrackID&, bool)>                onSetTrackMonitoring;
    std::function<void(const TrackID&, TrackRole)>           onSetTrackRole;
    std::function<void(const TrackID&, float, float)>        onCommitTrackVolume; // (beforeGain, afterGain)
    std::function<void(const TrackID&, float, float)>        onCommitTrackPan;
    std::function<void(const TrackID&, const TrackID&)>      onCreateFolderFromDrop;
    std::function<void(const TrackID&, const TrackID&, DragMode)> onFolderDropRequested;
    std::function<void(const TrackID&, bool)>                onFolderCollapseChanged;
    std::function<void(const TrackID&, int)>                 onTrackReorderRequested;
    TrackReorderCore::GroupReorderRequestedCallback           onMultiTrackReorderRequested;
    std::function<std::vector<TrackID>()>                    onGetMultiSelectedTrackIds;
    std::function<void()>                                    onRescanPlugins;
    std::function<void(const TrackID&, int, const TrackID&)> onPluginDropCopy;
    std::function<void()>                                    onCableRepaintNeeded;
    std::function<void(bool)>                                onBubblegumToggled;

    MixerPanel(TrackManager& trackManager, FaderRangeCore& faderRange)
        : trackManager_(trackManager), faderRange_(faderRange)
    {
        setOpaque(true);   // solid background — skip DWM transparent composite path
        trackManager_.addListener(this);
        ApexPresentationClock::instance().addReceiver(this);
        addAndMakeVisible(masterUtilityPanel_);
        rebuildStrips();
        updatePresentationDemand();
    }

    ~MixerPanel() override
    {
        ApexPresentationClock::instance().removeReceiver(this);
        trackManager_.removeListener(this);
        for (auto& strip : strips_)
            if (strip) strip->detachFromTrack();
        for (auto& strip : retiredStrips_)
            if (strip) strip->detachFromTrack();
    }

    // Binding
    void bindBubblegumV2(BubblegumV2System* bgV2)
    {
        bgV2_ = bgV2;
        if (onCableRepaintNeeded) onCableRepaintNeeded();
    }
    void bindFolderBus(FolderBusCore* folderBus)
    {
        folderBus_ = folderBus;
        dragStateMachine_.setFolderBus(folderBus);
    }
void bindRouting(RoutingGraph* graph, void*)
    {
        routingGraph_ = graph;
        for (auto* strip : stripPtrs_)
            if (strip != nullptr)
                strip->bindRoutingGraph(routingGraph_);
    }

    /** Refresh every strip's POST/PRE button from the routing graph.
     *  `query` returns the pre/post-fader summary for that track
     *  (0 = no sends, 1 = all post, 2 = all pre, 3 = mixed). */
    void refreshPreFaderStates(const std::function<int(const TrackID&)>& query)
    {
        for (auto& strip : strips_)
        {
            if (strip == nullptr || strip->getTrack().isMaster())
                continue;
            strip->setPreFaderState(query(strip->getTrack().getID()));
        }
    }

    /** Bind master bus engines to the master strip. Non-owning pointers;
     *  all must outlive this panel. Safe to call before or after
     *  rebuildStrips() — propagates immediately if strip already exists. */
    void bindMasterEngines(MasterCeilingCore*    ceiling,
                           MasterDitherCore*     dither,
                           MeteringFacadeCore*   postMeter,
                           PhaseWidthFacadeCore* phaseWidth)
    {
        masterCeiling_    = ceiling;
        masterDither_     = dither;
        masterPostMeter_  = postMeter;
        masterPhaseWidth_ = phaseWidth;
        applyMasterEnginesToExistingStrip();
    }
    void setViewport(juce::Viewport* vp)            { viewport_ = vp; updateMasterStripPosition(); }
    void setDockedState(bool docked)                { docked_ = docked; }
    void setPaused(bool p)                          { paused_ = p; updatePresentationDemand(); }
    void setTransportActive(bool active)
    {
        if (transportActive_ == active)
            return;
        transportActive_ = active;
        updatePresentationDemand();
    }
    void setCollapsedFolderBuses(const std::unordered_set<TrackID>& collapsed)
    {
        if (collapsedFolderBuses_ == collapsed)
            return;

        collapsedFolderBuses_ = collapsed;
        rebuildStrips();
        if (onCableRepaintNeeded) onCableRepaintNeeded();
        repaint();
    }

    // Visual selection
    void    selectTrack(const TrackID& id)
    {
        selectedTrackId_ = id;
        selectedTrackIds_.clear();
        if (id.isNotEmpty())
            selectedTrackIds_.insert(id);
        updateStripSelectionState();
        if (onTrackSelected) onTrackSelected(id);
    }
    void    selectTrackVisual(const TrackID& id)
    {
        selectedTrackId_ = id;
        selectedTrackIds_.clear();
        if (id.isNotEmpty())
            selectedTrackIds_.insert(id);
        updateStripSelectionState();
    }
    void setTrackSelectionVisual(const std::vector<TrackID>& ids,
                                 const TrackID& primaryId)
    {
        selectedTrackIds_.clear();
        selectedTrackIds_.insert(ids.begin(), ids.end());
        selectedTrackId_ = primaryId;
        updateStripSelectionState();
    }
    void applyMultiSelectionVisual(const std::vector<TrackID>& ids)
    {
        selectedTrackIds_.clear();
        selectedTrackIds_.insert(ids.begin(), ids.end());
        if (selectedTrackId_.isEmpty()
            || selectedTrackIds_.count(selectedTrackId_) == 0)
            selectedTrackId_ = ids.empty() ? TrackID{} : ids.back();
        updateStripSelectionState();
    }
    TrackID getSelectedTrackId() const noexcept  { return selectedTrackId_; }

    // Bubblegum activation
    void activateBubblegumMode(const TrackID& sourceId)
    {
        if (bgV2_) bgV2_->open(sourceId);
        if (onBubblegumToggled) onBubblegumToggled(true);
        if (onCableRepaintNeeded) onCableRepaintNeeded();
        repaint();
    }
    void deactivateBubblegumMode()
    {
        if (bgV2_) bgV2_->close();
        if (onBubblegumToggled) onBubblegumToggled(false);
        if (onCableRepaintNeeded) onCableRepaintNeeded();
        repaint();
    }

    // Bubblegum state
    bool isBubblegumModeActive() const noexcept
    {
        return bgV2_ != nullptr && bgV2_->panel.isOpen();
    }
    bool isSidePanelOpen() const noexcept        { return sidePanelOpen_; }
    bool isBrowserOpen() const noexcept          { return browserOpen_; }
    bool areCablesVisible() const noexcept       { return isBubblegumModeActive() || cableForceVisible_; }
    bool isOffscreenForceVisible() const noexcept { return offscreenForceVisible_; }

    void toggleSidePanel()
    {
        sidePanelOpen_ = !sidePanelOpen_;
        if (onToggleSidePanel) onToggleSidePanel(sidePanelOpen_);
    }
    void toggleBrowser()
    {
        browserOpen_ = !browserOpen_;
        if (onTogglePluginBrowser) onTogglePluginBrowser(browserOpen_);
    }

    int getMasterStripWidth() const
    {
        if (masterStrip_ != nullptr)
            return masterStrip_->getPreferredWidth();
        return 0;
    }

    int getMasterStripRightEdge() const
    {
        if (masterStrip_ != nullptr)
            return masterStrip_->getBounds().getRight();
        return getWidth();
    }

    int getMasterStripCenterY() const
    {
        if (masterStrip_ != nullptr)
            return masterStrip_->getBounds().getCentreY();
        return getHeight() / 2;
    }

    void toggleCableForceVisible()  { setCableForceVisible(!cableForceVisible_); }
    void setCableForceVisible(bool v)
    {
        cableForceVisible_ = v;
        if (onCableRepaintNeeded) onCableRepaintNeeded();
        repaint();
    }

    int getDefaultVisibleContentWidth() const noexcept
    {
        return computeDefaultVisibleContentWidth(trackManager_.getNumTracks(),
                                                  getMasterStripWidth());
    }
    void toggleOffscreenForceVisible() { setOffscreenForceVisible(!offscreenForceVisible_); }
    void setOffscreenForceVisible(bool v)
    {
        offscreenForceVisible_ = v;
        if (onCableRepaintNeeded) onCableRepaintNeeded();
        repaint();
    }

    // Appearance setters (no-op stubs; stored for visual use if needed)
    void setBubblegumSelectedAccentColour(juce::Colour c)   { bgSelectedAccent_ = c;  repaint(); }
    void setBubblegumTargetAccentColour(juce::Colour c)     { bgTargetAccent_ = c;    repaint(); }
    void setBubblegumSelectedSphereColour(juce::Colour c)   { bgSphereColour_ = c;    repaint(); }
    void setBubblegumSelectedParticleColour(juce::Colour c) { bgParticleColour_ = c;  repaint(); }
    void setBubblegumSendPillDisplayMode(int mode)          { sendPillMode_ = mode;   repaint(); }
    void setMasterAccentColour(juce::Colour c)              { masterAccent_ = c;      repaint(); }
    static constexpr bool useLegacyMixerSkinMode() noexcept { return false; }

    void setAllTracksFullDepth(bool v)
    {
        allTracksFullDepth_ = useLegacyMixerSkinMode() ? false : v;
        repaint();
    }

    // Cable quality
    using QualityMode = bubblegum::BubblegumCableStyleSettingsCore::Style::QualityMode;
    QualityMode getCableQuality() const noexcept          { return cableQuality_; }
    QualityMode getSidechainCableQuality() const noexcept { return sidechainCableQuality_; }
    void setCableQuality(QualityMode q)                   { cableQuality_ = q; }

    /** Returns the smoothed paint() calls-per-second for the mixer panel itself. */
    double getMixerPaintFps() const noexcept { return mixerPaintRateMeter_.getFps(); }
    void setMixerBubbleColours(juce::Colour deep, juce::Colour near)
    {
        bubbleDeep_ = deep;
        bubbleNear_ = near;
        for (auto* strip : stripPtrs_)
            if (strip) strip->setBubbleColours(deep, near);
        repaint();
    }

    // Strip access
    const std::vector<MixerStrip*>& getStrips() const noexcept                { return stripPtrs_; }
    const FolderBusCore*            getFolderBusCore() const noexcept          { return folderBus_; }
    const std::unordered_set<TrackID>& getCollapsedFolderBuses() const noexcept { return collapsedFolderBuses_; }

// Navigation
    void scrollToTrack(const TrackID& id)
    {
        if (!viewport_) return;
        for (auto* strip : stripPtrs_)
        {
            if (strip && strip->getTrack().getID() == id)
            {
                // The Master is pinned to the viewport edge and is already
                // visible; scrolling for it would only move normal strips
                // underneath the fixed Master.
                if (strip->getTrack().isMaster())
                    return;

                const int viewW = viewport_->getViewWidth();
                const int safeW = computeSafeNormalViewportWidth(
                    viewW, getMasterStripWidth());
                const int curX = viewport_->getViewPositionX();
                auto* viewed = viewport_->getViewedComponent();
                const int maxX = viewed != nullptr
                    ? juce::jmax(0, viewed->getWidth() - viewW)
                    : 0;
                const int newX = computeRevealScrollX(strip->getBounds().getX(),
                                                       strip->getPreferredWidth(),
                                                       safeW, curX, maxX);
                if (newX != curX)
                    viewport_->setViewPosition(newX, 0);
                return;
            }
        }
    }

    // ── Pure viewport reveal math ────────────────────────────────────────
    // Given a strip's x position/width, the viewport width, the current
    // scroll X and the maximum legal scroll X, returns the scroll X that
    // reveals the strip with the MINIMUM delta. Returns curX unchanged when
    // the strip is already fully visible (no re-centering, no oscillation).
    // Static and side-effect free so keyboard-navigation reveal logic can be
    // unit-tested without a GUI.
    static int computeRevealScrollX(int stripX, int stripW, int viewW,
                                    int curX, int maxX) noexcept
    {
        if (stripX >= curX && stripX + stripW <= curX + viewW)
            return curX; // already fully visible → no scroll

        int newX = curX;
        if (stripX < curX)
            newX = stripX;                      // reveal left edge
        else if (stripX + stripW > curX + viewW)
            newX = stripX + stripW - viewW;     // reveal right edge
        return juce::jlimit(0, juce::jmax(0, maxX), newX);
    }

    void openSettingsMenu()
    {
        if (settingsMenuOpen_)
        {
            juce::PopupMenu::dismissAllActiveMenus();
            settingsMenuOpen_ = false;
            repaint();
            return;
        }

        juce::PopupMenu menu;

        constexpr int kToggleSidePanel = 1;
        constexpr int kToggleBrowser = 2;
        constexpr int kToggleCables = 3;
        constexpr int kToggleOffscreen = 4;
        constexpr int kRescanPlugins = 5;

        menu.addSectionHeader("Mixer");
        menu.addItem(kToggleSidePanel,
                     sidePanelOpen_ ? "Hide FX Chain" : "Show FX Chain",
                     true, sidePanelOpen_);
        menu.addItem(kToggleBrowser,
                     browserOpen_ ? "Hide Plugin Browser" : "Show Plugin Browser",
                     true, browserOpen_);
        menu.addSeparator();
        menu.addItem(kToggleCables,
                     "Always Show Bubblegum Cables",
                     true, cableForceVisible_);
        menu.addItem(kToggleOffscreen,
                     "Always Show Offscreen Endpoints",
                     true, offscreenForceVisible_);
        menu.addSeparator();
        menu.addItem(kRescanPlugins, "Rescan Plugins...");

        settingsMenuOpen_ = true;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [this](int result)
                           {
                               settingsMenuOpen_ = false;

                               switch (result)
                               {
                                   case 1: toggleSidePanel(); break;
                                   case 2: toggleBrowser(); break;
                                   case 3: toggleCableForceVisible(); break;
                                   case 4: toggleOffscreenForceVisible(); break;
                                   case 5:
                                       if (onRescanPlugins)
                                           onRescanPlugins();
                                       break;
                                   default:
                                       break;
                               }

                               repaint();
                           });
    }

    // Component overrides
    void paint(juce::Graphics& g) override
    {
        mixerPaintRateMeter_.tick();
        const auto bounds = getLocalBounds();

        if (useLegacyMixerSkinMode())
        {
            g.fillAll(Theme::getInstance().colors.backgroundDark);
            return;
        }

        // ── Static background cache ───────────────────────────────────────
        // APEX signal-core gradient is fully static; only re-bake on resize.
        auto& a = Theme::getInstance().apex;
        const int w = bounds.getWidth(), h = bounds.getHeight();
        if (!panelBgCache_.isValid() || panelBgCache_.getWidth() != w || panelBgCache_.getHeight() != h)
        {
            panelBgCache_ = juce::Image(juce::Image::RGB, juce::jmax(1, w), juce::jmax(1, h), false);
            juce::Graphics ig(panelBgCache_);
            const auto fb = panelBgCache_.getBounds().toFloat();

            // Base vertical gradient — deep space to dark ultraviolet.
            // The mixer identity is VIOLET-dominant; blue/cyan stays a
            // restrained signal accent (see the reduced cyan bloom below).
            {
                const auto ultravioletTop = a.color.panelB.interpolatedWith(a.color.violet, 0.10f);
                const auto ultravioletMid = a.color.panelA.interpolatedWith(a.color.violet, 0.06f);
                const auto ultravioletBase = a.color.deepestA.interpolatedWith(a.color.violet, 0.05f);
                juce::ColourGradient base(
                    ultravioletTop, fb.getCentreX(), fb.getY(),
                    ultravioletBase, fb.getCentreX(), fb.getBottom(), false);
                base.addColour(0.45, ultravioletMid);
                ig.setGradientFill(base);
                ig.fillRect(fb);
            }
            // Vapor light: cyan signal wash — broad and directional, NOT a
            // radial glow. Radial blooms read as stray "bloobs" near the cable
            // anchor zone on the violet base, so every vapor light here is a
            // soft directional wash instead.
            {
                juce::ColourGradient vl(
                    a.color.cyan.withAlpha(0.035f), fb.getX(), fb.getY(),
                    juce::Colours::transparentBlack, fb.getRight(), fb.getY() + fb.getHeight() * 0.30f, false);
                ig.setGradientFill(vl);
                ig.fillRect(fb.withHeight(fb.getHeight() * 0.30f));
            }
            // Vapor light: violet transformation wash, right-upper — primary
            // energy, directional (no circular glow).
            {
                juce::ColourGradient vl(
                    a.color.violet.withAlpha(0.12f), fb.getX() + fb.getWidth() * 0.55f, fb.getY(),
                    juce::Colours::transparentBlack, fb.getRight(), fb.getY() + fb.getHeight() * 0.55f, false);
                ig.setGradientFill(vl);
                ig.fillRect(fb.withHeight(fb.getHeight() * 0.55f));
            }
            // Vapor light: magenta creative wash, left-bottom (subtle accent,
            // directional — no circular glow).
            {
                juce::ColourGradient vl(
                    a.color.magenta.withAlpha(0.06f), fb.getX(), fb.getY() + fb.getHeight() * 0.60f,
                    juce::Colours::transparentBlack, fb.getX() + fb.getWidth() * 0.55f, fb.getBottom(), false);
                ig.setGradientFill(vl);
                ig.fillRect(fb.withTop(fb.getY() + fb.getHeight() * 0.60f));
            }
            // Floor shadow
            {
                juce::ColourGradient floor(
                    juce::Colours::transparentBlack,  fb.getCentreX(), fb.getBottom() - fb.getHeight() * 0.28f,
                    a.color.deepestB.withAlpha(0.55f), fb.getCentreX(), fb.getBottom(), false);
                ig.setGradientFill(floor);
                ig.fillRect(fb.withTop(fb.getBottom() - fb.getHeight() * 0.28f));
            }
            // Top magenta→violet signal hairline
            {
                juce::ColourGradient hl(
                    a.color.magenta.withAlpha(0.45f), fb.getX(),       fb.getY(),
                    a.color.violet.withAlpha(0.30f),  fb.getRight(),   fb.getY(), false);
                hl.addColour(0.70, a.color.pink.withAlpha(0.22f));
                ig.setGradientFill(hl);
                ig.fillRect(fb.withHeight(1.f));
            }
        }
        g.drawImageAt(panelBgCache_, 0, 0, false);

        paintFolderHierarchyBackplates(g);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        paintDragIntentOverlay(g);

        if (pluginDropHoverTrackId_.isNotEmpty())
            if (auto* target = findStripByTrackId(pluginDropHoverTrackId_))
            {
                auto bounds = target->getBounds().toFloat().reduced(1.0f);
                g.setColour(Theme::getInstance().apex.color.cyan.withAlpha(0.14f));
                g.fillRoundedRectangle(bounds, 6.0f);
                g.setColour(Theme::getInstance().apex.color.cyan.withAlpha(0.72f));
                g.drawRoundedRectangle(bounds, 6.0f, 2.0f);
            }
    }

    void resized() override
    {
        panelBgCache_ = {};  // invalidate on resize
        updateMasterStripPosition();
        layoutMasterUtilityPanel();
    }

    int clampTrackInsertionIndex(int insertionIndex) const
    {
        const int numTracks = trackManager_.getNumTracks();
        if (numTracks <= 0)
            return -1;

        // Reorder destinations are gaps in the normal-track order.  `numTracks`
        // is therefore the valid end gap; the pinned Master is never part of
        // this coordinate system.
        return juce::jlimit(0, numTracks, insertionIndex);
    }

    std::vector<TrackID> getProjectTrackOrderForReorder() const
    {
        std::vector<TrackID> order;
        order.reserve((size_t) trackManager_.getNumTracks());
        for (auto* track : trackManager_.getAllTracks())
            if (track != nullptr)
                order.push_back(track->getID());
        return order;
    }

    bool isTrackHiddenByCollapsedFolder(const Track& track) const
    {
        auto parentId = track.getParentTrackID();
        while (parentId.isNotEmpty())
        {
            if (collapsedFolderBuses_.count(parentId) > 0)
                return true;

            auto* parentTrack = trackManager_.getTrack(parentId);
            if (parentTrack == nullptr)
                break;

            parentId = parentTrack->getParentTrackID();
        }

        return false;
    }

    void paintFolderHierarchyBackplates(juce::Graphics& g)
    {
        if (folderBus_ == nullptr)
            return;

        for (auto* strip : stripPtrs_)
        {
            if (strip == nullptr)
                continue;

            const auto& parentTrack = strip->getTrack();
            const auto parentId = parentTrack.getID();
            if (parentTrack.getRole() != TrackRole::FolderBus)
                continue;
            if (collapsedFolderBuses_.count(parentId) > 0)
                continue;

            juce::Rectangle<float> groupBounds;
            bool haveVisibleChild = false;

            // Get ALL descendants including deeply nested ones
            for (const auto& childId : folderBus_->getAllDescendants(parentId))
            {
                auto* childStrip = findStripByTrackId(childId);
                if (childStrip == nullptr)
                    continue;

                // Include this child even if it's inside a nested folder
                auto childBounds = childStrip->getBounds().toFloat();
                if (childBounds.isEmpty())
                    continue;

                groupBounds = haveVisibleChild ? groupBounds.getUnion(childBounds) : childBounds;
                haveVisibleChild = true;
            }

            if (!haveVisibleChild)
                continue;

            const int folderDepth = juce::jmax(0, folderBus_->getDepth(parentId));
            const auto parentBounds = strip->getBounds().toFloat();

            // Elegant family colour — composed, not neon
            auto familyColour = parentTrack.getColor();
            if (folderDepth > 0)
                familyColour = familyColour.withRotatedHue(0.12f * (float) folderDepth)
                                           .withMultipliedSaturation(1.10f)
                                           .brighter(0.12f);

            // Group lane spans parent + all visible descendants, tight padding
            auto lane = groupBounds;
            lane.setX(parentBounds.getX());
            lane.setY(parentBounds.getY());
            lane.setBottom(juce::jmax(groupBounds.getBottom() + 4.0f, parentBounds.getBottom()));
            lane.setRight(groupBounds.getRight() + 4.0f);

            const float inset = juce::jmin(6.0f, 2.0f + (float) folderDepth * 2.0f);
            auto laneBody = lane.reduced(0.0f, 0.0f).withTrimmedLeft(inset * 0.5f);

            // ── 1. Quiet tinted platform — reads as "these live together" ──
            juce::ColourGradient platform(
                familyColour.withAlpha(0.070f), laneBody.getX(), laneBody.getY(),
                familyColour.withAlpha(0.025f), laneBody.getX(), laneBody.getBottom(), false);
            g.setGradientFill(platform);
            g.fillRoundedRectangle(laneBody, 9.0f);

            // ── 2. Hairline outline — one line, defines the family ─────────
            g.setColour(familyColour.withAlpha(0.28f));
            g.drawRoundedRectangle(laneBody.reduced(0.5f), 8.5f, 1.0f);

            // ── 3. Top connector rail — from parent cap across all children ─
            const float railY = lane.getY() + 19.0f;
            juce::ColourGradient railGrad(
                familyColour.withAlpha(0.75f), parentBounds.getRight() - 6.0f, railY,
                familyColour.withAlpha(0.20f), laneBody.getRight() - 4.0f, railY, false);
            g.setGradientFill(railGrad);
            g.fillRoundedRectangle(parentBounds.getRight() - 6.0f, railY - 0.8f,
                                   laneBody.getRight() - parentBounds.getRight() + 2.0f, 1.6f, 0.8f);

            // ── 4. End cap tick — closes the group at its right edge ───────
            g.setColour(familyColour.withAlpha(0.45f));
            g.fillRoundedRectangle(laneBody.getRight() - 2.4f, railY - 0.8f,
                                   1.6f, lane.getBottom() - railY - 8.0f, 0.8f);

            // ── 5. Soft floor shadow under the group — grounded, premium ───
            juce::ColourGradient floorGrad(
                familyColour.withAlpha(0.16f), laneBody.getX() + 8.0f, lane.getBottom() - 2.0f,
                juce::Colours::transparentBlack, laneBody.getRight() - 8.0f, lane.getBottom() - 2.0f, false);
            g.setGradientFill(floorGrad);
            g.fillRoundedRectangle(laneBody.getX() + 6.0f, lane.getBottom() - 3.0f,
                                   laneBody.getWidth() - 12.0f, 2.0f, 1.0f);
        }
    }

    int getTrackDepth(const Track& track) const
    {
        int depth = 0;
        auto parentId = track.getParentTrackID();
        while (parentId.isNotEmpty())
        {
            ++depth;
            auto* parentTrack = trackManager_.getTrack(parentId);
            if (parentTrack == nullptr)
                break;
            parentId = parentTrack->getParentTrackID();
        }

        return depth;
    }

private:
    TrackID resolvePluginDropTarget(juce::Point<int> position) const noexcept
    {
        for (auto* strip : stripPtrs_)
        {
            if (strip == nullptr || strip->getTrack().isMaster())
                continue;
            if (strip->getBounds().contains(position))
                return strip->getTrack().getID();
        }
        return {};
    }

    bool autoScrollDuringReorder()
    {
        const auto& drag = dragStateMachine_.getState();
        if (!drag.active || drag.mode != DragMode::Reorder || viewport_ == nullptr)
            return false;

        const auto visible = viewport_->getViewArea();
        const int viewW = viewport_->getViewWidth();
        const int safeW = computeSafeNormalViewportWidth(viewW, getMasterStripWidth());
        const int edge = 64;
        const int safeLeft = visible.getX();
        const int safeRight = safeLeft + safeW;
        const int cursorX = drag.cursorPos.x;
        const int currentX = viewport_->getViewPositionX();
        auto* viewed = viewport_->getViewedComponent();
        const int maxX = viewed != nullptr
            ? juce::jmax(0, viewed->getWidth() - viewW) : 0;

        const int delta = computeReorderAutoScrollDelta(cursorX, safeLeft, safeRight, edge);

        const int nextX = juce::jlimit(0, maxX, currentX + delta);
        if (delta == 0 || nextX == currentX)
            return false;

        viewport_->setViewPosition(nextX, viewport_->getViewPositionY());

        // The pointer is stationary in screen space.  Convert it into the
        // newly scrolled content coordinate before recomputing the insertion
        // intent, otherwise the marker lags one scroll tick behind the drag.
        auto updatedCursor = drag.cursorPos;
        updatedCursor.x += nextX - currentX;
        dragStateMachine_.update(updatedCursor, stripPtrs_, getProjectTrackOrderForReorder());
        return true;
    }

    /** Push the bound master engine pointers into the master strip if it
     *  already exists. Called both at bind time and at end of rebuildStrips(). */
    void applyMasterEnginesToExistingStrip()
    {
        masterUtilityPanel_.setMasterEngines(masterCeiling_,
                                             masterDither_,
                                             masterPostMeter_,
                                             masterPhaseWidth_);

        if (masterStrip_ != nullptr)
        {
            masterStrip_->setMasterEngines(masterCeiling_,
                                           masterDither_,
                                           masterPostMeter_,
                                           masterPhaseWidth_);
        }
    }

    // TrackManager::Listener overrides
    void trackAdded(Track*) override           { scheduleRebuild(); }
    void trackRemoved(const TrackID& removedId) override
    {
        // TrackManager destroys the Track immediately after this notification.
        // Remove the matching strip from every active traversal and detach its
        // listeners now, while the Track is still valid.  Keep the component
        // object alive until the current UI callback unwinds.
        for (auto it = strips_.begin(); it != strips_.end(); ++it)
        {
            auto* strip = it->get();
            if (strip == nullptr || strip->getTrack().getID() != removedId)
                continue;

            strip->detachFromTrack();
            removeChildComponent(strip);
            stripPtrs_.erase(std::remove(stripPtrs_.begin(), stripPtrs_.end(), strip),
                             stripPtrs_.end());
            if (masterStrip_ == strip)
                masterStrip_ = nullptr;

            retiredStrips_.push_back(std::move(*it));
            strips_.erase(it);
            scheduleRetiredStripCleanup();
            break;
        }

        scheduleRebuild();
    }
    void trackOrderChanged() override          { scheduleRebuild(); }

    void scheduleRetiredStripCleanup()
    {
        if (retiredStripCleanupPending_)
            return;

        retiredStripCleanupPending_ = true;
        juce::Component::SafePointer<MixerPanel> safePanel(this);
        juce::MessageManager::callAsync([safePanel]
        {
            if (safePanel == nullptr)
                return;

            safePanel->retiredStripCleanupPending_ = false;
            safePanel->retiredStrips_.clear();
        });
    }

    void scheduleRebuild()
    {
        if (rebuildPending_) return;
        rebuildPending_ = true;
        juce::Component::SafePointer<MixerPanel> safePanel(this);
        juce::MessageManager::callAsync([safePanel]
        {
            if (safePanel == nullptr)
                return;

            safePanel->rebuildPending_ = false;
            safePanel->rebuildStrips();
            if (safePanel->onCableRepaintNeeded)
                safePanel->onCableRepaintNeeded();
        });
    }

    void onPresentationTick(double deltaSeconds) override
    {
        if (!isShowing())
        {
            updatePresentationDemand();
            return;
        }

        const float dt = (float) juce::jlimit(0.0, 0.25, deltaSeconds);
        const auto visibleArea = viewport_ != nullptr ? viewport_->getViewArea()
                                                       : getLocalBounds();

        bool needsRepaint = false;

        if (bodyDragActive_ && autoScrollDuringReorder())
            needsRepaint = true;

        for (auto* strip : stripPtrs_)
        {
            if (!strip || !strip->isShowing()) continue;

            const bool inView = visibleArea.intersects(strip->getBounds());
            if (!inView)
                continue;

            if (transportActive_ || strip->needsMeterPresentationTick())
                strip->tickMeter();                   // updates meter smooth levels

            const bool lavaActive = strip->stepLava(dt);

            strip->presentationTick();

            if (strip->isFaderDragging())
                strip->repaintFaderRegion();

            // Only force a strip repaint if lava is animating and strip is visible.
            if (lavaActive)
            {
                strip->repaint();
                needsRepaint = true;
            }
        }

        masterUtilityPanel_.presentationTick();

        const auto& drag = dragStateMachine_.getState();
        if (drag.active && drag.draggedTrackId.isNotEmpty())
        {
            dragOverlayPhase_ += dt;
            if (dragOverlayPhase_ > juce::MathConstants<float>::twoPi * 64.0f)
                dragOverlayPhase_ -= juce::MathConstants<float>::twoPi * 64.0f;

            needsRepaint = true;
        }

        // Only repaint the mixer panel when something actually changed.
        // This avoids an expensive full-window repaint every 16 ms when
        // the mixer is idle (no meters moving, no lava, no drag).
        if (needsRepaint)
            repaint();

        updatePresentationDemand();
    }

    void visibilityChanged() override
    {
        updatePresentationDemand();
    }

    void updatePresentationDemand()
    {
        const bool visible = isShowing();
        bool needsPresentation = visible && !paused_
            && (transportActive_ || faderDragActive_ || bodyDragActive_
                || pluginDropActive_);

        if (!needsPresentation && visible && !paused_)
        {
            for (auto* strip : stripPtrs_)
            {
                if (strip == nullptr || !strip->isShowing())
                    continue;

                if (strip->needsMeterPresentationTick()
                    || strip->needsLavaPresentationTick())
                {
                    needsPresentation = true;
                    break;
                }
            }
        }

        if (!needsPresentation && masterUtilityPanel_.needsPresentationTick())
            needsPresentation = true;

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

    void paintDragIntentOverlay(juce::Graphics& g)
    {
        const auto& drag = dragStateMachine_.getState();
        if (!drag.active || drag.draggedTrackId.isEmpty())
            return;

        const auto pulseSlow = 0.5f + 0.5f * std::sin(dragOverlayPhase_ * 1.7f);
        const auto pulseFast = 0.5f + 0.5f * std::sin(dragOverlayPhase_ * 3.6f + 0.8f);

        if (isDetachFromFolderPreview(drag))
        {
            paintDetachFromFolderPreview(g, drag, pulseSlow, pulseFast);
            return;
        }

        if (drag.mode == DragMode::Reorder)
        {
            paintReorderPreview(g, drag, pulseSlow, pulseFast);
            return;
        }

        auto* targetStrip = findStripByTrackId(drag.targetTrackId);
        auto* draggedStrip = findStripByTrackId(drag.draggedTrackId);
        if (targetStrip == nullptr || draggedStrip == nullptr)
            return;

        const bool adoptIntoFolder = drag.mode == DragMode::AdoptIntoFolder;
        paintFolderDropPreview(g, *draggedStrip, *targetStrip, drag.cursorPos.x,
                               adoptIntoFolder, pulseSlow, pulseFast);
    }

    MixerStrip* findStripByTrackId(const TrackID& trackId) const noexcept
    {
        if (trackId.isEmpty())
            return nullptr;

        for (auto* strip : stripPtrs_)
            if (strip != nullptr && strip->getTrack().getID() == trackId)
                return strip;

        return nullptr;
    }

    bool isDetachFromFolderPreview(const DragState& drag) const
    {
        if (folderBus_ == nullptr)
            return false;

        if (!folderBus_->isChildOfAnyFolderBus(drag.draggedTrackId))
            return false;

        const auto& parentId = folderBus_->getParentFolderBus(drag.draggedTrackId);
        if (parentId.isEmpty() || collapsedFolderBuses_.count(parentId) > 0)
            return false;

        auto* parentTrack = trackManager_.getTrack(parentId);
        if (parentTrack == nullptr)
            return false;

        int parentIndex = trackManager_.getTrackIndex(parentId);
        if (parentIndex < 0)
            return false;

        int lastDescendantIndex = parentIndex;
        for (const auto& descendantId : folderBus_->getAllDescendants(parentId))
            lastDescendantIndex = juce::jmax(lastDescendantIndex, trackManager_.getTrackIndex(descendantId));

        const int insertionIndex = juce::jlimit(0, trackManager_.getNumTracks(), drag.insertionIndex);
        return insertionIndex <= parentIndex || insertionIndex > lastDescendantIndex + 1;
    }

    void paintReorderPreview(juce::Graphics& g,
                             const DragState& drag,
                             float pulseSlow,
                             float pulseFast)
    {
        auto* draggedStrip = findStripByTrackId(drag.draggedTrackId);
        if (draggedStrip == nullptr)
            return;

        const auto draggedBounds = draggedStrip->getBounds().toFloat();
        const float x = (float)juce::jlimit(0, getWidth(), drag.cursorPos.x);
        const float top = draggedBounds.getY() + 8.0f;
        const float bottom = draggedBounds.getBottom() - 12.0f;
        const float glowW = 10.0f + pulseSlow * 6.0f;

        g.setColour(juce::Colours::white.withAlpha(0.06f + pulseSlow * 0.05f));
        g.fillRoundedRectangle(x - glowW * 0.5f, top, glowW, bottom - top, glowW * 0.5f);

        g.setColour(juce::Colour(0xFFF4E8D8).withAlpha(0.28f + pulseFast * 0.14f));
        g.fillRoundedRectangle(x - 2.0f, top, 4.0f, bottom - top, 2.0f);

        g.setColour(juce::Colours::white.withAlpha(0.70f));
        g.fillRoundedRectangle(x - 1.0f, top + 10.0f, 2.0f, bottom - top - 20.0f, 1.0f);

        const float y = draggedBounds.getBottom() - 22.0f;
        g.setColour(juce::Colour(0xFFE7D3BC).withAlpha(0.24f + pulseSlow * 0.10f));
        g.drawLine(x - 20.0f, y, x + 20.0f, y, 1.4f);
        g.drawLine(x - 14.0f, y - 6.0f, x, y, 1.1f);
        g.drawLine(x + 14.0f, y - 6.0f, x, y, 1.1f);
    }

    void paintDetachFromFolderPreview(juce::Graphics& g,
                                      const DragState& drag,
                                      float pulseSlow,
                                      float pulseFast)
    {
        auto* draggedStrip = findStripByTrackId(drag.draggedTrackId);
        if (draggedStrip == nullptr || folderBus_ == nullptr)
            return;

        const auto draggedBounds = draggedStrip->getBounds().toFloat();
        const auto& parentId = folderBus_->getParentFolderBus(drag.draggedTrackId);
        auto* parentStrip = findStripByTrackId(parentId);

        if (parentStrip != nullptr)
        {
            auto parentBounds = parentStrip->getBounds().toFloat().reduced(4.0f, 6.0f);
            g.setColour(juce::Colour(0xFFE7C7A2).withAlpha(0.16f + pulseSlow * 0.06f));
            g.drawRoundedRectangle(parentBounds, 14.0f, 1.8f);

            const float exitY = parentBounds.getBottom() - 12.0f;
            g.setColour(juce::Colour(0xFFFFF1DE).withAlpha(0.26f + pulseFast * 0.10f));
            g.drawLine(parentBounds.getX() + 10.0f, exitY,
                       parentBounds.getRight() - 10.0f, exitY, 1.4f);
        }

        const float x = (float) juce::jlimit(0, getWidth(), drag.cursorPos.x);
        const float top = draggedBounds.getY() + 10.0f;
        const float bottom = draggedBounds.getBottom() - 18.0f;

        g.setColour(juce::Colour(0xFFFFE9D1).withAlpha(0.08f + pulseSlow * 0.05f));
        g.fillRoundedRectangle(x - 7.0f, top, 14.0f, bottom - top, 6.0f);

        g.setColour(juce::Colour(0xFFE7B67A).withAlpha(0.34f + pulseFast * 0.12f));
        g.fillRoundedRectangle(x - 1.6f, top + 8.0f, 3.2f, bottom - top - 16.0f, 1.4f);

        const float arrowY = draggedBounds.getBottom() - 26.0f;
        g.setColour(juce::Colours::white.withAlpha(0.72f));
        g.drawLine(x - 18.0f, arrowY, x + 18.0f, arrowY, 1.4f);
        g.drawLine(x + 18.0f, arrowY, x + 10.0f, arrowY - 6.0f, 1.2f);
        g.drawLine(x + 18.0f, arrowY, x + 10.0f, arrowY + 6.0f, 1.2f);
    }

    void paintFolderDropPreview(juce::Graphics& g,
                                const MixerStrip& draggedStrip,
                                const MixerStrip& targetStrip,
                                int cursorX,
                                bool adoptIntoFolder,
                                float pulseSlow,
                                float pulseFast)
    {
        auto target = targetStrip.getBounds().toFloat().reduced(3.0f, 4.0f);
        auto dragged = draggedStrip.getBounds().toFloat().reduced(8.0f, 16.0f);
        const auto& folderColours = Theme::getInstance().apex.color;
        const auto folderBrown = folderColours.folderTimber;
        const auto folderBrownBright = folderColours.folderTimberBright;

        const float expand = adoptIntoFolder ? (4.0f + pulseSlow * 2.0f)
                                             : (6.0f + pulseSlow * 3.0f);
        auto halo = target.expanded(expand, adoptIntoFolder ? 8.0f : 10.0f);

        auto haloColour = folderBrownBright.withAlpha((adoptIntoFolder ? 0.10f : 0.12f)
                                                       + pulseSlow * (adoptIntoFolder ? 0.05f : 0.06f));

        juce::ColourGradient haloGrad(
            haloColour, halo.getCentreX(), halo.getY(),
            juce::Colours::transparentBlack, halo.getCentreX(), halo.getBottom(), false);
        g.setGradientFill(haloGrad);
        g.fillRoundedRectangle(halo, 16.0f);

        juce::Colour rimA = folderBrownBright.brighter(0.42f)
            .withAlpha((adoptIntoFolder ? 0.42f : 0.46f) + pulseFast * (adoptIntoFolder ? 0.16f : 0.18f));
        juce::Colour rimB = folderBrown
            .withAlpha((adoptIntoFolder ? 0.26f : 0.30f) + pulseSlow * 0.10f);

        juce::ColourGradient rim(rimA, halo.getCentreX(), halo.getY(), rimB, halo.getCentreX(), halo.getBottom(), false);
        g.setGradientFill(rim);
        g.drawRoundedRectangle(target.expanded(2.0f, 3.0f), 14.0f, adoptIntoFolder ? 2.0f : 2.4f);

        g.setColour(juce::Colours::white.withAlpha(adoptIntoFolder ? 0.16f : 0.20f));
        g.drawRoundedRectangle(target.reduced(1.0f), 12.0f, 0.9f);

        const auto from = juce::Point<float>(dragged.getCentreX(), dragged.getBottom() - 8.0f);
        const auto to   = juce::Point<float>(target.getCentreX(), target.getY() + (adoptIntoFolder ? target.getHeight() * 0.38f
                                                                                                    : target.getHeight() * 0.50f));
        const float lift = juce::jmax(18.0f, std::abs(to.x - from.x) * 0.18f + 16.0f);

        juce::Path stream;
        stream.startNewSubPath(from);
        stream.cubicTo(from.x, from.y + lift,
                       to.x,   to.y - lift * (adoptIntoFolder ? 0.65f : 0.52f),
                       to.x,   to.y);

        g.setColour(folderBrownBright.brighter(0.38f).withAlpha(0.10f + pulseSlow * 0.04f));
        g.strokePath(stream, juce::PathStrokeType(8.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(folderBrownBright.withAlpha(0.20f + pulseFast * 0.08f));
        g.strokePath(stream, juce::PathStrokeType(3.4f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(juce::Colours::white.withAlpha(0.34f + pulseFast * 0.10f));
        g.strokePath(stream, juce::PathStrokeType(1.1f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float beadR = adoptIntoFolder ? (6.0f + pulseFast * 1.4f)
                                            : (7.0f + pulseFast * 1.8f);
        g.setColour(folderBrownBright.brighter(0.45f).withAlpha(0.28f + pulseFast * 0.10f));
        g.fillEllipse(to.x - beadR * 1.5f, to.y - beadR * 1.5f, beadR * 3.0f, beadR * 3.0f);
        g.setColour(folderBrownBright.withAlpha(0.44f + pulseFast * 0.12f));
        g.fillEllipse(to.x - beadR, to.y - beadR, beadR * 2.0f, beadR * 2.0f);

        if (adoptIntoFolder)
        {
            // Open-folder + inward arrow icon communicates the release action
            // independently of colour and animation.
            const float iconX = target.getCentreX() - 10.0f;
            const float iconY = target.getY() + 18.0f;
            juce::Path folderIcon;
            folderIcon.addRoundedRectangle(iconX, iconY + 4.0f, 20.0f, 12.0f, 2.5f);
            folderIcon.addRoundedRectangle(iconX, iconY, 9.0f, 7.0f, 2.0f);
            g.setColour(folderBrownBright.brighter(0.45f).withAlpha(0.72f + pulseFast * 0.16f));
            g.strokePath(folderIcon, juce::PathStrokeType(1.7f));
            g.drawLine(iconX - 8.0f, iconY + 10.0f, iconX + 4.0f, iconY + 10.0f, 1.8f);
            g.drawLine(iconX + 4.0f, iconY + 10.0f, iconX, iconY + 6.0f, 1.5f);
            g.drawLine(iconX + 4.0f, iconY + 10.0f, iconX, iconY + 14.0f, 1.5f);

            const float laneY = target.getBottom() - 14.0f;
            g.setColour(folderBrownBright.withAlpha(0.26f + pulseSlow * 0.10f));
            g.drawLine(target.getX() + 10.0f, laneY,
                       target.getRight() - 10.0f, laneY, 1.6f);

            const float minSlotX = target.getX() + 14.0f;
            const float maxSlotX = target.getRight() - 14.0f;
            const float cursorXf = (float) cursorX;
            const float slotX = juce::jmax(minSlotX, juce::jmin(maxSlotX, cursorXf));
            g.setColour(juce::Colours::white.withAlpha(0.74f));
            g.fillRoundedRectangle(slotX - 1.4f, laneY - 6.0f, 2.8f, 12.0f, 1.2f);
        }
        else
        {
            const float iconX = target.getCentreX() - 10.0f;
            const float iconY = target.getY() + 18.0f;
            juce::Path folderIcon;
            folderIcon.addRoundedRectangle(iconX, iconY + 4.0f, 20.0f, 12.0f, 2.5f);
            folderIcon.addRoundedRectangle(iconX, iconY, 9.0f, 7.0f, 2.0f);
            g.setColour(folderBrownBright.brighter(0.45f).withAlpha(0.72f + pulseFast * 0.16f));
            g.strokePath(folderIcon, juce::PathStrokeType(1.7f));
            g.drawLine(iconX + 10.0f, iconY + 6.0f, iconX + 10.0f, iconY + 14.0f, 1.5f);
            g.drawLine(iconX + 6.0f, iconY + 10.0f, iconX + 14.0f, iconY + 10.0f, 1.5f);

            auto splitBand = target.withTrimmedTop(target.getHeight() * 0.48f)
                                   .withHeight(2.0f)
                                   .reduced(10.0f, 0.0f);
            g.setColour(folderBrownBright.brighter(0.42f).withAlpha(0.42f + pulseFast * 0.12f));
            g.fillRoundedRectangle(splitBand, 1.0f);
        }
    }

    void updateStripSelectionState()
    {
        for (auto* strip : stripPtrs_)
        {
            if (!strip) continue;
            const bool sel = selectedTrackIds_.count(strip->getTrack().getID()) > 0;
            strip->setSelected(sel);
        }
    }

    void rebuildStrips()
    {
        masterStrip_ = nullptr;
        for (auto& s : strips_)
            if (s) removeChildComponent(s.get());
        strips_.clear();
        stripPtrs_.clear();

        const int n = trackManager_.getNumTracks();
        strips_.reserve((size_t)n);
        stripPtrs_.reserve((size_t)n);

        for (int i = 0; i < n; ++i)
        {
            auto* track = trackManager_.getTrack(i);
            if (!track) continue;
            if (isTrackHiddenByCollapsedFolder(*track)) continue;
            auto strip = std::make_unique<MixerStrip>(*track, faderRange_);
            strip->setSelected(selectedTrackIds_.count(track->getID()) > 0);
            strip->setAccentColour(track->getColor());
            auto parentColour = juce::Colours::transparentBlack;
            if (folderBus_ != nullptr)
            {
                const auto parentId = folderBus_->getParentFolderBus(track->getID());
                if (parentId.isNotEmpty())
                    if (auto* parentTrack = trackManager_.getTrack(parentId))
                        parentColour = parentTrack->getColor();
            }

            strip->setFolderVisualState(track->getRole() == TrackRole::FolderBus,
                                        collapsedFolderBuses_.count(track->getID()) == 0,
                                        getTrackDepth(*track),
                                        1.0f,
                                        -1.0f,
                                        parentColour);

            // Wire strip callbacks back to MixerPanel callbacks
            strip->onMuteToggled = [this, track](bool muted) {
                if (onToggleTrackMute) onToggleTrackMute(track->getID(), muted);
                else track->setMuted(muted);
            };
            strip->onExclusiveMuteRequested = [this, track] {
                if (onExclusiveTrackMute) onExclusiveTrackMute(track->getID());
            };
            strip->onConvertFolderToTrack = [this, track] {
                if (onConvertFolderToTrack) onConvertFolderToTrack(track->getID());
            };
             strip->onDeleteRequested = [this, track] {
                 if (onDeleteTrack) onDeleteTrack(track->getID());
             };
             strip->onMultiDeleteRequested = [this, track] {
                 if (onMultiDeleteTrack) onMultiDeleteTrack(track->getID());
             };
            strip->onSoloToggled = [this, track](bool soloed) {
                if (onToggleTrackSolo) onToggleTrackSolo(track->getID(), soloed);
                else track->setSoloed(soloed);
            };
            strip->onArmToggled = [this, track](bool armed) {
                juce::ignoreUnused(armed);
                if (onToggleTrackArm) onToggleTrackArm(track->getID());
                else track->setArmed(!track->isArmed());
            };
            strip->onClicked = [this, track] {
                selectTrack(track->getID());
            };
            strip->onSelectedWithModifiers = [this](const TrackID& id,
                                                     const juce::ModifierKeys& mods)
            {
                if (onTrackSelectedWithModifiers)
                    onTrackSelectedWithModifiers(id, mods);
                else if (onTrackSelected)
                    onTrackSelected(id);
            };
            strip->onQueryIsMultiSelected = [this](const TrackID& id) -> bool
            {
                return onGetIsMultiSelected ? onGetIsMultiSelected(id) : false;
            };
            strip->onPianoRollRequested = [this, track] {
                selectTrack(track->getID());
                if (onOpenTrackPianoRoll) onOpenTrackPianoRoll(track->getID());
            };
strip->onInputPanelRequested = [this, track] {
                selectTrack(track->getID());
                if (onOpenInputTrimPanel) onOpenInputTrimPanel(track->getID());
                else InputTrimFloatingPanel::showForTrack(*track, getTopLevelComponent());
            };
strip->onQuickSendToggleRequested = [this, track](const TrackID&)
             {
                 if (onStripQuickSendToggleRequested) onStripQuickSendToggleRequested(track->getID());
             };
             strip->onQuickSendToggleSendRequested = [this](const TrackID& targetId)
             {
                 if (onQuickSendToggleSendRequested) onQuickSendToggleSendRequested(targetId);
             };
             strip->onQuickSendToggleSidechainRequested = [this](const TrackID& targetId)
             {
                 if (onQuickSendToggleSidechainRequested) onQuickSendToggleSidechainRequested(targetId);
             };
            strip->onPreFaderToggleRequested = [this](const TrackID& id)
            {
                if (onStripPreFaderToggled) onStripPreFaderToggled(id);
            };

            strip->onTrackLensRequested = [this, track](const TrackID&)
            {
                selectTrack(track->getID());
                if (onTrackLensRequested) onTrackLensRequested(track->getID());
            };
            strip->onFolderToggleRequested = [this](const TrackID& trackId, bool expanded)
            {
                // Guard against any handler destroying this MixerPanel.
                juce::Component::SafePointer<MixerPanel> safePanel(this);

                // Update local collapse state immediately, but DEFER the strip
                // rebuild to the next message-loop turn. A synchronous rebuild
                // here destroys the very strip that is dispatching this
                // callback, and JUCE's mouse source can then hit freed memory
                // (crash reproduced with an empty FolderBus chevron click).
                if (expanded)
                    collapsedFolderBuses_.erase(trackId);
                else
                    collapsedFolderBuses_.insert(trackId);
                scheduleRebuild();
                if (safePanel != nullptr && safePanel->onCableRepaintNeeded)
                    safePanel->onCableRepaintNeeded();
                if (safePanel != nullptr && safePanel->onFolderCollapseChanged)
                    safePanel->onFolderCollapseChanged(trackId, expanded);

                if (safePanel != nullptr)
                    safePanel->repaint();
            };
            strip->onBodyDragBegin = [this](const TrackID& trackId, juce::Point<int> pos)
            {
                bodyDragActive_ = true;
                reorderSelectionAtDragStart_.clear();
                if (onGetIsMultiSelected != nullptr
                    && onGetIsMultiSelected(trackId)
                    && onGetMultiSelectedTrackIds != nullptr)
                    reorderSelectionAtDragStart_ = onGetMultiSelectedTrackIds();
                dragStateMachine_.begin(trackId, pos);
                updatePresentationDemand();
            };
            strip->onBodyDragMove = [this](juce::Point<int> pos)
            {
                if (dragStateMachine_.update(pos, stripPtrs_, getProjectTrackOrderForReorder()))
                    repaint();
                updatePresentationDemand();
            };
            strip->onBodyDragEnd = [this](juce::Point<int> pos)
            {
                // Guard against MixerPanel being destroyed during topology-mutating callbacks
                // (folder creation/reorder triggers TrackManager::Listener ? rebuildStrips)
                juce::Component::SafePointer<MixerPanel> safePanel(this);

                const auto projectOrder = getProjectTrackOrderForReorder();
                dragStateMachine_.update(pos, stripPtrs_, projectOrder);
                auto state = dragStateMachine_.getState();
                auto mode = dragStateMachine_.commit(pos, stripPtrs_, projectOrder);
                bodyDragActive_ = false;
                updatePresentationDemand();

                // Copy state to locals before invoking callbacks that may destroy this panel
                const auto draggedId = state.draggedTrackId;
                const auto targetId = state.targetTrackId;
                const int insertionIndex = state.insertionIndex;

                if (mode == DragMode::Reorder)
                {
                    auto targetIndex = clampTrackInsertionIndex(insertionIndex);
                    auto selectedAtDragStart = reorderSelectionAtDragStart_;
                    reorderSelectionAtDragStart_.clear();

                    const bool draggedWasSelected = onGetIsMultiSelected
                        && onGetIsMultiSelected(draggedId);
                    if (targetIndex >= 0 && draggedWasSelected
                        && selectedAtDragStart.size() > 1
                        && onMultiTrackReorderRequested)
                    {
                        onMultiTrackReorderRequested(selectedAtDragStart, targetIndex);
                    }
                    else if (targetIndex >= 0 && onTrackReorderRequested)
                    {
                        onTrackReorderRequested(draggedId, targetIndex);
                    }
                }
                else if ((mode == DragMode::CreateFolder || mode == DragMode::AdoptIntoFolder)
                         && draggedId.isNotEmpty()
                         && targetId.isNotEmpty())
                {
                    if (onFolderDropRequested)
                        onFolderDropRequested(draggedId, targetId, mode);
                    else if (onCreateFolderFromDrop)
                        onCreateFolderFromDrop(draggedId, targetId);
                }

                // Only repaint if panel still exists after topology mutation
                if (safePanel != nullptr)
                    safePanel->repaint();
            };
            strip->onVolumeChanged = [this, track](float beforeGain, float afterGain) {
                if (onCommitTrackVolume) onCommitTrackVolume(track->getID(), beforeGain, afterGain);
            };
            strip->onPanChanged = [this, track](float oldPan, float newPan) {
                if (onCommitTrackPan) onCommitTrackPan(track->getID(), oldPan, newPan);
            };
            strip->onFaderDragStateChanged = [this](bool dragging)
            {
                faderDragActive_ = dragging;
                updatePresentationDemand();
            };
            strip->bindRoutingGraph(routingGraph_);

            // Wire routing ring color queries so selected-strip bubbles reflect cable colors
            strip->onQueryHasSend = [this](const TrackID& id) -> bool {
                if (!routingGraph_) return false;
                for (auto* c : routingGraph_->getOutputConnections(id))
                    if (c->type == ConnectionType::Send || c->type == ConnectionType::PreSend)
                        return true;
                return false;
            };
            strip->onQueryHasSidechain = [this](const TrackID& id) -> bool {
                if (!routingGraph_) return false;
                for (auto* c : routingGraph_->getAllConnections())
                    if (c->sourceNodeId == id && c->type == ConnectionType::Sidechain)
                        return true;
                return false;
            };
            strip->onQuerySendColour = [this](const TrackID&) -> juce::Colour {
                // Use the live Bubblegum cable accent color
                return bgV2_ ? juce::Colour(0xFFEE4FA0) // fallback: cable kBody
                             : BubblegumAppearanceSettings::getDefaultCableAccent();
            };
            strip->onQuerySidechainColour = [](const TrackID&) -> juce::Colour {
                return juce::Colour(0xFFE7C98D); // sidechain colourBright
            };

            addAndMakeVisible(strip.get());
            stripPtrs_.push_back(strip.get());
            strips_.push_back(std::move(strip));
        }

        // ── Master track strip — always rightmost, always present ──────────
        if (trackManager_.hasMasterTrack())
        {
            if (auto* master = trackManager_.getMasterTrack())
            {
                auto strip = std::make_unique<MixerStrip>(*master, faderRange_);
                strip->setSelected(selectedTrackIds_.count(master->getID()) > 0);
                strip->setAccentColour(juce::Colour(0xFF222222)); // matte black — no gold
                strip->activateLava();
                // Matte penthouse: near-black bubble body, no gold anywhere
                strip->setBubbleColours(juce::Colour(0xFF080808), juce::Colour(0xFF1C1C1C));
                strip->setRestBubbleColours(juce::Colour(0xFF080808), juce::Colour(0xFF1C1C1C));
                strip->setSphereColour(juce::Colour(0xFF1A1A1A));
                strip->setParticleColour(juce::Colour(0xFF1C1C1C));
                strip->setSpheresPainted(true); // always animated on master, not tied to selection
                strip->setAlwaysVisible(true);  // pin alpha=1 regardless of selection state

                strip->onClicked = [this, master] { selectTrack(master->getID()); };
                strip->onPianoRollRequested = {};
                strip->onBodyDragBegin = {};
                strip->onBodyDragMove = {};
                strip->onBodyDragEnd = {};
                strip->onMuteToggled = [this, master](bool muted) {
                    if (onToggleTrackMute) onToggleTrackMute(master->getID(), muted);
                    else master->setMuted(muted);
                };
                strip->onSoloToggled = [this, master](bool soloed) {
                    if (onToggleTrackSolo) onToggleTrackSolo(master->getID(), soloed);
                    else master->setSoloed(soloed);
                };
                strip->onVolumeChanged = [this, master](float beforeGain, float afterGain) {
                    if (onCommitTrackVolume) onCommitTrackVolume(master->getID(), beforeGain, afterGain);
                };
                strip->onPanChanged = [this, master](float oldPan, float newPan) {
                    if (onCommitTrackPan) onCommitTrackPan(master->getID(), oldPan, newPan);
                };
                strip->onFaderDragStateChanged = [this](bool dragging)
                {
                    faderDragActive_ = dragging;
                    updatePresentationDemand();
                };
                 strip->onMasterUtilityRequested = [this](MixerStrip& masterStrip)
                 {
                     if (!MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled)
                     {
                         masterUtilityOpen_ = false;
                         masterUtilityPanel_.setVisible(false);
                         masterUtilityPanel_.setBounds({});
                         return;
                     }

                     masterUtilityOpen_ = !masterUtilityOpen_;
                    masterUtilityPanel_.setVisible(masterUtilityOpen_);
                    layoutMasterUtilityPanel(&masterStrip);
                    masterUtilityPanel_.toFront(false);
                    repaint();
                };
                // Quick Send mode: the master strip is a receive target like
                // any other strip — left-click toggles the master send
                // (create/remove), right-click toggles the sidechain. Mirrors
                // the per-track strip wiring in the loop above.
                strip->onQuickSendToggleSendRequested = [this](const TrackID& targetId)
                {
                    if (onQuickSendToggleSendRequested) onQuickSendToggleSendRequested(targetId);
                };
                strip->onQuickSendToggleSidechainRequested = [this](const TrackID& targetId)
                {
                    if (onQuickSendToggleSidechainRequested) onQuickSendToggleSidechainRequested(targetId);
                };

                addAndMakeVisible(strip.get());
                masterStrip_ = strip.get();
                stripPtrs_.push_back(strip.get());
                strips_.push_back(std::move(strip));
            }
        }

        int totalW = 0;
        for (auto* strip : stripPtrs_)
            if (strip != nullptr)
                totalW += strip->getPreferredWidth() + kStripGap;
        totalW -= kStripGap; // remove gap after last strip
        if (totalW != getWidth())
        {
            setSize(totalW, getHeight());
            if (onContentWidthChanged) onContentWidthChanged(totalW);
        }
        else
        {
            resized();
        }

        // Wire master engines if already bound (no-op if not yet bound)
        applyMasterEnginesToExistingStrip();
        updatePresentationDemand();
    }

    void layoutMasterUtilityPanel(MixerStrip* explicitMasterStrip = nullptr)
    {
        if (!MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled)
        {
            masterUtilityOpen_ = false;
            masterUtilityPanel_.setVisible(false);
            masterUtilityPanel_.setBounds({});
            return;
        }

        MixerStrip* masterStrip = explicitMasterStrip != nullptr ? explicitMasterStrip : masterStrip_;

        if (masterStrip == nullptr || !masterUtilityOpen_)
        {
            masterUtilityPanel_.setVisible(false);
            masterUtilityPanel_.setBounds({});
            return;
        }

        const int panelW = 174;
        const int gap = 4;
        auto masterBounds = masterStrip->getBounds();
        const bool placeLeft = masterBounds.getCentreX() > getWidth() / 2;
        int panelX = placeLeft ? masterBounds.getX() - gap - panelW
                               : masterBounds.getRight() + gap;
        panelX = juce::jlimit(0, juce::jmax(0, getWidth() - panelW), panelX);
        masterUtilityPanel_.setBounds(panelX,
                                      masterBounds.getY(),
                                      panelW,
                                      juce::jmin(masterBounds.getHeight(), 330));
        masterUtilityPanel_.setVisible(true);
        masterUtilityPanel_.toFront(false);
    }

    TrackManager&   trackManager_;
    FaderRangeCore& faderRange_;

    BubblegumV2System* bgV2_         = nullptr;
    FolderBusCore*     folderBus_    = nullptr;
    RoutingGraph*      routingGraph_ = nullptr;
    juce::Viewport*    viewport_     = nullptr;

    // Master engine pointers (non-owning, set via bindMasterEngines)
    MasterCeilingCore*    masterCeiling_    = nullptr;
    MasterDitherCore*     masterDither_     = nullptr;
    MeteringFacadeCore*   masterPostMeter_  = nullptr;
    PhaseWidthFacadeCore* masterPhaseWidth_ = nullptr;

    TrackID selectedTrackId_;
    std::unordered_set<TrackID> selectedTrackIds_;
    bool    docked_                = false;
    bool    paused_                = false;
    bool    sidePanelOpen_         = false;
    bool    browserOpen_           = false;
    bool    settingsMenuOpen_      = false;
    bool    masterUtilityOpen_     = false;
    bool    cableForceVisible_     = true;
    bool    offscreenForceVisible_ = true;
    bool    allTracksFullDepth_    = false;
    bool    transportActive_       = false;
    bool    faderDragActive_       = false;
    bool    bodyDragActive_        = false;
    bool    pluginDropActive_      = false;
    bool    presentationUpdateActive_ = false;
    std::vector<TrackID> reorderSelectionAtDragStart_;
    int     sendPillMode_          = 0;

    juce::Colour bgSelectedAccent_  = juce::Colour(0xFFFF2D78);
    juce::Colour bgTargetAccent_    = juce::Colour(0xFF8B5CF6);
    juce::Colour bgSphereColour_    = juce::Colour(0xFF1A1A1A);
    juce::Colour bgParticleColour_  = juce::Colour(0xFFFF2D78);
    juce::Colour masterAccent_      = juce::Colour(0xFFCC9900);
    juce::Colour bubbleDeep_        = juce::Colour(0xFF3A4455);
    juce::Colour bubbleNear_        = juce::Colour(0xFF6A7A90);

    QualityMode cableQuality_          = QualityMode::Performance;
    QualityMode sidechainCableQuality_ = QualityMode::Performance;
    float       dragOverlayPhase_      = 0.0f;

    MixerStrip*                              masterStrip_     = nullptr;
    std::unordered_set<TrackID>              collapsedFolderBuses_;
    std::vector<std::unique_ptr<MixerStrip>> strips_;
    std::vector<MixerStrip*>                 stripPtrs_;
    std::vector<std::unique_ptr<MixerStrip>> retiredStrips_;
    MasterUtilityPanel                       masterUtilityPanel_;
    MixerDragStateMachine                    dragStateMachine_;
    TrackID                                   pluginDropHoverTrackId_;

    // ?? Honest paint-rate counter for the mixer panel itself ????????????????
    struct MixerPaintRateMeter
    {
        void tick() noexcept
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            ++framesSinceLastSample_;
            if (lastSampleTime_ <= 0.0) { lastSampleTime_ = now; return; }
            const double elapsedMs = now - lastSampleTime_;
            if (elapsedMs >= 500.0)
            {
                const double instantFps = (framesSinceLastSample_ * 1000.0) / elapsedMs;
                smoothedFps_ = (smoothedFps_ <= 0.0) ? instantFps : (smoothedFps_ * 0.6 + instantFps * 0.4);
                framesSinceLastSample_ = 0;
                lastSampleTime_ = now;
            }
        }
        double getFps() const noexcept { return smoothedFps_; }
    private:
        double lastSampleTime_       = 0.0;
        int    framesSinceLastSample_= 0;
        double smoothedFps_          = 0.0;
    };
    mutable MixerPaintRateMeter mixerPaintRateMeter_;

    // ── Panel background image cache ──────────────────────────────────────
    mutable juce::Image panelBgCache_;

    bool rebuildPending_ = false;
    bool retiredStripCleanupPending_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanel)
};

} // namespace DAW
