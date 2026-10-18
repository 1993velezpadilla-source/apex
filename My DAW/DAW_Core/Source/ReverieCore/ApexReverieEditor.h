#pragma once
// APEX Reverie: original APEX/Bubblegum JUCE GUI, never copied artwork.
// Every rotary knob has an always-visible value and host automation gestures.
#include <JuceHeader.h>
#include <array>
#include "ApexReverieProcessor.h"
#include "../PluginHostCore/ApexEvoEyeStamp.h"

namespace APEX::Reverie
{
class ReverieLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,
                          float start,float end,juce::Slider&) override
    {
        const float cx=x+w*0.5f, cy=y+h*0.5f;
        const float radius=std::min(w,h)*0.34f;
        g.setColour(juce::Colour(0xff1e3043));
        g.fillEllipse(cx-radius-5,cy-radius-5,2*(radius+5),2*(radius+5));
        g.setColour(juce::Colour(0xff07121e));
        g.fillEllipse(cx-radius,cy-radius,2*radius,2*radius);
        juce::Path track;
        track.addCentredArc(cx,cy,radius+8,radius+8,0,start,end,true);
        g.setColour(juce::Colour(0xff365166));
        g.strokePath(track,juce::PathStrokeType(3.2f));
        juce::Path lit;
        lit.addCentredArc(cx,cy,radius+8,radius+8,0,start,start+(end-start)*position,true);
        g.setColour(juce::Colour(0xffff69b5));
        g.strokePath(lit,juce::PathStrokeType(3.7f));
        const float angle=start+(end-start)*position;
        g.setColour(juce::Colour(0xffa5f3f0));
        g.drawLine(cx,cy,cx+std::sin(angle)*radius*0.77f,
                    cy-std::cos(angle)*radius*0.77f,3.0f);
        g.setColour(juce::Colour(0xff253e54));
        g.fillEllipse(cx-5,cy-5,10,10);
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor& processor)
        : juce::AudioProcessorEditor(&processor),processor_(processor)
    {
        setLookAndFeel(&look_);
        setOpaque(true);
        setResizable(true,true);
        setResizeLimits(720,450,1600,840);

        mode_.setName("REVERB MODE");
        mode_.addItemList({"PURE PLATE","VINTAGE PLATE","HALL",
                           "CHAMBER","ROOM","AMBIENT"},1);
        mode_.onChange=[this]
        {
            auto& p=processor_.modeParameter();
            p.beginChangeGesture();
            p.setValueNotifyingHost(static_cast<float>(mode_.getSelectedId()-1)/5.0f);
            p.endChangeGesture();
        };
        addAndMakeVisible(mode_);

        era_.setName("COLOR ERA");
        era_.addItemList({"1970s","1980s","MODERN"},1);
        era_.onChange=[this]
        {
            auto& p=processor_.eraParameter();
            p.beginChangeGesture();
            p.setValueNotifyingHost(static_cast<float>(era_.getSelectedId()-1)/2.0f);
            p.endChangeGesture();
        };
        addAndMakeVisible(era_);

        bypass_.setButtonText("BYPASS");
        bypass_.setName("BYPASS");
        bypass_.onClick=[this]
        {
            auto& p=processor_.bypassParameter();
            p.beginChangeGesture();
            p.setValueNotifyingHost(bypass_.getToggleState()?1.0f:0.0f);
            p.endChangeGesture();
        };
        bypass_.setColour(juce::ToggleButton::textColourId,juce::Colour(0xffc8dcea));
        addAndMakeVisible(bypass_);

        static constexpr const char* captions[10]={
            "PRE-DELAY","DECAY","SIZE","DAMPING","LOW CUT",
            "HIGH CUT","MODULATION","WIDTH","MIX","VOCAL DUCK"
        };
        static constexpr double minValue[10]={
            0.0,0.2,0.0,750.0,20.0,1500.0,0.0,0.0,0.0,0.0
        };
        static constexpr double maxValue[10]={
            250.0,18.0,100.0,20000.0,1500.0,20000.0,100.0,200.0,100.0,100.0
        };
        for(int i=0;i<10;++i)
        {
            auto& knob=knobs_[static_cast<size_t>(i)];
            knob.setName(captions[i]);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,96,25);
            knob.setRange(minValue[i],maxValue[i],i==1?0.01:0.1);
            knob.setValue(processor_.floatAt(i+2).get(),juce::dontSendNotification);
            knob.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffdef5f8));
            knob.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff162838));
            knob.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff36536a));
            knob.onDragStart=[this,i]
            {
                active_=i;
                processor_.floatAt(i+2).beginChangeGesture();
            };
            knob.onDragEnd=[this,i]
            {
                processor_.floatAt(i+2).endChangeGesture();
                if(active_==i)active_=-1;
            };
            knob.onValueChange=[this,i]
            {
                auto& p=processor_.floatAt(i+2);
                p.setValueNotifyingHost(p.convertTo0to1(
                    static_cast<float>(knobs_[static_cast<size_t>(i)].getValue())));
            };
            addAndMakeVisible(knob);
            auto& label=labels_[static_cast<size_t>(i)];
            label.setText(captions[i],juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setColour(juce::Label::textColourId,juce::Colour(0xffbdd3df));
            label.setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));
            addAndMakeVisible(label);
        }
        mode_.setSelectedId(processor_.modeParameter().getIndex()+1,
                            juce::dontSendNotification);
        era_.setSelectedId(processor_.eraParameter().getIndex()+1,
                           juce::dontSendNotification);
        bypass_.setToggleState(processor_.bypassParameter().get(),
                               juce::dontSendNotification);
        setSize(980,540);
        startTimerHz(20);
    }
    ~Editor() override { stopTimer(); setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        const auto bounds=getLocalBounds().toFloat();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff101a29),0,0,
             juce::Colour(0xff08121e),bounds.getWidth(),bounds.getHeight(),false));
        g.fillAll();
        const auto pink=juce::Colour(0xffff69b5),cyan=juce::Colour(0xff72e0e6);
        g.setColour(juce::Colour(0xff243c50));
        g.fillRoundedRectangle(bounds.reduced(16.0f),15.0f);
        g.setColour(juce::Colour(0xff112438));
        g.fillRoundedRectangle(bounds.reduced(18.0f),13.0f);
        g.setColour(pink.withAlpha(0.7f));
        g.fillRoundedRectangle(30.0f,30.0f,5.0f,46.0f,2.0f);
        g.setFont(juce::Font(juce::FontOptions(30.0f,juce::Font::bold)));
        g.setColour(juce::Colours::white);
        g.drawText("APEX / REVERIE",48,23,getWidth()-100,45,juce::Justification::centredLeft);
        g.setColour(cyan);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("PLATE FIRST  /  VINTAGE DIGITAL  /  MODERN SPACES",
                   50,68,getWidth()-100,22,juce::Justification::centredLeft);
        APEX::Branding::EvoEyeStamp::paint(g,
            {(float)getWidth()-148.0f, 26.0f, 120.0f, 44.0f});
        const float mid=static_cast<float>(getHeight())*0.545f;
        g.setColour(juce::Colour(0xff20384a));
        g.drawLine(30.0f,mid,static_cast<float>(getWidth())-30.0f,mid,1.0f);
        g.setColour(pink.withAlpha(0.6f));
        g.fillRoundedRectangle(31.0f,static_cast<float>(getHeight())-32.0f,
            static_cast<float>(getWidth())-62.0f,3.0f,1.0f);
        g.setColour(juce::Colour(0xff7f9fb1));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("ORIGINAL APEX ALGORITHMIC DSP  •  0 SAMPLES PLUGIN LATENCY",
            34,getHeight()-27,getWidth()-68,20,juce::Justification::centred);
    }

    void resized() override
    {
        const int pad=30, gap=10;
        const int usable=getWidth()-pad*2;
        const int cell=usable/5;
        mode_.setBounds(pad+6,111,std::min(300,usable/3),37);
        era_.setBounds(pad+std::min(300,usable/3)+22,111,180,37);
        bypass_.setBounds(getWidth()-160,111,126,37);
        const int startY=170;
        const int rowSpace=(getHeight()-startY-50)/2;
        for(int i=0;i<10;++i)
        {
            const int c=i%5, row=i/5;
            const int x=pad+c*cell+(cell-gap-116)/2;
            const int y=startY+row*rowSpace;
            knobs_[static_cast<size_t>(i)].setBounds(x,y+22,116,
                std::max(105,rowSpace-28));
            labels_[static_cast<size_t>(i)].setBounds(
                pad+c*cell,y,cell-gap,22);
        }
    }
private:
    void timerCallback() override
    {
        for(int i=0;i<10;++i)
            if(i!=active_)
                knobs_[static_cast<size_t>(i)].setValue(
                    processor_.floatAt(i+2).get(),juce::dontSendNotification);
        const int nextMode=processor_.modeParameter().getIndex()+1;
        const int nextEra=processor_.eraParameter().getIndex()+1;
        if(mode_.getSelectedId()!=nextMode)
            mode_.setSelectedId(nextMode,juce::dontSendNotification);
        if(era_.getSelectedId()!=nextEra)
            era_.setSelectedId(nextEra,juce::dontSendNotification);
        bypass_.setToggleState(processor_.bypassParameter().get(),
                               juce::dontSendNotification);
    }
    Processor& processor_;
    ReverieLookAndFeel look_;
    juce::ComboBox mode_,era_;
    juce::ToggleButton bypass_;
    std::array<juce::Slider,10> knobs_{};
    std::array<juce::Label,10> labels_{};
    int active_=-1;
};

inline juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }
} // namespace APEX::Reverie
