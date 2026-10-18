#pragma once
#include <JuceHeader.h>
#include "ApexBubblegumDelayEngine.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace APEX::Delay
{
enum ParameterIndex : int
{
    TimeMs=0, SyncMode, Note, ManualBpm, Feedback, Mix, OutputDb, HiPass,
    LowPass, ModDepth, ModRate, Analog, LoFi, PingPong, PhaseLeft,
    PhaseRight, Bypass, ParameterCount
};

// BubblegumDelayParameter has stable IDs, lock-free control publication, and gestures
// via the editor. No GUI fields are DSP state.
class BubblegumDelayParameter final : public juce::AudioProcessorParameterWithID
{
public:
    BubblegumDelayParameter(const char* id,const char* name,float lo,float hi,
                    float initial,int steps=0,const char* unit="")
        : AudioProcessorParameterWithID(juce::ParameterID(id,1),name),
          low_(lo),high_(hi),initial_(initial),steps_(steps),unit_(unit)
    {
        normal_.store(toNormalised(initial),std::memory_order_relaxed);
    }
    float getValue() const override { return normal_.load(std::memory_order_relaxed); }
    void setValue(float n) override
    {
        if(!std::isfinite(n)) return;
        n=juce::jlimit(0.0f,1.0f,n);
        if(steps_>1) n=std::round(n*(steps_-1))/(steps_-1);
        normal_.store(n,std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return toNormalised(initial_); }
    float toNormalised(float v) const noexcept
    {
        return juce::jlimit(0.0f,1.0f,(v-low_)/(high_-low_));
    }
    float getUnits() const noexcept { return fromNormalised(getValue()); }
    float fromNormalised(float n) const noexcept { return low_+(high_-low_)*n; }
    juce::String getText(float n,int) const override
    {
        const float units=fromNormalised(n);
        if(steps_==2) return n>=0.5f?"On":"Off";
        return juce::String(units,steps_>2?0:((high_-low_)>100.0f?0:2))+unit_;
    }
    float getValueForText(const juce::String& text) const override
    {
        if(steps_==2)
            return (text.equalsIgnoreCase("on")||text=="1")?1.0f:0.0f;
        return toNormalised(text.getFloatValue());
    }
    int getNumSteps() const override
    {
        return steps_>1?steps_:juce::AudioProcessorParameter::getDefaultNumParameterSteps();
    }
    bool isDiscrete() const override { return steps_>1; }
    bool isBoolean() const override { return steps_==2; }
    juce::String getLabel() const override { return unit_; }
private:
    const float low_,high_,initial_;
    const int steps_;
    const juce::String unit_;
    std::atomic<float> normal_ {0.0f};
};
static_assert(std::atomic<float>::is_always_lock_free,
              "APEX delay parameter reads must be lock-free");

class Processor final : public juce::AudioPluginInstance
{
public:
    static constexpr int kUniqueId=0x41504459; // APDY
    static constexpr const char* kPluginName="APEX Bubblegum Delay";
    static constexpr const char* kFormatName="APEX Native";
    static constexpr const char* kFileOrIdentifier="APEX::BubblegumDelay";
    static constexpr const char* kCategory="Delay";
    static constexpr const char* kManufacturer="APEX";
    static constexpr const char* kVersion="0.1.0";
    static constexpr const char* kStateTag="apex-bubblegum-delay-v1";

    Processor()
        : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
            .withInput("Input",juce::AudioChannelSet::stereo(),true)
            .withOutput("Output",juce::AudioChannelSet::stereo(),true))
    {
        add(TimeMs,"delay.time","Delay (ms)",1,3500,375,0," ms");
        add(SyncMode,"delay.sync","Sync Mode",0,2,1,3);
        add(Note,"delay.note","Note Division",0,12,5,13);
        add(ManualBpm,"delay.bpm","Manual Tempo",30,300,120,0," BPM");
        add(Feedback,"delay.feedback","Feedback",0,200,45,0," %");
        add(Mix,"delay.mix","Dry / Wet",0,100,35,0," %");
        add(OutputDb,"delay.output","Output", -18,18,0,0," dB");
        add(HiPass,"delay.hp","Highpass",20,20000,20,0," Hz");
        add(LowPass,"delay.lp","Lowpass",20,20000,20000,0," Hz");
        add(ModDepth,"delay.depth","Mod Depth",0,100,0,0," %");
        add(ModRate,"delay.rate","Mod Rate",0.1f,12,0.4f,0," Hz");
        add(Analog,"delay.analog","Analog Colour",0,4,0,5);
        add(LoFi,"delay.lofi","LoFi",0,1,0,2);
        add(PingPong,"delay.pingpong","Ping Pong",0,1,0,2);
        add(PhaseLeft,"delay.phaseL","Reverse Phase Left",0,1,0,2);
        add(PhaseRight,"delay.phaseR","Reverse Phase Right",0,1,0,2);
        add(Bypass,"delay.bypass","Bypass",0,1,0,2);
    }
    const juce::String getName() const override { return kPluginName; }
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name=kPluginName; d.descriptiveName=kPluginName;
        d.pluginFormatName=kFormatName; d.category=kCategory;
        d.manufacturerName=kManufacturer; d.version=kVersion;
        d.fileOrIdentifier=kFileOrIdentifier; d.uniqueId=kUniqueId;
        d.deprecatedUid=kUniqueId; d.isInstrument=false;
        d.numInputChannels=2; d.numOutputChannels=2;
    }
    bool isBusesLayoutSupported(const BusesLayout& b) const override
    {
        const auto input=b.getMainInputChannelSet(),output=b.getMainOutputChannelSet();
        return input==output && (input==juce::AudioChannelSet::mono()
                              || input==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double sampleRate,int blockSize) override
    {
        setRateAndBufferSizeDetails(sampleRate,blockSize);
        engine_.setSettings(readSettings());
        engine_.prepare(sampleRate);
        setLatencySamples(0);
    }
    void releaseResources() override {}
    void reset() override { engine_.reset(); }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        if(buffer.getNumChannels()<1||buffer.getNumSamples()<1) return;
        if(auto* playhead=getPlayHead())
            if(auto position=playhead->getPosition())
                if(auto bpm=position->getBpm())
                    if(std::isfinite(*bpm) && *bpm>1.0 && *bpm<1000.0)
                    {
                        // APEX's HostPlayheadCore publishes the live project
                        // transport BPM, including changes while playing.
                        // Engine.setHostTempo and parameter snapshots are
                        // allocation-free on the audio thread.
                        engine_.setHostTempo(*bpm);
                        hostBpmDisplay_.store(*bpm,std::memory_order_relaxed);
                        receivedHostTempo_.store(true,std::memory_order_relaxed);
                    }
        engine_.setSettings(readSettings());
        engine_.process(buffer.getWritePointer(0),
                        buffer.getNumChannels()>1?buffer.getWritePointer(1):nullptr,
                        buffer.getNumSamples());
    }
    double getTailLengthSeconds() const override
    {
        const double feedback=juce::jlimit(0.0,2.0,
            static_cast<double>(getParameterAt(Feedback).getUnits())/100.0);
        if(feedback>=0.999) return 60.0; // potentially self-oscillating.
        // Tail query may run outside the audio callback. Read only lock-free
        // hosted controls and the atomic tempo snapshot, never engine state.
        const double ms=getEffectiveDelayMsForDisplay();
        const double repeats=feedback<0.001?1.0:
            1.0+std::ceil(std::log(0.001)/std::log(feedback));
        return juce::jlimit(0.001,60.0,repeats*ms/1000.0);
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorParameter* getBypassParameter() const override {return parameters_[Bypass];}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Default";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    double getHostTempoForDisplay() const noexcept
    {
        return hostBpmDisplay_.load(std::memory_order_relaxed);
    }
    bool hasHostTempo() const noexcept
    {
        return receivedHostTempo_.load(std::memory_order_relaxed);
    }
    // Shared by GUI and audio/tail calculations: Host mode always follows
    // the project tempo; no editing the Manual BPM knob is required.
    double getEffectiveDelayMsForDisplay() const noexcept
    {
        const int sync=static_cast<int>(std::lround(getParameterAt(SyncMode).getUnits()));
        if(sync==0) return getParameterAt(TimeMs).getUnits();
        const int note=juce::jlimit(0,12,
            static_cast<int>(std::lround(getParameterAt(Note).getUnits())));
        const double bpm=sync==2?getParameterAt(ManualBpm).getUnits()
                                 :getHostTempoForDisplay();
        return juce::jlimit(1.0,Engine::kMaxSyncedMs,60000.0
            *Engine::kNoteBeats[static_cast<std::size_t>(note)]/juce::jmax(1.0,bpm));
    }
    BubblegumDelayParameter& getParameterAt(int i) noexcept {return *parameters_[i];}
    const BubblegumDelayParameter& getParameterAt(int i) const noexcept {return *parameters_[i];}
    void getStateInformation(juce::MemoryBlock& b) override
    {
        juce::ValueTree state(kStateTag);
        state.setProperty("version",1,nullptr);
        for(int i=0;i<ParameterCount;++i)
            state.setProperty(juce::Identifier(ids_[i]),parameters_[i]->getValue(),nullptr);
        juce::MemoryOutputStream output(b,false);
        state.writeToStream(output);
    }
    void setStateInformation(const void* data,int bytes) override
    {
        if(data==nullptr||bytes<=0) return;
        auto state=juce::ValueTree::readFromData(data,static_cast<size_t>(bytes));
        if(!state.isValid()||!state.hasType(juce::Identifier(kStateTag))) return;
        for(int i=0;i<ParameterCount;++i)
        {
            const juce::Identifier id(ids_[i]);
            if(!state.hasProperty(id)) continue;
            const float n=static_cast<float>(state.getProperty(id));
            if(std::isfinite(n)) parameters_[i]->setValue(n);
        }
    }
private:
    void add(int index,const char* id,const char* name,float lo,float hi,
             float initial,int steps=0,const char* unit="")
    {
        auto p=std::make_unique<BubblegumDelayParameter>(id,name,lo,hi,initial,steps,unit);
        parameters_[index]=p.get();
        addHostedParameter(std::move(p));
    }
    Settings readSettings() const noexcept
    {
        const auto v=[this](int index){return parameters_[index]->getUnits();};
        Settings s;
        s.timeMs=v(TimeMs);
        s.sync=static_cast<int>(std::lround(v(SyncMode)));
        s.note=static_cast<int>(std::lround(v(Note)));
        s.manualBpm=v(ManualBpm);
        s.feedback=v(Feedback)/100.0f;
        s.mix=v(Mix)/100.0f;
        s.outputDb=v(OutputDb);
        s.highpassHz=v(HiPass);
        s.lowpassHz=v(LowPass);
        s.modulationDepth=v(ModDepth)/100.0f;
        s.modulationHz=v(ModRate);
        s.analog=static_cast<int>(std::lround(v(Analog)));
        s.lofi=v(LoFi)>0.5f;
        s.pingPong=v(PingPong)>0.5f;
        s.phaseLeft=v(PhaseLeft)>0.5f;
        s.phaseRight=v(PhaseRight)>0.5f;
        s.bypass=v(Bypass)>0.5f;
        return s;
    }
    inline static constexpr std::array<const char*,ParameterCount> ids_ {{
        "delay.time","delay.sync","delay.note","delay.bpm","delay.feedback",
        "delay.mix","delay.output","delay.hp","delay.lp","delay.depth",
        "delay.rate","delay.analog","delay.lofi","delay.pingpong",
        "delay.phaseL","delay.phaseR","delay.bypass"
    }};
    std::array<BubblegumDelayParameter*,ParameterCount> parameters_ {};
    Engine engine_;
    std::atomic<double> hostBpmDisplay_ {120.0};
    std::atomic<bool> receivedHostTempo_ {false};
};
} // namespace APEX::Delay

// Header-only editor avoids changing Projucer's explicit source list.
#include "ApexBubblegumDelayEditor.h"
