#pragma once
#include <JuceHeader.h>
#include "ApexLimiterCore.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace APEX { namespace Limiter {

enum Parameter : int
{
    Gain, Ceiling, Lookahead, Attack, Release, ChannelLink, LimitingStyle,
    TruePeak, DetectorQuality, Bypass, UnityGain, Audition, DcFilter, Count
};

class LimiterHostedParameter final : public juce::AudioProcessorParameterWithID
{
public:
    LimiterHostedParameter(juce::ParameterID id, juce::String name,
                    float low, float high, float initial, int steps = 0)
        : juce::AudioProcessorParameterWithID(id,name), low_(low),high_(high),
          default_(initial), steps_(steps), value_(toNormal(initial)) {}
    float getValue() const override { return value_.load(std::memory_order_relaxed); }
    void setValue(float v) override
    {
        if (!std::isfinite(v)) return;
        v = juce::jlimit(0.0f,1.0f,v);
        if (steps_ > 1)
            v = std::round(v * (steps_-1)) / static_cast<float>(steps_-1);
        value_.store(v,std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return toNormal(default_); }
    int getNumSteps() const override
    {
        return steps_ > 1 ? steps_ : juce::AudioProcessorParameter::getDefaultNumParameterSteps();
    }
    bool isBoolean() const override { return steps_ == 2; }
    bool isDiscrete() const override { return steps_ > 1; }
    juce::String getText(float normal,int) const override
    {
        const float v = toUnits(normal);
        return steps_ == 2 ? (v > 0.5f ? "On" : "Off") : juce::String(v,2);
    }
    float getValueForText(const juce::String& s) const override
    {
        if (steps_ == 2 && (s.equalsIgnoreCase("on") || s.equalsIgnoreCase("off")))
            return s.equalsIgnoreCase("on") ? 1.0f : 0.0f;
        return toNormal(s.getFloatValue());
    }
    float toNormal(float units) const noexcept
    { return juce::jlimit(0.0f,1.0f,(units-low_)/(high_-low_)); }
    float toUnits(float normal) const noexcept
    {
        const float value = low_ + juce::jlimit(0.0f,1.0f,normal)*(high_-low_);
        return steps_ > 1 ? std::round(value) : value;
    }
    float units() const noexcept { return toUnits(getValue()); }
private:
    float low_,high_,default_;
    int steps_;
    std::atomic<float> value_;
};

class Processor final : public juce::AudioPluginInstance
{
public:
    static constexpr int kUniqueId = 0x42474C4D; // BGLM
    static constexpr const char* kPluginName = "APEX Bubblegum Limiter";
    static constexpr const char* kFormatName = "APEX Native";
    static constexpr const char* kFileOrIdentifier = "APEX::BubblegumLimiter";
    static constexpr const char* kCategory = "Dynamics";
    static constexpr const char* kStateName = "apexbubblegumlimiterstate";

    Processor() : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true))
    {
        add(Gain,"lim.gain","Input Gain (dB)",0,30,0);
        add(Ceiling,"lim.ceiling","Output Ceiling (dBFS)",-30,0,-1);
        add(Lookahead,"lim.lookahead","Lookahead (ms)",0.1f,20,5);
        add(Attack,"lim.attack","Attack (ms)",0.01f,1000,0.5f);
        add(Release,"lim.release","Release (ms)",10,3000,140);
        add(ChannelLink,"lim.link","Stereo Link",0,100,100);
        add(LimitingStyle,"lim.style","Limiting Style",0,7,5,8);
        add(TruePeak,"lim.tp","Inter-sample Peak Guard",0,1,1,2);
        add(DetectorQuality,"lim.quality","Detector Resolution",0,3,2,4);
        add(Bypass,"lim.bypass","Bypass",0,1,0,2);
        add(UnityGain,"lim.unity","Unity Gain Monitoring",0,1,0,2);
        add(Audition,"lim.audition","Audition Difference",0,1,0,2);
        add(DcFilter,"lim.dc","DC Offset Filter",0,1,0,2);
    }
    const juce::String getName() const override { return kPluginName; }
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name=kPluginName; d.descriptiveName=kPluginName;
        d.pluginFormatName=kFormatName; d.category=kCategory;
        d.manufacturerName="APEX"; d.version="0.1.0-experimental";
        d.fileOrIdentifier=kFileOrIdentifier; d.uniqueId=kUniqueId;
        d.deprecatedUid=kUniqueId; d.isInstrument=false;
        d.numInputChannels=2; d.numOutputChannels=2;
    }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    {
        const auto in=layout.getMainInputChannelSet();
        return in==layout.getMainOutputChannelSet()
            && (in==juce::AudioChannelSet::mono() || in==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double rate,int block) override
    {
        setRateAndBufferSizeDetails(rate,block);
        core_.prepare(rate);
        core_.setSettings(readSettings());
        core_.reset();
        setLatencySamples(core_.latencySamples());
        gainReduction_.store(0,std::memory_order_relaxed);
        peakOutput_.store(0,std::memory_order_relaxed);
        peakInput_.store(0,std::memory_order_relaxed);
    }
    void releaseResources() override {}
    void reset() override { core_.reset(); }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override
    {
        if (buffer.getNumChannels() < 1 || buffer.getNumSamples() < 1) return;
        core_.setSettings(readSettings());
        core_.process(buffer.getWritePointer(0),
            buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr,
            buffer.getNumSamples());
        for (int ch=2; ch<buffer.getNumChannels(); ++ch) buffer.clear(ch,0,buffer.getNumSamples());
        gainReduction_.store(static_cast<float>(core_.reductionDb()),std::memory_order_relaxed);
        peakOutput_.store(static_cast<float>(core_.outputPeak()),std::memory_order_relaxed);
        peakInput_.store(static_cast<float>(core_.inputPeak()),std::memory_order_relaxed);
    }
    float meterReduction() const noexcept { return gainReduction_.load(std::memory_order_relaxed); }
    float meterInput() const noexcept { return peakInput_.load(std::memory_order_relaxed); }
    float meterOutput() const noexcept { return peakOutput_.load(std::memory_order_relaxed); }
    LimiterHostedParameter& getLimiterParameter(int i) noexcept { return *limiterParameters_[static_cast<size_t>(i)]; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return limiterParameters_[Bypass]; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock& data) override
    {
        juce::ValueTree tree(kStateName);
        tree.setProperty("version",1,nullptr);
        for (int i=0;i<Count;++i)
            tree.setProperty(juce::Identifier(ids()[static_cast<size_t>(i)]),
                             limiterParameters_[static_cast<size_t>(i)]->getValue(),nullptr);
        juce::MemoryOutputStream output(data,false);
        tree.writeToStream(output);
    }
    void setStateInformation(const void* data,int bytes) override
    {
        if (!data || bytes<=0) return;
        const auto tree = juce::ValueTree::readFromData(data,static_cast<size_t>(bytes));
        if (!tree.isValid() || !tree.hasType(juce::Identifier(kStateName))) return;
        for (int i=0;i<Count;++i)
        {
            const juce::Identifier id(ids()[static_cast<size_t>(i)]);
            if (!tree.hasProperty(id)) continue;
            const float v=static_cast<float>(tree.getProperty(id));
            if (std::isfinite(v))
                limiterParameters_[static_cast<size_t>(i)]->setValue(v);
        }
    }
private:
    static const std::array<const char*,Count>& ids()
    {
        static const std::array<const char*,Count> values = {{
            "lim.gain","lim.ceiling","lim.lookahead","lim.attack",
            "lim.release","lim.link","lim.style","lim.tp","lim.quality",
            "lim.bypass","lim.unity","lim.audition","lim.dc"
        }};
        return values;
    }
    void add(int i,const char* id,const char* name,float low,float high,float value,int steps=0)
    {
        auto p=std::make_unique<LimiterHostedParameter>(juce::ParameterID(id,1),name,low,high,value,steps);
        limiterParameters_[static_cast<size_t>(i)]=p.get();
        addLimiterHostedParameter(std::move(p));
    }
    Settings readSettings() const noexcept
    {
        Settings s;
        s.gainDb=limiterParameters_[Gain]->units();
        s.ceilingDb=limiterParameters_[Ceiling]->units();
        s.lookaheadMs=limiterParameters_[Lookahead]->units();
        s.attackMs=limiterParameters_[Attack]->units();
        s.releaseMs=limiterParameters_[Release]->units();
        s.channelLink=limiterParameters_[ChannelLink]->units()/100.0;
        s.style=static_cast<int>(limiterParameters_[LimitingStyle]->units());
        s.truePeak=limiterParameters_[TruePeak]->units()>0.5f;
        s.detectorQuality=static_cast<int>(limiterParameters_[DetectorQuality]->units());
        s.bypass=limiterParameters_[Bypass]->units()>0.5f;
        s.unityGain=limiterParameters_[UnityGain]->units()>0.5f;
        s.audition=limiterParameters_[Audition]->units()>0.5f;
        s.dcFilter=limiterParameters_[DcFilter]->units()>0.5f;
        return s;
    }
    std::array<LimiterHostedParameter*,Count> limiterParameters_ {};
    Core core_;
    std::atomic<float> gainReduction_{0},peakInput_{0},peakOutput_{0};
};

}} // namespace APEX::Limiter
#include "ApexLimiterEditor.h"
