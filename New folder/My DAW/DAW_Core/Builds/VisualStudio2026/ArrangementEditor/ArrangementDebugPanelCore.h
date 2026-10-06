#pragma once

#include <JuceHeader.h>

namespace ArrangementEditor
{
    class ArrangementDebugPanelCore : public juce::Component,
                                      private juce::Timer
    {
    public:
        ArrangementDebugPanelCore()
            : m_copyButton("Copy Debug Log")
            , m_clearButton("Clear")
        {
            addAndMakeVisible(m_copyButton);
            addAndMakeVisible(m_clearButton);
            addAndMakeVisible(m_text);

            m_text.setMultiLine(true);
            m_text.setReadOnly(true);
            m_text.setScrollbarsShown(true);
            m_text.setCaretVisible(false);
            m_text.setPopupMenuEnabled(true);
            m_text.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            m_text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            m_text.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.96f));
            m_text.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));

            m_copyButton.onClick = [this]()
            {
                if (onCopy)
                    onCopy();
            };

            m_clearButton.onClick = [this]()
            {
                if (onClear)
                    onClear();
                refreshNow();
            };

            startTimerHz(8);
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();

            g.setColour(juce::Colours::black.withAlpha(0.84f));
            g.fillRoundedRectangle(bounds, 8.0f);

            g.setColour(juce::Colours::hotpink.withAlpha(0.85f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.drawText("Arrangement Debug Panel", 12, 8, 220, 20, juce::Justification::centredLeft, false);

            g.setColour(juce::Colours::limegreen.withAlpha(0.9f));
            g.fillEllipse(252.0f, 13.0f, 8.0f, 8.0f);
            g.setColour(juce::Colours::hotpink.withAlpha(0.9f));
            g.fillEllipse(266.0f, 13.0f, 8.0f, 8.0f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(10);
            auto top = area.removeFromTop(28);
            m_clearButton.setBounds(top.removeFromRight(64));
            top.removeFromRight(6);
            m_copyButton.setBounds(top.removeFromRight(130));
            area.removeFromTop(4);
            m_text.setBounds(area);
        }

        void refreshNow()
        {
            if (onRequestText)
            {
                const auto newText = onRequestText();
                if (newText != m_cachedText)
                {
                    m_cachedText = newText;
                    m_text.setText(m_cachedText, false);
                }
            }
        }

        std::function<juce::String()> onRequestText;
        std::function<void()> onCopy;
        std::function<void()> onClear;

    private:
        void timerCallback() override
        {
            if (isVisible())
                refreshNow();
        }

        juce::TextButton m_copyButton;
        juce::TextButton m_clearButton;
        juce::TextEditor m_text;
        juce::String m_cachedText;
    };
}
