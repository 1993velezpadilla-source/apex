#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "PluginScannerCore.h"

namespace DAW {

/**
 * PluginScanStartupDialog
 *
 * Nucleus: a modal-style overlay shown on launch that scans for
 * VST3 / VST2 plugins. Displays progress bar and current plugin name.
 * Stays open until user clicks the close button.
 */
class PluginScanStartupDialog : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onScanComplete;

    explicit PluginScanStartupDialog(PluginScannerCore& scanner)
        : scanner_(scanner)
    {
        setOpaque(false);
        setAlwaysOnTop(true);

        closeBtn_.setButtonText("x");
        closeBtn_.onClick = [this]
        {
            cachedAutoCloseDeadlineMs_ = 0.0;
            setVisible(false);
            stopTimer();
            if (onScanComplete)
                onScanComplete();
        };
        addAndMakeVisible(closeBtn_);

        rescanBtn_.setButtonText("Rescan All");
        rescanBtn_.onClick = [this]
        {
            startScan(true);
        };
        addAndMakeVisible(rescanBtn_);
    }

    void resized() override
    {
        auto box = getLocalBounds().withSizeKeepingCentre(420, 180);
        closeBtn_.setBounds(box.getRight() - 28, box.getY() + 8, 20, 20);
        rescanBtn_.setBounds(box.getX() + 20, box.getBottom() - 36, 90, 24);
    }

    void showCachedLoadAndAutoClose()
    {
        progress_ = 1.0f;
        visiblePluginCount_ = scanner_.getKnownPlugins().getNumTypes();
        visibleFailureCount_ = scanner_.getFailureStore().getCount();
        visibleCacheCount_ = scanner_.getCache().getCount();
        currentPlugin_ = "Loaded cached plugins";
        cachedAutoCloseDeadlineMs_ = juce::Time::getMillisecondCounterHiRes() + 750.0;
        startTimerHz(15);
        setVisible(true);
    }

void startScan(bool forceFull = false)
    {
        // If a scan is already running, its progress + completion callbacks are
        // already wired to this dialog. Do NOT wipe shared scan state mid-scan or
        // strand the UI; just make sure the dialog is visible and repainting.
        if (scanner_.isScanning())
        {
            cachedAutoCloseDeadlineMs_ = 0.0;
            startTimerHz(15);
            setVisible(true);
            return;
        }

        cachedAutoCloseDeadlineMs_ = 0.0;
        progress_ = 0.f;
        currentPlugin_ = "Starting...";
        visiblePluginCount_ = scanner_.getKnownPlugins().getNumTypes();
        visibleFailureCount_ = scanner_.getFailureStore().getCount();
        visibleCacheCount_ = scanner_.getCache().getCount();
        startTimerHz(15);
        setVisible(true);

        scanner_.addDefaultSearchPaths();
        auto shouldRunCleanRescan = forceFull
            || (scanner_.getCache().getCount() == 0
                && scanner_.getFailureStore().getCount() == 0);

        if (forceFull)
            scanner_.clearPluginList();

        PluginScanAuditLogCore::appendStartupTrace(
            "PluginScanStartupDialog::startScan",
            shouldRunCleanRescan ? "clean" : "normal",
            scanner_.getCache().getCount(),
            scanner_.getFailureStore().getCount(),
            "knownPlugins=" + juce::String(scanner_.getKnownPlugins().getNumTypes()));

        // Use SafePointer so async callbacks don't crash if dialog is deleted
        juce::Component::SafePointer<PluginScanStartupDialog> safeThis(this);

        auto progressCallback = [safeThis](float p, const juce::String& name)
        {
            if (safeThis != nullptr)
            {
                safeThis->progress_ = p;
                safeThis->currentPlugin_ = name;
            }
        };

        auto doneCallback = [safeThis]()
        {
            if (safeThis == nullptr) return;
            safeThis->progress_ = 1.f;
            safeThis->currentPlugin_ = "Complete!";
            safeThis->visiblePluginCount_ = safeThis->scanner_.getKnownPlugins().getNumTypes();
            safeThis->visibleFailureCount_ = safeThis->scanner_.getFailureStore().getCount();
            safeThis->visibleCacheCount_ = safeThis->scanner_.getCache().getCount();
            safeThis->stopTimer();
            if (safeThis->onScanComplete)
                safeThis->onScanComplete();
        };

        if (shouldRunCleanRescan)
            scanner_.cleanFullRescan(progressCallback, doneCallback);
        else
            scanner_.startScan(progressCallback, doneCallback);
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();

        // Semi-transparent backdrop
        g.fillAll(juce::Colours::black.withAlpha(0.6f));

        // Dialog box centred
        auto box = getLocalBounds().withSizeKeepingCentre(420, 180).toFloat();
        g.setColour(t.colors.surface);
        g.fillRoundedRectangle(box, 10.f);
        g.setColour(t.colors.border.withAlpha(0.5f));
        g.drawRoundedRectangle(box, 10.f, 1.5f);

        auto inner = box.reduced(20.f);

        // Title
        g.setFont(juce::Font(14.f, juce::Font::bold));
        g.setColour(t.colors.text);
        g.drawText("Plugin Scan", inner.removeFromTop(24.f), juce::Justification::centredLeft);
        inner.removeFromTop(8.f);

        // Progress bar
        auto barArea = inner.removeFromTop(14.f);
        if (progress_ >= 1.f)
        {
            // Done — show filled bar with green accent
            g.setColour(t.colors.accent);
            g.fillRoundedRectangle(barArea, 5.f);
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.setFont(juce::Font(10.f, juce::Font::bold));
            g.drawText("DONE", barArea, juce::Justification::centred);
        }
        else
        {
            g.setColour(t.colors.backgroundDark);
            g.fillRoundedRectangle(barArea, 5.f);
            if (progress_ > 0.f)
            {
                auto filled = barArea.withWidth(barArea.getWidth() * progress_);
                g.setColour(t.colors.accent);
                g.fillRoundedRectangle(filled, 5.f);
            }
        }
        inner.removeFromTop(8.f);

        // Current plugin name
        g.setFont(juce::Font(10.f));
        g.setColour(t.colors.textSecondary);
        auto statusText = currentPlugin_;
        if (progress_ >= 1.f && currentPlugin_ == "Loaded cached plugins")
            statusText = "Finished - close when ready";
        g.drawText(statusText, inner.removeFromTop(16.f), juce::Justification::centredLeft, true);

        // Plugin count + safety info
        int count      = visiblePluginCount_;
        int failureN   = visibleFailureCount_;
        int cachedN    = visibleCacheCount_;

        g.setColour(t.colors.textSecondary.withAlpha(0.6f));
        juce::String info = juce::String(count) + " plugins";
        if (cachedN > 0) info += "  |  " + juce::String(cachedN) + " cached";
        if (failureN > 0) info += "  |  " + juce::String(failureN) + " failures";
        g.drawText(info, inner.removeFromTop(14.f), juce::Justification::centredLeft);

        // Diagnostic hint for failed scans
        if (failureN > 0)
        {
            g.setColour(t.colors.textSecondary.withAlpha(0.4f));
            g.setFont(juce::Font(9.f));
            auto logsPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("DAW_Core").getFullPathName();
            g.drawText("Check logs: " + logsPath, inner, juce::Justification::centredLeft);
        }
    }

    bool hitTest(int, int) override { return true; }

private:
    PluginScannerCore& scanner_;
    juce::TextButton closeBtn_;
    juce::TextButton rescanBtn_;
    float progress_  = 0.f;
    juce::String currentPlugin_;
    int visiblePluginCount_ = 0;
    int visibleFailureCount_ = 0;
    int visibleCacheCount_ = 0;
    double cachedAutoCloseDeadlineMs_ = 0.0;

    void timerCallback() override
    {
        repaint();

        if (cachedAutoCloseDeadlineMs_ <= 0.0
            || juce::Time::getMillisecondCounterHiRes() < cachedAutoCloseDeadlineMs_)
            return;

        cachedAutoCloseDeadlineMs_ = 0.0;
        stopTimer();
        setVisible(false);
        if (onScanComplete)
            onScanComplete();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanStartupDialog)
};

} // namespace DAW
