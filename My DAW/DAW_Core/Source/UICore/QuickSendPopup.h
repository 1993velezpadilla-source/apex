#pragma once
#include <JuceHeader.h>
#include <functional>

namespace DAW
{
    /**
     * Quick Send mode floating popup.
     *
     * Shown while Quick Send mode is active, positioned near the source row.
     * Displays the source track name and — when the currently selected track
     * has a send from the source — a send level slider, an Active toggle and a
     * Delete button. All mutations are delegated to MainComponent through
     * callbacks (which route through BubblegumV2System's undo-wrapped API).
     *
     * Header-only so no project file changes are required.
     */
    class QuickSendPopup : public juce::Component
    {
    public:
        std::function<juce::String()> onGetSourceName;
        std::function<DAW::TrackID()> onGetTargetId;
        std::function<bool()>         onHasSendForTarget;
        std::function<bool()>         onIsSendActiveForTarget;
        std::function<float()>        onGetSendLevel;
        std::function<void(float)>    onSetSendLevel;
        std::function<void()>         onToggleActive;
        std::function<void()>         onDeleteSend;
        std::function<void()>         onExit;

        QuickSendPopup()
        {
            setOpaque(false);
            setInterceptsMouseClicks(true, true);

            levelSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
            levelSlider_.setRange(0.0, 1.0, 0.01);
            levelSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            levelSlider_.onValueChange = [this]
            {
                if (onSetSendLevel) onSetSendLevel((float) levelSlider_.getValue());
            };
            addAndMakeVisible(levelSlider_);

            activeBtn_.setButtonText("Active");
            activeBtn_.setClickingTogglesState(true);
            activeBtn_.onClick = [this]
            {
                if (onToggleActive) onToggleActive();
            };
            addAndMakeVisible(activeBtn_);

            deleteBtn_.setButtonText("Delete");
            deleteBtn_.onClick = [this]
            {
                if (onDeleteSend) onDeleteSend();
            };
            addAndMakeVisible(deleteBtn_);

            exitBtn_.setButtonText("Exit");
            exitBtn_.onClick = [this]
            {
                if (onExit) onExit();
            };
            addAndMakeVisible(exitBtn_);
        }

        void refresh()
        {
            const bool hasSend = onHasSendForTarget ? onHasSendForTarget() : false;
            const float level  = onGetSendLevel ? onGetSendLevel() : 0.0f;
            const bool active  = onIsSendActiveForTarget ? onIsSendActiveForTarget() : false;

            levelSlider_.setVisible(hasSend);
            activeBtn_.setVisible(hasSend);
            deleteBtn_.setVisible(hasSend);

            if (hasSend)
            {
                levelSlider_.setValue(level, juce::dontSendNotification);
                activeBtn_.setToggleState(active, juce::dontSendNotification);
            }
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();

            g.setColour(juce::Colour(0xF0101018));
            g.fillRoundedRectangle(bounds, 6.f);

            const auto goldCol = juce::Colour(0xFFFFC94D);
            g.setColour(goldCol.withAlpha(0.85f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), 6.f, 1.2f);

            // Header
            juce::String sourceName = onGetSourceName ? onGetSourceName() : juce::String("?");
            g.setColour(goldCol.withAlpha(0.95f));
            g.setFont(Theme::getInstance().fonts.bold.withHeight(11.f));
            g.drawText("QUICK SEND  \xE2\x86\x92  " + sourceName,
                       juce::Rectangle<float>(10.f, 6.f, (float) getWidth() - 20.f, 18.f),
                       juce::Justification::centredLeft, false);

            g.setColour(juce::Colours::white.withAlpha(0.75f));
            g.setFont(Theme::getInstance().fonts.regular.withHeight(10.f));
            g.drawText("Click a track to create a send  \xC2\xB7  Esc to exit",
                       juce::Rectangle<float>(10.f, 24.f, (float) getWidth() - 20.f, 14.f),
                       juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced(10, 42);
            if (b.getHeight() <= 0)
                return;

            const int btnW = 52;
            const int gap  = 6;
            exitBtn_.setBounds(b.removeFromRight(btnW));
            b.removeFromRight(gap);
            deleteBtn_.setBounds(b.removeFromRight(btnW));
            b.removeFromRight(gap);
            activeBtn_.setBounds(b.removeFromRight(btnW));
            b.removeFromRight(gap);
            levelSlider_.setBounds(b);
        }

    private:
        juce::Slider     levelSlider_;
        juce::TextButton activeBtn_;
        juce::TextButton deleteBtn_;
        juce::TextButton exitBtn_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickSendPopup)
    };
}