#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../ControlsCore/ModernControls.h"
#include "CursorThemeCore.h"
#include "../TransportCore/TransportController.h"
#include "../ActionCore/ActionManager.h"
#include "../ActionCore/ActionID.h"
#include "ClickToolbarStrip.h"
#include "../Automation/AutomationSystemCore.h"
#include "../AutomationCore/PluginAutomationRecorderCore.h"

namespace DAW {

// ─── TempoDisplay ──────────────────────────────────────────────────────────
// LED-style BPM readout — drag vertically or scroll wheel to change,
// tap repeatedly for tap-tempo
class TempoDisplay : public juce::Component
{
public:
    explicit TempoDisplay(TransportController& transport)
        : transport_(transport) { setSize(140, 44); }

    void setShowLabel(bool shouldShow) { showLabel_ = shouldShow; repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto& a = t.apex;
        auto b  = getLocalBounds().toFloat().reduced(2.f, 4.f);

        g.setColour(a.color.deepestB);
        g.fillRoundedRectangle(b, a.metric.radiusControl);
        g.setColour(a.color.borderSoftA);
        g.drawRoundedRectangle(b, a.metric.radiusControl, a.metric.strokeThin);

        // BPM value — cyan signal LED (monospaced tabular numerics)
        auto valArea = b;
        juce::Rectangle<float> labelArea;
        if (showLabel_)
        {
            valArea = b.withTrimmedRight(38.f);
            labelArea = b.removeFromRight(36.f);
        }

        auto tempStr = juce::String(transport_.getTempo(), 1);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 18.f, juce::Font::bold));
        g.setColour(a.color.cyan.withAlpha(0.18f));
        g.drawText(tempStr, valArea.translated(0, 1), juce::Justification::centredRight);
        g.setColour(a.color.cyan);
        g.drawText(tempStr, valArea, juce::Justification::centredRight);

        if (showLabel_)
        {
            g.setFont(t.fonts.small);
            g.setColour(a.color.textSecondary);
            g.drawText("BPM", labelArea, juce::Justification::centredLeft);
        }
    }

    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails& w) override
    {
        transport_.setTempo(transport_.getTempo() + (w.deltaY > 0 ? 1.0 : -1.0));
        repaint();
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStartTempo_ = transport_.getTempo();
        juce::int64 now = juce::Time::currentTimeMillis();
        if (tapCount_ > 0 && (now - lastTapMs_) > 2000) tapCount_ = 0;
        if (tapCount_ > 0)
            transport_.setTempo(60000.0 / double(now - lastTapMs_));
        lastTapMs_ = now;
        ++tapCount_;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        transport_.setTempo(dragStartTempo_
            - e.getDistanceFromDragStartY() * 0.3);
        repaint();
    }

private:
    TransportController& transport_;
    double       dragStartTempo_ = 120.0;
    int          tapCount_  = 0;
    juce::int64  lastTapMs_ = 0;
    bool         showLabel_ = true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoDisplay)
};

class ProjectKeyDisplay : public juce::Component
{
public:
    ProjectKeyDisplay()
    {
        addAndMakeVisible(keyBox_);
        keyBox_.setJustificationType(juce::Justification::centred);
        keyBox_.setTextWhenNothingSelected("Key");

        const juce::StringArray roots{ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        int itemId = 1;
        for (auto& r : roots)
            keyBox_.addItem(r + " Maj", itemId++);
        for (auto& r : roots)
            keyBox_.addItem(r + " Min", itemId++);

        keyBox_.onChange = [this]()
        {
            if (onKeyChanged)
                onKeyChanged(getSelectedKeyText());
        };
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto& a = t.apex;
        auto b = getLocalBounds().toFloat().reduced(2.f, 4.f);

        g.setColour(a.color.deepestB);
        g.fillRoundedRectangle(b, a.metric.radiusControl);
        g.setColour(a.color.borderSoftA);
        g.drawRoundedRectangle(b, a.metric.radiusControl, a.metric.strokeThin);
    }

    void resized() override
    {
        keyBox_.setBounds(getLocalBounds().reduced(8, 8));
    }

    juce::String getSelectedKeyText() const
    {
        const auto text = keyBox_.getText().trim();
        return text.isEmpty() ? juce::String{} : text;
    }

    void setSelectedKeyText(const juce::String& text)
    {
        const auto wanted = text.trim();
        if (wanted.isEmpty())
        {
            keyBox_.setSelectedId(0, juce::dontSendNotification);
            return;
        }

        for (int i = 0; i < keyBox_.getNumItems(); ++i)
        {
            if (keyBox_.getItemText(i).compareIgnoreCase(wanted) == 0)
            {
                keyBox_.setSelectedId(keyBox_.getItemId(i), juce::dontSendNotification);
                return;
            }
        }

        keyBox_.setText(wanted, juce::dontSendNotification);
    }

    std::function<void(const juce::String&)> onKeyChanged;

    void setShowLabel(bool shouldShow)
    {
        showLabel_ = shouldShow;
        keyBox_.setTextWhenNothingSelected(showLabel_ ? "Key" : "");
        repaint();
    }

private:
    juce::ComboBox keyBox_;
    bool showLabel_ = true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectKeyDisplay)
};

// ─── TransportBar ──────────────────────────────────────────────────────────
class TransportBar : public juce::Component,
                     public juce::TooltipClient,
                     public juce::Timer
{
public:
    /** Quick-access callbacks for panels — wired from MainComponent */
    std::function<void()> onQuickAccessSidePanel;
    std::function<void()> onQuickAccessFxBrowser;

    /** Fired when the user picks a new project key. Wired from MainComponent. */
    std::function<void(const juce::String&)> onProjectKeyChanged;

    /** Current device/project sample rate used to convert the transport's
     *  sample position into seconds for the playhead clock. Wired from
     *  MainComponent; falls back to 44100 when unset. */
    std::function<double()> sampleRateProvider;

    /** Programmatically set the displayed project key (no callback fired). */
    void setProjectKeyText(const juce::String& keyText)
    {
        projectKey_.setSelectedKeyText(keyText);
    }

    explicit TransportBar(TransportController& transport, ClickStateModel* clickState = nullptr)
        : transport_(transport), tempo_(transport)
    {
        auto& t = Theme::getInstance();
        auto* skipBtn = addBtn("<<", t.apex.color.textSecondary,  TransportIcon::SkipBack,
               [this]{ ActionManager::getInstance().dispatch(ActionID::TransportJumpStart); });
        skipBtn->tooltipText = "Skip to Start\nJump the playhead to the beginning of the project.";

        // PLAY carries the primary creative-energy accent: strong APEX magenta,
        // visible even at idle (primary action must be findable without search).
        playBtn_  = addBtn(">",  t.apex.color.magenta,   TransportIcon::Play,
               []{ ActionManager::getInstance().dispatch(ActionID::TransportPlayStop); });
        playBtn_->setIdentityAccent(true);
        playBtn_->tooltipText = "Play / Stop\nStart or stop playback from the current playhead position.";

        auto* stopBtn = addBtn("[]", t.apex.color.textMuted,  TransportIcon::Stop,
               []{ ActionManager::getInstance().dispatch(ActionID::TransportStop); });
        stopBtn->tooltipText = "Stop\nStop playback or recording. Playhead returns to start if return mode is on.";

        recBtn_   = addBtn("O",  t.colors.transportRecord, TransportIcon::Record,
               []{ ActionManager::getInstance().dispatch(ActionID::TransportRecord); });
        recBtn_->tooltipText = "Record\nStart recording on all armed tracks. Press again to stop.";

        loopBtn_  = addBtn("~",  t.apex.color.violet,          TransportIcon::Loop,
               []{ ActionManager::getInstance().dispatch(ActionID::TransportToggleLoop); });
        loopBtn_->tooltipText = "Loop\nToggle loop mode. Playback loops between the left and right locators.";

        returnBtn_ = addBtn(juce::String(juce::CharPointer_UTF8("↩")),  t.apex.color.cyan, TransportIcon::Return,
               [this]{ transport_.setReturnToLastPosition(!transport_.shouldReturnToLastPosition()); });
        returnBtn_->tooltipText = "Return to Start\nToggle return-to-start. Playhead returns to its last start position on stop.";

        addAndMakeVisible(tempo_);
        addAndMakeVisible(projectKey_);
        tempo_.setShowLabel(false);
        projectKey_.setShowLabel(false);
        projectKey_.onKeyChanged = [this](const juce::String& newKey)
        {
            if (onProjectKeyChanged)
                onProjectKeyChanged(newKey);
        };
        if (clickState != nullptr)
        {
            clickStrip_ = std::make_unique<ClickToolbarStrip>(*clickState);
            addAndMakeVisible(clickStrip_.get());
        }
        // ===== Automation record-arm button =================================
        automArmBtn_.setButtonText (juce::String (juce::CharPointer_UTF8 ("A\u2022REC")));
        automArmBtn_.setColour (juce::TextButton::buttonColourId,    t.apex.color.panelC);
        automArmBtn_.setColour (juce::TextButton::buttonOnColourId,  t.apex.color.magentaDeep);
        automArmBtn_.setColour (juce::TextButton::textColourOffId,   t.apex.color.textSecondary);
        automArmBtn_.setColour (juce::TextButton::textColourOnId,    t.apex.color.textPrimary);
        automArmBtn_.setClickingTogglesState (true);
        apex::automation::AutomationSystem::getInstance().armRecording(false);
        apex::automation::AutomationSystem::getInstance().setAutoCreateLaneOnTouch(false);
        DAW::PluginAutomationRecorderCore::setGlobalMode(DAW::AutomationWriteMode::Read);
        automArmBtn_.onClick = [this]
        {
            const bool armed = automArmBtn_.getToggleState();
            apex::automation::AutomationSystem::getInstance().armRecording(armed);
            apex::automation::AutomationSystem::getInstance().setAutoCreateLaneOnTouch(armed);
            DAW::PluginAutomationRecorderCore::setGlobalMode(
                armed ? DAW::AutomationWriteMode::Touch
                      : DAW::AutomationWriteMode::Read);
        };
        addAndMakeVisible (automArmBtn_);
        // ===== End automation arm button ====================================
        startTimerHz(60);
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto& a = t.apex;
        auto  b = getLocalBounds().toFloat();

        // Deep-navy transport deck: subtle top→bottom gradient
        juce::ColourGradient bg(a.color.deepestB, 0, 0,
                                a.color.deepestA, 0, b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRect(b);

        // Hairline separator with soft border presence
        g.setColour(a.color.borderSoftB);
        g.fillRect(0.f, b.getBottom() - 2.f, b.getWidth(), 2.f);

        // Position readout — dark well, hairline border.
        // Line 1: bars:beats (musical position). Line 2: the playhead clock
        // (HH:MM:SS.mmm) so the user always sees the song time in seconds,
        // synced to the same transport position — like every DAW.
        auto tb = posReadoutBounds_.toFloat().reduced(0, 4);
        g.setColour(a.color.deepestA);
        g.fillRoundedRectangle(tb, a.metric.radiusControl);
        g.setColour(a.color.borderSoftA);
        g.drawRoundedRectangle(tb, a.metric.radiusControl, a.metric.strokeThin);

        double pos  = transport_.getPositionBarBeat();
        int    bar  = (int)pos + 1;
        int    beat = (int)((pos - (int)pos) * 4.0) + 1;

        const float readoutH = tb.getHeight();
        auto barBeatArea = tb.removeFromTop(readoutH * 0.52f);

        // Bar/beat LED — magenta creative-energy emphasis (real transport value)
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.f, juce::Font::bold));
        g.setColour(a.color.magentaBright.withAlpha(0.20f));
        auto posStr = juce::String::formatted("%03d : %d", bar, beat);
        g.drawText(posStr, barBeatArea.translated(0, 1), juce::Justification::centred);
        g.setColour(a.color.magentaBright);
        g.drawText(posStr, barBeatArea, juce::Justification::centred);

        // Playhead clock — HH:MM:SS.mmm, cyan signal LED, monospaced tabular
        double sampleRate = sampleRateProvider ? sampleRateProvider() : 44100.0;
        const int64_t totalMs = (int64_t) juce::jmax(0.0,
            (double) transport_.getPosition() / juce::jmax(1.0, sampleRate) * 1000.0);
        const int hh = (int) (totalMs / 3600000);
        const int mm = (int) ((totalMs / 60000) % 60);
        const int ss = (int) ((totalMs / 1000) % 60);
        const int ms = (int) (totalMs % 1000);
        const auto clockStr = juce::String::formatted("%02d:%02d:%02d.%03d", hh, mm, ss, ms);

        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.f, juce::Font::bold));
        g.setColour(a.color.cyan.withAlpha(0.20f));
        g.drawText(clockStr, tb.translated(0, 1), juce::Justification::centred);
        g.setColour(a.color.cyan);
        g.drawText(clockStr, tb, juce::Justification::centred);

        // Quick-access panel buttons with icons — shared control-state language
        auto mouse = getMouseXYRelative().toFloat();
        // FX Chain / Side Panel icon — chain links
        {
            auto r = fxBtnBounds_.toFloat();
            bool hov = r.contains(mouse);
            g.setColour(hov ? a.color.violet.withAlpha(a.state.selectedStrength)
                            : a.color.panelB.withAlpha(0.45f));
            g.fillRoundedRectangle(r, a.metric.radiusControl);
            g.setColour(hov ? a.color.violet.withAlpha(0.7f)
                            : a.color.borderSoftA.withAlpha(0.6f));
            g.drawRoundedRectangle(r, a.metric.radiusControl, a.metric.strokeThin);
            g.setColour(hov ? a.color.violetBright
                            : a.color.textSecondary.withAlpha(0.8f));
            auto c = r.getCentre();
            // Chain-link icon: two interlocked rounded rects
            g.drawRoundedRectangle(c.x - 7.f, c.y - 3.f, 9.f, 6.f, 2.f, 1.6f);
            g.drawRoundedRectangle(c.x - 2.f, c.y - 3.f, 9.f, 6.f, 2.f, 1.6f);
        }
        // FX Browser icon — magnifying glass
        {
            auto r = brBtnBounds_.toFloat();
            bool hov = r.contains(mouse);
            g.setColour(hov ? a.color.violet.withAlpha(a.state.selectedStrength)
                            : a.color.panelB.withAlpha(0.45f));
            g.fillRoundedRectangle(r, a.metric.radiusControl);
            g.setColour(hov ? a.color.violet.withAlpha(0.7f)
                            : a.color.borderSoftA.withAlpha(0.6f));
            g.drawRoundedRectangle(r, a.metric.radiusControl, a.metric.strokeThin);
            g.setColour(hov ? a.color.violetBright
                            : a.color.textSecondary.withAlpha(0.8f));
            auto c = r.getCentre();
            // Magnifying glass: circle + handle line
            g.drawEllipse(c.x - 5.f, c.y - 5.f, 8.f, 8.f, 1.6f);
            g.drawLine(c.x + 2.f, c.y + 2.f, c.x + 5.5f, c.y + 5.5f, 1.6f);
        }
    }

    void resized() override
    {
        const int totalW = getWidth();
        const int totalH = getHeight();
        const int padX   = 8;
        const int padY   = 6;

        // ── Right-side block: [Key] [BPM] [Position] ────────────────────────
        // These live bottom-right per requested layout.
        constexpr int keyW   = 100;
        constexpr int tempoW = 100;
        constexpr int posW   = 104;
        constexpr int sepW   =   6;

        int rx = totalW - padX;
        const int blockH = totalH - padY * 2;
        const int y = totalH - padY - blockH;

        auto makeR = [&](int w) -> juce::Rectangle<int>
        {
            rx -= w;
            auto r = juce::Rectangle<int>(rx, y, w, blockH);
            rx -= sepW;
            return r;
        };

        // Bottom-right placement (stacked to the right, aligned to bottom)
        posReadoutBounds_ = makeR(posW);
        tempo_.setBounds  (makeR(tempoW));
        projectKey_.setBounds(makeR(keyW));
        projectKey_.setVisible(true);
        tempo_.setVisible(true);

        // ── Left-side block: transport buttons (bottom-left) ────────────────
        auto b = juce::Rectangle<int>(padX, y, rx - padX, blockH);

        // Bottom-left: show ALL transport controls as requested.
        for (int i = 0; i < (int)buttons_.size(); ++i)
        {
            auto* btn = buttons_[(size_t)i];
            if (btn == nullptr)
                continue;

            btn->setVisible(true);
            btn->setBounds(b.removeFromLeft(34));
            b.removeFromLeft(3);
        }

        if (clickStrip_)
        {
            b.removeFromLeft(6);
            clickStrip_->setBounds(b.removeFromLeft(132));
        }

        b.removeFromLeft(8);
        fxBtnBounds_ = b.removeFromLeft(26).reduced(0, 3);
        b.removeFromLeft(4);
        brBtnBounds_ = b.removeFromLeft(26).reduced(0, 3);
        b.removeFromLeft(6);
        automArmBtn_.setBounds(b.removeFromLeft(50).reduced(0, 3));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (fxBtnBounds_.toFloat().contains(e.position))
        {
            DBG("[TransportBar] Quick access Side Panel clicked");
            if (onQuickAccessSidePanel) onQuickAccessSidePanel();
            return;
        }
        if (brBtnBounds_.toFloat().contains(e.position))
        {
            DBG("[TransportBar] Quick access FX Browser clicked");
            if (onQuickAccessFxBrowser) onQuickAccessFxBrowser();
            return;
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (fxBtnBounds_.toFloat().contains(e.position) || brBtnBounds_.toFloat().contains(e.position))
        {
            setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::PointingHandCursor));
            repaint();
        }
        else
        {
            setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::NormalCursor));
        }

        // Update tooltip for transport buttons
        juce::String tip;
        for (int i = 0; i < (int)buttons_.size(); ++i)
        {
            auto* btn = buttons_[(size_t)i];
            if (btn != nullptr && btn->getBounds().contains(e.getPosition()))
            {
                switch (btn->getIcon())
                {
                case TransportIcon::SkipBack:  tip = "Skip to Start\nJump the playhead to the beginning of the project."; break;
                case TransportIcon::Play:      tip = transport_.isPlaying() ? "Stop\nStop playback and return playhead to start (if return mode is on)." : "Play\nStart playback from the current playhead position."; break;
                case TransportIcon::Stop:      tip = "Stop\nStop playback or recording. Playhead returns to start if return mode is on."; break;
                case TransportIcon::Record:    tip = "Record\nStart recording on all armed tracks. Press again to stop recording."; break;
                case TransportIcon::Loop:      tip = "Loop\nToggle loop mode. When enabled, playback loops between the left and right locators."; break;
                case TransportIcon::Return:    tip = "Return to Start\nToggle return-to-start. When enabled, the playhead returns to its last start position on stop."; break;
                default: break;
                }
                break;
            }
        }

        if (tip.isEmpty() && fxBtnBounds_.toFloat().contains(e.position))
            tip = "FX Mixer\nOpen the mixer panel to add, remove, and reorder audio effects on this track.";
        else if (tip.isEmpty() && brBtnBounds_.toFloat().contains(e.position))
            tip = "FX Browser\nBrowse and search available plugins by name, category, or manufacturer.";
        else if (tip.isEmpty() && automArmBtn_.getBounds().contains(e.getPosition()))
            tip = "Automation Record Arm\nArm all tracks for automation recording. Parameter changes will be captured as Touch-mode data.";

        transportTooltip_ = tip;
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        transportTooltip_ = {};
        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::NormalCursor));
    }

    juce::String getTooltip() override { return transportTooltip_; }

    void timerCallback() override
    {
        if (playBtn_)  playBtn_->setActive(transport_.isPlaying());
        if (recBtn_)   recBtn_->setActive(transport_.isRecording());
        if (loopBtn_)  loopBtn_->setActive(transport_.isLooping());
        if (returnBtn_) returnBtn_->setActive(transport_.shouldReturnToLastPosition());
        repaint(posReadoutBounds_);
    }

private:
    TransportController& transport_;
    TempoDisplay         tempo_;
    ProjectKeyDisplay    projectKey_;
    juce::OwnedArray<TransportButton> owned_;
    std::vector<TransportButton*>     buttons_;
    std::unique_ptr<ClickToolbarStrip> clickStrip_;
    TransportButton* playBtn_  = nullptr;
    TransportButton* recBtn_   = nullptr;
    TransportButton* loopBtn_  = nullptr;
    TransportButton* returnBtn_ = nullptr;
    juce::Rectangle<int> posReadoutBounds_;
    juce::Rectangle<int> fxBtnBounds_;
    juce::Rectangle<int> brBtnBounds_;
    juce::TextButton     automArmBtn_;
    juce::String         transportTooltip_;

    TransportButton* addBtn(const juce::String& lbl, juce::Colour col,
                            TransportIcon icon,
                            std::function<void()> cb)
    {
        auto* b  = new TransportButton(lbl, col, icon);
        b->onClick = std::move(cb);
        addAndMakeVisible(b);
        buttons_.push_back(b);
        owned_.add(b);
        return b;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace DAW
