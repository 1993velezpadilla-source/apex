#pragma once
// APEX native dark editor; included at the END of ApexTuneLiveProcessor.h.
// Uses hosted JUCE parameter gestures, preserving APEX automation Write/Read.
// No timers, component mutations or heap work on the audio callback.
#include "../TuneLiveCore/ApexTuneLivePresets.h"
#include "ApexTuneLiveVisualTheme.h"
#include <array>
#include <cmath>

namespace APEX { namespace TuneLive {

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor& processor)
      : juce::AudioProcessorEditor(processor), processor_(processor)
    {
        setOpaque(true);
        setLookAndFeel(&studioLook_);
        setResizable(true, true);
        // Controls/latency readout occupy rows through y=516; a 480px
        // editor cut off the last row. Enforce a true touch-safe minimum.
        setResizeLimits(720, 455, 1600, 1000);
        setSize(1200, 750);

        constexpr std::array<const char*,6> names{{
            "CORRECTION", "RE-TUNE", "HUMANIZE", "FLEX TUNE", "STABILITY", "WET"
        }};
        constexpr std::array<int,6> parameterIds{{
            Processor::kAmount, Processor::kSpeed, Processor::kHumanize,
            Processor::kFlexTune, Processor::kNoteStability, Processor::kMix
        }};
        for (std::size_t i=0; i<knobs_.size(); ++i) {
            auto& knob=knobs_[i];
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,88,27);
            knob.setRange(0.0,1.0,0.001);
            // User-facing engineering units; normalized parameter values
            // remain stable for existing DAW host automation and state.
            knob.textFromValueFunction=[i](double normalized) {
                if(i==1)
                    return juce::String(1.0+149.0*normalized,1)+" ms";
                return juce::String(juce::roundToInt(normalized*100.0))+" %";
            };
            knob.valueFromTextFunction=[i](const juce::String& value) {
                const double parsed=value.getDoubleValue();
                return juce::jlimit(0.0,1.0,i==1?(parsed-1.0)/149.0:parsed/100.0);
            };
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
            title.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
            addAndMakeVisible(title);

            auto* bound=&parameter(parameterIds[i]);
            knob.onDragStart=[bound](){bound->beginChangeGesture();};
            knob.onValueChange=[bound,&knob](){
                bound->setValueNotifyingHost(static_cast<float>(knob.getValue()));
            };
            knob.onDragEnd=[bound](){bound->endChangeGesture();};
        }

        // Both enharmonic spellings are shown explicitly (e.g. Ab and G#).
        // They intentionally map to the SAME existing 12 semitone values:
        // zero new parameter IDs, no automation or serialized-state change.
        for(std::size_t k=0;k<keyOptions_.size();++k)
            keys_.addItem(keyOptions_[k].name,static_cast<int>(k)+1);
        keys_.setSelectedId(1, juce::dontSendNotification);
        reference_.setSliderStyle(juce::Slider::LinearHorizontal);
        reference_.setRange(0.0,1.0,0.001);
        reference_.setTextBoxStyle(juce::Slider::TextBoxRight,false,108,24);
        reference_.textFromValueFunction=[](double normalized){
            return juce::String(430.0+normalized*20.0,1)+" Hz";
        };
        reference_.valueFromTextFunction=[](const juce::String& str){
            return juce::jlimit(0.0,1.0,(str.getDoubleValue()-430.0)/20.0);
        };
        reference_.setColour(juce::Slider::trackColourId,juce::Colour::fromRGB(230,161,98));
        reference_.setColour(juce::Slider::thumbColourId,juce::Colour::fromRGB(255,223,161));
        reference_.setColour(juce::Slider::textBoxTextColourId,juce::Colours::white);
        reference_.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour::fromRGB(36,33,44));
        reference_.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        reference_.setValue(parameter(Processor::kA4Reference).getValue(),juce::dontSendNotification);
        reference_.onDragStart=[this](){parameter(Processor::kA4Reference).beginChangeGesture();};
        reference_.onValueChange=[this](){
            parameter(Processor::kA4Reference).setValueNotifyingHost(static_cast<float>(reference_.getValue()));
        };
        reference_.onDragEnd=[this](){parameter(Processor::kA4Reference).endChangeGesture();};
        addAndMakeVisible(reference_);
        keys_.onChange=[this](){
            const int option=keys_.getSelectedId()-1;
            if(option>=0 && option<static_cast<int>(keyOptions_.size()))
                setDiscrete(Processor::kKey,keyOptions_[static_cast<std::size_t>(option)].semitone,11);
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
            button.setToggleState(i==0,juce::dontSendNotification);
            button.onClick=[this,i](){
                for(int j=0;j<3;++j)
                    styles_[static_cast<std::size_t>(j)].setToggleState(j==i,juce::dontSendNotification);
                applyStyle(static_cast<VocalStyle>(i));
            };
            addAndMakeVisible(button);
        }
        for(auto* label : {&pitchLabel_, &pitchCaption_, &latencyLabel_}) {
            label->setJustificationType(juce::Justification::centredLeft);
            label->setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(*label);
        }
        pitchLabel_.setFont(juce::Font(juce::FontOptions(72.0f).withStyle("Bold")));
        pitchLabel_.setJustificationType(juce::Justification::centred);
        pitchLabel_.setColour(juce::Label::textColourId,
                              juce::Colour::fromRGB(250,205,151));
        pitchCaption_.setFont(juce::Font(juce::FontOptions(12.0f)));
        pitchCaption_.setJustificationType(juce::Justification::centred);
        latencyLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
        startTimerHz(25); // GUI-only controls and pitch meter.
        timerCallback();
    }

    ~Editor() override { stopTimer(); setLookAndFeel(nullptr); }

    // All coordinates are expressed in the approved 1200x750 artboard.
    // Child controls are repositioned with the same transform in resized().
    // Every element is real JUCE vector paint, not a flattened screenshot.
    void paint(juce::Graphics& g) override {
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::scale(getWidth()/1200.0f,getHeight()/750.0f));
        g.setColour(Visual::midnight());
        g.fillRect(juce::Rectangle<float>(0.0f,0.0f,1200.0f,750.0f));

        Visual::panel(g,{13.0f,11.0f,1174.0f,86.0f},14.0f);
        // Original APEX warm metallic identity, laid out like the reference.
        g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(255,207,142),
                           32.0f,26.0f,juce::Colour::fromRGB(227,139,75),178.0f,74.0f,false));
        g.setFont(juce::Font(juce::FontOptions(35.0f).withStyle("Bold")));
        g.drawText("A P E X",34,23,214,41,juce::Justification::centredLeft);
        g.setColour(Visual::muted().withAlpha(0.7f));
        g.fillRect(239.0f,30.0f,1.0f,37.0f);
        g.setColour(Visual::ivory());
        g.setFont(juce::Font(juce::FontOptions(35.0f)));
        g.drawText("Tune Live",254,23,285,43,juce::Justification::centredLeft);
        g.setColour(Visual::muted());
        g.setFont(juce::Font(juce::FontOptions(9.4f).withStyle("Bold")));
        g.drawText("N A T I V E   V O C A L   C O R R E C T I O N    •    P O W E R E D   B Y   B U B B L E G U M   E N G I N E",
                   37,71,760,16,juce::Justification::centredLeft);
        // Decorative amplitude-free arc: NOT a fabricated live spectrum.
        for(int i=0;i<4;++i) {
            juce::Path wave;
            const float offset=static_cast<float>(i)*4.0f;
            for(int x=610;x<=1095;x+=5) {
                const float t=(x-610)/485.0f;
                const float y=55.0f+22.0f*std::sin(t*9.0f+offset*0.10f)
                       *std::sin(t*3.14159265f)+offset*0.40f;
                if(x==610)wave.startNewSubPath(static_cast<float>(x),y);
                else wave.lineTo(static_cast<float>(x),y);
            }
            g.setColour((i%2?Visual::gold():Visual::cyan()).withAlpha(0.23f));
            g.strokePath(wave,juce::PathStrokeType(0.8f));
        }
        g.setColour(Visual::gold());
        g.setFont(juce::Font(juce::FontOptions(39.0f).withStyle("Bold")));
        g.drawText("A",1125,23,50,47,juce::Justification::centred);

        Visual::panel(g,{16.0f,104.0f,1168.0f,210.0f});
        Visual::sectionTitle(g,"T R A C K E D   P I T C H",35.0f,116.0f);
        // Pitch grid and cents meter are real calibrated MIDI-cents readouts.
        constexpr float gridL=58.0f,gridR=754.0f,gridY=243.0f;
        for(int n=0;n<=20;++n) {
            const float x=gridL+(gridR-gridL)*n/20.0f;
            g.setColour(Visual::cyan().withAlpha(n%5==0?0.18f:0.08f));
            g.drawVerticalLine(static_cast<int>(x),165.0f,258.0f);
        }
        for(int n=0;n<77;++n) {
            const float x=gridL+9.0f*n;
            const float envelope=std::exp(-std::pow((n-39.0f)/31.0f,2.0f));
            const float wave=std::abs(std::sin(n*0.34f)*std::cos(n*0.19f));
            const float h=7.0f+37.0f*envelope*(0.20f+wave);
            const auto shade=n<40?Visual::cyan():Visual::gold();
            // In standby this is intentionally decorative, not an analyzer.
            g.setColour(shade.withAlpha(trackedHz_>=55.0f?0.50f:0.20f));
            g.fillRoundedRectangle(x,gridY-h,4.1f,h,1.1f);
        }
        g.setColour(Visual::muted().withAlpha(0.6f));
        g.drawHorizontalLine(static_cast<int>(gridY),gridL,gridR);
        for(int n=0;n<5;++n) {
            const int cents=(n-2)*50;
            const float x=gridL+(gridR-gridL)*n/4.0f;
            g.setColour(Visual::muted());
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText((cents>0?"+":"")+juce::String(cents),static_cast<int>(x)-24,
                       263,48,16,juce::Justification::centred);
        }
        const float indicator=juce::jlimit(-100.0f,100.0f,trackedCents_);
        const float markerX=gridL+(indicator+100.0f)*(gridR-gridL)/200.0f;
        if(trackedHz_>=55.0f) {
            g.setColour(Visual::gold().withAlpha(0.15f));
            g.fillRoundedRectangle(markerX-4.0f,184.0f,8.0f,60.0f,3.0f);
            g.setColour(Visual::gold());
            g.fillRoundedRectangle(markerX-1.0f,194.0f,2.0f,50.0f,1.0f);
        } else {
            g.setColour(Visual::cyan().withAlpha(0.36f));
            g.fillRect((gridL+gridR)*0.5f,220.0f,1.0f,24.0f);
        }
        g.setColour(Visual::cyan().withAlpha(0.35f));
        g.fillRoundedRectangle(268.0f,285.0f,278.0f,21.0f,8.0f);
        g.setColour(Visual::midnight());
        g.fillRoundedRectangle(269.0f,286.0f,276.0f,19.0f,8.0f);

        g.setColour(Visual::cyan().withAlpha(0.25f));
        g.fillRect(770.0f,126.0f,1.0f,166.0f);
        g.setColour(Visual::muted());
        g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
        g.drawText("K E Y",800,130,130,20,juce::Justification::centredLeft);
        g.drawText("S C A L E",974,130,150,20,juce::Justification::centredLeft);
        g.drawText("C O N C E R T   A 4",800,212,210,18,juce::Justification::centredLeft);

        Visual::panel(g,{16.0f,323.0f,1168.0f,258.0f});
        Visual::sectionTitle(g,"M U S I C A L   C O N T R O L",35.0f,335.0f);
        for(int k=1;k<6;++k) {
            const float x=36.0f+static_cast<float>(k)*193.0f;
            g.setColour(Visual::cyan().withAlpha(0.08f));
            g.fillRect(x,375.0f,1.0f,182.0f);
        }

        Visual::panel(g,{16.0f,590.0f,1168.0f,146.0f});
        Visual::sectionTitle(g,"V O C A L   S T Y L E",35.0f,601.0f);
        g.setColour(Visual::cyan().withAlpha(0.35f));
        g.fillRect(749.0f,621.0f,1.0f,98.0f);
        g.setColour(Visual::cyan().withAlpha(0.22f));
        g.fillRect(777.0f,686.0f,374.0f,1.0f);

        g.setColour(Visual::muted().withAlpha(0.5f));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("Realtime vocal tuning • Monitoring delay also depends on interface and DAW routing",
                   30,736,1050,13,juce::Justification::centredLeft);
    }

    void resized() override {
        const float sx=getWidth()/1200.0f,sy=getHeight()/750.0f;
        const auto box=[sx,sy](int x,int y,int w,int h){
            return juce::Rectangle<int>(juce::roundToInt(x*sx),juce::roundToInt(y*sy),
                                         std::max(1,juce::roundToInt(w*sx)),
                                         std::max(1,juce::roundToInt(h*sy)));
        };
        pitchLabel_.setBounds(box(220,138,374,102));
        pitchCaption_.setBounds(box(268,284,278,23));
        keys_.setBounds(box(797,159,163,41));
        scales_.setBounds(box(973,159,177,41));
        reference_.setBounds(box(787,231,365,50));
        constexpr std::array<int,6> x{{38,231,424,617,810,1003}};
        for(std::size_t i=0;i<knobs_.size();++i) {
            knobLabels_[i].setBounds(box(x[i],365,168,23));
            knobs_[i].setBounds(box(x[i]+6,392,156,169));
        }
        for(int i=0;i<3;++i)
            styles_[static_cast<std::size_t>(i)].setBounds(box(49+i*228,640,211,57));
        formants_.setBounds(box(788,640,246,40));
        bypass_.setBounds(box(1037,640,137,40));
        latencyLabel_.setBounds(box(792,688,360,26));
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
        setParameter(Processor::kFlexTune,profile.flexTune);
        setParameter(Processor::kNoteStability,profile.stability);
        setParameter(Processor::kMix,profile.mix);
        setParameter(Processor::kPreserveFormants,profile.preserveFormants?1.0f:0.0f);
        // Key + scale remain under the artist's control when switching styles.
        timerCallback();
    }

    void timerCallback() override {
        static constexpr std::array<int,6> ids{{
            Processor::kAmount, Processor::kSpeed, Processor::kHumanize,
            Processor::kFlexTune, Processor::kNoteStability, Processor::kMix
        }};
        for(std::size_t i=0;i<ids.size();++i)
            if(!knobs_[i].isMouseButtonDown())
                knobs_[i].setValue(parameter(ids[i]).getValue(),juce::dontSendNotification);
        const int activeKey=juce::jlimit(0,11,juce::roundToInt(parameter(Processor::kKey).getValue()*11));
        const int chosenIndex=keys_.getSelectedId()-1;
        // Keep the artist's Ab/Bb spelling if the parameter pitch class is
        // unchanged. Automation/state recall may choose the canonical sharp
        // option because a 12-tone numeric key cannot persist spelling.
        if(chosenIndex<0 || chosenIndex>=static_cast<int>(keyOptions_.size())
           || keyOptions_[static_cast<std::size_t>(chosenIndex)].semitone!=activeKey) {
            for(std::size_t k=0;k<keyOptions_.size();++k)
                if(keyOptions_[k].semitone==activeKey) {
                    keys_.setSelectedId(static_cast<int>(k)+1,juce::dontSendNotification);
                    break;
                }
        }
        scales_.setSelectedId(1+juce::jlimit(0,2,juce::roundToInt(parameter(Processor::kScale).getValue()*2)),
                              juce::dontSendNotification);
        if(!reference_.isMouseButtonDown())
            reference_.setValue(parameter(Processor::kA4Reference).getValue(),juce::dontSendNotification);
        formants_.setToggleState(parameter(Processor::kPreserveFormants).getValue()>=0.5f,
                                 juce::dontSendNotification);
        bypass_.setToggleState(parameter(Processor::kBypass).getValue()>=0.5f,
                               juce::dontSendNotification);
        const auto hz=processor_.detectedFrequencyHz();
        trackedHz_=hz;
        trackedCents_=0.0f;
        if(hz>=50.0f && std::isfinite(hz)) {
            const float reference=430.0f+20.0f*parameter(Processor::kA4Reference).getValue();
            const float midi=69.0f+12.0f*std::log2(hz/reference);
            const int note=juce::jlimit(0,127,juce::roundToInt(midi));
            trackedCents_=(midi-static_cast<float>(note))*100.0f;
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
        latencyLabel_.setText("PLUGIN PDC    "+juce::String(processor_.getLatencyForDisplay())
             +" samples",juce::dontSendNotification);
        repaint(); // GUI thread only: update live pitch pointer and note styling.
    }

    struct KeyOption { const char* name; int semitone; };
    inline static constexpr std::array<KeyOption,17> keyOptions_{{
        {"C",0},{"C#",1},{"Db",1},{"D",2},{"D#",3},{"Eb",3},
        {"E",4},{"F",5},{"F#",6},{"Gb",6},{"G",7},{"G#",8},
        {"Ab",8},{"A",9},{"A#",10},{"Bb",10},{"B",11}
    }};
    Processor& processor_;
    Visual::StudioLookAndFeel studioLook_;
    float trackedHz_=0.0f;
    float trackedCents_=0.0f;
    std::array<juce::Slider,6> knobs_;
    std::array<juce::Label,6> knobLabels_;
    std::array<juce::TextButton,3> styles_;
    juce::ComboBox keys_,scales_;
    juce::Slider reference_;
    juce::ToggleButton formants_,bypass_;
    juce::Label pitchLabel_,pitchCaption_,latencyLabel_;
};

}} // namespace APEX::TuneLive
