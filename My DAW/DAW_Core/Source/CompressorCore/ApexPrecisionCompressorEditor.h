#pragma once
// APEX-original responsive dynamics workspace; not FabFilter trade dress.
// Touch-sized controls with real JUCE begin/end automation gestures.
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "../PluginHostCore/ApexEvoEyeStamp.h"

namespace APEX::PrecisionC3
{
class PrecisionLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float value, float start, float finish,
                           juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                         static_cast<float>(y), static_cast<float>(w),
                         static_cast<float>(h));
        const auto c=bounds.getCentre();
        const float radius=std::min(bounds.getWidth(),bounds.getHeight())*0.40f;
        const auto arcStart = start + 0.06f;
        const auto arcEnd = finish - 0.06f;
        juce::Path track, amount;
        track.addCentredArc(c.x,c.y,radius,radius,0,arcStart,arcEnd,true);
        amount.addCentredArc(c.x,c.y,radius,radius,0,arcStart,
                              arcStart+(arcEnd-arcStart)*value,true);
        g.setColour(juce::Colour(0xff303c4b));
        g.strokePath(track,juce::PathStrokeType(6.0f,juce::PathStrokeType::curved,
                        juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xff64e5df));
        g.strokePath(amount,juce::PathStrokeType(6.0f,juce::PathStrokeType::curved,
                        juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xff111a25));
        g.fillEllipse(c.x-radius*0.69f,c.y-radius*0.69f,
                      1.38f*radius,1.38f*radius);
        g.setColour(juce::Colour(0xffb6d6e0));
        const float angle=arcStart+(arcEnd-arcStart)*value;
        g.drawLine(c.x+std::sin(angle)*radius*0.25f,
                   c.y-std::cos(angle)*radius*0.25f,
                   c.x+std::sin(angle)*radius*0.59f,
                   c.y-std::cos(angle)*radius*0.59f,2.0f);
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor& p) : AudioProcessorEditor(&p), processor_(p)
    {
        setLookAndFeel(&lookAndFeel_);
        constexpr std::array<int,15> indices {{
            Threshold,Ratio,Knee,Range,Attack,Release,
            Hold,Input,Wet,Dry,Output,Drive,
            SidechainHP,SidechainLP,StereoLink
        }};
        constexpr std::array<const char*,15> labels {{
            "THRESHOLD","RATIO","KNEE","RANGE","ATTACK","RELEASE",
            "HOLD","INPUT","WET GAIN","DRY","OUTPUT","DRIVE",
            "SC HIGH-PASS","SC LOW-PASS","STEREO LINK"
        }};
        for (int i=0; i<15; ++i)
        {
            auto& slider=knobs_[static_cast<std::size_t>(i)];
            auto& parameter=processor_.parameter(indices[static_cast<std::size_t>(i)]);
            const auto low=parameter.fromNormalised(0.0f);
            const auto high=parameter.fromNormalised(1.0f);
            slider.setName(labels[static_cast<std::size_t>(i)]);
            slider.setTooltip(labels[static_cast<std::size_t>(i)]);
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,98,22);
            slider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffd5e9ee));
            slider.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff101823));
            slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff263646));
            slider.setRange(low,high,0.0);
            slider.setValue(parameter.getUnits(),juce::dontSendNotification);
            slider.setDoubleClickReturnValue(true,
                parameter.fromNormalised(parameter.getDefaultValue()));
            slider.onDragStart=[this,i,indices] {
                processor_.parameter(indices[static_cast<std::size_t>(i)]).beginChangeGesture();
            };
            slider.onValueChange=[this,i,indices] {
                auto& prm=processor_.parameter(indices[static_cast<std::size_t>(i)]);
                prm.setValueNotifyingHost(prm.toNormalised(static_cast<float>(
                    knobs_[static_cast<std::size_t>(i)].getValue())));
            };
            slider.onDragEnd=[this,i,indices] {
                processor_.parameter(indices[static_cast<std::size_t>(i)]).endChangeGesture();
            };
            addAndMakeVisible(slider);
        }
        for (int i=0; i<14; ++i) style_.addItem(kStyleNames[static_cast<std::size_t>(i)],i+1);
        style_.setSelectedId(static_cast<int>(processor_.parameter(Style).getUnits())+1,
                             juce::dontSendNotification);
        style_.setTooltip("Dynamics topology and musical response");
        style_.onChange=[this] { updateChoice(Style,style_.getSelectedId()-1); };
        addAndMakeVisible(style_);
        constexpr const char* characters[] {"Off","Tube","Diode","Bright"};
        for (int i=0; i<4; ++i) character_.addItem(characters[i],i+1);
        character_.setSelectedId(1,juce::dontSendNotification);
        character_.setTooltip("APEX analog-inspired coloration");
        character_.onChange=[this] { updateChoice(Character,character_.getSelectedId()-1); };
        addAndMakeVisible(character_);

        constexpr std::array<int,4> toggles {{
            AutoThreshold,AutoRelease,AutoGain,Bypass
        }};
        constexpr std::array<const char*,4> names {{
            "AUTO THRESHOLD","AUTO RELEASE","AUTO GAIN","BYPASS"
        }};
        for (int i=0; i<4; ++i)
        {
            auto& button=switches_[static_cast<std::size_t>(i)];
            button.setButtonText(names[static_cast<std::size_t>(i)]);
            button.setColour(juce::ToggleButton::textColourId,juce::Colour(0xffd7e8ec));
            button.setToggleState(processor_.parameter(toggles[static_cast<std::size_t>(i)])
                                   .getUnits()>0.5f,juce::dontSendNotification);
            button.onClick=[this,i,toggles] {
                auto& prm=processor_.parameter(toggles[static_cast<std::size_t>(i)]);
                prm.beginChangeGesture();
                prm.setValueNotifyingHost(switches_[static_cast<std::size_t>(i)]
                                          .getToggleState()?1.0f:0.0f);
                prm.endChangeGesture();
            };
            addAndMakeVisible(button);
        }
        setOpaque(true);
        setResizable(true,true);
        setResizeLimits(770,530,1660,1137);
        getConstrainer()->setFixedAspectRatio(1080.0/740.0);
        setSize(1080,740);
        startTimerHz(25);
    }
    ~Editor() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff080e16));
        g.saveState();
        const auto scale=faceScale();
        g.addTransform(juce::AffineTransform::scale(scale).translated(
            (getWidth()-1080.0f*scale)*0.5f,
            (getHeight()-740.0f*scale)*0.5f));

        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff121f2d),0,0,juce::Colour(0xff081019),1070,730,false));
        g.fillRoundedRectangle(8,8,1064,724,16);
        g.setColour(juce::Colour(0xff273d4a));
        g.drawRoundedRectangle(8,8,1064,724,16,1.0f);
        g.setColour(juce::Colour(0xffd9f1f4));
        g.setFont(juce::Font(juce::FontOptions(27.0f,juce::Font::bold)));
        g.drawText("APEX   /   PRECISION C3",30,18,570,43,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff7da0a8));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText("DYNAMICS   /   BUBBLEGUM ENGINE",32,56,460,20,
                   juce::Justification::centredLeft);
        g.drawText("CLEAN-ROOM DSP  /  PREVIEW",796,30,246,24,
                   juce::Justification::centredRight);

        drawRoundedPanel(g,{24,87,1032,173});
        drawDynamicsGraph(g);
        drawRoundedPanel(g,{24,274,1032,259});
        drawRoundedPanel(g,{24,544,1032,169});
        g.setFont(juce::Font(juce::FontOptions(13.0f,juce::Font::bold)));
        g.setColour(juce::Colour(0xff8caab5));
        g.drawText("SIDECHAIN / LINK",47,547,350,21,juce::Justification::left);
        g.drawText("STYLE",481,556,111,21,juce::Justification::left);
        g.drawText("CHARACTER",725,556,144,21,juce::Justification::left);
        g.drawText("APEX  /  DIGITAL DYNAMICS",30,706,500,20,juce::Justification::left);

        constexpr std::array<const char*,15> labels {{
            "THRESHOLD","RATIO","KNEE","RANGE","ATTACK","RELEASE",
            "HOLD","INPUT","WET GAIN","DRY","OUTPUT","DRIVE",
            "SC HIGH-PASS","SC LOW-PASS","STEREO LINK"
        }};
        g.setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));
        g.setColour(juce::Colour(0xff96b9c2));
        for (int i=0; i<12; ++i)
        {
            const int row=i/6, col=i%6;
            g.drawText(labels[static_cast<std::size_t>(i)],44+col*170,
                       282+row*124,154,20,juce::Justification::centred);
        }
        for (int i=12; i<15; ++i)
        {
            const int col=i-12;
            g.drawText(labels[static_cast<std::size_t>(i)],
                       43+col*139,569,130,20,juce::Justification::centred);
        }
        g.restoreState();
    }
    void resized() override
    {
        for (int i=0; i<12; ++i)
        {
            const int row=i/6,col=i%6;
            knobs_[static_cast<std::size_t>(i)].setBounds(
                rect(44+col*170,305+row*124,154,101));
        }
        for (int i=12; i<15; ++i)
            knobs_[static_cast<std::size_t>(i)].setBounds(
                rect(45+(i-12)*139,588,125,103));
        style_.setBounds(rect(475,586,235,42));
        character_.setBounds(rect(719,586,291,42));
        for (int i=0; i<4; ++i)
            switches_[static_cast<std::size_t>(i)].setBounds(
                rect(482+(i%2)*267,642+(i/2)*27,250,26));
    }

private:
    static void drawRoundedPanel(juce::Graphics& g,juce::Rectangle<float> area)
    {
        g.setColour(juce::Colour(0xff0b1520));
        g.fillRoundedRectangle(area,11.0f);
        g.setColour(juce::Colour(0xff243443));
        g.drawRoundedRectangle(area,11.0f,1.1f);
    }
    void drawDynamicsGraph(juce::Graphics& g)
    {
        const auto originX=52.0f, originY=111.0f, areaW=745.0f, areaH=126.0f;
        g.setColour(juce::Colour(0xff182e38));
        for (int i=0; i<=6; ++i)
        {
            const float x=originX+i*areaW/6;
            const float y=originY+i*areaH/6;
            g.drawVerticalLine(static_cast<int>(x),originY,originY+areaH);
            g.drawHorizontalLine(static_cast<int>(y),originX,originX+areaW);
        }
        juce::Path diagonal;
        diagonal.startNewSubPath(originX,originY+areaH);
        diagonal.lineTo(originX+areaW,originY);
        const float threshold=processor_.parameter(Threshold).getUnits();
        const float ratio=processor_.parameter(Ratio).getUnits();
        const float knee=processor_.parameter(Knee).getUnits();
        juce::Path response;
        for (int i=0; i<=140; ++i)
        {
            const auto inputDb=-60.0f + 60.0f*i/140.0f;
            const auto over=inputDb-threshold;
            float reduction=0.0f;
            const float slope=1.0f-1.0f/juce::jmax(1.0f,ratio);
            if (knee <= 0.001f)
                reduction=slope*juce::jmax(0.0f,over);
            else if(over >= knee*0.5f)
                reduction=slope*over;
            else if(over > -knee*0.5f)
                reduction=slope*(over+knee*0.5f)*(over+knee*0.5f)/(2.0f*knee);
            const auto outputDb=inputDb-reduction;
            const auto x=originX+areaW*i/140.0f;
            const auto y=originY+areaH-(outputDb+60.0f)*areaH/60.0f;
            if (i==0) response.startNewSubPath(x,y);
            else response.lineTo(x,y);
        }
        g.setColour(juce::Colour(0xff34525d));
        g.strokePath(diagonal,juce::PathStrokeType(1.5f));
        g.setColour(juce::Colour(0xff62dfd6));
        g.strokePath(response,juce::PathStrokeType(2.8f));
        g.setColour(juce::Colour(0xffa8cbd1));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("INPUT / OUTPUT TRANSFER",54,91,346,19,juce::Justification::left);
        g.drawText("-60 dB",54,237,95,17,juce::Justification::left);
        g.drawText("0 dB",733,237,64,17,juce::Justification::right);

        const auto gr=juce::jlimit(0.0f,36.0f,processor_.gainReduction());
        const auto input=juce::jlimit(-60.0f,6.0f,processor_.inputLevel());
        const auto output=juce::jlimit(-60.0f,6.0f,processor_.outputLevel());
        g.setColour(juce::Colour(0xff91afb9));
        g.drawText("IN",831,102,54,18,juce::Justification::centred);
        g.drawText("OUT",902,102,54,18,juce::Justification::centred);
        g.drawText("GR",973,102,54,18,juce::Justification::centred);
        const float y=127.0f, height=112.0f;
        const float levels[3]={(input+60.0f)/66.0f,
                               (output+60.0f)/66.0f,gr/36.0f};
        for (int i=0; i<3; ++i)
        {
            const float x=838.0f+i*71.0f;
            g.setColour(juce::Colour(0xff182d39));
            g.fillRoundedRectangle(x,y,26,height,4);
            const auto v=juce::jlimit(0.0f,1.0f,levels[i]);
            g.setColour(i==2 ? juce::Colour(0xffffbb67) : juce::Colour(0xff72d9cb));
            g.fillRoundedRectangle(x,y+height*(1.0f-v),26,height*v,3);
        }
        g.setColour(juce::Colour(0xffffc47a));
        g.setFont(juce::Font(juce::FontOptions(13.0f,juce::Font::bold)));
        g.drawText(juce::String(gr,1)+" dB GR",832,240,185,17,juce::Justification::centred);
    }
    float faceScale() const noexcept
    {
        return juce::jmin(getWidth()/1080.0f,getHeight()/740.0f);
    }
    juce::Rectangle<int> rect(float x,float y,float w,float h) const
    {
        const auto scale=faceScale();
        const float shiftX=(getWidth()-1080.0f*scale)*0.5f;
        const float shiftY=(getHeight()-740.0f*scale)*0.5f;
        return {juce::roundToInt(shiftX+x*scale),juce::roundToInt(shiftY+y*scale),
                juce::jmax(20,juce::roundToInt(w*scale)),
                juce::jmax(20,juce::roundToInt(h*scale))};
    }
    void updateChoice(int index, int value)
    {
        if (value<0) return;
        auto& prm=processor_.parameter(index);
        prm.beginChangeGesture();
        prm.setValueNotifyingHost(prm.toNormalised(static_cast<float>(value)));
        prm.endChangeGesture();
    }
    void timerCallback() override
    {
        constexpr std::array<int,15> indices {{
            Threshold,Ratio,Knee,Range,Attack,Release,Hold,Input,Wet,Dry,Output,
            Drive,SidechainHP,SidechainLP,StereoLink
        }};
        for (int i=0; i<15; ++i)
            if (!knobs_[static_cast<std::size_t>(i)].isMouseButtonDown())
                knobs_[static_cast<std::size_t>(i)].setValue(
                    processor_.parameter(indices[static_cast<std::size_t>(i)]).getUnits(),
                    juce::dontSendNotification);
        if (!style_.isMouseButtonDown())
            style_.setSelectedId(static_cast<int>(std::lround(
                processor_.parameter(Style).getUnits()))+1,juce::dontSendNotification);
        if (!character_.isMouseButtonDown())
            character_.setSelectedId(static_cast<int>(std::lround(
                processor_.parameter(Character).getUnits()))+1,juce::dontSendNotification);
        constexpr std::array<int,4> toggles {{
            AutoThreshold,AutoRelease,AutoGain,Bypass
        }};
        for (int i=0; i<4; ++i)
            switches_[static_cast<std::size_t>(i)].setToggleState(
                processor_.parameter(toggles[static_cast<std::size_t>(i)]).getUnits() >= 0.5f,
                juce::dontSendNotification);
        repaint(rect(33,90,1013,170));
    }

    Processor& processor_;
    PrecisionLookAndFeel lookAndFeel_;
    std::array<juce::Slider,15> knobs_;
    juce::ComboBox style_,character_;
    std::array<juce::ToggleButton,4> switches_;
};

inline juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor(*this);
}
} // namespace APEX::PrecisionC3
