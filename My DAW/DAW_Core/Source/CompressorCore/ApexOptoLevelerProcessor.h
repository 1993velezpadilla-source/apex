#pragma once

// APEX Opto 2A — experimental native insert wrapper.
// The DSP is an original optical-inspired prototype, NOT yet a measured
// 1:1 hardware match. Do not ship this as a production emulation.
#include <JuceHeader.h>
#include "ApexOptoLevelerCore.h"
#include "ApexOptoVuBallisticsCore.h"
#include <array>
#include <atomic>
#include <memory>

namespace APEX::Compressor
{

enum ParameterIndex : int
{
    PeakReduction = 0, Gain, Mix, Emphasis, ColorDrive,
    Limit, ColorEnabled, Bypass, Voicing, ParameterCount
};

class OptoHostedParameter final : public juce::AudioProcessorParameterWithID
{
public:
    OptoHostedParameter (const juce::ParameterID& id, const juce::String& name,
                         float low, float high, float initial, bool toggle = false,
                         int discreteSteps = 0)
        : juce::AudioProcessorParameterWithID(id,name),
          minimum_(low), maximum_(high), initial_(initial), toggle_(toggle),
          discreteSteps_(discreteSteps),
          normalised_(toNormalised(initial))
    {
    }

    float getValue() const override { return normalised_.load(std::memory_order_relaxed); }
    void setValue (float n) override
    {
        if (std::isfinite(n))
        {
            float next=juce::jlimit(0.0f,1.0f,n);
            if (discreteSteps_>1)
                next=std::round(next*static_cast<float>(discreteSteps_-1))
                    /static_cast<float>(discreteSteps_-1);
            normalised_.store(next,std::memory_order_relaxed);
        }
    }
    float getDefaultValue() const override { return toNormalised(initial_); }
    juce::String getText(float n, int) const override
    {
        if (toggle_) return n >= 0.5f ? "On" : "Off";
        if (discreteSteps_ == 3)
        {
            static constexpr const char* names[]={"FAST","CLASSIC","SLOW"};
            return names[juce::jlimit(0,2,static_cast<int>(std::lround(fromNormalised(n))))];
        }
        return juce::String(fromNormalised(n), 2);
    }
    float getValueForText(const juce::String& s) const override
    {
        if (toggle_)
            return (s.equalsIgnoreCase("on") || s == "1" || s.equalsIgnoreCase("true")) ? 1.0f : 0.0f;
        if (discreteSteps_ == 3)
        {
            if (s.equalsIgnoreCase("FAST")) return 0.0f;
            if (s.equalsIgnoreCase("CLASSIC")) return 0.5f;
            if (s.equalsIgnoreCase("SLOW")) return 1.0f;
        }
        return toNormalised(s.getFloatValue());
    }
    int getNumSteps() const override
    {
        return toggle_ ? 2 : (discreteSteps_ > 1 ? discreteSteps_
            : juce::AudioProcessorParameter::getDefaultNumParameterSteps());
    }
    bool isBoolean() const override { return toggle_; }
    bool isDiscrete() const override { return toggle_ || discreteSteps_ > 1; }
    float toNormalised(float units) const noexcept
    {
        return juce::jlimit(0.0f,1.0f,(units - minimum_)/(maximum_ - minimum_));
    }
    float fromNormalised(float n) const noexcept
    {
        if (toggle_) return n >= 0.5f ? 1.0f : 0.0f;
        return discreteSteps_ > 1 ? std::round(n * (discreteSteps_ - 1))
            : minimum_ + n * (maximum_ - minimum_);
    }
    float getUnits() const noexcept { return fromNormalised(getValue()); }
private:
    const float minimum_, maximum_, initial_;
    const bool toggle_;
    const int discreteSteps_;
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
    static constexpr const char* kVersion = "0.2.0-integration";
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
        add(Voicing, "opto.voice", "Optical Voice", 0, 2, 1, false, 3);
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
        vu_.prepare(sampleRate);
        dsp_.setParameters(readParameters());
        meterReductionDb_.store(0.0f,std::memory_order_relaxed);
        outputRmsDbfs_.store(-120.0f,std::memory_order_relaxed);
        setLatencySamples(0);
    }
    void releaseResources() override {}
    void reset() override { dsp_.reset(); vu_.reset(); }

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
        // Hardware-style output meter dynamics are display-only. Analogue
        // 300 ms VU ballistics are accumulated at sample rate independently of
        // host buffer size and published once per block for the GUI.
        vu_.process(buffer.getReadPointer(0),
                    channels >= 2 ? buffer.getReadPointer(1) : nullptr,
                    buffer.getNumSamples());
        outputRmsDbfs_.store(static_cast<float>(vu_.getIndicatedRmsDbfs()),
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
    float getOutputRmsDbfs() const noexcept
    {
        return outputRmsDbfs_.load(std::memory_order_relaxed);
    }
    int getMeterMode() const noexcept
    {
        return meterMode_.load(std::memory_order_relaxed);
    }
    void setMeterMode(int choice) noexcept
    {
        meterMode_.store(juce::jlimit(0,2,choice),std::memory_order_relaxed);
    }

    void getStateInformation(juce::MemoryBlock& result) override
    {
        juce::ValueTree state(kStateName);
        state.setProperty("version",2,nullptr);
        state.setProperty("opto.meter.mode",getMeterMode(),nullptr);
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
        const juce::Identifier meterId("opto.meter.mode");
        if(state.hasProperty(meterId))
            setMeterMode(static_cast<int>(state.getProperty(meterId)));
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
            "opto.drive","opto.limit","opto.color","opto.bypass",
            "opto.voice"
        };
        return ids[index];
    }
    void add(int index,const char* id,const char* name,float low,float high,
             float initial,bool toggle=false,int discreteSteps=0)
    {
        auto value=std::make_unique<OptoHostedParameter>(
            juce::ParameterID(id,1),name,low,high,initial,toggle,discreteSteps);
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
        p.voicing=static_cast<int>(params_[Voicing]->getUnits());
        return p;
    }

    std::array<OptoHostedParameter*,ParameterCount> params_ {};
    ApexOptoLevelerCore dsp_;
    OptoVuBallisticsCore vu_;
    std::atomic<float> meterReductionDb_{0.0f};
    std::atomic<float> outputRmsDbfs_{-120.0f};
    std::atomic<int> meterMode_{0};
};

} // namespace APEX::Compressor

// Keep custom editor in its own compilation unit for clean DSP / UI ownership.
// Header-only editor definition requires no change to Projucer source lists.
#include "ApexOptoLevelerEditor.h"
