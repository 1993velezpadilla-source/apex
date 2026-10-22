#pragma once
#include <JuceHeader.h>
#include "ApexLimiterCore.h"
#include "ApexLimiterLoudness.h"
#include "../MeteringCore/LufsMeterCore.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace APEX { namespace Limiter {

enum Parameter : int
{
    Gain, Ceiling, Lookahead, Attack, Release, ChannelLink, LimitingStyle,
    TruePeak, DetectorQuality, Bypass, UnityGain, Audition, DcFilter, ChannelLinkRelease,
    DitherMode, DitherBits, Count
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
        add(ChannelLinkRelease,"lim.link.release","Release Stereo Link",0,100,100);
        add(DitherMode,"lim.dither.mode","Dither Mode",0,2,0,3);
        add(DitherBits,"lim.dither.bits","Dither Bits",0,1,1,2);
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
        lufsLeft_.prepare(rate);
        lufsRight_.prepare(rate);
        summary_.prepare(rate);
        core_.setSettings(readSettings());
        core_.reset();
        setLatencySamples(core_.latencySamples());
        momentaryLufs_.store(-120.0f,std::memory_order_relaxed);
        shortTermLufs_.store(-120.0f,std::memory_order_relaxed);
        integratedLufs_.store(-120.0f,std::memory_order_relaxed);
        loudnessRange_.store(0.0f,std::memory_order_relaxed);
        gainReduction_.store(0,std::memory_order_relaxed);
        peakOutput_.store(0,std::memory_order_relaxed);
        peakInput_.store(0,std::memory_order_relaxed);
        // Fixed time-base (~100 history frames/second), independent of host
        // buffer size. Reinitialised outside the audio callback.
        displayFrameSamples_=juce::jlimit(64,8192,juce::roundToInt(rate/100.0));
        displaySamplesCollected_=0;
        displayFrameInput_=displayFrameOutput_=displayFrameReduction_=0.0f;
        displayCursor_.store(0,std::memory_order_relaxed);
        for (auto& f:displayInput_) f.store(0,std::memory_order_relaxed);
        for (auto& f:displayOutput_) f.store(0,std::memory_order_relaxed);
        for (auto& f:displayReduction_) f.store(0,std::memory_order_relaxed);
    }
    void releaseResources() override {}
    void reset() override { core_.reset(); summary_.reset(); }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override
    {
        if (buffer.getNumChannels() < 1 || buffer.getNumSamples() < 1) return;
        core_.setSettings(readSettings());
        // Segment DSP at persistent sample-count boundaries, not host callback
        // boundaries. This produces one real peak/reduction history frame every
        // ~10 ms at 44.1/48/96 kHz, whether the host uses 64, 128, 1024 or
        // irregular block sizes. No allocation or lock on the audio thread.
        float blockInput=0.0f,blockOutput=0.0f,blockReduction=0.0f;
        const int total=buffer.getNumSamples();
        for (int base=0;base<total;)
        {
            const int chunk=juce::jmin(total-base,displayFrameSamples_-displaySamplesCollected_);
            core_.process(buffer.getWritePointer(0,base),
                buffer.getNumChannels()>1?buffer.getWritePointer(1,base):nullptr,chunk);
            const float input=static_cast<float>(core_.inputPeak());
            const float output=static_cast<float>(core_.outputPeak());
            const float reduction=static_cast<float>(core_.reductionDb());
            blockInput=juce::jmax(blockInput,input);
            blockOutput=juce::jmax(blockOutput,output);
            blockReduction=juce::jmax(blockReduction,reduction);
            displayFrameInput_=juce::jmax(displayFrameInput_,input);
            displayFrameOutput_=juce::jmax(displayFrameOutput_,output);
            displayFrameReduction_=juce::jmax(displayFrameReduction_,reduction);
            base+=chunk;
            displaySamplesCollected_+=chunk;
            if(displaySamplesCollected_==displayFrameSamples_)
            {
                const auto cursor=displayCursor_.load(std::memory_order_relaxed);
                const auto slot=static_cast<size_t>(cursor%kDisplayHistory);
                displayInput_[slot].store(displayFrameInput_,std::memory_order_relaxed);
                displayOutput_[slot].store(displayFrameOutput_,std::memory_order_relaxed);
                displayReduction_[slot].store(displayFrameReduction_,std::memory_order_relaxed);
                displayCursor_.store(cursor+1,std::memory_order_release);
                displaySamplesCollected_=0;
                displayFrameInput_=displayFrameOutput_=displayFrameReduction_=0.0f;
            }
        }
        for (int ch=2; ch<buffer.getNumChannels(); ++ch) buffer.clear(ch,0,total);
        // 400ms overlapping stereo K-weighted integration / gated range.
        // Rolling state is entirely allocated during prepareToPlay().
        summary_.process(buffer.getReadPointer(0),
            buffer.getNumChannels()>1 ? buffer.getReadPointer(1) : nullptr,
            total);
        const float integrated=summary_.integratedLufs();
        integratedLufs_.store(std::isfinite(integrated)?integrated:-120.0f,
                              std::memory_order_relaxed);
        loudnessRange_.store(summary_.loudnessRange(),std::memory_order_relaxed);
        // Reuse the APEX K-weighting meter, bounded to its preallocated 8192
        // sample scratch: NO resizing, allocation or locks in the callback.
        for(int base=0;base<buffer.getNumSamples();base+=8192)
        {
            const int count=juce::jmin(8192,buffer.getNumSamples()-base);
            lufsLeft_.process(buffer.getReadPointer(0,base),count);
            if (buffer.getNumChannels()>1)
                lufsRight_.process(buffer.getReadPointer(1,base),count);
        }
        const auto combine=[&](float left,float right) noexcept {
            const double a=std::isfinite(left)?std::pow(10.0,(left+0.691)/10.0):0.0;
            const double b=(buffer.getNumChannels()>1&&std::isfinite(right))
                         ?std::pow(10.0,(right+0.691)/10.0):0.0;
            return a+b>0.0?static_cast<float>(-0.691+10.0*std::log10(a+b)):-120.0f;
        };
        momentaryLufs_.store(combine(lufsLeft_.getMomentaryLufs(),lufsRight_.getMomentaryLufs()),
                              std::memory_order_relaxed);
        shortTermLufs_.store(combine(lufsLeft_.getShortTermLufs(),lufsRight_.getShortTermLufs()),
                              std::memory_order_relaxed);
        gainReduction_.store(blockReduction,std::memory_order_relaxed);
        peakOutput_.store(blockOutput,std::memory_order_relaxed);
        peakInput_.store(blockInput,std::memory_order_relaxed);
    }
    float meterReduction() const noexcept { return gainReduction_.load(std::memory_order_relaxed); }
    float meterMomentaryLufs() const noexcept { return momentaryLufs_.load(std::memory_order_relaxed); }
    float meterShortTermLufs() const noexcept { return shortTermLufs_.load(std::memory_order_relaxed); }
    float meterIntegratedLufs() const noexcept { return integratedLufs_.load(std::memory_order_relaxed); }
    float meterLoudnessRange() const noexcept { return loudnessRange_.load(std::memory_order_relaxed); }
    float meterInput() const noexcept { return peakInput_.load(std::memory_order_relaxed); }
    float meterOutput() const noexcept { return peakOutput_.load(std::memory_order_relaxed); }
    static constexpr size_t kDisplayHistory=512;
    uint32_t displayFramesPublished() const noexcept
    {
        return displayCursor_.load(std::memory_order_acquire);
    }
    struct DisplayFrame { float input=0.0f,output=0.0f,reduction=0.0f; };
    DisplayFrame displayFrame(size_t age) const noexcept
    {
        const auto end=displayCursor_.load(std::memory_order_acquire);
        if (age>=kDisplayHistory || age>=end) return {};
        const auto slot=static_cast<size_t>((end-1-static_cast<uint32_t>(age))%kDisplayHistory);
        return {displayInput_[slot].load(std::memory_order_relaxed),
                displayOutput_[slot].load(std::memory_order_relaxed),
                displayReduction_[slot].load(std::memory_order_relaxed)};
    }
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
            "lim.bypass","lim.unity","lim.audition","lim.dc","lim.link.release",
            "lim.dither.mode","lim.dither.bits"
        }};
        return values;
    }
    void add(int i,const char* id,const char* name,float low,float high,float value,int steps=0)
    {
        auto p=std::make_unique<LimiterHostedParameter>(juce::ParameterID(id,1),name,low,high,value,steps);
        limiterParameters_[static_cast<size_t>(i)]=p.get();
        addHostedParameter(std::move(p));
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
        s.releaseLink=limiterParameters_[ChannelLinkRelease]->units()/100.0;
        s.style=static_cast<int>(limiterParameters_[LimitingStyle]->units());
        s.truePeak=limiterParameters_[TruePeak]->units()>0.5f;
        s.detectorQuality=static_cast<int>(limiterParameters_[DetectorQuality]->units());
        s.bypass=limiterParameters_[Bypass]->units()>0.5f;
        s.unityGain=limiterParameters_[UnityGain]->units()>0.5f;
        s.audition=limiterParameters_[Audition]->units()>0.5f;
        s.dcFilter=limiterParameters_[DcFilter]->units()>0.5f;
        s.ditherMode=static_cast<int>(limiterParameters_[DitherMode]->units());
        s.ditherBits=limiterParameters_[DitherBits]->units()>0.5f?24:16;
        return s;
    }
    std::array<LimiterHostedParameter*,Count> limiterParameters_ {};
    Core core_;
    LoudnessSummary summary_;
    DAW::LufsMeterCore lufsLeft_,lufsRight_;
    std::atomic<float> momentaryLufs_{-120},shortTermLufs_{-120};
    std::atomic<float> integratedLufs_{-120},loudnessRange_{0};
    std::atomic<float> gainReduction_{0},peakInput_{0},peakOutput_{0};
    std::array<std::atomic<float>,kDisplayHistory> displayInput_{};
    std::array<std::atomic<float>,kDisplayHistory> displayOutput_{};
    std::array<std::atomic<float>,kDisplayHistory> displayReduction_{};
    std::atomic<uint32_t> displayCursor_{0};
    int displayFrameSamples_=480;
    int displaySamplesCollected_=0;
    float displayFrameInput_=0.0f,displayFrameOutput_=0.0f,displayFrameReduction_=0.0f;
};

}} // namespace APEX::Limiter
#include "ApexLimiterEditor.h"
