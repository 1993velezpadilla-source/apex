#pragma once

// APEX Precision C3: separate native dynamics insert, never replaces Opto 2A.
// All parameters are host-visible with stable IDs for session persistence and
// APEX automation. Editor is compiled inline at the bottom of this header.
#include <JuceHeader.h>
#include "ApexPrecisionCompressorCore.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace APEX::PrecisionC3
{
enum ParameterIndex : int
{
    Threshold = 0, Ratio, Knee, Range, Attack, Release, Hold, Input, Wet,
    Dry, Output, SidechainHP, SidechainLP, StereoLink, Drive, Style,
    Character, AutoThreshold, AutoRelease, AutoGain, Bypass,
    ExternalSidechain, CharacterPre,
    SC1Freq,SC1Gain,SC1Q,SC2Freq,SC2Gain,SC2Q,
    SC3Freq,SC3Gain,SC3Q,SC4Freq,SC4Gain,SC4Q,
    DryGainDb, // appended; legacy c3.dry percent remains recall-compatible
    LookaheadMode, // append-only index; public Precision C, legacy c3 IDs
    ParameterCount
};

class PrecisionC3HostedParameter final : public juce::AudioProcessorParameterWithID
{
public:
    PrecisionC3HostedParameter (const char* id, const char* name, float lo, float hi,
                     float initial, int steps = 0, const char* unit = "")
        : AudioProcessorParameterWithID (juce::ParameterID (id, 1), name),
          low_(lo), high_(hi), initial_(initial), steps_(steps), unit_(unit)
    {
        normalised_.store(toNormalised(initial), std::memory_order_relaxed);
    }
    float getValue() const override { return normalised_.load(std::memory_order_relaxed); }
    void setValue(float value) override
    {
        if (!std::isfinite(value)) return;
        value = juce::jlimit(0.0f, 1.0f, value);
        if (steps_ > 1) value = std::round(value * (steps_ - 1)) / (steps_ - 1);
        normalised_.store(value, std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return toNormalised(initial_); }
    float toNormalised (float value) const noexcept
    {
        return juce::jlimit(0.0f, 1.0f, (value - low_) / (high_ - low_));
    }
    float fromNormalised (float normal) const noexcept
    {
        return low_ + (high_ - low_) * normal;
    }
    float getUnits() const noexcept { return fromNormalised(getValue()); }
    juce::String getText (float normal, int) const override
    {
        if (steps_ == 2) return normal >= 0.5f ? "On" : "Off";
        const int index = steps_ > 1
            ? juce::jlimit(0, steps_ - 1,
                static_cast<int>(std::lround(normal * (steps_ - 1)))) : 0;
        if (steps_ == 14) return kStyleNames[static_cast<std::size_t>(index)];
        if (steps_ == 5)
        {
            constexpr const char* times[] {"0 ms","1 ms","2 ms","5 ms","10 ms"};
            return times[index];
        }
        if (steps_ == 4)
        {
            constexpr const char* names[] {"Off", "Tube", "Diode", "Bright"};
            return names[index];
        }
        return juce::String(fromNormalised(normal), 2) + unit_;
    }
    float getValueForText (const juce::String& value) const override
    {
        if (steps_ == 2) return value.equalsIgnoreCase("On") || value == "1" ? 1.0f : 0.0f;
        if (steps_ == 14)
            for (int i = 0; i < 14; ++i)
                if (value.equalsIgnoreCase(kStyleNames[static_cast<std::size_t>(i)]))
                    return static_cast<float>(i) / 13.0f;
        if (steps_ == 4)
        {
            constexpr const char* names[] {"Off", "Tube", "Diode", "Bright"};
            for (int i = 0; i < 4; ++i)
                if (value.equalsIgnoreCase(names[i])) return i / 3.0f;
        }
        if (steps_ == 5)
        {
            constexpr float times[] {0.0f,1.0f,2.0f,5.0f,10.0f};
            const float ms = value.getFloatValue();
            int selected=0;
            for(int i=1;i<5;++i)
                if(std::abs(ms-times[i])<std::abs(ms-times[selected]))selected=i;
            return selected*0.25f;
        }
        return toNormalised(value.getFloatValue());
    }
    int getNumSteps() const override
    {
        return steps_ > 1 ? steps_ : juce::AudioProcessorParameter::getDefaultNumParameterSteps();
    }
    bool isDiscrete() const override { return steps_ > 1; }
    bool isBoolean() const override { return steps_ == 2; }
    juce::String getLabel() const override { return unit_; }
private:
    const float low_, high_, initial_;
    const int steps_;
    const juce::String unit_;
    std::atomic<float> normalised_ {0.0f};
};
static_assert(std::atomic<float>::is_always_lock_free,
              "APEX Precision C3 requires lock-free hosted parameter reads");

class Processor final : public juce::AudioPluginInstance, private juce::Timer
{
public:
    static constexpr int kUniqueId = 0x41504333; // APC3
    static constexpr const char* kPluginName = "APEX Precision C";
    static constexpr const char* kFormatName = "APEX Native";
    static constexpr const char* kFileOrIdentifier = "APEX::PrecisionC3";
    static constexpr const char* kCategory = "Dynamics";
    static constexpr const char* kManufacturer = "APEX";
    static constexpr const char* kVersion = "0.1.0-preview";
    static constexpr const char* kStateTag = "apex-precision-c3-v1";

    Processor()
        : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        add(Threshold,     "c3.threshold", "Threshold", -60, 0, -18, 0, " dB");
        add(Ratio,         "c3.ratio", "Ratio", 1, 40, 4, 0, ":1");
        add(Knee,          "c3.knee", "Knee", 0, 72, 12, 0, " dB");
        add(Range,         "c3.range", "Range", 0, 60, 60, 0, " dB");
        add(Attack,        "c3.attack", "Attack", .005f, 250, 10, 0, " ms");
        add(Release,       "c3.release", "Release", 5, 3000, 120, 0, " ms");
        add(Hold,          "c3.hold", "Hold", 0, 500, 0, 0, " ms");
        add(Input,         "c3.input", "Input", -24, 24, 0, 0, " dB");
        add(Wet,           "c3.wet", "Wet Gain", -24, 24, 0, 0, " dB");
        add(Dry,           "c3.dry", "Dry", 0, 100, 0, 0, " %");
        add(Output,        "c3.output", "Output", -24, 24, 0, 0, " dB");
        add(SidechainHP,   "c3.schp", "SC High-pass", 0, 2000, 0, 0, " Hz");
        add(SidechainLP,   "c3.sclp", "SC Low-pass", 100, 20000, 20000, 0, " Hz");
        add(StereoLink,    "c3.link", "Stereo Link", 0, 100, 100, 0, " %");
        add(Drive,         "c3.drive", "Drive", 0, 100, 0, 0, " %");
        add(Style,         "c3.style", "Style", 0, 13, 0, 14);
        add(Character,     "c3.character", "Character", 0, 3, 0, 4);
        add(AutoThreshold, "c3.autothr", "Auto Threshold", 0, 1, 0, 2);
        add(AutoRelease,   "c3.autorel", "Auto Release", 0, 1, 0, 2);
        add(AutoGain,      "c3.autogain", "Auto Gain", 0, 1, 0, 2);
        add(Bypass,        "c3.bypass", "Bypass", 0, 1, 0, 2);
        add(ExternalSidechain, "c3.external", "External sidechain", 0, 1, 0, 2);
        add(CharacterPre, "c3.charpre", "Character pre", 0, 1, 0, 2);
        // Stable append-only indices preserve sessions and automation made
        // before the detector six-filter expansion.
        add(SC1Freq,"c3.sc1.freq","SC Bell 1 Frequency",20,20000,130,0," Hz");
        add(SC1Gain,"c3.sc1.gain","SC Bell 1 Gain",-18,18,0,0," dB");
        add(SC1Q,"c3.sc1.q","SC Bell 1 Q",0.15f,18,1);
        add(SC2Freq,"c3.sc2.freq","SC Bell 2 Frequency",20,20000,680,0," Hz");
        add(SC2Gain,"c3.sc2.gain","SC Bell 2 Gain",-18,18,0,0," dB");
        add(SC2Q,"c3.sc2.q","SC Bell 2 Q",0.15f,18,1);
        add(SC3Freq,"c3.sc3.freq","SC Bell 3 Frequency",20,20000,2700,0," Hz");
        add(SC3Gain,"c3.sc3.gain","SC Bell 3 Gain",-18,18,0,0," dB");
        add(SC3Q,"c3.sc3.q","SC Bell 3 Q",0.15f,18,1);
        add(SC4Freq,"c3.sc4.freq","SC Bell 4 Frequency",20,20000,9300,0," Hz");
        add(SC4Gain,"c3.sc4.gain","SC Bell 4 Gain",-18,18,0,0," dB");
        add(SC4Q,"c3.sc4.q","SC Bell 4 Q",0.15f,18,1);
        add(DryGainDb,"c3.drygain","Dry Gain",-100,18,-100,0," dB");
        add(LookaheadMode,"c3.lookahead","Lookahead",0,4,0,5," ms");
        // JUCE setLatencySamples() can notify the host and is not RT safe.
        // Poll only the atomic requested value from the message thread.
        // The audio callback never posts messages, locks or allocates.
        startTimerHz(20);
    }

    ~Processor() override { stopTimer(); }

    const juce::String getName() const override { return kPluginName; }
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name=kPluginName; d.descriptiveName=kPluginName;
        d.pluginFormatName=kFormatName; d.category=kCategory;
        d.manufacturerName=kManufacturer; d.version=kVersion;
        d.fileOrIdentifier=kFileOrIdentifier; d.uniqueId=kUniqueId;
        d.deprecatedUid=kUniqueId; d.isInstrument=false;
        d.numInputChannels=2; d.numOutputChannels=2;
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto input=l.getMainInputChannelSet();
        if(input != l.getMainOutputChannelSet()
           || (input != juce::AudioChannelSet::mono()
               && input != juce::AudioChannelSet::stereo()))
            return false;
        // Optional external detector bus: disabled, mono, or stereo, but
        // never required by existing APEX projects using only main inputs.
        if(l.inputBuses.size() > 1)
        {
            const auto sc=l.inputBuses[1];
            if(!sc.isDisabled() && sc != juce::AudioChannelSet::mono()
                                && sc != juce::AudioChannelSet::stereo())
                return false;
        }
        return true;
    }
    void prepareToPlay(double sampleRate, int blockSize) override
    {
        setRateAndBufferSizeDetails(sampleRate, blockSize);
        engine_.prepare(sampleRate);
        engine_.setParameters(readParameters());
        requestedLatency_.store(engine_.latencySamples(),std::memory_order_relaxed);
        setLatencySamples(engine_.latencySamples());
        meterInput_.store(-120.0f, std::memory_order_relaxed);
        meterOutput_.store(-120.0f, std::memory_order_relaxed);
        meterGR_.store(0.0f, std::memory_order_relaxed);
        meterHistoryCounter_.store(0, std::memory_order_release);
        // Reporting is performed above after engine is prepared; at the
        // 0 ms default the native insert is truly zero latency.
    }
    void releaseResources() override {}
    void reset() override { engine_.reset(); }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // Route the MAIN output bus only: host sidechain samples must NEVER
        // be overwritten. The detector bus stays off unless both host routing
        // and the explicit External trigger are enabled.
        auto main=getBusBuffer(buffer,true,0);
        const int channels=main.getNumChannels();
        if(channels<1 || main.getNumSamples()<=0) return;
        const bool wantExternal=getC3Parameter(ExternalSidechain).getUnits()>0.5f;
        const bool routed=wantExternal && getBusCount(true)>1
            && getBus(true,1)!=nullptr && getBus(true,1)->isEnabled();
        externalSidechainRouted_.store(routed,std::memory_order_relaxed);
        engine_.setParameters(readParameters());
        // No PDC notification from the audio callback; the GUI/message
        // thread timer applies changed latency outside the real-time thread.
        requestedLatency_.store(engine_.latencySamples(),std::memory_order_release);
        if(routed)
        {
            auto sc=getBusBuffer(buffer,true,1);
            engine_.process(main.getWritePointer(0),
                channels>1 ? main.getWritePointer(1) : nullptr,
                main.getNumSamples(),
                sc.getNumChannels()>0 ? sc.getReadPointer(0) : nullptr,
                sc.getNumChannels()>1 ? sc.getReadPointer(1) : nullptr);
        }
        else
            engine_.process(main.getWritePointer(0),
                channels>1 ? main.getWritePointer(1) : nullptr,
                main.getNumSamples());
        meterInput_.store(static_cast<float>(engine_.getInputPeakDb()),
                          std::memory_order_relaxed);
        meterOutput_.store(static_cast<float>(engine_.getOutputPeakDb()),
                           std::memory_order_relaxed);
        meterGR_.store(static_cast<float>(engine_.getGainReductionDb()),
                       std::memory_order_relaxed);
        // One atomic frame per processed buffer. No allocation or locking.
        const auto write = meterHistoryCounter_.load(std::memory_order_relaxed);
        const auto cell = static_cast<std::size_t>(write % kMeterHistoryLength);
        meterHistoryInput_[cell].store(inputLevel(), std::memory_order_relaxed);
        meterHistoryOutput_[cell].store(outputLevel(), std::memory_order_relaxed);
        meterHistoryGr_[cell].store(gainReduction(), std::memory_order_relaxed);
        meterHistoryCounter_.store(write + 1, std::memory_order_release);
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return precisionParameters_[Bypass]; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    PrecisionC3HostedParameter& getC3Parameter(int index) noexcept { return *precisionParameters_[static_cast<std::size_t>(index)]; }
    float gainReduction() const noexcept { return meterGR_.load(std::memory_order_relaxed); }
    float inputLevel() const noexcept { return meterInput_.load(std::memory_order_relaxed); }
    float outputLevel() const noexcept { return meterOutput_.load(std::memory_order_relaxed); }
    bool externalSidechainRouted() const noexcept
    {
        return externalSidechainRouted_.load(std::memory_order_relaxed);
    }

    // Read-only GUI history of the ACTUAL compressor DSP meters. Each atomic
    // cell can be read without blocking the audio callback.
    static constexpr std::size_t kMeterHistoryLength = 512;
    std::uint32_t meterHistoryCount() const noexcept
    {
        return meterHistoryCounter_.load(std::memory_order_acquire);
    }
    void meterHistoryFrame(std::uint32_t sequence, float& in,
                           float& out, float& gr) const noexcept
    {
        const auto cell=static_cast<std::size_t>(sequence % kMeterHistoryLength);
        in=meterHistoryInput_[cell].load(std::memory_order_relaxed);
        out=meterHistoryOutput_[cell].load(std::memory_order_relaxed);
        gr=meterHistoryGr_[cell].load(std::memory_order_relaxed);
    }

    void getStateInformation (juce::MemoryBlock& destination) override
    {
        juce::ValueTree state(kStateTag);
        state.setProperty("schema", 1, nullptr);
        for (int i=0; i<ParameterCount; ++i)
            state.setProperty(juce::Identifier(parameterIds_[static_cast<std::size_t>(i)]),
                precisionParameters_[static_cast<std::size_t>(i)]->getValue(), nullptr);
        juce::MemoryOutputStream stream(destination, false);
        state.writeToStream(stream);
    }
    void setStateInformation (const void* data, int numBytes) override
    {
        if (data == nullptr || numBytes <= 0) return;
        const auto state=juce::ValueTree::readFromData(data, static_cast<std::size_t>(numBytes));
        if (!state.isValid() || !state.hasType(juce::Identifier(kStateTag))) return;
        for (int i=0; i<ParameterCount; ++i)
        {
            const auto id=juce::Identifier(parameterIds_[static_cast<std::size_t>(i)]);
            if (!state.hasProperty(id)) continue;
            const auto normalised=static_cast<float>(state.getProperty(id));
            if (std::isfinite(normalised))
                precisionParameters_[static_cast<std::size_t>(i)]->setValue(normalised);
        }
    }
private:
    void timerCallback() override
    {
        // This is intentionally message-thread-only. setLatencySamples()
        // can call host listeners, which must NEVER happen in processBlock.
        const int requested=requestedLatency_.load(std::memory_order_acquire);
        if(getLatencySamples()!=requested)
            setLatencySamples(requested);
    }

    void add (int index, const char* id, const char* name, float lo, float hi,
              float initial, int steps=0, const char* unit="")
    {
        auto p=std::make_unique<PrecisionC3HostedParameter>(id,name,lo,hi,initial,steps,unit);
        precisionParameters_[static_cast<std::size_t>(index)]=p.get();
        addHostedParameter(std::move(p));
    }
    Parameters readParameters() const noexcept
    {
        auto value=[this](int index) noexcept
        {
            return static_cast<double>(
                precisionParameters_[static_cast<std::size_t>(index)]->getUnits());
        };
        Parameters p;
        p.thresholdDb=value(Threshold);
        p.ratio=value(Ratio);
        p.kneeDb=value(Knee);
        p.rangeDb=value(Range);
        p.attackMs=value(Attack);
        p.releaseMs=value(Release);
        p.holdMs=value(Hold);
        constexpr double lookaheadTimes[] {0,1,2,5,10};
        const int mode=juce::jlimit(0,4,(int)std::lround(value(LookaheadMode)));
        p.lookaheadMs=lookaheadTimes[mode];
        p.inputDb=value(Input);
        p.wetDb=value(Wet);
        p.dryPercent=value(Dry);
        p.dryGainDb=value(DryGainDb);
        p.outputDb=value(Output);
        p.sidechainHighpassHz=value(SidechainHP);
        p.sidechainLowpassHz=value(SidechainLP);
        constexpr int bellParams[4][3] = {
            {SC1Freq,SC1Gain,SC1Q},{SC2Freq,SC2Gain,SC2Q},
            {SC3Freq,SC3Gain,SC3Q},{SC4Freq,SC4Gain,SC4Q}
        };
        for(int i=0;i<4;++i)
        {
            p.sidechainBells[static_cast<std::size_t>(i)].hz=value(bellParams[i][0]);
            p.sidechainBells[static_cast<std::size_t>(i)].gainDb=value(bellParams[i][1]);
            p.sidechainBells[static_cast<std::size_t>(i)].q=value(bellParams[i][2]);
        }
        p.stereoLinkPercent=value(StereoLink);
        p.drive=value(Drive);
        p.style=static_cast<int>(std::lround(value(Style)));
        p.character=static_cast<int>(std::lround(value(Character)));
        p.characterPre=value(CharacterPre)>=0.5;
        p.autoThreshold=value(AutoThreshold)>=0.5;
        p.autoRelease=value(AutoRelease)>=0.5;
        p.autoGain=value(AutoGain)>=0.5;
        p.bypass=value(Bypass)>=0.5;
        return p;
    }
    static constexpr std::array<const char*,ParameterCount> parameterIds_ {{
        "c3.threshold","c3.ratio","c3.knee","c3.range","c3.attack",
        "c3.release","c3.hold","c3.input","c3.wet","c3.dry",
        "c3.output","c3.schp","c3.sclp","c3.link","c3.drive",
        "c3.style","c3.character","c3.autothr","c3.autorel",
        "c3.autogain","c3.bypass","c3.external","c3.charpre",
        "c3.sc1.freq","c3.sc1.gain","c3.sc1.q",
        "c3.sc2.freq","c3.sc2.gain","c3.sc2.q",
        "c3.sc3.freq","c3.sc3.gain","c3.sc3.q",
        "c3.sc4.freq","c3.sc4.gain","c3.sc4.q",
        "c3.drygain","c3.lookahead"
    }};
    std::array<PrecisionC3HostedParameter*,ParameterCount> precisionParameters_ {};
    Core engine_;
    std::atomic<float> meterGR_ {0.0f};
    std::atomic<float> meterInput_ {-120.0f};
    std::atomic<float> meterOutput_ {-120.0f};
    std::array<std::atomic<float>,kMeterHistoryLength> meterHistoryInput_ {};
    std::array<std::atomic<float>,kMeterHistoryLength> meterHistoryOutput_ {};
    std::array<std::atomic<float>,kMeterHistoryLength> meterHistoryGr_ {};
    std::atomic<std::uint32_t> meterHistoryCounter_ {0};
    std::atomic<bool> externalSidechainRouted_ {false};
    std::atomic<int> requestedLatency_ {0};
};
} // namespace APEX::PrecisionC3

#include "ApexPrecisionCompressorEditor.h"
