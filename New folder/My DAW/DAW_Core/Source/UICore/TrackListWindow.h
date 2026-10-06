#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../FloatingWindowCore/FloatingWindowBase.h"
#include "TrackList.h"

namespace DAW {

/**
 * TrackListWindow
 * Wraps TrackList in a floating window chrome.
 * Inherits all window management (drag, resize, snap, minimize, maximize)
 * from FloatingWindowBase.
 *
 * In embedded mode (setEmbedded(true)), all chrome is hidden and the
 * TrackList fills the full allocated bounds — use for docked layout.
 */
class TrackListWindow : public FloatingWindowBase
{
public:
    explicit TrackListWindow(TrackList& content)
        : FloatingWindowBase("Tracks"), content_(content)
    {
        addAndMakeVisible(content_);
        setSize(240, 400);
    }

protected:
    void layoutContent() override
    {
        if (isEmbedded())
        {
            content_.setVisible(true);
            content_.setBounds(getLocalBounds());
        }
        else
        {
            content_.setVisible(!isMinimized());
            if (!isMinimized())
                content_.setBounds(getContentArea());
        }
    }

private:
    TrackList& content_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackListWindow)
};

} // namespace DAW
