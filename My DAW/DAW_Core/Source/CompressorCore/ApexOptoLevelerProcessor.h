#pragma once

// APEX Opto 2A — experimental native insert wrapper.
// The DSP is an original optical-inspired prototype, NOT yet a measured
// 1:1 hardware match. Do not ship this as a production emulation.
#include <JuceHeader.h>
#include "ApexOptoLevelerCore.h"
#include <array>
#include <atomic>
#include <memory>

namespace APEX::Compressor
{

enum ParameterIndex : int
{
    PeakReduction = 0, Gain, Mix, Emphasis, ColorDrive,
    Limit, ColorEnabled, Bypass, ParameterCount
};

class OptoHostedParameter final : public juce::AudioProcessorParameterWithID
{
public:
    OptoHostedParameter (const juce::ParameterID& id, const juce::String& name,
                         float low, float high, float initial, bool toggle = false)
        : juce::AudioProcessorParameterWithID(id,name),
          minimum_(low), maximum_(high), initial_(initial), toggle_(toggle),
          normalised_(toNormalised(initial))
    {
    }

    float getValue() const override { return normalised_.load(std::memory_order_relaxed); }
    void setValue (float n) override
    {
        if (std::isfinite(n))
            normalised_.store(juce::jlimit(0.0f, 1.0f, n),std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return toNormalised(initial_); }
    juce::String getText(float n, int) const override
    {
        if (toggle_) return n >= 0.5f ? "On" : "Off";
        return juce::String(fromNormalised(n), 2);
    }
    float getValueForText(const juce::String& s) const override
    {
        if (toggle_)
            return (s.equalsIgnoreCase("on") || s == "1" || s.equalsIgnoreCase("true")) ? 1.0f : 0.0f;
        return toNormalised(s.getFloatValue());
    }
    int getNumSteps() const override
    {
        return toggle_ ? 2 : juce::AudioProcessorParameter::getDefaultNumParameterSteps();
    }
    bool isBoolean() const override { return toggle_; }
    bool isDiscrete() const override { return toggle_; }
    float toNormalised(float units) const noexcept
    {
        return juce::jlimit(0.0f,1.0f,(units - minimum_)/(maximum_ - minimum_));
    }
    float fromNormalised(float n) const noexcept
    {
        return toggle_ ? (n >= 0.5f ? 1.0f : 0.0f) : minimum_ + n * (maximum_ - minimum_);
    }
    float getUnits() const noexcept { return fromNormalised(getValue()); }
private:
    const float minimum_, maximum_, initial_;
    const bool toggle_;
    std::atomic<float> normalised_ { 0.0f };
};

class OptoProcessor final : public juce::AudioPluginInstance
{
public:
    static constexpr int kUniqueId = 0x4F503241; // OP2A
    static constexpr const char* kPluginName = "APEX Opto 2A";
    static constexpr const char* kFormatName = "APEX Native";
    static constexpr const char* kFileOrIdentifier = "APEX::Opto2A";
    static constexpr const char* kCategory = "Dynamics";
    static constexpr const char* kManufacturer = "APEX";
    static constexpr const char* kVersion = "0.1.0-experimental";
    static constexpr const char* kStateName = "apexopto2astate";

    OptoProcessor()
        : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        add(PeakReduction, "opto.peak", "Peak Reduction", 0, 100, 0);
        add(Gain, "opto.gain", "Gain (dB)", 0, 40, 0);
        add(Mix, "opto.mix", "Parallel Mix", 0, 100, 100);
        add(Emphasis, "opto.emphasis", "Sidechain Emphasis", 0, 100, 0);
        add(ColorDrive, "opto.drive", "Color Drive", 0.25f, 4.0f, 1.0f);
        add(Limit, "opto.limit", "Limit", 0, 1, 0, true);
        add(ColorEnabled, "opto.color", "Color (Experimental)", 0, 1, 0, true);
        add(Bypass, "opto.bypass", "Bypass", 0, 1, 0, true);
    }

    const juce::String getName() const override { return kPluginName; }
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name=kPluginName;d.descriptiveName=kPluginName;d.pluginFormatName=kFormatName;
        d.category=kCategory;d.manufacturerName=kManufacturer;d.version=kVersion;
        d.fileOrIdentifier=kFileOrIdentifier;d.uniqueId=kUniqueId;
        d.deprecatedUid=kUniqueId;d.isInstrument=false;
        d.numInputChannels=2;d.numOutputChannels=2;
    }

    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        const auto input=l.getMainInputChannelSet(), output=l.getMainOutputChannelSet();
        return input == output
            && (input == juce::AudioChannelSet::stereo() || input == juce::AudioChannelSet::mono());
    }

    void prepareToPlay(double sampleRate,int blockSize) override
    {
        setRateAndBufferSizeDetails(sampleRate,blockSize);
        dsp_.prepare(sampleRate);
        dsp_.setParameters(readParameters());
        meterReductionDb_.store(0.0f,std::memory_order_relaxed);
        setLatencySamples(0);
    }
    void releaseResources() override {}
    void reset() override { dsp_.reset(); }

    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override
    {
        const int channels=buffer.getNumChannels();
        if (channels<1 || buffer.getNumSamples()<1) return;
        dsp_.setParameters(readParameters());
        dsp_.process(buffer.getWritePointer(0),
                     channels >= 2 ? buffer.getWritePointer(1) : nullptr,
                     buffer.getNumSamples());
        meterReductionDb_.store(static_cast<float>(dsp_.getGainReductionDb()),
                                std::memory_order_relaxed);
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return params_[Bypass];
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    OptoHostedParameter& getParameterAt(int index) noexcept { return *params_[index]; }
    float getGainReductionMeterDb() const noexcept
    {
        return meterReductionDb_.load(std::memory_order_relaxed);
    }

    void getStateInformation(juce::MemoryBlock& result) override
    {
        juce::ValueTree state(kStateName);
        state.setProperty("version",1,nullptr);
        for (int i=0;i<ParameterCount;++i)
            state.setProperty(juce::Identifier(getParamId(i)),params_[i]->getValue(),nullptr);
        juce::MemoryOutputStream stream(result,false);
        state.writeToStream(stream);
    }
    void setStateInformation(const void* input,int bytes) override
    {
        if(input==nullptr || bytes<=0) return;
        const auto state=juce::ValueTree::readFromData(input,static_cast<size_t>(bytes));
        if(!state.isValid() || !state.hasType(juce::Identifier(kStateName))) return;
        for(int i=0;i<ParameterCount;++i)
        {
            const auto id=juce::Identifier(getParamId(i));
            if(!state.hasProperty(id)) continue;
            const auto normalised=static_cast<float>(state.getProperty(id));
            if(std::isfinite(normalised))
                params_[i]->setValue(normalised);
        }
    }
private:
    static const char* getParamId(int index) noexcept
    {
        static constexpr const char* ids[ParameterCount]={
            "opto.peak","opto.gain","opto.mix","opto.emphasis",
            "opto.drive","opto.limit","opto.color","opto.bypass"
        };
        return ids[index];
    }
    void add(int index,const char* id,const char* name,float low,float high,
             float initial,bool toggle=false)
    {
        auto value=std::make_unique<OptoHostedParameter>(
            juce::ParameterID(id,1),name,low,high,initial,toggle);
        params_[index]=value.get();
        addHostedParameter(std::move(value));
    }
    OptoParameters readParameters() const noexcept
    {
        OptoParameters p;
        p.peakReduction=params_[PeakReduction]->getUnits();
        p.gainDb=params_[Gain]->getUnits();
        p.mix=params_[Mix]->getUnits()/100.0;
        p.emphasis=params_[Emphasis]->getUnits()/100.0;
        p.colorDrive=params_[ColorDrive]->getUnits();
        p.limit=params_[Limit]->getUnits()>0.5f;
        p.colorEnabled=params_[ColorEnabled]->getUnits()>0.5f;
        p.bypass=params_[Bypass]->getUnits()>0.5f;
        return p;
    }

    std::array<OptoHostedParameter*,ParameterCount> params_ {};
    ApexOptoLevelerCore dsp_;
    std::atomic<float> meterReductionDb_{0.0f};
};

class OptoEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OptoEditor(OptoProcessor& processor)
      : juce::AudioProcessorEditor(&processor),processor_(processor)
    {
        using ST=juce::Slider;
        configureKnob(peak_,PeakReduction,"PEAK REDUCTION",0,100,"");
        configureKnob(gain_,Gain,"OUTPUT GAIN",0,40," dB");
        configureKnob(mix_,Mix,"DRY / WET",0,100," %");
        configureKnob(emphasis_,Emphasis,"KEY EMPHASIS",0,100," %");
        configureKnob(drive_,ColorDrive,"COLOR DRIVE",0.25,4.0,"");
        configureToggle(limit_,Limit,"LIMIT");
        configureToggle(color_,ColorEnabled,"COLOR");
        configureToggle(bypass_,Bypass,"BYPASS");
        setOpaque(true);
        setSize(780,385);
        startTimerHz(25);
    }

    void paint(juce::Graphics& g) override
    {
        using C=juce::Colour;
        const auto b=getLocalBounds().toFloat();
        g.fillAll(C(0xff10111b));
        g.setColour(C(0xff292a3e));
        g.fillRoundedRectangle(b.reduced(12.0f),16.0f);
        g.setColour(C(0xff121422));
        g.fillRoundedRectangle(20.0f,70.0f,b.getWidth()-40.0f,200.0f,13.0f);
        g.setColour(C(0xfffd3f99));
        g.setFont(juce::Font(juce::FontOptions(23.0f,juce::Font::bold)));
        g.drawText("APEX  /  OPTO 2A",30,22,405,34,juce::Justification::centredLeft);
        g.setColour(C(0xffb0b6c8));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("OPTICAL LEVELING  •  EXPERIMENTAL DSP",440,24,313,27,
                   juce::Justification::centredRight);
        const float x=55.0f,y=287.0f,w=650.0f;
        g.setColour(C(0xff31344b));
        g.fillRoundedRectangle(x,y,w,20.0f,9.0f);
        const double gr=juce::jlimit(0.0,36.0,
                                  static_cast<double>(processor_.getGainReductionMeterDb()));
        g.setColour(C(0xff20d7df));
        g.fillRoundedRectangle(x,y,static_cast<float>(w*gr/36.0),20.0f,9.0f);
        g.setColour(C(0xffb7bbd2));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("GAIN REDUCTION  " + juce::String(gr,1) + " dB",55,310,650,24,
                   juce::Justification::centredLeft);
        g.setColour(C(0xff788198));
        g.drawText("UNVERIFIED T4 MODEL  •  ZERO-LATENCY PARALLEL",28,347,720,24,
                   juce::Justification::centred);
    }
    void resized() override
    {
        const int left=42, top=99, step=142;
        std::array<juce::Slider*,5> sliders {&peak_,&gain_,&mix_,&emphasis_,&drive_};
        for (int i=0;i<5;++i)
            sliders[i]->setBounds(left+i*step,top,124,148);
        limit_.setBounds(50,252,120,28);
        color_.setBounds(211,252,120,28);
        bypass_.setBounds(372,252,120,28);
    }

private:
    void configureKnob(juce::Slider& slider,int index,const juce::String& title,
                       double low,double high,const juce::String& suffix)
    {
        slider.setName(title);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,24);
        slider.setRange(low,high,0.01);
        slider.setTextValueSuffix(suffix);
        slider.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xfffd3f99));
        slider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xff393d56));
        slider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffedf0ff));
        slider.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff202437));
        slider.setTooltip(title);
        slider.setValue(processor_.getParameterAt(index).getUnits(),juce::dontSendNotification);
        auto& param=processor_.getParameterAt(index);
        slider.onDragStart=[&param]{param.beginChangeGesture();};
        slider.onDragEnd=[&param]{param.endChangeGesture();};
        slider.onValueChange=[&slider,&param]{
            param.setValueNotifyingHost(param.toNormalised(static_cast<float>(slider.getValue())));
        };
        addAndMakeVisible(slider);
    }
    void configureToggle(juce::ToggleButton& button,int index,const juce::String& label)
    {
        button.setButtonText(label);
        button.setColour(juce::ToggleButton::tickColourId,juce::Colour(0xfffd3f99));
        button.setToggleState(processor_.getParameterAt(index).getUnits()>=0.5,
                              juce::dontSendNotification);
        auto& param=processor_.getParameterAt(index);
        button.onClick=[&button,&param]{
            param.beginChangeGesture();
            param.setValueNotifyingHost(button.getToggleState()?1.0f:0.0f);
            param.endChangeGesture();
        };
        addAndMakeVisible(button);
    }
    void timerCallback() override
    {
        const std::array<std::pair<juce::Slider*,int>,5> sliders {{
            {&peak_,PeakReduction},{&gain_,Gain},{&mix_,Mix},
            {&emphasis_,Emphasis},{&drive_,ColorDrive}
        }};
        for(auto [slider,index]:sliders)
            if(!slider->isMouseButtonDown())
                slider->setValue(processor_.getParameterAt(index).getUnits(),
                                 juce::dontSendNotification);
        limit_.setToggleState(processor_.getParameterAt(Limit).getUnits()>=0.5,
                               juce::dontSendNotification);
        color_.setToggleState(processor_.getParameterAt(ColorEnabled).getUnits()>=0.5,
                               juce::dontSendNotification);
        bypass_.setToggleState(processor_.getParameterAt(Bypass).getUnits()>=0.5,
                               juce::dontSendNotification);
        repaint(juce::Rectangle<int>(50,285,680,55));
    }
    OptoProcessor& processor_;
    juce::Slider peak_,gain_,mix_,emphasis_,drive_;
    juce::ToggleButton limit_,color_,bypass_;
};

inline juce::AudioProcessorEditor* OptoProcessor::createEditor()
{
    return new OptoEditor(*this);
}

} // namespace APEX::Compressor
