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
 * Auto-dismisses when scan completes.
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
            setVisible(false);
            stopTimer();
            if (onScanComplete)
                onScanComplete();
        };
        addAndMakeVisible(closeBtn_);
    }

    void resized() override
    {
        auto box = getLocalBounds().withSizeKeepingCentre(420, 160);
        closeBtn_.setBounds(box.getRight() - 28, box.getY() + 8, 20, 20);
    }

    void showCachedLoadAndAutoClose()
    {
        progress_ = 1.0f;
        visiblePluginCount_ = scanner_.getKnownPlugins().getNumTypes();
        visibleFailureCount_ = scanner_.getFailureStore().getCount();
        visibleCacheCount_ = scanner_.getCache().getCount();
        currentPlugin_ = "Loaded cached plugins";
        startTimerHz(15);
        setVisible(true);

        juce::Component::SafePointer<PluginScanStartupDialog> safeThis(this);
        juce::Timer::callAfterDelay(5000, [safeThis]()
        {
            if (safeThis == nullptr) return;
            safeThis->setVisible(false);
            safeThis->stopTimer();
            if (safeThis->onScanComplete)
                safeThis->onScanComplete();
        });
    }

    void startScan()
    {
        progress_ = 0.f;
        currentPlugin_ = "Starting...";
        visiblePluginCount_ = scanner_.getKnownPlugins().getNumTypes();
        visibleFailureCount_ = scanner_.getFailureStore().getCount();
        visibleCacheCount_ = scanner_.getCache().getCount();
        startTimerHz(15);
        setVisible(true);

        scanner_.addDefaultSearchPaths();
        auto shouldRunCleanRescan = scanner_.getCache().getCount() == 0
            && scanner_.getFailureStore().getCount() == 0;

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
            juce::Timer::callAfterDelay(600, [safeThis]()
            {
                if (safeThis == nullptr) return;
                safeThis->setVisible(false);
                safeThis->stopTimer();
                if (safeThis->onScanComplete)
                    safeThis->onScanComplete();
            });
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
        auto box = getLocalBounds().withSizeKeepingCentre(420, 160).toFloat();
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
        g.setColour(t.colors.backgroundDark);
        g.fillRoundedRectangle(barArea, 5.f);
        if (progress_ > 0.f)
        {
            auto filled = barArea.withWidth(barArea.getWidth() * progress_);
            g.setColour(t.colors.accent);
            g.fillRoundedRectangle(filled, 5.f);
        }
        inner.removeFromTop(8.f);

        // Current plugin name
        g.setFont(juce::Font(10.f));
        g.setColour(t.colors.textSecondary);
        g.drawText(currentPlugin_, inner.removeFromTop(16.f), juce::Justification::centredLeft, true);

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

    void mouseUp(const juce::MouseEvent&) override
    {
        // Click anywhere closes (acts like an X) to avoid trapping the user.
        setVisible(false);
        stopTimer();
        if (onScanComplete)
            onScanComplete();
    }

private:
    PluginScannerCore& scanner_;
    juce::TextButton closeBtn_;
    float progress_  = 0.f;
    juce::String currentPlugin_;
    int visiblePluginCount_ = 0;
    int visibleFailureCount_ = 0;
    int visibleCacheCount_ = 0;

    void timerCallback() override
    {
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanStartupDialog)
};

} // namespace DAW
