#pragma once
#include <JuceHeader.h>

#include "../TrackCore/Track.h"
#include "../AnalogVuMeterCore/AnalogVuMeterCore.h"
#include "../AnalogVuMeterCore/AnalogVuMeterComponent.h"
#include "../BubblegumKnobCore/BubblegumKnobCore.h"
#include "../BubblegumKnobCore/BubblegumKnobComponent.h"
#include "../BubblegumTaskbarCore/BubblegumTaskbar.h"
#include "../BubblegumFloatingPanelCore/BubblegumFloatingPanelCore.h"

namespace DAW {

/**
 * InputTrimFloatingPanel  (rebuilt — Batch 4)
 *
 * Floating panel that exposes a track's pre-fader input gain via a Bubblegum
 * knob and shows live input levels via an analog VU meter. Replaces the
 * previous file at this path.
 *
 * Composition (top to bottom):
 *   - BubblegumPanelHeaderCore        : "Input gain" title + minimize + close
 *   - BubblegumPanelToggleSwitch      : channel mode selector for the VU meter
 *   - AnalogVuMeterComponent          : cream-faced analog VU
 *   - Separate current/max VU, sample peak dBFS, and Trim dB readouts
 *   - Reference selector and reset control
 *   - BubblegumKnobComponent          : the gain knob
 *
 * Per-track uniqueness:
 *   - The knob's binding closes over the Track& passed to the constructor.
 *     Read = track_.getInputTrim().getTargetGainDb().
 *     Write = Track::setInputTrimDb(dB), which marks the project dirty.
 *     Two panels for two tracks have entirely separate bindings; their
 *     knobs cannot ever read/write each other's value.
 *   - The VU meter, mixer and arranger read the same audio-clock detector.
 *     Reference and channel mode are per-track settings saved in the project.
 *
 * Taskbar integration:
 *   - Inherits IBubblegumTaskbarHost. Registers with the global taskbar
 *     in the constructor, unregisters in the destructor.
 *   - Minimize button -> setVisible(false) + setHostActive(false).
 *   - Close button    -> deleteAfterCallback() (caller may override via
 *                        onCloseRequested).
 *
 * Show pattern:
 *   InputTrimFloatingPanel::showForTrack(track, &someParentComponent);
 *   The panel adds itself to the parent and sets its size + position. If
 *   parent is null, the panel adds itself to the desktop as a temporary
 *   floating window with a drop shadow.
 */
class InputTrimFloatingPanel : public juce::Component,
                               public IBubblegumTaskbarHost,
                               private juce::Timer
{
public:
    explicit InputTrimFloatingPanel(Track& track)
        : track_(track)
    {
        setSize(360, 526);
        setOpaque(false);

        // ── Header ───────────────────────────────────────────────
        header_.setTitle("Trim / VU");
        header_.setSubtitle(juce::String(juce::CharPointer_UTF8("pre-FX \xc2\xb7 ch ")) + juce::String(track_.getIndex() + 1));
        header_.onMinimizeClicked = [this] { minimizeFromTaskbar(); };
        header_.onCloseClicked    = [this] { closeFromTaskbar(); };
        addAndMakeVisible(header_);

        // ── Channel mode toggle for the VU meter ─────────────────
        lrToggle_.setLabel(channelModeLabel(track_.getInputMeter().getVuChannelMode()));
        lrToggle_.setComponentID("trim.vu-channel");
        lrToggle_.setOn(true);
        lrToggle_.onChanged = [this](bool on)
        {
            // A click turns the pill off; restoring its lit indicator must
            // not advance the channel a second time.
            if (!on) cycleChannelMode();
        };
        addAndMakeVisible(lrToggle_);

        // ── VU meter, bound to per-track InputMeterCore ──────────
        vu_.setSource(&track_.getInputMeter());
        addAndMakeVisible(vu_);

        reference_.setComponentID("trim.vu-reference");
        for (int db = -30; db <= 0; ++db)
            reference_.addItem("0 VU = " + juce::String(db) + " dBFS", db + 31);
        reference_.onChange = [this]
        {
            if (!detached_ && reference_.getSelectedId() > 0)
                track_.setTrimVuReferenceDb((float) (reference_.getSelectedId() - 31));
        };
        reference_.setTooltip("Calibration for a steady sine. Changes the VU reference, not the audio gain.");
        addAndMakeVisible(reference_);

        // ── Knob, bound to per-track InputTrimCore ───────────────
        BubblegumKnobValueBinding b;
        b.getValue    = [this] { return track_.getInputTrim().getTargetGainDb(); };
        b.setValue    = [this](float dB) { track_.setInputTrimDb(dB); };
        b.formatValue = [](float dB)
        {
            if (dB <= -120.0f) return juce::String("-inf");
            return (dB > 0.0f ? juce::String("+") : juce::String())
                 + juce::String(dB, 1) + " dB";
        };
        b.minValue     = -24.0f;
        b.maxValue     =  24.0f;
        b.defaultValue =   0.0f;
        knob_.setBinding(std::move(b));
        knob_.onValueChanged = [this] { refreshReadouts(); };
        addAndMakeVisible(knob_);

        // ── Readouts ─────────────────────────────────────────────
        currentReadout_.setLabel("TRIM dB");
        currentReadout_.setUseAlternateValueColour(false);
        addAndMakeVisible(currentReadout_);

        peakMaxReadout_.setLabel("PEAK dBFS");
        peakMaxReadout_.setUseAlternateValueColour(true);
        peakMaxReadout_.onClicked = [this]
        {
            vu_.resetPeakHold();
            refreshReadouts();
        };
        addAndMakeVisible(peakMaxReadout_);

        vuCurrentReadout_.setLabel("CURRENT VU");
        vuCurrentReadout_.setComponentID("trim.current-vu");
        addAndMakeVisible(vuCurrentReadout_);
        vuMaxReadout_.setLabel("MAX VU");
        vuMaxReadout_.setComponentID("trim.max-vu");
        vuMaxReadout_.onClicked = [this] { vu_.resetPeakHold(); refreshReadouts(); };
        addAndMakeVisible(vuMaxReadout_);
        resetMeters_.setButtonText("Reset meters");
        resetMeters_.setComponentID("trim.reset-meters");
        resetMeters_.onClick = [this] { vu_.resetPeakHold(); refreshReadouts(); };
        addAndMakeVisible(resetMeters_);
        help_.setText("Set Trim around 0 VU for nominal level.\nWatch PEAK separately for digital clipping.", juce::dontSendNotification);
        help_.setFont(juce::Font(12.0f));
        help_.setColour(juce::Label::textColourId, juce::Colour(0xFFBFC4D0));
        help_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(help_);

        // ── Taskbar registration ─────────────────────────────────
        BubblegumTaskbarCore::getGlobalInstance().registerHost(this);

        // ── Periodic refresh for the readouts ────────────────────
        startTimerHz(60);

        refreshReadouts();
    }

    ~InputTrimFloatingPanel() override
    {
        stopTimer();
        BubblegumTaskbarCore::getGlobalInstance().unregisterHost(this);
    }

    /** Break every binding to the owning Track BEFORE the Track is destroyed.
     *  TrackManager::deleteTrack destroys the Track synchronously after
     *  notifying listeners, while this panel may live until the next message
     *  tick — without this, the 60 Hz VU/timer callbacks could read a freed
     *  Track/InputMeterCore and freeze the needle permanently. */
    void detachFromTrack() noexcept
    {
        detached_ = true;
        vu_.setSource(nullptr);
    }

    bool isDetached() const noexcept { return detached_; }

    /** Optional callback fired when the panel decides to close itself.
     *  If the caller owns the panel, set this to null its pointer and
     *  delete the panel. If unset, the panel deletes itself via async
     *  message dispatch. */
    std::function<void(InputTrimFloatingPanel*)> onCloseRequested;

    /** External callbacks for bubbleTaskbar integration from MainComponent. */
    std::function<void()> onMinimizedExternal;
    std::function<void()> onClosedExternal;

    /** Single-owner router for panel show requests. The app-wide
     *  InputTrimPanelManager registers itself here so that EVERY entry point
     *  (mixer strip, arranger timeline row, monitor toggle) converges on the
     *  same per-track panel instance instead of spawning independent ones. */
    struct ITrimPanelOwner
    {
        virtual ~ITrimPanelOwner() = default;
        virtual InputTrimFloatingPanel* showTrimPanelForTrack(Track& track) = 0;
    };

    static void setSharedTrimPanelOwner(ITrimPanelOwner* owner) noexcept { sharedTrimPanelOwner_ = owner; }
    static ITrimPanelOwner* getSharedTrimPanelOwner() noexcept { return sharedTrimPanelOwner_; }

    /** Convenience opener. If a shared InputTrimPanelManager owner is
     *  registered (normal app runtime), the request is delegated to it and
     *  deduplicated per track — the mixer and arranger trim buttons open the
     *  SAME panel. If parent is non-null (legacy fallback only, no manager),
     *  the panel becomes a child of it; otherwise it goes onto the desktop as
     *  a temporary floating window. Returns the panel pointer for the caller
     *  to track if needed. */
    static InputTrimFloatingPanel* showForTrack(Track& track,
                                                juce::Component* parent = nullptr)
    {
        if (sharedTrimPanelOwner_ != nullptr)
            return sharedTrimPanelOwner_->showTrimPanelForTrack(track);

        auto* panel = new InputTrimFloatingPanel(track);
        if (parent != nullptr)
        {
            parent->addAndMakeVisible(panel);
            panel->centreWithSize(panel->getWidth(), panel->getHeight());
        }
        else
        {
            panel->addToDesktop(juce::ComponentPeer::windowHasDropShadow);
            panel->setTopLeftPosition(120, 120);
            panel->setVisible(true);
        }
        return panel;
    }

    // ── IBubblegumTaskbarHost ────────────────────────────────────
    juce::String getChipLabel() const override
    {
        return juce::String(juce::CharPointer_UTF8("Input trim gain \xc2\xb7 ch ")) + juce::String(track_.getIndex() + 1);
    }

    juce::Colour getChipAccentColour() const override
    {
        return juce::Colour(0xFFFF4F8A);
    }

    void restoreFromTaskbar() override
    {
        setVisible(true);
        toFront(true);
        BubblegumTaskbarCore::getGlobalInstance().setHostActive(this, true);
    }

    void minimizeFromTaskbar() override
    {
        setVisible(false);
        BubblegumTaskbarCore::getGlobalInstance().setHostActive(this, false);
        if (onMinimizedExternal)
            onMinimizedExternal();
    }

    void closeFromTaskbar() override
    {
        if (onClosedExternal)
            onClosedExternal();
            
        if (onCloseRequested)
        {
            onCloseRequested(this);
            return;
        }
        // Self-deletion deferred to next message tick — safe even when
        // called from inside a button click callback chain.
        juce::MessageManager::callAsync([self = juce::Component::SafePointer<InputTrimFloatingPanel>(this)]
        {
            if (self != nullptr) delete self.getComponent();
        });
    }

    // ── juce::Component ──────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xFF181B25));
        g.fillRoundedRectangle(b, 14.0f);
        g.setColour(juce::Colour(0xFF2A2F3D));
        g.drawRoundedRectangle(b, 14.0f, 0.8f);
    }

    void resized() override
    {
        const int W = getWidth();

        // Header occupies the top 60 px of the panel.
        header_.setBounds(0, 0, W, 60);

        // Leave room for L+R / MAX beside the indicator dot.
        const int toggleX = juce::roundToInt(header_.getRightWidgetEdgeX() - 64.0f);
        lrToggle_.setBounds(toggleX, 14, 64, 18);

        // VU meter: 320 wide, 200 tall, 20 px horizontal margin, starts at y=70.
        vu_.setBounds(20, 70, W - 40, 200);

        reference_.setBounds(40, 282, W - 80, 26);
        const int readoutWidth = (W - 56) / 3;
        vuCurrentReadout_.setBounds(20, 320, readoutWidth, 42);
        vuMaxReadout_.setBounds(28 + readoutWidth, 320, readoutWidth, 42);
        peakMaxReadout_.setBounds(36 + 2 * readoutWidth, 320, readoutWidth, 42);
        currentReadout_.setBounds(20, 396, 88, 42);
        resetMeters_.setBounds(W - 110, 398, 90, 30);

        // Knob centred at y=340, 110x110.
        const int knobSize = 110;
        knob_.setBounds((W - knobSize) / 2, 378, knobSize, knobSize);
        help_.setBounds(16, 487, W - 32, 36);
    }

private:
    void timerCallback() override
    {
        if (detached_ || !isShowing()) return;
        refreshReadouts();
        knob_.repaint();
    }

    void refreshReadouts()
    {
        if (detached_) return;
        const float currentDb = track_.getInputTrim().getTargetGainDb();
        currentReadout_.setValue(formatDb(currentDb));
        reference_.setSelectedId(juce::roundToInt(track_.getInputMeter().getVuReferenceDb()) + 31,
                                 juce::dontSendNotification);
        lrToggle_.setLabel(channelModeLabel(vu_.getChannelMode()));
        vuCurrentReadout_.setValue(formatDb(vu_.getNeedleDb()));
        vuMaxReadout_.setValue(formatDb(vu_.getVuMaxDb()));

        const float peakDb = vu_.getPeakMaxDb();
        peakMaxReadout_.setValue(peakDb <= -120.0f ? juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"))
                                                   : formatDb(peakDb));
    }

    static juce::String formatDb(float dB)
    {
        if (dB <= -120.0f) return "-inf";
        if (std::abs(dB) < 0.05f) return "0.0";
        return (dB > 0.0f ? juce::String("+") : juce::String())
             + juce::String(dB, 1);
    }

    void cycleChannelMode()
    {
        if (detached_) return;
        VuChannelMode next;
        switch (vu_.getChannelMode())
        {
            case VuChannelMode::Average:   next = VuChannelMode::LeftOnly;  break;
            case VuChannelMode::LeftOnly:  next = VuChannelMode::RightOnly; break;
            case VuChannelMode::RightOnly: next = VuChannelMode::MaxLR;     break;
            default:                       next = VuChannelMode::Average;   break;
        }

        track_.setTrimVuChannelMode(next);
        lrToggle_.setLabel(channelModeLabel(next));
        lrToggle_.setOn(true);
        lrToggle_.repaint();
    }

    Track&                     track_;
    bool                       detached_ { false };

    inline static ITrimPanelOwner* sharedTrimPanelOwner_ = nullptr;

    BubblegumPanelHeaderCore   header_;
    BubblegumPanelToggleSwitch lrToggle_;
    AnalogVuMeterComponent     vu_;
    BubblegumKnobComponent     knob_;
    BubblegumPanelReadoutBox   currentReadout_;
    BubblegumPanelReadoutBox   peakMaxReadout_;
    BubblegumPanelReadoutBox   vuCurrentReadout_, vuMaxReadout_;
    juce::ComboBox             reference_;
    juce::TextButton           resetMeters_;
    juce::Label                help_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputTrimFloatingPanel)
};

} // namespace DAW
