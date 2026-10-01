#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumPanelReadoutBox
 *
 * Generic dB / numeric readout box: small label on top (e.g. "CURRENT",
 * "PEAK MAX"), large bold value below (e.g. "-3.0", "+1.4"). Click to
 * trigger an optional onClicked callback (used by panels for "click PEAK
 * MAX to reset the latched peak").
 *
 * Two value colour variants: primary (neutral white) and alternate
 * (warm/peak red). Toggle via setUseAlternateValueColour(bool).
 */
class BubblegumPanelReadoutBox : public juce::Component
{
public:
    struct Config
    {
        juce::Colour fill        { 0xFF0F1219 };
        juce::Colour border      { 0xFF2A2F3D };
        juce::Colour labelInk    { 0xFF6B7080 };
        juce::Colour valueInk    { 0xFFD8DCE6 };
        juce::Colour valueInkAlt { 0xFFFF6B6B };

        float cornerRadius   { 6.0f  };
        float borderStroke   { 0.6f  };
        float labelFontSize  { 11.0f };
        float valueFontSize  { 17.0f };
        float labelY         { 8.0f  };
        float valueY         { 22.0f };
    };

    BubblegumPanelReadoutBox()
    {
        setInterceptsMouseClicks(true, false);
    }

    void setLabel(juce::String l)                 { label_ = std::move(l); repaint(); }
    void setValue(juce::String v)                 { value_ = std::move(v); repaint(); }
    void setUseAlternateValueColour(bool alt)     { useAlt_ = alt; repaint(); }
    void setConfig(const Config& c)               { config_ = c; repaint(); }
    const Config& getConfig() const               { return config_; }

    std::function<void()> onClicked;

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        g.setColour(config_.fill);
        g.fillRoundedRectangle(b, config_.cornerRadius);
        g.setColour(config_.border);
        g.drawRoundedRectangle(b, config_.cornerRadius, config_.borderStroke);

        g.setColour(config_.labelInk);
        g.setFont(juce::Font(config_.labelFontSize));
        g.drawText(label_,
                   juce::Rectangle<float>(b.getX(), config_.labelY, b.getWidth(), 14.0f),
                   juce::Justification::centred);

        g.setColour(useAlt_ ? config_.valueInkAlt : config_.valueInk);
        g.setFont(juce::Font(config_.valueFontSize, juce::Font::bold));
        g.drawText(value_,
                   juce::Rectangle<float>(b.getX(), config_.valueY, b.getWidth(), 20.0f),
                   juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onClicked) onClicked();
    }

private:
    Config       config_;
    juce::String label_;
    juce::String value_;
    bool         useAlt_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumPanelReadoutBox)
};

} // namespace DAW
