#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "../ThemeCore/Theme.h"
#include "InputTrimFloatingPanel.h"

namespace DAW {

class InputMonitorToggleStrip : public juce::Component
{
public:
    explicit InputMonitorToggleStrip(Track& track)
        : track_(track)
    {
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        g.setColour(t.colors.controlIdle);
        g.fillRoundedRectangle(trimButtonBounds_.toFloat(), 4.0f);
        g.setColour(t.colors.accent);
        g.drawRoundedRectangle(trimButtonBounds_.toFloat(), 4.0f, 1.0f);
        g.setColour(t.colors.text);
        g.drawText("Trim", trimButtonBounds_, juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        trimButtonBounds_ = b.removeFromLeft(46).reduced(1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStartTrimDb_ = track_.getInputTrim().getTargetGainDb();

        if (trimButtonBounds_.contains(e.getPosition()))
        {
            InputTrimFloatingPanel::showForTrack(track_, getTopLevelComponent());
        }
    }

private:
    Track& track_;
    juce::Rectangle<int> trimButtonBounds_;
    float dragStartTrimDb_ { 0.0f };
};

} // namespace DAW
