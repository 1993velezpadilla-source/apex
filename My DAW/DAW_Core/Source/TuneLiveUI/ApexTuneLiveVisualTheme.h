#pragma once
// APEX Tune Live / Bubblegum Engine visual skin.
// UI-only JUCE painting. No DSP, realtime callbacks, parameter IDs or state.
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>

namespace APEX { namespace TuneLive { namespace Visual {
inline juce::Colour midnight()   { return juce::Colour::fromRGB(5, 11, 19); }
inline juce::Colour surface()    { return juce::Colour::fromRGB(11, 24, 37); }
inline juce::Colour panelTop()   { return juce::Colour::fromRGB(15, 29, 44); }
inline juce::Colour panelBottom(){ return juce::Colour::fromRGB(8, 19, 30); }
inline juce::Colour cyan()       { return juce::Colour::fromRGB(25, 192, 212); }
inline juce::Colour gold()       { return juce::Colour::fromRGB(255, 187, 103); }
inline juce::Colour ivory()      { return juce::Colour::fromRGB(241, 237, 232); }
inline juce::Colour muted()      { return juce::Colour::fromRGB(164, 185, 203); }

inline void panel(juce::Graphics& g,juce::Rectangle<float> bounds,float radius=18.0f) {
    g.setColour(juce::Colours::black.withAlpha(0.75f));
    g.fillRoundedRectangle(bounds.translated(0.0f,3.0f),radius);
    g.setGradientFill(juce::ColourGradient(panelTop(),bounds.getTopLeft(),
                                         panelBottom(),bounds.getBottomRight(),false));
    g.fillRoundedRectangle(bounds,radius);
    g.setColour(cyan().withAlpha(0.29f));
    g.drawRoundedRectangle(bounds.reduced(0.6f),radius,1.2f);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawHorizontalLine(static_cast<int>(bounds.getY()+3.0f),
                         bounds.getX()+radius,bounds.getRight()-radius);
}
inline void sectionTitle(juce::Graphics& g,const juce::String& title,float x,float y) {
    g.setColour(gold());
    g.fillRoundedRectangle(x,y+7.0f,26.0f,2.0f,1.0f);
    g.setColour(muted());
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    g.drawText(title,static_cast<int>(x+37.0f),static_cast<int>(y-2.0f),
               300,27,juce::Justification::centredLeft);
}
class StudioLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    StudioLookAndFeel() {
        setColour(juce::ComboBox::backgroundColourId,surface());
        setColour(juce::ComboBox::outlineColourId,cyan().withAlpha(0.63f));
        setColour(juce::ComboBox::textColourId,ivory());
        setColour(juce::ComboBox::arrowColourId,muted());
        setColour(juce::PopupMenu::backgroundColourId,surface());
        setColour(juce::PopupMenu::textColourId,ivory());
        setColour(juce::PopupMenu::highlightedBackgroundColourId,juce::Colour::fromRGB(30,83,102));
        setColour(juce::PopupMenu::highlightedTextColourId,juce::Colours::white);
        setColour(juce::Slider::textBoxTextColourId,ivory());
        setColour(juce::Slider::textBoxBackgroundColourId,midnight());
        setColour(juce::Slider::textBoxOutlineColourId,cyan().withAlpha(0.28f));
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,
                          float position,float startAngle,float endAngle,
                          juce::Slider&) override {
        const float size=static_cast<float>(std::min(width,height));
        const float cx=static_cast<float>(x)+static_cast<float>(width)*0.5f;
        const float cy=static_cast<float>(y)+static_cast<float>(height)*0.5f;
        const float radius=size*0.37f;
        const float angle=startAngle+position*(endAngle-startAngle);
        const juce::Rectangle<float> circle(cx-radius,cy-radius,2.0f*radius,2.0f*radius);
        // Scale the light and texture with the knob; no raster assets.
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.fillEllipse(circle.expanded(7.0f).translated(0.0f,3.0f));
        for(int i=0;i<=42;++i) {
            const float a=startAngle+static_cast<float>(i)*(endAngle-startAngle)/42.0f;
            const float r0=radius+12.0f,r1=radius+14.5f;
            g.setColour(cyan().withAlpha(i%3==0?0.64f:0.25f));
            g.drawLine(cx+std::sin(a)*r0,cy-std::cos(a)*r0,
                       cx+std::sin(a)*r1,cy-std::cos(a)*r1,1.05f);
        }
        juce::Path track, lit;
        track.addCentredArc(cx,cy,radius+5.0f,radius+5.0f,0.0f,
                            startAngle,endAngle,true);
        lit.addCentredArc(cx,cy,radius+5.0f,radius+5.0f,0.0f,
                          startAngle,std::max(startAngle+0.001f,angle),true);
        g.setColour(juce::Colour::fromRGB(47,69,79));
        g.strokePath(track,juce::PathStrokeType(6.0f,juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour(gold().withAlpha(0.22f));
        g.strokePath(lit,juce::PathStrokeType(12.0f,juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
        g.setColour(gold());
        g.strokePath(lit,juce::PathStrokeType(5.2f,juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
        g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(93,113,126),
                                             circle.getTopLeft(),
                                             juce::Colour::fromRGB(13,23,32),
                                             circle.getBottomRight(),false));
        g.fillEllipse(circle);
        g.setColour(juce::Colour::fromRGB(9,19,28));
        g.fillEllipse(circle.reduced(7.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(37,52,65),
                                             circle.getTopLeft(),
                                             juce::Colour::fromRGB(10,19,29),
                                             circle.getBottomRight(),false));
        g.fillEllipse(circle.reduced(10.0f));
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawEllipse(circle.reduced(10.0f),1.0f);
        const float tip=radius+4.5f;
        g.setColour(gold().withAlpha(0.20f));
        g.fillEllipse(cx+std::sin(angle)*tip-9.0f,
                      cy-std::cos(angle)*tip-9.0f,18.0f,18.0f);
        g.setColour(juce::Colour::fromRGB(255,223,154));
        g.fillEllipse(cx+std::sin(angle)*tip-5.3f,
                      cy-std::cos(angle)*tip-5.3f,10.6f,10.6f);
    }
    void drawComboBox(juce::Graphics& g,int width,int height,bool,
                      int,int,int,int,juce::ComboBox&) override {
        auto r=juce::Rectangle<float>(0.7f,0.7f,
                                     static_cast<float>(width)-1.4f,
                                     static_cast<float>(height)-1.4f);
        g.setGradientFill(juce::ColourGradient(surface(),r.getTopLeft(),
                            midnight(),r.getBottomRight(),false));
        g.fillRoundedRectangle(r,6.0f);
        g.setColour(cyan().withAlpha(0.65f));
        g.drawRoundedRectangle(r,6.0f,1.0f);
        const float cx=static_cast<float>(width)-19.0f,cy=static_cast<float>(height)*0.5f;
        juce::Path chevron;
        chevron.startNewSubPath(cx-6.0f,cy-3.0f);
        chevron.lineTo(cx,cy+3.0f);chevron.lineTo(cx+6.0f,cy-3.0f);
        g.setColour(muted());g.strokePath(chevron,juce::PathStrokeType(1.8f));
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& button,
                              const juce::Colour&,bool over,bool down) override {
        auto r=button.getLocalBounds().toFloat().reduced(1.5f);
        const bool selected=button.getToggleState();
        const auto accent=selected?gold():cyan();
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(r.translated(0,2),7.0f);
        g.setGradientFill(juce::ColourGradient(selected?
            juce::Colour::fromRGB(68,47,35):juce::Colour::fromRGB(15,35,47),
            r.getTopLeft(),midnight(),r.getBottomRight(),false));
        g.fillRoundedRectangle(r,7.0f);
        g.setColour(accent.withAlpha(over||down?1.0f:(selected?0.95f:0.68f)));
        g.drawRoundedRectangle(r,7.0f,selected?2.0f:1.2f);
        if(selected) {
            g.setColour(gold().withAlpha(0.10f));
            g.fillRoundedRectangle(r.reduced(3.0f),5.0f);
        }
    }
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& button,
                          bool hovered,bool pressed) override {
        auto box=juce::Rectangle<float>(4.0f,static_cast<float>(button.getHeight())*0.5f-9.0f,
                                        18.0f,18.0f);
        const bool on=button.getToggleState();
        g.setColour(on?gold():muted().withAlpha(0.50f));
        g.drawRoundedRectangle(box,4.0f,1.7f);
        if(on) {
            g.setColour(gold().withAlpha(hovered||pressed?0.35f:0.17f));
            g.fillRoundedRectangle(box,4.0f);
            juce::Path tick;tick.startNewSubPath(8.5f,box.getCentreY());
            tick.lineTo(12.0f,box.getBottom()-5.0f);
            tick.lineTo(19.5f,box.getY()+4.0f);
            g.setColour(gold());g.strokePath(tick,juce::PathStrokeType(2.0f));
        }
        g.setColour(hovered?juce::Colours::white:ivory());
        g.setFont(juce::Font(juce::FontOptions(12.3f).withStyle("Bold")));
        g.drawText(button.getButtonText(),30,0,std::max(0,button.getWidth()-32),
                   button.getHeight(),juce::Justification::centredLeft);
    }
};
}}} // namespace APEX::TuneLive::Visual
