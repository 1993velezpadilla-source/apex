#pragma once
#include <JuceHeader.h>
#include "ExportRenderCore.h"

namespace DAW {

/**
 * ExportProgressWindow — modal export progress UI.
 *
 * Shows filename, progress bar, elapsed time, ETA, and a Cancel button.
 * Lives on the message thread. ExportRenderCore posts progress via
 * MessageManager::callAsync so this is always updated safely.
 *
 * Usage:
 *   auto* w = new ExportProgressWindow(core, outputFile, parentComponent);
 *   // window self-destructs when export finishes or is cancelled.
 */
class ExportProgressWindow final : public juce::Component,
                                   private juce::Timer
{
public:
    ExportProgressWindow(ExportRenderCore& core,
                         const juce::File& outputFile,
                         juce::Component* parent)
        : core_(core), outputFile_(outputFile)
    {
        setSize(480, 180);

        addAndMakeVisible(titleLabel_);
        titleLabel_.setText("Exporting Audio", juce::dontSendNotification);
        titleLabel_.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        titleLabel_.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(fileLabel_);
        fileLabel_.setText(outputFile_.getFileName(), juce::dontSendNotification);
        fileLabel_.setFont(juce::FontOptions(12.0f));
        fileLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
        fileLabel_.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(progressBar_);
        progressBar_.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xff8855ff));
        progressBar_.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff333333));

        addAndMakeVisible(timeLabel_);
        timeLabel_.setFont(juce::FontOptions(12.0f));
        timeLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
        timeLabel_.setJustificationType(juce::Justification::centredLeft);
        timeLabel_.setText("Elapsed: 0:00   ETA: --:--", juce::dontSendNotification);

        addAndMakeVisible(cancelButton_);
        cancelButton_.setButtonText("Cancel");
        cancelButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3a3a3a));
        cancelButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        cancelButton_.onClick = [this] { onCancelClicked(); };

        startTimerHz(10); // poll at 10 Hz — smooth enough, low CPU

        startTime_ = juce::Time::getCurrentTime();

        // Show inside a DialogWindow owned by the parent
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setNonOwned(this);
        opts.dialogTitle = "Exporting...";
        opts.dialogBackgroundColour = juce::Colour(0xff1e1e1e);
        opts.escapeKeyTriggersCloseButton = false;
        opts.useNativeTitleBar = false;
        opts.resizable = false;
        opts.componentToCentreAround = parent;
        dialogWindow_ = opts.launchAsync();
    }

    ~ExportProgressWindow() override
    {
        stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1e1e1e));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(20);
        titleLabel_.setBounds(area.removeFromTop(28));
        area.removeFromTop(4);
        fileLabel_.setBounds(area.removeFromTop(18));
        area.removeFromTop(14);
        progressBar_.setBounds(area.removeFromTop(22));
        area.removeFromTop(8);
        timeLabel_.setBounds(area.removeFromTop(18));
        area.removeFromTop(14);
        cancelButton_.setBounds(area.removeFromRight(100).removeFromTop(32));
    }

    // Called by ExportRenderCore's ProgressCallback (message thread).
    void setProgress(float p)
    {
        progress_ = (double) juce::jlimit(0.0f, 1.0f, p);
    }

    // Called when export finishes (success or failure).
    void exportFinished(const ExportRenderCore::Result& result)
    {
        stopTimer();
        finished_ = true;

        if (dialogWindow_ != nullptr)
            dialogWindow_->exitModalState(0);

        if (!result.cancelled)
        {
            juce::AlertWindow::showMessageBoxAsync(
                result.success ? juce::MessageBoxIconType::InfoIcon
                               : juce::MessageBoxIconType::WarningIcon,
                result.success ? "Export Complete" : "Export Failed",
                result.success
                    ? "Saved to:\n" + outputFile_.getFullPathName()
                    : result.message);
        }

        juce::MessageManager::callAsync([this] { delete this; });
    }

private:
    void timerCallback() override
    {
        if (!core_.isExportRunning() && !finished_)
        {
            // Export ended before completion callback fired — close cleanly.
            stopTimer();
            if (dialogWindow_ != nullptr)
                dialogWindow_->exitModalState(0);
            juce::MessageManager::callAsync([this] { delete this; });
            return;
        }

        // Update progress bar
        // progressBar_ reads progress_ by reference — just repaint to reflect update
        progressBar_.repaint();

        // Update time labels
        auto elapsed = juce::Time::getCurrentTime() - startTime_;
        const int elapsedSec = (int) elapsed.inSeconds();
        juce::String elapsedStr = formatTime(elapsedSec);

        juce::String etaStr = "--:--";
        if (progress_ > 0.01)
        {
            const double totalSec = elapsed.inSeconds() / progress_;
            const int remainingSec = juce::jmax(0, (int)(totalSec - elapsed.inSeconds()));
            etaStr = formatTime(remainingSec);
        }

        timeLabel_.setText("Elapsed: " + elapsedStr + "   ETA: " + etaStr,
                           juce::dontSendNotification);
    }

    void onCancelClicked()
    {
        cancelButton_.setEnabled(false);
        cancelButton_.setButtonText("Cancelling...");
        core_.requestCancel();
    }

    static juce::String formatTime(int totalSeconds)
    {
        const int m = totalSeconds / 60;
        const int s = totalSeconds % 60;
        return juce::String(m) + ":" + juce::String(s).paddedLeft('0', 2);
    }

    ExportRenderCore&            core_;
    juce::File                   outputFile_;
    juce::Label                  titleLabel_, fileLabel_, timeLabel_;
    juce::ProgressBar            progressBar_ { progress_ };
    juce::TextButton             cancelButton_;
    juce::Time                   startTime_;
    double                       progress_ = 0.0;
    bool                         finished_ = false;
    juce::Component::SafePointer<juce::DialogWindow> dialogWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportProgressWindow)
};

} // namespace DAW
