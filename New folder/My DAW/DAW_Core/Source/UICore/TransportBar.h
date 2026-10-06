#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../ControlsCore/ModernControls.h"
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
        auto b  = getLocalBounds().toFloat().reduced(2.f, 4.f);

        g.setColour(juce::Colour(0xff080808));
        g.fillRoundedRectangle(b, 4.f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(b, 4.f, 1.f);

        // BPM value — LED monospaced
        auto valArea = b;
        juce::Rectangle<float> labelArea;
        if (showLabel_)
        {
            valArea = b.withTrimmedRight(38.f);
            labelArea = b.removeFromRight(36.f);
        }

        auto tempStr = juce::String(transport_.getTempo(), 1);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 18.f, juce::Font::bold));
        g.setColour(juce::Colour(0xff00d4ff).withAlpha(0.18f));
        g.drawText(tempStr, valArea.translated(0, 1), juce::Justification::centredRight);
        g.setColour(juce::Colour(0xff00d4ff));
        g.drawText(tempStr, valArea, juce::Justification::centredRight);

        if (showLabel_)
        {
            g.setFont(t.fonts.small);
            g.setColour(t.colors.textSecondary);
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
        auto b = getLocalBounds().toFloat().reduced(2.f, 4.f);

        g.setColour(juce::Colour(0xff080808));
        g.fillRoundedRectangle(b, 4.f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(b, 4.f, 1.f);
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
                     public juce::Timer
{
public:
    /** Quick-access callbacks for panels — wired from MainComponent */
    std::function<void()> onQuickAccessSidePanel;
    std::function<void()> onQuickAccessFxBrowser;

    /** Fired when the user picks a new project key. Wired from MainComponent. */
    std::function<void(const juce::String&)> onProjectKeyChanged;

    /** Programmatically set the displayed project key (no callback fired). */
    void setProjectKeyText(const juce::String& keyText)
    {
        projectKey_.setSelectedKeyText(keyText);
    }

    explicit TransportBar(TransportController& transport, ClickStateModel* clickState = nullptr)
        : transport_(transport), tempo_(transport)
    {
        auto& t = Theme::getInstance();
        addBtn("<<", t.colors.textSecondary,  TransportIcon::SkipBack,
               [this]{ ActionManager::getInstance().dispatch(ActionID::TransportJumpStart); });
        playBtn_  = addBtn(">",  t.colors.transportPlay,   TransportIcon::Play,
               []{ ActionManager::getInstance().dispatch(ActionID::TransportPlayStop); });
        addBtn("[]", t.colors.transportStop,  TransportIcon::Stop,
               []{ ActionManager::getInstance().dispatch(ActionID::TransportStop); });
        recBtn_   = addBtn("O",  t.colors.transportRecord, TransportIcon::Record,
               []{ ActionManager::getInstance().dispatch(ActionID::TransportRecord); });
        loopBtn_  = addBtn("~",  t.colors.accent,          TransportIcon::Loop,
               []{ ActionManager::getInstance().dispatch(ActionID::TransportToggleLoop); });
        returnBtn_ = addBtn(juce::String(juce::CharPointer_UTF8("↩")),  juce::Colour(0xFF00D4FF), TransportIcon::Return,
               [this]{ transport_.setReturnToLastPosition(!transport_.shouldReturnToLastPosition()); });
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
        automArmBtn_.setColour (juce::TextButton::buttonColourId,    juce::Colour (0xFF1A1A1A));
        automArmBtn_.setColour (juce::TextButton::buttonOnColourId,  juce::Colour (0xFFCC2222));
        automArmBtn_.setColour (juce::TextButton::textColourOffId,   juce::Colour (0xFF999999));
        automArmBtn_.setColour (juce::TextButton::textColourOnId,    juce::Colour (0xFFFFFFFF));
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
        startTimer(80);
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto  b = getLocalBounds().toFloat();

        // Subtle top→bottom gradient (avoids flat look)
        juce::ColourGradient bg(t.colors.backgroundDark,       0, 0,
                                t.colors.background.darker(0.1f), 0, b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRect(b);

        // Border/separator — thicker for stronger presence
        g.setColour(t.colors.border);
        g.fillRect(0.f, b.getBottom() - 2.f, b.getWidth(), 2.f);

        // Position readout
        auto tb = posReadoutBounds_.toFloat().reduced(0, 4);
        g.setColour(juce::Colour(0xff080808));
        g.fillRoundedRectangle(tb, 4.f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(tb, 4.f, 1.f);

        double pos  = transport_.getPositionBarBeat();
        int    bar  = (int)pos + 1;
        int    beat = (int)((pos - (int)pos) * 4.0) + 1;

        // Shadow pass for LED text
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 15.f, juce::Font::bold));
        g.setColour(juce::Colour(0xffffff00).withAlpha(0.18f));
        auto posStr = juce::String::formatted("%03d : %d", bar, beat);
        g.drawText(posStr, tb.translated(0, 1), juce::Justification::centred);
        g.setColour(juce::Colour(0xffffff00));
        g.drawText(posStr, tb, juce::Justification::centred);

        // Quick-access panel buttons with icons
        auto mouse = getMouseXYRelative().toFloat();
        // FX Chain / Side Panel icon — chain links
        {
            auto r = fxBtnBounds_.toFloat();
            bool hov = r.contains(mouse);
            g.setColour(hov ? t.colors.accent.withAlpha(0.25f) : t.colors.surface.withAlpha(0.35f));
            g.fillRoundedRectangle(r, 4.f);
            g.setColour(hov ? t.colors.accent.withAlpha(0.6f) : t.colors.border.withAlpha(0.4f));
            g.drawRoundedRectangle(r, 4.f, 1.f);
            g.setColour(hov ? t.colors.accent : t.colors.textSecondary.withAlpha(0.7f));
            auto c = r.getCentre();
            // Chain-link icon: two interlocked rounded rects
            g.drawRoundedRectangle(c.x - 7.f, c.y - 3.f, 9.f, 6.f, 2.f, 1.6f);
            g.drawRoundedRectangle(c.x - 2.f, c.y - 3.f, 9.f, 6.f, 2.f, 1.6f);
        }
        // FX Browser icon — magnifying glass
        {
            auto r = brBtnBounds_.toFloat();
            bool hov = r.contains(mouse);
            g.setColour(hov ? t.colors.accent.withAlpha(0.25f) : t.colors.surface.withAlpha(0.35f));
            g.fillRoundedRectangle(r, 4.f);
            g.setColour(hov ? t.colors.accent.withAlpha(0.6f) : t.colors.border.withAlpha(0.4f));
            g.drawRoundedRectangle(r, 4.f, 1.f);
            g.setColour(hov ? t.colors.accent : t.colors.textSecondary.withAlpha(0.7f));
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
        constexpr int posW   =  88;
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
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            repaint();
            return;
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

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
