#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../FloatingWindowCore/FloatingWindowBase.h"

namespace DAW {

/**
 * TimelineWindow
 * Floating window chrome for ArrangementView, FL Studio-style.
 * When maximized, fills the parent like a regular docked timeline.
 * When floating, can be freely positioned and resized.
 * Minimizes to a bubble in the taskbar.
 */
class TimelineWindow : public FloatingWindowBase
{
public:
    TimelineWindow() : FloatingWindowBase("TIMELINE")
    {
        // Navigation already invalidates the visible timeline. Drawing into the
        // inherited window buffer first adds a full-surface intermediate copy.
        setBufferedToImage(false);
        setSize(900, 500);
    }

    std::function<void()> onDockRequested;

    void setContent(juce::Component* content)
    {
        content_ = content;
        if (content_) { addAndMakeVisible(content_); layoutContent(); }
    }

protected:
    void layoutContent() override
    {
        if (content_) content_->setVisible(!isMinimized());
        if (content_ && !isMinimized())
            content_->setBounds(getContentArea());
    }

private:
    juce::Component* content_ = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineWindow)
};

} // namespace DAW
