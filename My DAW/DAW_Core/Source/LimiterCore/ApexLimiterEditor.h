#pragma once
// Original APEX dark mastering interface. No FabFilter assets, logos or skin.
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "ApexLimiterProcessor.h"

namespace APEX { namespace Limiter {

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor& p) : juce::AudioProcessorEditor(&p), processor_(p)
    {
        setOpaque(true);
        setup(gain_, Gain, "INPUT GAIN",0,30,0.01);
        setup(ceiling_,Ceiling,"OUTPUT CEILING",-30,0,0.01);
        setup(lookahead_,Lookahead,"LOOKAHEAD",0.1,20,0.1);
        setup(attack_,Attack,"ATTACK",0.01,1000,0.01);
        setup(release_,Release,"RELEASE",10,3000,1);
        setup(link_,ChannelLink,"CHANNEL LINK",0,100,1);
        style_.addItemList({"Transparent","Punchy","Dynamic","Allround",
                            "Aggressive","Modern","Bus","Safe"},1);
        style_.setTooltip("Eight APEX-original limiting behavior profiles.");
        style_.setSelectedId(static_cast<int>(processor_.getLimiterParameter(LimitingStyle).units())+1,
                             juce::dontSendNotification);
        style_.onChange=[this]{ changeChoice(LimitingStyle,style_.getSelectedId()-1); };
        addAndMakeVisible(style_);
        quality_.addItemList({"1x","2x","4x","8x"},1);
        quality_.setTooltip("Inter-sample detector interpolation resolution (not linear-phase DSP oversampling).");
        quality_.setSelectedId(static_cast<int>(processor_.getLimiterParameter(DetectorQuality).units())+1,
                               juce::dontSendNotification);
        quality_.onChange=[this]{ changeChoice(DetectorQuality,quality_.getSelectedId()-1); };
        addAndMakeVisible(quality_);
        setupToggle(tp_,TruePeak,"INTER-SAMPLE", "Estimate peaks between digital samples");
        setupToggle(bypass_,Bypass,"BYPASS","Host-automatable bypass");
        setupToggle(unity_,UnityGain,"UNITY","Monitor loudness-compensated output");
        setupToggle(audition_,Audition,"DELTA","Audition limiting difference signal");
        setupToggle(dc_,DcFilter,"DC FILTER","High-pass DC offset at 12 Hz");
        setResizable(true,true);
        setResizeLimits(820,520,1800,1050);
        setSize(1060,640);
        startTimerHz(30);
    }
    ~Editor() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        const auto width=static_cast<float>(getWidth());
        const auto height=static_cast<float>(getHeight());
        g.fillAll(juce::Colour(0xff0c111d));
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff142638),0,0,juce::Colour(0xff111522),width,height,false));
        g.fillRoundedRectangle(10.0f,10.0f,width-20.0f,height-20.0f,16.0f);
        g.setColour(juce::Colour(0xffee5ba7));
        g.fillRoundedRectangle(23.0f,26.0f,7.0f,31.0f,3.0f);
        text(g,"APEX  /  BUBBLEGUM LIMITER",42,24,width-60,35,23,0xfff8effb,true);
        text(g,"Original mastering dynamics  •  inter-sample detector",42,63,width-80,23,
             12,0xff7b9bb6,false);
        const auto chart=chartBounds().toFloat();
        g.setColour(juce::Colour(0xff090e17));
        g.fillRoundedRectangle(chart,9);
        g.setColour(juce::Colour(0xff2d465c));
        g.drawRoundedRectangle(chart,9,1);
        for (int i=1;i<6;++i)
        {
            const float y=chart.getY()+chart.getHeight()*i/6.0f;
            g.setColour(juce::Colour(0xff1f3142));
            g.drawHorizontalLine(juce::roundToInt(y),chart.getX()+6,chart.getRight()-6);
        }
        for (int i=1;i<9;++i)
        {
            const float x=chart.getX()+chart.getWidth()*i/9.0f;
            g.setColour(juce::Colour(0xff1b2b3b));
            g.drawVerticalLine(juce::roundToInt(x),chart.getY()+6,chart.getBottom()-6);
        }
        // Historical gain-reduction trace. Non-interactive and UI-thread-only.
        juce::Path trace;
        for (size_t i=0;i<history_.size();++i)
        {
            const auto idx=(historyWrite_+i)%history_.size();
            const float x=chart.getX()+3.0f
                + (chart.getWidth()-6.0f)*static_cast<float>(i)/(history_.size()-1);
            const float y=chart.getY()+8.0f
                + (chart.getHeight()-16.0f)
                  * (1.0f-juce::jlimit(0.0f,1.0f,history_[idx]/24.0f));
            if(i==0) trace.startNewSubPath(x,y);
            else trace.lineTo(x,y);
        }
        g.setColour(juce::Colour(0xffe74894));
        g.strokePath(trace,juce::PathStrokeType(2.3f,
                     juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        text(g,"GAIN REDUCTION / dB",chart.getX()+14,chart.getY()+8,
             chart.getWidth()-30,24,12,0xff7d9eaf,true);
        text(g,juce::String(processor_.meterReduction(),1)+" dB",
             chart.getX()+16,chart.getBottom()-49,
             chart.getWidth()-30,39,30,0xffff8fc8,true);
        const auto output=processor_.meterOutput();
        const auto input=processor_.meterInput();
        const float inDb=linToDb(input);
        const float outDb=linToDb(output);
        const float sidebarX=width-144;
        text(g,"INPUT",sidebarX,124,130,19,12,0xff80a8be,true);
        text(g,juce::String(inDb,1)+" dBFS",sidebarX,149,130,25,18,0xffe1e6f5,true);
        text(g,"OUTPUT",sidebarX,207,130,19,12,0xff80a8be,true);
        text(g,juce::String(outDb,1)+" dBFS",sidebarX,232,130,25,18,0xffc5f2ef,true);
        g.setColour(juce::Colour(0xff152335));
        g.fillRoundedRectangle(sidebarX+25,281,15,165,4);
        g.fillRoundedRectangle(sidebarX+78,281,15,165,4);
        const float ih=juce::jlimit(0.0f,1.0f,(inDb+60.0f)/60.0f);
        const float oh=juce::jlimit(0.0f,1.0f,(outDb+60.0f)/60.0f);
        g.setColour(juce::Colour(0xffe95c99));
        g.fillRoundedRectangle(sidebarX+25,446-165*ih,15,165*ih,3);
        g.setColour(juce::Colour(0xff56e0d7));
        g.fillRoundedRectangle(sidebarX+78,446-165*oh,15,165*oh,3);
        text(g,"IN",sidebarX+18,458,34,18,11,0xff8da3b7,true);
        text(g,"OUT",sidebarX+71,458,39,18,11,0xff8da3b7,true);
        const int footerY=getHeight()-39;
        text(g,"APEX • Powered by Bubblegum Engine",30,footerY,getWidth()-52,25,
             12,0xffb4a4bc,true);
        text(g,"Experimental: true-peak and LUFS calibration pending",chart.getX(),
             chart.getBottom()+5,chart.getWidth(),23,11,0xffd6a5b5,false);
    }

    void resized() override
    {
        // Fixed control groups leave the visual output display unobstructed.
        const int top=102;
        gain_.setBounds(22,top,75,322);
        ceiling_.setBounds(101,top,75,322);
        style_.setBounds(191,105,200,31);
        quality_.setBounds(399,105,102,31);
        const int rowY=getHeight()-173;
        const int smallW=125;
        auto setRotary=[&](juce::Slider& s,int i) {
            s.setBounds(191+i*(smallW+12),rowY,smallW,104);
        };
        setRotary(lookahead_,0);
        setRotary(attack_,1);
        setRotary(release_,2);
        setRotary(link_,3);
        const int switchesY=getHeight()-62;
        tp_.setBounds(187,switchesY,147,27);
        unity_.setBounds(335,switchesY,93,27);
        audition_.setBounds(430,switchesY,93,27);
        dc_.setBounds(522,switchesY,117,27);
        bypass_.setBounds(getWidth()-144,76,126,33);
    }

private:
    static float linToDb(float linear) noexcept
    { return 20.0f*std::log10(juce::jmax(1.0e-8f,linear)); }
    juce::Rectangle<int> chartBounds() const noexcept
    {
        return {194,149,juce::jmax(270,getWidth()-364),
                juce::jmax(145,getHeight()-348)};
    }
    static void text(juce::Graphics& g,const juce::String& s,
                     float x,float y,float w,float h,float font,
                     juce::uint32 colour,bool bold)
    {
        g.setColour(juce::Colour(colour));
        g.setFont(juce::Font(juce::FontOptions(font,
                  bold ? juce::Font::bold : juce::Font::plain)));
        g.drawText(s,juce::Rectangle<float>(x,y,w,h),juce::Justification::centredLeft);
    }
    void setup(juce::Slider& slider,int index,const juce::String& name,
               double low,double high,double interval)
    {
        slider.setName(name);
        slider.setTooltip(name);
        slider.setSliderStyle(index==Gain || index==Ceiling
                 ? juce::Slider::LinearVertical
                 : juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,25);
        slider.setRange(low,high,interval);
        slider.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffec59ac));
        slider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xff38516b));
        slider.setColour(juce::Slider::thumbColourId,juce::Colour(0xfff884c5));
        slider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffd9f0fa));
        slider.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff182438));
        auto& p=processor_.getLimiterParameter(index);
        slider.setValue(p.units(),juce::dontSendNotification);
        slider.onDragStart=[this,index]{ processor_.getLimiterParameter(index).beginChangeGesture(); };
        slider.onValueChange=[this,index,&slider] {
            auto& param=processor_.getLimiterParameter(index);
            param.setValueNotifyingHost(param.toNormal(static_cast<float>(slider.getValue())));
        };
        slider.onDragEnd=[this,index]{ processor_.getLimiterParameter(index).endChangeGesture(); };
        addAndMakeVisible(slider);
    }
    void setupToggle(juce::ToggleButton& button,int index,
                     const juce::String& title,const juce::String& hint)
    {
        button.setButtonText(title);
        button.setTooltip(hint);
        button.setToggleState(processor_.getLimiterParameter(index).units()>0.5f,
                              juce::dontSendNotification);
        button.onClick=[this,index,&button] {
            auto& param=processor_.getLimiterParameter(index);
            param.beginChangeGesture();
            param.setValueNotifyingHost(button.getToggleState()?1.0f:0.0f);
            param.endChangeGesture();
        };
        addAndMakeVisible(button);
    }
    void changeChoice(int i,int value)
    {
        auto& p=processor_.getLimiterParameter(i);
        p.beginChangeGesture();
        p.setValueNotifyingHost(p.toNormal(static_cast<float>(value)));
        p.endChangeGesture();
    }
    void timerCallback() override
    {
        // Read host automation back into the editor. No DSP/GUI coupling.
        auto reflect=[this](juce::Slider& s,int i) {
            if (!s.isMouseButtonDown())
                s.setValue(processor_.getLimiterParameter(i).units(),juce::dontSendNotification);
        };
        reflect(gain_,Gain); reflect(ceiling_,Ceiling); reflect(lookahead_,Lookahead);
        reflect(attack_,Attack); reflect(release_,Release); reflect(link_,ChannelLink);
        style_.setSelectedId(static_cast<int>(processor_.getLimiterParameter(LimitingStyle).units())+1,
                             juce::dontSendNotification);
        quality_.setSelectedId(static_cast<int>(processor_.getLimiterParameter(DetectorQuality).units())+1,
                               juce::dontSendNotification);
        auto update=[this](juce::ToggleButton& b,int i) {
            b.setToggleState(processor_.getLimiterParameter(i).units()>0.5f,
                             juce::dontSendNotification);
        };
        update(tp_,TruePeak);update(bypass_,Bypass);update(unity_,UnityGain);
        update(audition_,Audition);update(dc_,DcFilter);
        history_[historyWrite_]=processor_.meterReduction();
        historyWrite_=(historyWrite_+1)%history_.size();
        repaint();
    }
    Processor& processor_;
    juce::Slider gain_,ceiling_,lookahead_,attack_,release_,link_;
    juce::ComboBox style_,quality_;
    juce::ToggleButton tp_,bypass_,unity_,audition_,dc_;
    std::array<float,180> history_{};
    size_t historyWrite_=0;
};

inline juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

}} // namespace APEX::Limiter
