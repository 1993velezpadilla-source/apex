// ===========================================================================
// ForensicAuditWindow.h
// In-DAW forensic audit log viewer with one-click copy.
// Shows all pitch/stretch coupling audit traps in real-time.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include <mutex>

namespace DAW {

// ═══════════════════════════════════════════════════════════════════════════
// ForensicAuditLogger — Thread-safe log collector
// ═══════════════════════════════════════════════════════════════════════════
class ForensicAuditLogger
{
public:
    static ForensicAuditLogger& getInstance()
    {
        static ForensicAuditLogger instance;
        return instance;
    }

    void addLog(const juce::String& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Fixed-size ring: overwrite oldest entry instead of reallocating.
        // The previous implementation called logs_.remove(0) when size > 1000,
        // which shifts every element in the StringArray — O(n) heap work on
        // every knob event. With a ring we never allocate after the first 1000.
        if (logs_.size() < 1000)
            logs_.add(message);
        else
        {
            logs_.set(ringHead_, message);
            ringHead_ = (ringHead_ + 1) % 1000;
        }
    }

    juce::String getAllLogs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        juce::String result;
        for (const auto& log : logs_)
            result += log + "\n";
        return result;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        logs_.clear();
    }

private:
    mutable std::mutex mutex_;
    juce::StringArray logs_;
    int ringHead_ = 0;
};

// Macro to log to both DBG and ForensicAuditLogger.
// Disabled when APEX_AUDIO_DEBUG_LOGS is 0 to avoid mutex + heap allocation
// on the audio thread. Set APEX_AUDIO_DEBUG_LOGS to 1 for debug sessions only.
#if APEX_AUDIO_DEBUG_LOGS
#define FORENSIC_LOG(msg) do { \
    juce::String logMsg; \
    logMsg << msg; \
    DBG(logMsg); \
    DAW::ForensicAuditLogger::getInstance().addLog(logMsg); \
} while(0)
#else
#define FORENSIC_LOG(msg) do {} while (false)
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ForensicAuditWindow — UI component
// ═══════════════════════════════════════════════════════════════════════════
class ForensicAuditWindow : public juce::DocumentWindow,
                            private juce::Timer
{
public:
    ForensicAuditWindow()
        : juce::DocumentWindow("Forensic Audit Log",
                               juce::Colours::darkgrey,
                               juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);

        auto* content = new ContentComponent();
        setContentOwned(content, true);

        setSize(800, 600);
        centreWithSize(800, 600);

        // Auto-refresh every 500ms
        startTimer(500);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

    void timerCallback() override
    {
        if (auto* content = dynamic_cast<ContentComponent*>(getContentComponent()))
            content->refreshLogs();
    }

private:
    class ContentComponent : public juce::Component
    {
    public:
        ContentComponent()
        {
            // Text editor for logs
            logEditor_.setMultiLine(true);
            logEditor_.setReadOnly(true);
            logEditor_.setScrollbarsShown(true);
            logEditor_.setCaretVisible(false);
            logEditor_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
            logEditor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1e1e1e));
            logEditor_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd4d4d4));
            addAndMakeVisible(logEditor_);

            // Copy button
            copyButton_.setButtonText("Copy All");
            copyButton_.onClick = [this] { copyToClipboard(); };
            addAndMakeVisible(copyButton_);

            // Clear button
            clearButton_.setButtonText("Clear");
            clearButton_.onClick = [this] { clearLogs(); };
            addAndMakeVisible(clearButton_);

            // Refresh button
            refreshButton_.setButtonText("Refresh");
            refreshButton_.onClick = [this] { refreshLogs(); };
            addAndMakeVisible(refreshButton_);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(10);

            // Buttons at top
            auto buttonRow = area.removeFromTop(30);
            copyButton_.setBounds(buttonRow.removeFromLeft(100).reduced(2));
            buttonRow.removeFromLeft(5);
            clearButton_.setBounds(buttonRow.removeFromLeft(100).reduced(2));
            buttonRow.removeFromLeft(5);
            refreshButton_.setBounds(buttonRow.removeFromLeft(100).reduced(2));

            area.removeFromTop(10);

            // Log editor fills rest
            logEditor_.setBounds(area);
        }

        void refreshLogs()
        {
            juce::String allLogs = ForensicAuditLogger::getInstance().getAllLogs();
            if (allLogs != logEditor_.getText())
            {
                logEditor_.setText(allLogs, false);
                logEditor_.moveCaretToEnd();
            }
        }

    private:
        void copyToClipboard()
        {
            juce::String allLogs = ForensicAuditLogger::getInstance().getAllLogs();
            juce::SystemClipboard::copyTextToClipboard(allLogs);

            // Flash button to show copy success
            copyButton_.setButtonText("Copied!");
            juce::Timer::callAfterDelay(1000, [this]() {
                if (copyButton_.isShowing())
                    copyButton_.setButtonText("Copy All");
            });
        }

        void clearLogs()
        {
            ForensicAuditLogger::getInstance().clear();
            logEditor_.clear();
        }

        juce::TextEditor logEditor_;
        juce::TextButton copyButton_;
        juce::TextButton clearButton_;
        juce::TextButton refreshButton_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ForensicAuditWindow)
};

} // namespace DAW
