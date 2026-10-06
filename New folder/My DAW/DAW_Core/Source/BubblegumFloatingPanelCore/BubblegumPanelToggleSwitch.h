#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumPanelToggleSwitch
 *
 * Compact pill-shaped on/off toggle. Used in InputTrimFloatingPanel as the
 * "L+R" channel-mode switch, but generic — drop it into any panel that
 * needs a small two-state toggle with a label and a coloured indicator dot.
 *
 * Single state: a bool. Click toggles it. Optional onChanged callback fires
 * with the new value.
 */
class BubblegumPanelToggleSwitch : public juce::Component
{
public:
    struct Config
    {
        juce::Colour fillOff       { 0xFF22273A };
        juce::Colour borderOff     { 0xFF3B4156 };
        juce::Colour fillOn        { 0xFF22273A };
        juce::Colour borderOn      { 0xFFFF4F8A };
        juce::Colour dotOff        { 0xFF5B6075 };
        juce::Colour dotOn         { 0xFFFF4F8A };
        juce::Colour labelOff      { 0xFF7A8094 };
        juce::Colour labelOn       { 0xFFD8DCE6 };

        float cornerRadius     { 9.0f  };
        float borderStroke     { 0.6f  };
        float dotRadius        { 4.5f  };
        float labelFontSize    { 11.0f };
        float labelLeftPad     { 8.0f  };
        float dotRightPad      { 8.0f  };
    };

    BubblegumPanelToggleSwitch()
    {
        setInterceptsMouseClicks(true, false);
    }

    void setLabel(juce::String l)        { label_ = std::move(l); repaint(); }
    void setOn(bool on)
    {
        if (on != on_)
        {
            on_ = on;
            repaint();
            if (onChanged) onChanged(on_);
        }
    }
    bool isOn() const                    { return on_; }
    void setConfig(const Config& c)      { config_ = c; repaint(); }
    const Config& getConfig() const      { return config_; }

    std::function<void(bool)> onChanged;

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        g.setColour(on_ ? config_.fillOn : config_.fillOff);
        g.fillRoundedRectangle(b, config_.cornerRadius);
        g.setColour(on_ ? config_.borderOn : config_.borderOff);
        g.drawRoundedRectangle(b, config_.cornerRadius, config_.borderStroke);

        const float dotSpace = config_.dotRadius * 2.0f + config_.dotRightPad;
        g.setColour(on_ ? config_.labelOn : config_.labelOff);
        g.setFont(juce::Font(config_.labelFontSize));
        g.drawText(label_,
                   juce::Rectangle<float>(b.getX() + config_.labelLeftPad,
                                          b.getY(),
                                          b.getWidth() - config_.labelLeftPad - dotSpace,
                                          b.getHeight()),
                   juce::Justification::centredLeft);

        const float dotCx = b.getRight() - config_.dotRightPad - config_.dotRadius;
        const float dotCy = b.getCentreY();
        g.setColour(on_ ? config_.dotOn : config_.dotOff);
        g.fillEllipse(dotCx - config_.dotRadius, dotCy - config_.dotRadius,
                      config_.dotRadius * 2.0f, config_.dotRadius * 2.0f);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        setOn(! on_);
    }

private:
    Config       config_;
    juce::String label_;
    bool         on_ { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumPanelToggleSwitch)
};

} // namespace DAW
