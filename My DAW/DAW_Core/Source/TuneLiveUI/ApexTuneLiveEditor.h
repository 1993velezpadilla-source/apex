#pragma once
// APEX native dark editor; included at the END of ApexTuneLiveProcessor.h.
// Uses hosted JUCE parameter gestures, preserving APEX automation Write/Read.
// No timers, component mutations or heap work on the audio callback.
#include "../TuneLiveCore/ApexTuneLivePresets.h"
#include <array>
#include <cmath>

namespace APEX { namespace TuneLive {

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor& processor)
      : juce::AudioProcessorEditor(processor), processor_(processor)
    {
        setOpaque(true);
        setResizable(true, true);
        setResizeLimits(620, 480, 1300, 900);
        setSize(900, 575);

        constexpr std::array<const char*,5> names{{
            "CORRECTION", "RE-TUNE", "HUMANIZE", "STABILITY", "WET"
        }};
        constexpr std::array<int,5> parameterIds{{
            Processor::kAmount, Processor::kSpeed, Processor::kHumanize,
            Processor::kNoteStability, Processor::kMix
        }};
        for (std::size_t i=0; i<knobs_.size(); ++i) {
            auto& knob=knobs_[i];
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,88,27);
            knob.setRange(0.0,1.0,0.001);
            knob.setColour(juce::Slider::rotarySliderFillColourId,
                           juce::Colour::fromRGB(238,173,107));
            knob.setColour(juce::Slider::rotarySliderOutlineColourId,
                           juce::Colour::fromRGB(58,54,67));
            knob.setColour(juce::Slider::thumbColourId,
                           juce::Colour::fromRGB(250,211,142));
            knob.setColour(juce::Slider::textBoxTextColourId,juce::Colours::white);
            knob.setColour(juce::Slider::textBoxOutlineColourId,
                           juce::Colours::transparentBlack);
            knob.setColour(juce::Slider::textBoxBackgroundColourId,
                           juce::Colour::fromRGB(36,33,44));
            knob.setValue(parameter(parameterIds[i]).getValue(), juce::dontSendNotification);
            addAndMakeVisible(knob);

            auto& title=knobLabels_[i];
            title.setText(names[i],juce::dontSendNotification);
            title.setJustificationType(juce::Justification::centred);
            title.setColour(juce::Label::textColourId,
                            juce::Colour::fromRGB(224,214,228));
            title.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
            addAndMakeVisible(title);

            auto* bound=&parameter(parameterIds[i]);
            knob.onDragStart=[bound](){bound->beginChangeGesture();};
            knob.onValueChange=[bound,&knob](){
                bound->setValueNotifyingHost(static_cast<float>(knob.getValue()));
            };
            knob.onDragEnd=[bound](){bound->endChangeGesture();};
        }

        for(int k=0;k<12;++k)
            keys_.addItem(juce::StringArray{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}[k],k+1);
        keys_.setSelectedId(1, juce::dontSendNotification);
        keys_.onChange=[this](){
            setDiscrete(Processor::kKey, keys_.getSelectedId()-1, 11);
        };
        addAndMakeVisible(keys_);

        for(int i=0;i<3;++i)
            scales_.addItem(juce::StringArray{"Chromatic","Major","Minor"}[i],i+1);
        scales_.setSelectedId(1,juce::dontSendNotification);
        scales_.onChange=[this](){setDiscrete(Processor::kScale,scales_.getSelectedId()-1,2);};
        addAndMakeVisible(scales_);

        formants_.setButtonText("PRESERVE FORMANTS");
        formants_.setColour(juce::ToggleButton::textColourId,juce::Colours::white);
        formants_.setToggleState(true,juce::dontSendNotification);
        formants_.onClick=[this](){setParameter(Processor::kPreserveFormants,
                             formants_.getToggleState()?1.0f:0.0f);};
        addAndMakeVisible(formants_);

        bypass_.setButtonText("BYPASS");
        bypass_.setColour(juce::ToggleButton::textColourId,juce::Colours::white);
        bypass_.onClick=[this](){setParameter(Processor::kBypass,
                           bypass_.getToggleState()?1.0f:0.0f);};
        addAndMakeVisible(bypass_);

        for(int i=0;i<3;++i) {
            auto& button=styles_[static_cast<std::size_t>(i)];
            button.setButtonText(kVocalStyles[static_cast<std::size_t>(i)].name);
            button.setColour(juce::TextButton::buttonColourId,
                             juce::Colour::fromRGB(56,48,67));
            button.setColour(juce::TextButton::buttonOnColourId,
                             juce::Colour::fromRGB(144,91,60));
            button.setColour(juce::TextButton::textColourOffId,juce::Colours::white);
            button.onClick=[this,i](){applyStyle(static_cast<VocalStyle>(i));};
            addAndMakeVisible(button);
        }
        startTimerHz(25); // GUI-only controls and pitch meter.
    }

    ~Editor() override { stopTimer(); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(23,22,30));
        auto r=getLocalBounds().toFloat();
        g.setColour(juce::Colour::fromRGB(34,32,44));
        g.fillRoundedRectangle(juce::Rectangle<float>(18.0f,70.0f,r.getWidth()-36.0f,131.0f),13.0f);
        g.setColour(juce::Colour::fromRGB(242,190,126));
        g.setFont(juce::Font(juce::FontOptions(27.0f,juce::Font::bold)));
        g.drawText("APEX  /  TUNE LIVE",24,15,getWidth()-48,38,juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGB(176,165,180));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("NATIVE VOCAL CORRECTION  -  POWERED BY BUBBLEGUM ENGINE",
                   25,49,getWidth()-48,17,juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGB(174,165,180));
        g.setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));
        g.drawText("TRACKED PITCH",40,84,150,18,juce::Justification::centredLeft);
        g.drawText("KEY",getWidth()-330,84,90,18,juce::Justification::centredLeft);
        g.drawText("SCALE",getWidth()-205,84,150,18,juce::Justification::centredLeft);

        g.setColour(juce::Colour::fromRGB(39,36,48));
        g.fillRoundedRectangle(juce::Rectangle<float>(18.0f,211.0f,r.getWidth()-36.0f,202.0f),12.0f);
        g.setColour(juce::Colour::fromRGB(167,158,172));
        g.setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));
        g.drawText("MUSICAL CONTROL",36,225,getWidth()-72,18,juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGB(113,105,126));
        g.drawHorizontalLine(424,22.0f,static_cast<float>(getWidth()-22));
        g.setColour(juce::Colour::fromRGB(167,158,172));
        g.drawText("VOCAL STYLE",29,435,145,17,juce::Justification::centredLeft);

        g.setColour(juce::Colour::fromRGB(126,120,138));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("Actual monitoring delay includes the audio interface, DAW routing and this plugin.",
                  20,getHeight()-26,getWidth()-40,17,juce::Justification::centredLeft);
    }

    void resized() override {
        const int w=getWidth();
        keys_.setBounds(w-332,110,110,39);
        scales_.setBounds(w-210,110,174,39);
        pitchLabel_.setBounds(38,109,std::max(180,w-425),42);
        pitchCaption_.setBounds(39,154,std::max(180,w-425),27);

        const int available=w-48;
        const int cell=available/5;
        for(int i=0;i<5;++i) {
            const int x=25+i*cell;
            knobLabels_[static_cast<std::size_t>(i)].setBounds(x,250,cell-5,24);
            knobs_[static_cast<std::size_t>(i)].setBounds(x,271,cell-7,128);
        }
        const int buttonsW=std::min(150, (w-240)/3);
        for(int i=0;i<3;++i)
            styles_[static_cast<std::size_t>(i)].setBounds(28+i*(buttonsW+10),463,buttonsW,38);
        formants_.setBounds(w-315,459,193,29);
        bypass_.setBounds(w-117,459,90,29);
        latencyLabel_.setBounds(w-335,492,310,24);
    }

private:
    juce::AudioProcessorParameter& parameter(int index) const {
        return *processor_.getParameters()[index];
    }
    void setParameter(int index,float normalised) {
        auto& p=parameter(index);
        p.beginChangeGesture();
        p.setValueNotifyingHost(juce::jlimit(0.0f,1.0f,normalised));
        p.endChangeGesture();
    }
    void setDiscrete(int index,int value,int max) {
        if(value < 0 || value > max)return;
        setParameter(index,static_cast<float>(value)/static_cast<float>(max));
    }
    void applyStyle(VocalStyle style) {
        const auto& profile=preset(style);
        setParameter(Processor::kAmount,profile.amount);
        setParameter(Processor::kSpeed,profile.retune);
        setParameter(Processor::kHumanize,profile.humanize);
        setParameter(Processor::kNoteStability,profile.stability);
        setParameter(Processor::kMix,profile.mix);
        setParameter(Processor::kPreserveFormants,profile.preserveFormants?1.0f:0.0f);
        // Key + scale remain under the artist's control when switching styles.
        timerCallback();
    }

    void timerCallback() override {
        static constexpr std::array<int,5> ids{{
            Processor::kAmount, Processor::kSpeed, Processor::kHumanize,
            Processor::kNoteStability, Processor::kMix
        }};
        for(std::size_t i=0;i<ids.size();++i)
            if(!knobs_[i].isMouseButtonDown())
                knobs_[i].setValue(parameter(ids[i]).getValue(),juce::dontSendNotification);
        keys_.setSelectedId(1+juce::jlimit(0,11,juce::roundToInt(parameter(Processor::kKey).getValue()*11)),
                            juce::dontSendNotification);
        scales_.setSelectedId(1+juce::jlimit(0,2,juce::roundToInt(parameter(Processor::kScale).getValue()*2)),
                              juce::dontSendNotification);
        formants_.setToggleState(parameter(Processor::kPreserveFormants).getValue()>=0.5f,
                                 juce::dontSendNotification);
        bypass_.setToggleState(parameter(Processor::kBypass).getValue()>=0.5f,
                               juce::dontSendNotification);
        const auto hz=processor_.detectedFrequencyHz();
        if(hz>=50.0f && std::isfinite(hz)) {
            const float midi=69.0f+12.0f*std::log2(hz/440.0f);
            const int note=juce::jlimit(0,127,juce::roundToInt(midi));
            constexpr std::array<const char*,12> names{{
                "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
            }};
            pitchLabel_.setText(juce::String(names[static_cast<std::size_t>(note%12)])
                +juce::String(note/12-1),juce::dontSendNotification);
            pitchCaption_.setText(juce::String((midi-note)*100.0f,1)
                +" cents   /   "+juce::String(hz,1)+" Hz",juce::dontSendNotification);
        } else {
            pitchLabel_.setText("--",juce::dontSendNotification);
            pitchCaption_.setText("Waiting for monophonic voice",juce::dontSendNotification);
        }
        latencyLabel_.setText("PLUGIN PDC  "+juce::String(processor_.getLatencyForDisplay())
             +" samples",juce::dontSendNotification);
    }

    Processor& processor_;
    std::array<juce::Slider,5> knobs_;
    std::array<juce::Label,5> knobLabels_;
    std::array<juce::TextButton,3> styles_;
    juce::ComboBox keys_,scales_;
    juce::ToggleButton formants_,bypass_;
    juce::Label pitchLabel_,pitchCaption_,latencyLabel_;
};

}} // namespace APEX::TuneLive
