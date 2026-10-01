#pragma once
#include <JuceHeader.h>

namespace DAW {

class QuitSafetyDialog : public juce::Component
{
public:
	struct Callbacks
	{
		std::function<void()> onSaveAndQuit;
		std::function<void()> onQuitWithoutSaving;
		std::function<void()> onCancel;
	} callbacks;

	QuitSafetyDialog()
	{
		setInterceptsMouseClicks(true, true);

		title_.setText("Save project before closing?", juce::dontSendNotification);
		title_.setJustificationType(juce::Justification::centred);
		title_.setColour(juce::Label::textColourId, juce::Colours::white);
		title_.setInterceptsMouseClicks(false, false);
		addAndMakeVisible(title_);

		saveBtn_.setButtonText("Save Project");
		cancelBtn_.setButtonText("Cancel");
		dontSaveBtn_.setButtonText("Don't Save");

		auto style = [](juce::TextButton& b, juce::Colour c)
		{
			b.setColour(juce::TextButton::buttonColourId, c.withAlpha(0.9f));
			b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
		};

		style(saveBtn_, juce::Colour(0xFF6A4C93));
		style(cancelBtn_, juce::Colour(0xFF2A2C36));
		style(dontSaveBtn_, juce::Colour(0xFFB00020));

		saveBtn_.onClick = [this] { if (callbacks.onSaveAndQuit) callbacks.onSaveAndQuit(); };
		cancelBtn_.onClick = [this] { if (callbacks.onCancel) callbacks.onCancel(); };
		dontSaveBtn_.onClick = [this] { if (callbacks.onQuitWithoutSaving) callbacks.onQuitWithoutSaving(); };

		addAndMakeVisible(saveBtn_);
		addAndMakeVisible(cancelBtn_);
		addAndMakeVisible(dontSaveBtn_);
	}

	void paint(juce::Graphics& g) override
	{
		auto b = getLocalBounds().toFloat();
		g.setColour(juce::Colours::black.withAlpha(0.72f));
		g.fillRect(b);

		auto panel = b.reduced(0.0f).withSizeKeepingCentre(360.0f, 160.0f);
		g.setColour(juce::Colour(0xFF101018).withAlpha(0.95f));
		g.fillRoundedRectangle(panel, 12.0f);
		g.setColour(juce::Colours::white.withAlpha(0.12f));
		g.drawRoundedRectangle(panel, 12.0f, 1.0f);
	}

	void resized() override
	{
		auto area = getLocalBounds();
		auto panel = area.withSizeKeepingCentre(360, 160);

		title_.setBounds(panel.removeFromTop(54).reduced(14, 14));
		panel.removeFromTop(4);

		auto btnRow = panel.removeFromBottom(54).reduced(14, 10);
		const int gap = 10;
		const int w = (btnRow.getWidth() - gap * 2) / 3;
		saveBtn_.setBounds(btnRow.removeFromLeft(w));
		btnRow.removeFromLeft(gap);
		cancelBtn_.setBounds(btnRow.removeFromLeft(w));
		btnRow.removeFromLeft(gap);
		dontSaveBtn_.setBounds(btnRow);
	}

private:
	juce::Label title_;
	juce::TextButton saveBtn_;
	juce::TextButton cancelBtn_;
	juce::TextButton dontSaveBtn_;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuitSafetyDialog)
};

} // namespace DAW
