#pragma once
#include <JuceHeader.h>
#include "ApexLimiterCore.h"
#include "ApexLimiterLoudness.h"
#include "ApexLimiterOversampling.h"
#include "ApexLimiterTruePeakReserve.h"
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
    DitherMode, DitherBits, AudioOversampling, Count
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
        // Plugin format preserves old parameter IDs by appending this one.
        // Changes are staged until prepareToPlay() so host PDC stays fixed.
        add(AudioOversampling,"lim.os.audio","Audio Oversampling (restart engine)",0,5,0,6);
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
        const int osIndex=juce::jlimit(0,5,static_cast<int>(
            limiterParameters_[AudioOversampling]->units()));
        const int factor=1<<osIndex;
        // Core adds 36 high-rate guard samples and the 95-tap
        // polyphase roundtrip contributes 93*(factor-1) high frames.
        // An additional EXACT 57 high-rate samples yields
        // [ceil(20ms*rate*factor) + 36 + 93*(factor-1) + 57]
        // / factor == 20ms*rate + 93 at 44.1/48/96 kHz.
        // No fractional host-rate resampling or wet/dry phase compromise.
        oversampler_.prepare(factor,128,factor>1?57:0);
        core_.prepare(rate*factor,factor>1);
        activeOversampling_.store(factor,std::memory_order_release);
        lufsLeft_.prepare(rate);
        lufsRight_.prepare(rate);
        summary_.prepare(rate);
        core_.setSettings(effectiveSettings(readSettings()));
        core_.reset();
        oversampler_.reset();
        resetHostDither();
        // PDC is fixed for each prepare() and never changed on audio thread.
        // The extra 57 high-rate frames align the WET signal to a whole
        // host-frame delay at standard sample rates; the independent dry
        // delay lane now has the same integer target.
        // Still requires cross-device low-latency and TP certification.
        const auto nativeFrames=static_cast<double>(core_.latencySamples())/factor
                               +oversampler_.nominalFilterDelayFrames();
        setLatencySamples(juce::roundToInt(nativeFrames));
        // Dry host-rate lane bypasses the AUDIO oversampling filters entirely.
        // Preallocate enough samples to write a whole 100Hz history chunk
        // without overwriting the delayed dry samples read after the DSP.
        const int dryCapacity=getLatencySamples()
                            +juce::jlimit(64,8192,juce::roundToInt(rate/100.0))+8;
        for(auto& ch:hostDry_)
            ch.assign(static_cast<size_t>(dryCapacity),0.0f);
        hostDryFrames_=0;
        hostBypassBlend_=readSettings().bypass?1.0:0.0;
        hostBypassCoeff_=std::exp(-1.0/(0.003*rate));
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
    void reset() override
    {
        core_.reset(); oversampler_.reset(); summary_.reset(); resetHostDither();
        for(auto& ch:hostDry_)std::fill(ch.begin(),ch.end(),0.0f);
        hostDryFrames_=0;
        hostBypassBlend_=readSettings().bypass?1.0:0.0;
    }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override
    {
        if (buffer.getNumChannels() < 1 || buffer.getNumSamples() < 1) return;
        const Settings hostSettings=readSettings();
        core_.setSettings(effectiveSettings(hostSettings));
        // Segment DSP at persistent sample-count boundaries, not host callback
        // boundaries. This produces one real peak/reduction history frame every
        // ~10 ms at 44.1/48/96 kHz, whether the host uses 64, 128, 1024 or
        // irregular block sizes. No allocation or lock on the audio thread.
        float blockInput=0.0f,blockOutput=0.0f,blockReduction=0.0f;
        const int total=buffer.getNumSamples();
        for (int base=0;base<total;)
        {
            const int chunk=juce::jmin(total-base,displayFrameSamples_-displaySamplesCollected_);
            float input=0.0f,output=0.0f,reduction=0.0f;
            auto* left=buffer.getWritePointer(0,base);
            auto* right=buffer.getNumChannels()>1
                  ?buffer.getWritePointer(1,base):nullptr;
            if(activeOversampling_.load(std::memory_order_relaxed)==1)
            {
                // Preserve the native 1x DSP topology. The 8x FIR TP
                // quality setting now optionally includes a small ISP
                // allowance (effectiveSettings), not a certified TP guard.
                core_.process(left,right,chunk);
                input=static_cast<float>(core_.inputPeak());
                output=static_cast<float>(core_.outputPeak());
                reduction=static_cast<float>(core_.blockReductionMaxDb());
            }
            else
            {
                // Stats aggregate ALL high-rate sub-blocks, not just the last
                // callback's tail. No allocation, lock or host PDC update.
                const int64_t dryStart=hostDryFrames_;
                captureDry(left,right,chunk);
                HighRateReadout readout{&core_};
                oversampler_.process(left,right,chunk,&readout,&processHighRate);
                input=readout.input;
                reduction=readout.reduction;
                // Final sample clamp and dither happen at the host rate.
                // A dry sample is then mixed from a separate delay-only lane:
                // bypassed audio never traverses the OS antialias filters.
                finishHostRate(left,right,chunk,hostSettings);
                blendDelayedDry(left,right,chunk,dryStart,hostSettings.bypass);
                for(int i=0;i<chunk;++i)
                {
                    output=juce::jmax(output,std::abs(left[i]));
                    if(right)output=juce::jmax(output,std::abs(right[i]));
                }
            }
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
        // Each high-rate chunk is already post-decimation processed and
        // mixed with the sample-exact host dry lane above.
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
    int activeAudioOversampling() const noexcept
    { return activeOversampling_.load(std::memory_order_acquire); }
    bool audioOversamplingChangePending() const noexcept
    {
        const int requested=1<<juce::jlimit(0,5,static_cast<int>(
            limiterParameters_[AudioOversampling]->units()));
        return requested!=activeAudioOversampling();
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
    struct HighRateReadout
    {
        Core* core=nullptr;
        float input=0.0f, reduction=0.0f;
    };
    static void processHighRate(void* opaque,float* left,float* right,int frames) noexcept
    {
        auto& readout=*static_cast<HighRateReadout*>(opaque);
        readout.core->process(left,right,frames);
        readout.input=std::max(readout.input,
            static_cast<float>(readout.core->inputPeak()));
        readout.reduction=std::max(readout.reduction,
            static_cast<float>(readout.core->blockReductionMaxDb()));
    }
    struct DitherState
    {
        uint32_t rng=0;
        double error=0.0;
    };
    void resetHostDither() noexcept
    {
        finalDither_[0]={0x12345678u,0.0};
        finalDither_[1]={0x91ABCDEFu,0.0};
    }
    void finishHostRate(float* left,float* right,int frames,Settings settings) noexcept
    {
        // After the FIR, clamp/quantize WET only. The delayed dry bypass
        // never clips or adds noise. Sample peak != certified true peak.
        if(settings.audition || settings.unityGain)return;
        const double ceiling=std::pow(10.0,settings.ceilingDb/20.0);
        const bool quantise=settings.ditherMode>0;
        const double lsb=std::ldexp(1.0,1-settings.ditherBits);
        for(int ch=0;ch<(right?2:1);++ch)
        {
            auto* samples=ch==0?left:right;
            auto& state=finalDither_[static_cast<size_t>(ch)];
            const auto random=[&state]() noexcept
            {
                state.rng=state.rng*1664525u+1013904223u;
                return static_cast<double>(state.rng>>8)/16777216.0;
            };
            for(int i=0;i<frames;++i)
            {
                double x=std::clamp(static_cast<double>(samples[i]),-ceiling,ceiling);
                if(quantise)
                {
                    // TPDF noise and optional bounded first-order shaping;
                    // applied once at actual HOST sample rate, never 32x.
                    const double triangular=(random()-random())*lsb;
                    const double shaping=settings.ditherMode==2
                        ?std::clamp(state.error,-lsb,lsb)*0.85:0.0;
                    const double candidate=x+triangular+shaping;
                    const double rounded=std::round(candidate/lsb)*lsb;
                    state.error=settings.ditherMode==2
                        ?std::clamp(candidate-rounded,-lsb,lsb):0.0;
                    x=std::clamp(rounded,-ceiling,ceiling);
                }
                samples[i]=static_cast<float>(x);
            }
        }
    }
    void captureDry(const float* left,const float* right,int frames) noexcept
    {
        const auto capacity=static_cast<int64_t>(hostDry_[0].size());
        if(capacity<=0)return;
        for(int i=0;i<frames;++i,++hostDryFrames_)
        {
            const size_t slot=static_cast<size_t>(hostDryFrames_%capacity);
            hostDry_[0][slot]=std::isfinite(left[i])?left[i]:0.0f;
            if(right)hostDry_[1][slot]=std::isfinite(right[i])?right[i]:0.0f;
        }
    }
    void blendDelayedDry(float* left,float* right,int frames,
                         int64_t start,bool bypass) noexcept
    {
        const auto capacity=static_cast<int64_t>(hostDry_[0].size());
        if(capacity<=0)return;
        const int delay=getLatencySamples();
        const double target=bypass?1.0:0.0;
        for(int i=0;i<frames;++i)
        {
            hostBypassBlend_=hostBypassCoeff_*hostBypassBlend_
                             +(1.0-hostBypassCoeff_)*target;
            const double dryWeight=hostBypassBlend_;
            if(dryWeight<1.0e-9)continue;
            const int64_t readIndex=start+i-delay;
            const auto dry=[&](int channel) noexcept -> float
            {
                return readIndex<0 ? 0.0f
                     : hostDry_[static_cast<size_t>(channel)]
                               [static_cast<size_t>(readIndex%capacity)];
            };
            left[i]=static_cast<float>(left[i]*(1.0-dryWeight)
                                       +dry(0)*dryWeight);
            if(right)right[i]=static_cast<float>(right[i]*(1.0-dryWeight)
                                                +dry(1)*dryWeight);
        }
    }

    Settings effectiveSettings(Settings settings) const noexcept
    {
        if(activeOversampling_.load(std::memory_order_relaxed)==1)
        {
            // The experimental native 8x FIR detector can reconstruct
            // intersample peaks but cannot directly constrain the final
            // interpolated WET waveform. Only while TP is enabled, reserve
            // a small provisional 0.06dB. Quiet input/bypass/unity remain
            // unaffected. 2x/4x detector variants remain uncalibrated.
            if(settings.truePeak && !settings.bypass &&
               !settings.audition && !settings.unityGain)
                settings.ceilingDb-=native1xTruePeakReserveDb(
                    settings.detectorQuality,true);
            return settings;
        }
        if(activeOversampling_.load(std::memory_order_relaxed)>1)
        {
            // High-rate audio already resolves fractional peaks. Additional
            // detector FIR is redundant and extraordinarily costly at 32x.
            // The user-facing ceiling is an actual reconstructed TP
            // target, not merely an oversampled sample peak. Apply a
            // small, explicitly documented empirical safety reserve to
            // the WET limiter before final host-rate reconstruction.
            // Do not change the final user-selected HOST sample ceiling,
            // the bypass lane, or the 1x legacy bit-identical path.
            if(settings.truePeak && !settings.audition && !settings.unityGain)
                settings.ceilingDb-=audioTruePeakReserveDb(
                    activeOversampling_.load(std::memory_order_relaxed),true);
            settings.truePeak=false;
            settings.detectorQuality=0;
            // The oversampled Core always processes WET so host dry can
            // crossfade without forcing audio through interpolation filters.
            settings.bypass=false;
            // 16/24-bit dithering is handled exactly once by finishHostRate()
            // AFTER the decimation FIR, at the project/native sample rate.
            settings.ditherMode=0;
        }
        return settings;
    }
    static const std::array<const char*,Count>& ids()
    {
        static const std::array<const char*,Count> values = {{
            "lim.gain","lim.ceiling","lim.lookahead","lim.attack",
            "lim.release","lim.link","lim.style","lim.tp","lim.quality",
            "lim.bypass","lim.unity","lim.audition","lim.dc","lim.link.release",
            "lim.dither.mode","lim.dither.bits","lim.os.audio"
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
    AudioRateConverter oversampler_;
    std::atomic<int> activeOversampling_{1};
    std::array<DitherState,2> finalDither_{};
    std::array<std::vector<float>,2> hostDry_{};
    int64_t hostDryFrames_=0;
    double hostBypassBlend_=0.0,hostBypassCoeff_=0.0;
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
