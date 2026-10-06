#pragma once
#include <JuceHeader.h>
#include "../PluginScanCore/PluginScanAuditLogCore.h"
#include "../ThemeCore/Theme.h"
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
        juce::Logger::writeToLog (juce::String ("[AUTO-STRIP] ctor-entry")
            + " track=" + track_.getName()
            + " isMaster=" + juce::String ((int) track_.isMaster()));

        setOpaque(false);
        setPaintingIsUnclipped(true);
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

                juce::Logger::writeToLog (juce::String ("[AUTO-STRIP] ctor-registered")
                    + " track=" + track_.getName()
                    + " paramID=0x" + juce::String::toHexString ((int) paramID)
                    + " registered=" + juce::String ((int) (volumeParameter_ != nullptr)));
            }
        }
        // ===== End Phase 1C registration =============================================

        registerAdditionalNativeAutomationParameters();

        initChildren();
    }

    ~MixerStrip() override
    {
        unregisterNativeAutomationParameters();
        if (auto* c = FaderRangeCore::getGlobalInstance()) c->removeListener(this);
        // Unregister from flip state if we registered (master only)
        if (flipObserver_ != nullptr)
            MasterStripFlipState::getGlobalInstance().removeListener(flipObserver_.get());
        track_.removeListener(this);
    }

    void faderRangeChanged() override { invalidateFaderScaleCache(); repaint(); }

    // ===== AutomationParameter::Listener (Phase 1C) =======================
    void parameterValueChanged (apex::automation::AutomationParameter& p,
                                float                                  newNormalized,
                                apex::automation::ChangeSource         source) override
    {
        if (updatingFromAutomation_) return;

        const juce::ScopedValueSetter<bool> guard (updatingFromAutomation_, true);

        const float clampedNorm = juce::jlimit (0.0f, 1.0f, newNormalized);
        if (&p == volumeParameter_)
        {
            const float newDb       = faderRange_.normToDb (clampedNorm);
            const float newGain     = faderRange_.dbToGain (newDb);

            track_.setVolume (newGain);

            if (source == apex::automation::ChangeSource::User)
                if (onVolumeChanged) onVolumeChanged (dragStartVolumeDb_, newDb);

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

    void setSelected(bool sel)
    {
        if (lastSetSelectedValue_ == sel && lastSetSelectedInitialised_)
            return;
        lastSetSelectedValue_       = sel;
        lastSetSelectedInitialised_ = true;

        selectionVisual_.setSelected(sel);

        if (sel)
        {
            // Resolve routing ring colors from graph
            auto& lava = selectionVisual_.getLavaCore();
            const juce::Colour sendCol    = onQuerySendColour    ? onQuerySendColour(track_.getID())    : juce::Colour(0xFFEE4FA0);
            const juce::Colour scCol      = onQuerySidechainColour ? onQuerySidechainColour(track_.getID()) : juce::Colour(0xFFE7C98D);
            const bool hasSend  = onQueryHasSend     ? onQueryHasSend(track_.getID())     : false;
            const bool hasSC    = onQueryHasSidechain ? onQueryHasSidechain(track_.getID()) : false;

            using Mode = TrackLavaLampCore::RingColorMode;
            if (hasSend && hasSC)
                lava.setRingColors(sendCol, scCol, Mode::Both);
            else if (hasSend)
                lava.setRingColors(sendCol, scCol, Mode::Send);
            else if (hasSC)
                lava.setRingColors(sendCol, scCol, Mode::Sidechain);
            else
                lava.clearRingColors();
        }
        else
        {
            selectionVisual_.getLavaCore().clearRingColors();
        }

        repaint();
    }

    // Wired by MixerPanel after construction
    std::function<bool(const TrackID&)>        onQueryHasSend;
    std::function<bool(const TrackID&)>        onQueryHasSidechain;
    std::function<juce::Colour(const TrackID&)> onQuerySendColour;
    std::function<juce::Colour(const TrackID&)> onQuerySidechainColour;

    bool stepLava(float dt)
    {
        if (useLegacyMixerSkin())
            return false;

        if (track_.isMaster() && selectionVisual_.getLavaCore().isAlwaysVisible())
            return false;

        if (!selectionVisual_.needsAnimationTick())
            return false;

        accentPulsePhase_ += dt * 4.5f;  // ~0.72 Hz pulse
        if (accentPulsePhase_ > juce::MathConstants<float>::twoPi)
            accentPulsePhase_ -= juce::MathConstants<float>::twoPi;

        selectionVisual_.stepSweep(dt);

        return selectionVisual_.getLavaCore().stepExternal(dt);
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
        folderToggleBtn_.expandedAmount = folderOpenAmount_;
        if (! getLocalBounds().isEmpty())
            resized();
        repaint();
    }

    // Callbacks wired by MixerPanel
    std::function<void()>      onClicked;
    std::function<void(bool)>  onMuteToggled;
    std::function<void(bool)>  onSoloToggled;
    std::function<void(bool)>  onArmToggled;
    std::function<void()>      onPianoRollRequested;
    std::function<void(float, float)> onVolumeChanged;   // (oldDb, newDb)
    std::function<void(float, float)> onPanChanged;      // (oldPan, newPan)
    std::function<void()>             onInputPanelRequested;
    std::function<void(const TrackID&, juce::Point<int>)> onBodyDragBegin;
    std::function<void(juce::Point<int>)>                 onBodyDragMove;
    std::function<void(juce::Point<int>)>                 onBodyDragEnd;
    std::function<void(const TrackID&, bool)>             onFolderToggleRequested;

    void tickMeter()
    {
        meter_.setLevels(track_.getPeakLevelLeft(), track_.getPeakLevelRight());
        meter_.tick();
    }

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
        selectionVisual_.setAccentColour(track_.isMaster() ? juce::Colour(0xFFD4AF37)
                                                           : track_.getColor());
        resized();
        repaint();
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
                // Monochrome white fill — pro DAW standard
                float fillH = norm * ft.getHeight();
                g.setColour(juce::Colours::white.withAlpha(0.72f));
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

                // ── Brushed aluminium body ─────────────────────────────
                // Top half: bright silver. Bottom half: dark grey.
                // This gives a machined metal look at small scale.
                if (!thumbCapGradientBuilt_) { cachedThumbCapGradient_ = juce::ColourGradient(juce::Colour(0xFFCECBC4), 0.f, 0.f, juce::Colour(0xFF303030), 0.f, 1.f, false); cachedThumbCapGradient_.addColour(0.45, juce::Colour(0xFF888680)); cachedThumbCapGradient_.addColour(0.55, juce::Colour(0xFF505050)); thumbCapGradientBuilt_ = true; } auto cap = cachedThumbCapGradient_; cap.point1 = { thumbR.getCentreX(), thumbR.getY() }; cap.point2 = { thumbR.getCentreX(), thumbR.getBottom() }; g.setGradientFill(cap); g.fillRoundedRectangle(thumbR, thCR);

                // Top-edge catch light (single bright line)
                g.setColour(juce::Colours::white.withAlpha(0.70f));
                g.drawLine(thumbR.getX() + thCR, thumbR.getY() + 0.6f,
                           thumbR.getRight() - thCR, thumbR.getY() + 0.6f, 0.9f);

                // Bottom-edge deep shadow line
                g.setColour(juce::Colours::black.withAlpha(0.50f));
                g.drawLine(thumbR.getX() + thCR, thumbR.getBottom() - 0.6f,
                           thumbR.getRight() - thCR, thumbR.getBottom() - 0.6f, 0.8f);

                // Outer rim
                g.setColour(juce::Colours::white.withAlpha(0.30f));
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
        label_.setBounds(titleRow);

        // Mute / Solo / Arm row
        auto btnRow = b.removeFromTop(18).reduced(4, 1);
        const bool isMaster = track_.isMaster();
        const bool showPiano = !isMaster && (track_.getRole() == TrackRole::MIDI || track_.getRole() == TrackRole::Instrument);
        const int buttonCount = isMaster ? 2 : 3;
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

        // Automation mode button — one row below M/S/R, full strip width
        {
            auto autoRow = b.removeFromTop(16).reduced(4, 1);
            autoModeBtn_.setBounds (autoRow);
        }

        // Input button + pan knob
        if (!isMaster)
        {
            auto inputRow = b.removeFromTop(18).reduced(6, 1);
            inputBtn_.setVisible(true);
            inputBtn_.setBounds(inputRow);
        }
        else
        {
            inputBtn_.setVisible(false);
            inputBtn_.setBounds({});
        }

        auto panRow = b.removeFromTop(30);
        auto panArea = panRow.reduced(6, 1);
        auto tlBtnArea = panArea.removeFromRight(12);
        trackLensBtn_.setBounds(tlBtnArea.withSizeKeepingCentre(12, 12));
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
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) return;
        if (pianoBtn_.isVisible() && pianoBtn_.getBounds().contains(e.getPosition()))
        {
            if (onClicked) onClicked();
            if (onPianoRollRequested) onPianoRollRequested();
            return;
        }
        // Notify panel that this strip was clicked (for selection)
        if (onClicked) onClicked();
        // Fader drag
        if (getFaderThumbBounds().contains(e.position))
        {
            dragStartY_      = e.getScreenPosition().y;
            dragStartVolumeDb_ = FaderRangeCore::gainToDb(track_.getVolume());
            draggingFader_   = true;
            if (volumeParameter_ != nullptr)
            {
                // ===== TEMP DIAGNOSTIC =====
                DBG ("[AUTO-STRIP] mouseDown"
                    << " track=" << track_.getName()
                    << " paramHasValue=" << (int) (volumeParameter_ != nullptr)
                    << " paramID=0x" << (volumeParameter_ != nullptr
                                          ? juce::String::toHexString ((int) volumeParameter_->getID())
                                          : juce::String ("null")));
                // ===== END DIAGNOSTIC =====
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
                // ===== TEMP DIAGNOSTIC =====
                {
                    static int dragCount = 0;
                    ++dragCount;
                    if (dragCount <= 10 || dragCount % 20 == 0)
                    {
                        DBG ("[AUTO-STRIP] mouseDrag#" << dragCount
                            << " track=" << track_.getName()
                            << " newNorm=" << newNorm
                            << " volumeParameterNull=" << (int) (volumeParameter_ == nullptr));
                    }
                }
                // ===== END DIAGNOSTIC =====
                volumeParameter_->setValueFromUser (juce::jlimit (0.0f, 1.0f, newNorm));
            }
            else
            {
                if (onVolumeChanged) onVolumeChanged(dragStartVolumeDb_, newDb);
                track_.setVolume(faderRange_.dbToGain(newDb));
            }
            repaint();
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

        if (onClicked)
            onClicked();

        if (!getFaderThumbBounds().contains(e.position))
            return;

        const float oldDb = FaderRangeCore::gainToDb(track_.getVolume());
        const float resetDb = 0.0f;
        track_.setVolume(faderRange_.dbToGain(resetDb));
        if (onVolumeChanged)
            onVolumeChanged(oldDb, resetDb);
        draggingFader_ = false;
        repaint();
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
        draggingFader_ = false;
        if (volumeParameter_ != nullptr)
            volumeParameter_->endGesture();
    }

private:
    // ── Small button helper ──────────────────────────────────────────────
    struct SmallButton : public juce::Component
    {
        juce::String label;
        bool         active   = false;
        juce::Colour activeCol;
        std::function<void()> onClick;
        std::function<void(const juce::MouseEvent&)> onRightClick;

        SmallButton(const juce::String& lbl, juce::Colour ac)
            : label(lbl), activeCol(ac) {}

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
                juce::ColourGradient fill(
                    juce::Colour(0xFF1B1E26), b.getCentreX(), b.getY(),
                    juce::Colour(0xFF0D1016), b.getCentreX(), b.getBottom(), false);
                fill.addColour(0.45, juce::Colour(0xFF141821));
                g.setGradientFill(fill);
                g.fillRoundedRectangle(b, 2.0f);

                g.setColour(juce::Colours::white.withAlpha(0.10f));
                g.drawLine(b.getX() + 1.5f, b.getY() + 0.6f, b.getRight() - 1.5f, b.getY() + 0.6f, 0.8f);
                g.setColour(juce::Colours::white.withAlpha(0.11f));
                g.drawRoundedRectangle(b, 2.0f, 0.8f);
            }

            juce::ColourGradient gloss(
                juce::Colours::white.withAlpha(active ? 0.14f : 0.08f), b.getCentreX(), b.getY(),
                juce::Colours::transparentWhite, b.getCentreX(), b.getBottom(), false);
            g.setGradientFill(gloss);
            g.fillRoundedRectangle(b.withHeight(b.getHeight() * 0.42f), 2.0f);

            g.setColour(active ? juce::Colours::white : juce::Colour(0xFF8A8FA0));
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
            g.setColour(juce::Colour(0xFFF7ECDD).withAlpha(0.92f));
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

            // ── Clean premium knob ─────────────────────────────────────────
            g.setColour(juce::Colours::black.withAlpha(0.65f));
            g.fillEllipse(outer.expanded(1.5f).translated(0.f, 2.f));

            // Restrained dark chrome collar
            {
                juce::ColourGradient ring(
                    juce::Colour(0xFFA6AFBD), c.x, outer.getY(),
                    juce::Colour(0xFF2D3440), c.x, outer.getBottom(), false);
                ring.addColour(0.42, juce::Colour(0xFF5A6370));
                g.setGradientFill(ring);
                g.fillEllipse(outer.expanded(1.0f));
            }

            // Body: black glass / graphite
            {
                juce::ColourGradient body(
                    juce::Colour(0xFF434A57), c.x, outer.getY(),
                    juce::Colour(0xFF090B0F), c.x, outer.getBottom(), false);
                body.addColour(0.22, juce::Colour(0xFF2B313D));
                body.addColour(0.55, juce::Colour(0xFF151920));
                g.setGradientFill(body);
                g.fillEllipse(outer);
            }

            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.drawEllipse(outer.reduced(2.5f), 0.8f);

            // Dome gloss
            {
                auto domeR = outer.reduced(r * 0.08f);
                juce::ColourGradient dome(
                    juce::Colours::white.withAlpha(0.28f), c.x, domeR.getY(),
                    juce::Colours::transparentWhite, c.x, domeR.getY() + domeR.getHeight() * 0.38f, false);
                g.setGradientFill(dome);
                g.fillEllipse(domeR.withHeight(domeR.getHeight() * 0.40f));
            }

            // Outer rim
            {
                juce::ColourGradient rim(
                    juce::Colours::white.withAlpha(0.38f), c.x, outer.getY(),
                    juce::Colour(0xFF151920).withAlpha(0.52f), c.x, outer.getBottom(), false);
                g.setGradientFill(rim);
                g.drawEllipse(outer, 1.0f);
            }

            // Pearl-white pointer
            float angle = value * juce::MathConstants<float>::pi * 0.75f;
            {
                float pr = juce::jmax(2.0f, r - 4.5f);
                float lx = c.x + pr * std::sin(angle);
                float ly = c.y - pr * std::cos(angle);
                // Soft glow behind tip
                g.setColour(juce::Colours::white.withAlpha(0.18f));
                g.fillEllipse(lx - 3.f, ly - 3.f, 6.f, 6.f);
                // Bright pearl tip
                g.setColour(juce::Colours::white.withAlpha(0.96f));
                g.fillEllipse(lx - 1.6f, ly - 1.6f, 3.2f, 3.2f);
                // Pointer line
                g.setColour(juce::Colours::white.withAlpha(0.72f));
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
    SmallButton  inputBtn_ { "Trim", juce::Colour(0xFF7C3AED) };
    SmallButton  autoModeBtn_ { "OFF", juce::Colour(0xFF00BFFF) };
    FolderToggleButton folderToggleBtn_;
    PanKnob      panKnob_;
    SmallButton  trackLensBtn_ { "TL", juce::Colour(0xFFE84393) };
    LevelMeter   meter_;

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

    juce::Rectangle<float> getFaderThumbBounds() const
    {
        if (faderTrackBounds_.getHeight() <= 8.0f)
            return {};

        const auto ft = faderTrackBounds_;
        const float cx = ft.getCentreX();
        const float norm = faderRange_.dbToNorm(FaderRangeCore::gainToDb(track_.getVolume()));
        const float thW = juce::jmin(ft.getWidth() - 2.0f, 26.0f);
        const float thH = 7.0f;
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
        if (onFolderToggleRequested)
            onFolderToggleRequested(track_.getID(), folderExpanded_);
        repaint();
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

        const float contentX = cap.getX() + 10.0f;
        const float arrowRight = (float) folderToggleBtn_.getRight() + 4.0f;
        const float labelRight = cap.getRight() - 42.0f;
        const float labelWidth = juce::jmax(16.0f, labelRight - arrowRight);
        auto nameBounds = juce::Rectangle<float>(arrowRight, cap.getY() + 1.0f, labelWidth, cap.getHeight() - 2.0f);
        auto sepX = nameBounds.getRight() + 3.0f;

        // Track name with text shadow for contrast
        g.setColour(juce::Colours::black.withAlpha(0.60f));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(track_.getName(), nameBounds.toNearestInt().translated(0, 1), juce::Justification::centredLeft, true);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(track_.getName(), nameBounds.toNearestInt(), juce::Justification::centredLeft, true);

        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawLine(sepX, cap.getY() + 4.0f, sepX, cap.getBottom() - 4.0f, 1.0f);

        auto folderLabelBounds = juce::Rectangle<float>(sepX + 4.0f, cap.getY() + 1.0f,
                                                        cap.getRight() - sepX - 8.0f,
                                                        cap.getHeight() - 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.50f));
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.drawText("FOLDER", folderLabelBounds.toNearestInt(), juce::Justification::centredRight, false);

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

        juce::ignoreUnused(contentX);
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
                juce::Colour col = isUnity ? juce::Colour(0xFFD4AF37) : juce::Colours::white;
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

        const float pulse = 0.5f + 0.5f * std::sin(accentPulsePhase_);

        // Outer neon magenta bloom — wide diffuse halo
        for (int i = 1; i <= 3; ++i)
        {
            const float expand = (float)i * 1.4f;
            const float a = (0.18f + pulse * 0.10f) / (float)i;
            g.setColour(juce::Colour(0xFFFF0090).withAlpha(a));
            g.drawRoundedRectangle(bounds.expanded(expand), cr + expand * 0.3f, 1.2f);
        }

        // Main neon rim — top brighter, fades to pure magenta at bottom
        juce::ColourGradient rim(
            juce::Colour(0xFFFF55C0).withAlpha(0.95f + pulse * 0.05f), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0xFFFF0090).withAlpha(0.80f + pulse * 0.10f), bounds.getCentreX(), bounds.getBottom(), false);
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
        // Label — nucleos strip-title: serif italic, pearl, gold glow
        label_.setText(track_.getName(), juce::dontSendNotification);
        {
            juce::Font f(13.f, juce::Font::italic);
            if (track_.isMaster())
            {
                f = juce::Font(14.f, juce::Font::plain);
                label_.setColour(juce::Label::textColourId, juce::Colour(0xFFF8E0A8));
            }
            else
            {
                label_.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFEC));
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
                               [this](int result)
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

        addAndMakeVisible(muteBtn_);
        addAndMakeVisible(soloBtn_);
        addAndMakeVisible(armBtn_);
        addAndMakeVisible(inputBtn_);
        addAndMakeVisible(autoModeBtn_);

        folderToggleBtn_.setVisible(false);
        folderToggleBtn_.onClick = [this]
        {
            requestFolderToggle();
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
            if (onMasterUtilityRequested) onMasterUtilityRequested(*this);
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

        addAndMakeVisible(utilityBtn_);
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

        // Right side: single panel button, vertically centred in header row.
        auto rightSlot = headerRow.removeFromRight(btnW);
        auto centredBtn = rightSlot.withSizeKeepingCentre(btnW, btnH);
        utilityBtn_.setBounds(centredBtn);
        utilityBtn_.setVisible(true);
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
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);

        // 1. Drip background — cached image (body, drips, header tint, border are static)
        {
            const int w = juce::jmax(1, (int)std::ceil(bounds.getWidth()));
            const int h = juce::jmax(1, (int)std::ceil(bounds.getHeight()));
            if (!cachedDripBg_.isValid() || cachedDripBg_.getWidth() != w || cachedDripBg_.getHeight() != h)
            {
                cachedDripBg_ = juce::Image(juce::Image::ARGB, w, h, true);
                juce::Graphics ig(cachedDripBg_);
                dripRenderer_.paint(ig, juce::Rectangle<float>(0.f, 0.f, (float)w, (float)h), false);
            }
            g.drawImageAt(cachedDripBg_, (int)std::round(bounds.getX()), (int)std::round(bounds.getY()), false);
        }

        // 2. Animated floating spheres — lava core
        {
            auto sphereArea = bounds.withTrimmedTop(44.0f);
            selectionVisual_.getLavaCore().paintBackground(g, sphereArea);
        }

        // 3. Header chrome — gradient glow + title text
        {
            const bool isFront = MasterStripFlipState::getGlobalInstance().isFront();
            auto header = bounds.withHeight(43.0f).reduced(8.0f, 5.0f);
            header.removeFromRight(40.0f);

            juce::ColourGradient headerGlow(
                juce::Colour(0x24FFE4B0), header.getCentreX(), header.getY(),
                juce::Colours::transparentBlack, header.getCentreX(), header.getBottom(), false);
            g.setGradientFill(headerGlow);
            g.fillRoundedRectangle(header.expanded(4.0f, 2.0f), 7.0f);

            auto titleArea = header.withHeight(16.0f).translated(0.0f, 2.0f);
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.drawText("MASTER", titleArea.toNearestInt().translated(0, 1), juce::Justification::centred, false);
            g.setColour(juce::Colour(0xFFFFE3B0));
            g.drawText("MASTER", titleArea.toNearestInt(), juce::Justification::centred, false);

            auto subtitleArea = header.withTrimmedTop(16.0f).withHeight(12.0f);
            g.setFont(juce::Font(8.5f, juce::Font::italic));
            g.setColour(juce::Colour(0xFFFFB3D0).withAlpha(0.88f));
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

    float accentPulsePhase_ = 0.f;  // advanced in stepLava(), used in paintSelectedPurpleAccent()

    // ── Master drip background cache ────────────────────────────────
    mutable juce::Image      cachedDripBg_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerStrip)
};

class MasterUtilityPanel : public juce::Component,
                           private juce::Timer
{
public:
    MasterUtilityPanel()
    {
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
        startTimerHz(60);
    }

    ~MasterUtilityPanel() override
    {
        stopTimer();
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
    void timerCallback() override
    {
        if (!isShowing()) return;
        const bool nowFront = MasterStripFlipState::getGlobalInstance().isFront();
        if (nowFront != lastFront_)
        {
            lastFront_ = nowFront;
            resized();
            repaint();
        }
    }

    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz(60);
        else
            stopTimer();
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
                   private juce::Timer,
                   private TrackManager::Listener
{
public:
    static constexpr int kStripW   = 80;
    static constexpr int kStripGap = 2;

    // Debug fields (set by MainComponent for diagnostics)
    juce::String debugLastLauncher_;
    juce::String debugLastSendToggle_;

    // Callbacks wired by MainComponent
    std::function<void(const TrackID&)>                      onTrackSelected;
    std::function<void(const TrackID&)>                      onTrackLensRequested;
    std::function<void(const TrackID&)>                      onOpenTrackPianoRoll;
    std::function<void(const TrackID&)>                      onOpenInputTrimPanel;
    std::function<void(int)>                                 onResizeDrag;
    std::function<void()>                                    onUndockRequested;
    std::function<void()>                                    onDockRequested;
    std::function<void(bool)>                                onToggleSidePanel;
    std::function<void(bool)>                                onTogglePluginBrowser;
    std::function<void()>                                    onCreateBus;
    std::function<void()>                                    onCreateFolderBus;
    std::function<void(const TrackID&)>                      onDeleteTrack;
    std::function<void(const TrackID&, bool)>                onToggleTrackMute;
    std::function<void(const TrackID&, bool)>                onToggleTrackSolo;
    std::function<void(const TrackID&)>                      onToggleTrackArm;
    std::function<void(const TrackID&, bool)>                onSetTrackMonitoring;
    std::function<void(const TrackID&, TrackRole)>           onSetTrackRole;
    std::function<void(const TrackID&, float, float)>        onCommitTrackVolume;
    std::function<void(const TrackID&, float, float)>        onCommitTrackPan;
    std::function<void(const TrackID&, const TrackID&)>      onCreateFolderFromDrop;
    std::function<void(const TrackID&, const TrackID&, DragMode)> onFolderDropRequested;
    std::function<void(const TrackID&, bool)>                onFolderCollapseChanged;
    std::function<void(const TrackID&, int)>                 onTrackReorderRequested;
    std::function<void()>                                    onRescanPlugins;
    std::function<void(const TrackID&, int, const TrackID&)> onPluginDropCopy;
    std::function<void()>                                    onCableRepaintNeeded;
    std::function<void(bool)>                                onBubblegumToggled;

    MixerPanel(TrackManager& trackManager, FaderRangeCore& faderRange)
        : trackManager_(trackManager), faderRange_(faderRange)
    {
        setOpaque(true);   // solid background — skip DWM transparent composite path
        trackManager_.addListener(this);
        addAndMakeVisible(masterUtilityPanel_);
        rebuildStrips();
        startTimerHz(60);
    }

    ~MixerPanel() override
    {
        stopTimer();
        trackManager_.removeListener(this);
    }

    // Binding
    void bindBubblegumV2(BubblegumV2System* bgV2)
    {
        bgV2_ = bgV2;
        if (onCableRepaintNeeded) onCableRepaintNeeded();
    }
    void bindFolderBus(FolderBusCore* folderBus)   { folderBus_ = folderBus; }
    void bindRouting(RoutingGraph* graph, void*)
    {
        routingGraph_ = graph;
        for (auto* strip : stripPtrs_)
            if (strip != nullptr)
                strip->bindRoutingGraph(routingGraph_);
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
    void setViewport(juce::Viewport* vp)            { viewport_ = vp; }
    void setDockedState(bool docked)                { docked_ = docked; }
    void setPaused(bool p)                          { paused_ = p; }
    void setCollapsedFolderBuses(const std::unordered_set<TrackID>& collapsed)
    {
        if (collapsedFolderBuses_ == collapsed)
            return;

        collapsedFolderBuses_ = collapsed;
        rebuildStrips();
        repaint();
    }

    // Visual selection
    void    selectTrack(const TrackID& id)
    {
        selectedTrackId_ = id;
        updateStripSelectionState();
        if (onTrackSelected) onTrackSelected(id);
    }
    void    selectTrackVisual(const TrackID& id)
    {
        selectedTrackId_ = id;
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

    void toggleCableForceVisible()  { setCableForceVisible(!cableForceVisible_); }
    void setCableForceVisible(bool v)
    {
        cableForceVisible_ = v;
        if (onCableRepaintNeeded) onCableRepaintNeeded();
        repaint();
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
                viewport_->setViewPosition(strip->getBounds().getX(), 0);
                return;
            }
        }
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
        // The dusk-studio gradient is fully static; only re-bake on resize.
        const int w = bounds.getWidth(), h = bounds.getHeight();
        if (!panelBgCache_.isValid() || panelBgCache_.getWidth() != w || panelBgCache_.getHeight() != h)
        {
            panelBgCache_ = juce::Image(juce::Image::RGB, juce::jmax(1, w), juce::jmax(1, h), false);
            juce::Graphics ig(panelBgCache_);
            const auto fb = panelBgCache_.getBounds().toFloat();

            // Base vertical gradient
            {
                juce::ColourGradient base(
                    juce::Colour(0xFF1A2438), fb.getCentreX(), fb.getY(),
                    juce::Colour(0xFF0D0808), fb.getCentreX(), fb.getBottom(), false);
                base.addColour(0.30, juce::Colour(0xFF211A22));
                base.addColour(0.60, juce::Colour(0xFF1C1410));
                ig.setGradientFill(base);
                ig.fillRect(fb);
            }
            // Vapor light: cool blue top-right bloom
            {
                juce::ColourGradient vl(
                    juce::Colour(0x33B4D2F0), fb.getX() + fb.getWidth() * 0.78f, fb.getY() + fb.getHeight() * 0.18f,
                    juce::Colours::transparentBlack, fb.getCentreX(), fb.getCentreY(), true);
                ig.setGradientFill(vl);
                ig.fillRect(fb);
            }
            // Vapor light: warm amber right-upper bloom
            {
                juce::ColourGradient vl(
                    juce::Colour(0x24DCB48C), fb.getX() + fb.getWidth() * 0.88f, fb.getY() + fb.getHeight() * 0.35f,
                    juce::Colours::transparentBlack, fb.getRight(), fb.getCentreY(), true);
                ig.setGradientFill(vl);
                ig.fillRect(fb);
            }
            // Vapor light: warm left-bottom bloom
            {
                juce::ColourGradient vl(
                    juce::Colour(0x388C6446), fb.getX() + fb.getWidth() * 0.18f, fb.getY() + fb.getHeight() * 0.75f,
                    juce::Colours::transparentBlack, fb.getCentreX(), fb.getBottom(), true);
                ig.setGradientFill(vl);
                ig.fillRect(fb);
            }
            // Floor warm shadow
            {
                juce::ColourGradient floor(
                    juce::Colours::transparentBlack,           fb.getCentreX(), fb.getBottom() - fb.getHeight() * 0.28f,
                    juce::Colour(0xFF3C2819).withAlpha(0.38f), fb.getCentreX(), fb.getBottom(), false);
                ig.setGradientFill(floor);
                ig.fillRect(fb.withTop(fb.getBottom() - fb.getHeight() * 0.28f));
            }
            // Top gold hairline
            {
                juce::ColourGradient hl(
                    juce::Colours::transparentBlack, fb.getX(),       fb.getY(),
                    juce::Colour(0x8CF8E0A8),        fb.getCentreX(), fb.getY(), false);
                hl.addColour(0.70, juce::Colour(0x47FFFAEC));
                hl.addColour(1.00, juce::Colours::transparentBlack);
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
    }

    void resized() override
    {
        panelBgCache_ = {};  // invalidate on resize
        int x = 0;
        for (auto* strip : stripPtrs_)
        {
            if (!strip) continue;

            const int depth = getTrackDepth(strip->getTrack());
            const bool isChildTrack = depth > 0 && !strip->getTrack().isMaster();
            // Keep hierarchy readable but avoid the previous "stepped" cramped look.
            const int childInsetX = isChildTrack ? juce::jmin(14, 8 + depth * 2) : 0;
            const int childTopInset = isChildTrack ? juce::jmin(14, 8 + depth * 2) : 0;
            const int childBottomLift = isChildTrack ? juce::jmin(18, 10 + depth * 3) : 0;
            const int stripW = strip->getPreferredWidth();

            strip->setBounds(x + childInsetX,
                             childTopInset,
                             stripW - childInsetX - (isChildTrack ? 4 : 0),
                             juce::jmax(108, getHeight() - childTopInset - childBottomLift));
            x += stripW + kStripGap;
        }
        layoutMasterUtilityPanel();
    }

    int clampTrackInsertionIndex(int insertionIndex) const
    {
        const int numTracks = trackManager_.getNumTracks();
        if (numTracks <= 0)
            return -1;

        return juce::jlimit(0, juce::jmax(0, numTracks - 1), insertionIndex);
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
    /** Push the bound master engine pointers into the master strip if it
     *  already exists. Called both at bind time and at end of rebuildStrips(). */
    void applyMasterEnginesToExistingStrip()
    {
        masterUtilityPanel_.setMasterEngines(masterCeiling_,
                                             masterDither_,
                                             masterPostMeter_,
                                             masterPhaseWidth_);

        if (!trackManager_.hasMasterTrack()) return;
        const auto* master = trackManager_.getMasterTrack();
        if (master == nullptr) return;

        for (auto* strip : stripPtrs_)
        {
            if (strip != nullptr && &strip->getTrack() == master)
            {
                strip->setMasterEngines(masterCeiling_,
                                        masterDither_,
                                        masterPostMeter_,
                                        masterPhaseWidth_);
                break;
            }
        }
    }

    // TrackManager::Listener overrides
    void trackAdded(Track*) override           { scheduleRebuild(); }
    void trackRemoved(const TrackID&) override { scheduleRebuild(); }
    void trackOrderChanged() override          { scheduleRebuild(); }

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

    void timerCallback() override
    {
        constexpr float dt = 1.0f / 60.0f;
        const auto visibleArea = viewport_ != nullptr ? viewport_->getViewArea()
                                                      : getLocalBounds();

        for (auto* strip : stripPtrs_)
        {
            if (!strip || !strip->isShowing()) continue;

            const bool inView = visibleArea.intersects(strip->getBounds());
            if (!inView)
                continue;

            strip->tickMeter();                       // self-repaints when level changes

            const bool lavaActive = strip->stepLava(dt);

            // Only force a strip repaint if lava is animating and strip is visible.
            if (lavaActive)
                strip->repaint();
        }

        const auto& drag = dragStateMachine_.getState();
        if (drag.active && drag.draggedTrackId.isNotEmpty())
        {
            dragOverlayPhase_ += dt;
            if (dragOverlayPhase_ > juce::MathConstants<float>::twoPi * 64.0f)
                dragOverlayPhase_ -= juce::MathConstants<float>::twoPi * 64.0f;

            repaint();
        }

        // Keep the mixer host itself paced at 60 Hz. Strip meters/lava repaint
        // their own child regions, but the floating-window compositor can settle
        // to a lower dirty-region cadence after the window is moved unless the
        // mixer panel remains explicitly invalidated.
        repaint();
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

        const float expand = adoptIntoFolder ? (4.0f + pulseSlow * 2.0f)
                                             : (6.0f + pulseSlow * 3.0f);
        auto halo = target.expanded(expand, adoptIntoFolder ? 8.0f : 10.0f);

        auto haloColour = adoptIntoFolder
            ? juce::Colour(0xFFEBCFA8).withAlpha(0.10f + pulseSlow * 0.05f)
            : juce::Colour(0xFFF3D8C2).withAlpha(0.12f + pulseSlow * 0.06f);

        juce::ColourGradient haloGrad(
            haloColour, halo.getCentreX(), halo.getY(),
            juce::Colours::transparentBlack, halo.getCentreX(), halo.getBottom(), false);
        g.setGradientFill(haloGrad);
        g.fillRoundedRectangle(halo, 16.0f);

        juce::Colour rimA = adoptIntoFolder
            ? juce::Colour(0xFFFFF3DE).withAlpha(0.42f + pulseFast * 0.16f)
            : juce::Colour(0xFFFFF6EA).withAlpha(0.46f + pulseFast * 0.18f);
        juce::Colour rimB = adoptIntoFolder
            ? juce::Colour(0xFFD2A86B).withAlpha(0.26f + pulseSlow * 0.10f)
            : juce::Colour(0xFFE9C8A8).withAlpha(0.30f + pulseSlow * 0.10f);

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

        g.setColour(juce::Colour(0xFFFFE7CC).withAlpha(0.10f + pulseSlow * 0.04f));
        g.strokePath(stream, juce::PathStrokeType(8.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(juce::Colour(0xFFD8B07A).withAlpha(0.20f + pulseFast * 0.08f));
        g.strokePath(stream, juce::PathStrokeType(3.4f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(juce::Colours::white.withAlpha(0.34f + pulseFast * 0.10f));
        g.strokePath(stream, juce::PathStrokeType(1.1f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float beadR = adoptIntoFolder ? (6.0f + pulseFast * 1.4f)
                                            : (7.0f + pulseFast * 1.8f);
        g.setColour(juce::Colour(0xFFFFF3E1).withAlpha(0.28f + pulseFast * 0.10f));
        g.fillEllipse(to.x - beadR * 1.5f, to.y - beadR * 1.5f, beadR * 3.0f, beadR * 3.0f);
        g.setColour(juce::Colour(0xFFE5BD84).withAlpha(0.44f + pulseFast * 0.12f));
        g.fillEllipse(to.x - beadR, to.y - beadR, beadR * 2.0f, beadR * 2.0f);

        if (adoptIntoFolder)
        {
            const float laneY = target.getBottom() - 14.0f;
            g.setColour(juce::Colour(0xFFE3C8A4).withAlpha(0.26f + pulseSlow * 0.10f));
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
            auto splitBand = target.withTrimmedTop(target.getHeight() * 0.48f)
                                   .withHeight(2.0f)
                                   .reduced(10.0f, 0.0f);
            g.setColour(juce::Colour(0xFFFFF2E1).withAlpha(0.42f + pulseFast * 0.12f));
            g.fillRoundedRectangle(splitBand, 1.0f);
        }
    }

    void updateStripSelectionState()
    {
        for (auto* strip : stripPtrs_)
        {
            if (!strip) continue;
            bool sel = (strip->getTrack().getID() == selectedTrackId_);
            strip->setSelected(sel);
        }
    }

    void rebuildStrips()
    {
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
            strip->setSelected(track->getID() == selectedTrackId_);
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
            strip->onPianoRollRequested = [this, track] {
                selectTrack(track->getID());
                if (onOpenTrackPianoRoll) onOpenTrackPianoRoll(track->getID());
            };
            strip->onInputPanelRequested = [this, track] {
                selectTrack(track->getID());
                if (onOpenInputTrimPanel) onOpenInputTrimPanel(track->getID());
                else InputTrimFloatingPanel::showForTrack(*track, getTopLevelComponent());
            };

            strip->onTrackLensRequested = [this, track](const TrackID&)
            {
                selectTrack(track->getID());
                if (onTrackLensRequested) onTrackLensRequested(track->getID());
            };
            strip->onFolderToggleRequested = [this](const TrackID& trackId, bool expanded)
            {
                // Guard against rebuildStrips() (or anything it triggers) destroying this MixerPanel.
                juce::Component::SafePointer<MixerPanel> safePanel(this);

                // Update local state and rebuild BEFORE invoking the external callback.
                // onFolderCollapseChanged can destroy this MixerPanel entirely, so we
                // must not dereference 'this' after that call returns.
                if (expanded)
                    collapsedFolderBuses_.erase(trackId);
                else
                    collapsedFolderBuses_.insert(trackId);
                rebuildStrips();
                if (onFolderCollapseChanged)
                    onFolderCollapseChanged(trackId, expanded);

                if (safePanel != nullptr)
                    safePanel->repaint();
            };
            strip->onBodyDragBegin = [this](const TrackID& trackId, juce::Point<int> pos)
            {
                dragStateMachine_.begin(trackId, pos);
            };
            strip->onBodyDragMove = [this](juce::Point<int> pos)
            {
                if (dragStateMachine_.update(pos, stripPtrs_))
                    repaint();
            };
            strip->onBodyDragEnd = [this](juce::Point<int> pos)
            {
                // Guard against MixerPanel being destroyed during topology-mutating callbacks
                // (folder creation/reorder triggers TrackManager::Listener ? rebuildStrips)
                juce::Component::SafePointer<MixerPanel> safePanel(this);

                dragStateMachine_.update(pos, stripPtrs_);
                auto state = dragStateMachine_.getState();
                auto mode = dragStateMachine_.commit(pos, stripPtrs_);

                // Copy state to locals before invoking callbacks that may destroy this panel
                const auto draggedId = state.draggedTrackId;
                const auto targetId = state.targetTrackId;
                const int insertionIndex = state.insertionIndex;

                if (mode == DragMode::Reorder)
                {
                    auto targetIndex = clampTrackInsertionIndex(insertionIndex);
                    if (targetIndex >= 0 && onTrackReorderRequested)
                        onTrackReorderRequested(draggedId, targetIndex);
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
            strip->onVolumeChanged = [this, track](float oldDb, float newDb) {
                if (onCommitTrackVolume) onCommitTrackVolume(track->getID(), oldDb, newDb);
            };
            strip->onPanChanged = [this, track](float oldPan, float newPan) {
                if (onCommitTrackPan) onCommitTrackPan(track->getID(), oldPan, newPan);
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
                strip->setSelected(master->getID() == selectedTrackId_);
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
                strip->onVolumeChanged = [this, master](float oldDb, float newDb) {
                    if (onCommitTrackVolume) onCommitTrackVolume(master->getID(), oldDb, newDb);
                };
                strip->onPanChanged = [this, master](float oldPan, float newPan) {
                    if (onCommitTrackPan) onCommitTrackPan(master->getID(), oldPan, newPan);
                };
                strip->onMasterUtilityRequested = [this](MixerStrip& masterStrip)
                {
                    masterUtilityOpen_ = !masterUtilityOpen_;
                    masterUtilityPanel_.setVisible(masterUtilityOpen_);
                    layoutMasterUtilityPanel(&masterStrip);
                    masterUtilityPanel_.toFront(false);
                    repaint();
                };

                addAndMakeVisible(strip.get());
                stripPtrs_.push_back(strip.get());
                strips_.push_back(std::move(strip));
            }
        }

        int totalW = 0;
        for (auto* strip : stripPtrs_)
            if (strip != nullptr)
                totalW += strip->getPreferredWidth() + kStripGap;
        if (totalW > getWidth())
            setSize(totalW, getHeight());
        resized();

        // Wire master engines if already bound (no-op if not yet bound)
        applyMasterEnginesToExistingStrip();
    }

    void layoutMasterUtilityPanel(MixerStrip* explicitMasterStrip = nullptr)
    {
        MixerStrip* masterStrip = explicitMasterStrip;
        if (masterStrip == nullptr)
        {
            for (auto* strip : stripPtrs_)
                if (strip != nullptr && strip->getTrack().isMaster())
                    masterStrip = strip;
        }

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
    bool    docked_                = false;
    bool    paused_                = false;
    bool    sidePanelOpen_         = false;
    bool    browserOpen_           = false;
    bool    settingsMenuOpen_      = false;
    bool    masterUtilityOpen_     = false;
    bool    cableForceVisible_     = true;
    bool    offscreenForceVisible_ = true;
    bool    allTracksFullDepth_    = false;
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

    std::unordered_set<TrackID>              collapsedFolderBuses_;
    std::vector<std::unique_ptr<MixerStrip>> strips_;
    std::vector<MixerStrip*>                 stripPtrs_;
    MasterUtilityPanel                       masterUtilityPanel_;
    MixerDragStateMachine                    dragStateMachine_;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanel)
};

} // namespace DAW