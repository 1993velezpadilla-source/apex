// APEX Opto 2A — JUCE host-boundary research tests ONLY.
// Intentionally not registered with the APEX Native plugin format.
// In particular, this is not the PR #103 OptoProcessor or its production DSP.
#include <JuceHeader.h>
#include "../../../Source/CompressorCore/ApexOptoCircuitPathResearchCore.h"
#include "../../../Source/AutomationCore/PluginSlotMixCore.h"
#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
// A real JUCE AudioProcessor probe to test the JUCE latency contract using
// the *actual* research DSP. No user-visible plugin registration is performed.
class OptoCircuitJuceResearchProbe final : public juce::AudioProcessor {
public:
    explicit OptoCircuitJuceResearchProbe(bool useMagnetic)
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          useMagnetic_(useMagnetic) {}

    const juce::String getName() const override { return "APEX Opto 2A [Research Probe]"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Research"; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*,int) override {}
    void releaseResources() override {}
    void reset() override { core_.reset(); }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        const auto input=layout.getMainInputChannelSet();
        return input==layout.getMainOutputChannelSet()
            && (input==juce::AudioChannelSet::mono()
                || input==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double sampleRate,int blockSize) override {
        setRateAndBufferSizeDetails(sampleRate,blockSize);
        core_.prepare(sampleRate,useMagnetic_);
        core_.setParameters(parameters_);
        // MUST occur on the host's prepare/control thread, not audio callback.
        setLatencySamples(core_.latencySamples());
    }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override {
        if(buffer.getNumChannels()<=0 || buffer.getNumSamples()<=0)return;
        core_.process(buffer.getWritePointer(0),
                      buffer.getNumChannels()>1?buffer.getWritePointer(1):nullptr,
                      buffer.getNumSamples());
    }
    void setResearchParameters(APEX::Compressor::ApexOptoCircuitResearchParameters p) {
        parameters_=p;
        core_.setParameters(parameters_);
    }
    void setMagneticForNextPrepare(bool value) { useMagnetic_=value; }
private:
    APEX::Compressor::ApexOptoCircuitPathResearchCore core_;
    APEX::Compressor::ApexOptoCircuitResearchParameters parameters_{};
    bool useMagnetic_=false;
};

// This is a NON-PRODUCTION reference delay for host slot/chain parallel
// mixing verification, not proof that APEX's actual PluginChainCore currently
// aligns its separate slot and chain wet/dry mix buffers.
struct TestHostDryDelay {
    void prepare(int latency) {
        jassert(latency==0 || latency==32);
        samples_.fill(0.f); cursor_=0; latency_=latency;
    }
    float process(float x) {
        if(latency_==0)return x;
        const float out=samples_[(cursor_+samples_.size()-static_cast<std::size_t>(latency_))
                                 %samples_.size()];
        samples_[cursor_]=x;
        cursor_=(cursor_+1)%samples_.size();
        return out;
    }
private:
    std::array<float,64> samples_{};
    std::size_t cursor_=0;
    int latency_=0;
};
// Exact, deterministic 32-sample 2-channel plugin for actual PluginChainCore
// tests. Distinct from the FIR compressor (which has legitimate pre-ringing).
class ExactHostDelayResearchPlugin final : public juce::AudioPluginInstance {
public:
    ExactHostDelayResearchPlugin()
      : AudioPluginInstance(BusesProperties()
            .withInput("Input",juce::AudioChannelSet::stereo(),true)
            .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}
    const juce::String getName() const override {return "APEX Exact Latency Test";}
    void fillInPluginDescription(juce::PluginDescription& d) const override {
        d.name=getName();
        d.descriptiveName=getName();
        d.pluginFormatName="APEX Test";
        d.manufacturerName="APEX";
        d.version="1";
        d.uniqueId=0x454C3332;
        d.deprecatedUid=d.uniqueId;
        d.numInputChannels=2;
        d.numOutputChannels=2;
        d.isInstrument=false;
    }
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double sr,int block) override {
        setRateAndBufferSizeDetails(sr,block);
        reset();
        setLatencySamples(32);
    }
    void releaseResources() override {}
    void reset() override {for(auto& v:delay_)v.fill(0.f);index_=0;}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override {
        if(b.getNumChannels()<2)return;
        float* l=b.getWritePointer(0);
        float* r=b.getWritePointer(1);
        for(int n=0;n<b.getNumSamples();++n) {
            const float oldL=delay_[0][index_],oldR=delay_[1][index_];
            delay_[0][index_]=l[n];delay_[1][index_]=r[n];
            l[n]=oldL;r[n]=oldR;
            index_=(index_+1)%32;
        }
    }
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 0.0;}
    bool hasEditor() const override {return false;}
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Default";}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*,int) override {}
private:
    std::array<std::array<float,32>,2> delay_{};
    std::size_t index_=0;
};
} // namespace

class OptoCircuitJuceResearchHostTests final : public juce::UnitTest {
public:
    OptoCircuitJuceResearchHostTests()
       : juce::UnitTest("Opto2A.ResearchHost", "APEX.OptoResearchHost") {}
    void runTest() override {
        constexpr int sr=48000;
        juce::MidiBuffer midi;

        beginTest("JUCE AudioProcessor advertises 0 in baseline and 32 in 4x");
        for(bool magnetic:{false,true}) {
            auto probe=std::make_unique<OptoCircuitJuceResearchProbe>(magnetic);
            probe->prepareToPlay(sr,128);
            expectEquals(probe->getLatencySamples(),magnetic?32:0);
            expectEquals(probe->getSampleRate(),static_cast<double>(sr));
            expectEquals(probe->getBlockSize(),128);
            expect(!probe->hasEditor(),"test-only processor must not enter plugin catalog");
            probe->releaseResources();
        }

        beginTest("JUCE dry and bypass delay remains exact across 31/64/128");
        for(int block:{31,64,128}) {
            for(bool bypass:{false,true}) {
                OptoCircuitJuceResearchProbe probe(true);
                APEX::Compressor::ApexOptoCircuitResearchParameters p;
                p.mix=bypass?1.0:0.0;
                p.bypass=bypass;
                p.peakReduction=96.0;
                probe.setResearchParameters(p);
                probe.prepareToPlay(sr,block);
                expectEquals(probe.getLatencySamples(),32);
                std::vector<float> left(400),right(400),outL,outR;
                for(int i=0;i<400;++i) {
                    left[static_cast<std::size_t>(i)]=static_cast<float>(
                        .25*std::sin(i*.103)+.07*std::cos(i*.043));
                    right[static_cast<std::size_t>(i)]=static_cast<float>(
                        -.21*std::cos(i*.083)+.03*std::sin(i*.113));
                }
                outL=left;outR=right;
                for(int pos=0;pos<400;pos+=block) {
                    const int count=std::min(block,400-pos);
                    float* channels[]={outL.data()+pos,outR.data()+pos};
                    juce::AudioBuffer<float> subBlock(channels,2,count);
                    probe.processBlock(subBlock,midi);
                }
                for(int i=0;i<400;++i) {
                    const double l=i>=32?left[static_cast<std::size_t>(i-32)]:0;
                    const double r=i>=32?right[static_cast<std::size_t>(i-32)]:0;
                    expectWithinAbsoluteError(static_cast<double>(outL[static_cast<std::size_t>(i)]),l,1.e-6);
                    expectWithinAbsoluteError(static_cast<double>(outR[static_cast<std::size_t>(i)]),r,1.e-6);
                }
                probe.releaseResources();
            }
        }

        beginTest("Actual PluginSlotMixCore equal-power slot and chain need aligned dry");
        for(int block:{64,128}) {
            OptoCircuitJuceResearchProbe probe(true);
            APEX::Compressor::ApexOptoCircuitResearchParameters p;
            p.peakReduction=55.0;p.mix=1.0;
            probe.setResearchParameters(p);
            probe.prepareToPlay(sr,block);
            TestHostDryDelay slotLeft,slotRight,chainLeft,chainRight;
            for(auto* line:{&slotLeft,&slotRight,&chainLeft,&chainRight})
                line->prepare(probe.getLatencySamples());
            bool nonSilentWet=false;
            for(int start=0;start<1024;start+=block) {
                juce::AudioBuffer<float> wet(2,block);
                juce::AudioBuffer<float> slotDry(2,block);
                juce::AudioBuffer<float> chainDry(2,block);
                for(int i=0;i<block;++i) {
                    const int n=start+i;
                    const auto inL=static_cast<float>(
                        .4*std::sin(n*.031)+.04*std::cos(n*.119));
                    const auto inR=static_cast<float>(.3*std::cos(n*.023));
                    wet.setSample(0,i,inL); wet.setSample(1,i,inR);
                    slotDry.setSample(0,i,slotLeft.process(inL));
                    slotDry.setSample(1,i,slotRight.process(inR));
                    chainDry.setSample(0,i,chainLeft.process(inL));
                    chainDry.setSample(1,i,chainRight.process(inR));
                }
                probe.processBlock(wet,midi);
                if(start>=64 && std::abs(wet.getSample(0,0))>.001f)
                    nonSilentWet=true;

                // Preserve the real FIR wet output BEFORE parallel mixing.
                // A linear-phase 65-tap FIR has GROUP DELAY of 32 native
                // samples for two transformer stages; group delay is NOT
                // an onset gate. FIR pre-ringing can produce real wet audio
                // during samples 0..31, even when the dry delay is exact.
                juce::AudioBuffer<float> unblendedWet(2,block);
                for(int ch=0;ch<2;++ch)
                    unblendedWet.copyFrom(ch,0,wet,ch,0,block);

                DAW::PluginSlotMixCore::apply(
                    slotDry,wet,0.5f,DAW::SlotMixCurve::EqualPower,block);
                DAW::PluginSlotMixCore::apply(
                    chainDry,wet,0.5f,DAW::SlotMixCurve::EqualPower,block);

                constexpr double equalPower=0.7071067811865475244;
                for(int i=0;i<block;++i) {
                    for(int ch=0;ch<2;++ch) {
                        const double unblended=unblendedWet.getSample(ch,i);
                        const double delayedSlot=slotDry.getSample(ch,i);
                        const double delayedChain=chainDry.getSample(ch,i);
                        // Expected actual host mix law, with each external
                        // dry branch delayed by the processor's PDC contract.
                        const double afterSlot=equalPower*(delayedSlot+unblended);
                        const double expected=equalPower*(delayedChain+afterSlot);
                        const double actual=wet.getSample(ch,i);
                        expect(std::isfinite(actual));
                        expectWithinAbsoluteError(actual,expected,1.e-6);
                        if(start+i<probe.getLatencySamples()) {
                            // No premature dry leakage before the PDC point;
                            // any early output is physical FIR pre-ringing.
                            expectWithinAbsoluteError(
                                actual,unblended*0.5,1.e-6);
                        }
                    }
                }
            }
            expect(nonSilentWet,"Research circuit must produce nonzero wet audio");
        }

        beginTest("Negative control: unaligned PluginSlotMixCore leaks early dry signal");
        {
            juce::AudioBuffer<float> zeroWet(2,64),uncompensatedDry(2,64);
            zeroWet.clear();uncompensatedDry.clear();
            uncompensatedDry.setSample(0,0,1.0f);
            DAW::PluginSlotMixCore::apply(
                uncompensatedDry,zeroWet,.5f,DAW::SlotMixCurve::EqualPower,64);
            expect(std::abs(zeroWet.getSample(0,0))>0.5f,
                   "Undelayed dry leaks before x4 wet. Main host still needs fix.");
        }

        beginTest("44.1 kHz JUCE latency contract and 64/128 dry-bypass timing");
        for(const int frames:{64,128}) {
            for(const bool magnetic:{false,true}) {
                OptoCircuitJuceResearchProbe probe(magnetic);
                APEX::Compressor::ApexOptoCircuitResearchParameters p;
                p.mix=0.0;
                probe.setResearchParameters(p);
                probe.prepareToPlay(44100.0,frames);
                expectEquals(probe.getSampleRate(),44100.0);
                expectEquals(probe.getLatencySamples(),magnetic?32:0);
                std::vector<float> dryL(384),dryR(384);
                for(int n=0;n<384;++n) {
                    dryL[static_cast<std::size_t>(n)]=
                        static_cast<float>(.31*std::cos(n*.045));
                    dryR[static_cast<std::size_t>(n)]=
                        static_cast<float>(.19*std::sin(n*.073));
                }
                auto outL=dryL,outR=dryR;
                for(int start=0;start<384;start+=frames) {
                    float* channels[]={outL.data()+start,outR.data()+start};
                    juce::AudioBuffer<float> buffer(channels,2,frames);
                    probe.processBlock(buffer,midi);
                }
                const int latency=magnetic?32:0;
                for(int n=0;n<384;++n) {
                    const double expectedL=n<latency?0.0:
                        dryL[static_cast<std::size_t>(n-latency)];
                    const double expectedR=n<latency?0.0:
                        dryR[static_cast<std::size_t>(n-latency)];
                    expectWithinAbsoluteError(
                        static_cast<double>(outL[static_cast<std::size_t>(n)]),
                        expectedL,1.e-6);
                    expectWithinAbsoluteError(
                        static_cast<double>(outR[static_cast<std::size_t>(n)]),
                        expectedR,1.e-6);
                }
                probe.releaseResources();
            }
        }

#if defined(APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC) && APEX_ENABLE_EXPERIMENTAL_HOST_DRY_PDC
        beginTest("Real PluginChainCore slot + chain 32-sample PDC 44.1k/48k");
        for(const double sampleRate:{44100.0,48000.0}) {
            for(const int frames:{64,128}) {
                DAW::PluginChainCore chain;
                chain.prepare(sampleRate,frames);
                expectEquals(chain.appendPluginInstanceForTesting(
                    std::make_unique<ExactHostDelayResearchPlugin>()),0);
                expectEquals(chain.totalLatencySamples(),32);
                chain.setSlotMix(0,.5f);
                chain.setChainOutputMix(.5f);
                constexpr int total=512;
                std::vector<float> originalL(total),originalR(total);
                for(int n=0;n<total;++n) {
                    originalL[static_cast<std::size_t>(n)]=
                        static_cast<float>(.27*std::cos(.049*n));
                    originalR[static_cast<std::size_t>(n)]=
                        static_cast<float>(.22*std::sin(.063*n));
                }
                // Slot 50% equal-power gains sum to sqrt(2) when dry
                // and wet are the SAME aligned audio. The chain 50% mix
                // combines one dry input and that slot result, yielding
                // (1+sqrt(2))/sqrt(2) ~= 1.7071, NOT 2.0.
                // Requiring 2.0 was an incorrect test oracle, not DSP loss.
                constexpr double factor=1.0+0.7071067811865475244;
                for(int start=0;start<total;start+=frames) {
                    juce::AudioBuffer<float> audio(2,frames);
                    for(int n=0;n<frames;++n) {
                        audio.setSample(0,n,originalL[static_cast<std::size_t>(start+n)]);
                        audio.setSample(1,n,originalR[static_cast<std::size_t>(start+n)]);
                    }
                    chain.processBlock(audio,frames);
                    for(int n=0;n<frames;++n) {
                        const int at=start+n;
                        const double expectedL=at<32?0.0:
                            factor*originalL[static_cast<std::size_t>(at-32)];
                        const double expectedR=at<32?0.0:
                            factor*originalR[static_cast<std::size_t>(at-32)];
                        expectWithinAbsoluteError(
                            static_cast<double>(audio.getSample(0,n)),expectedL,3.e-5);
                        expectWithinAbsoluteError(
                            static_cast<double>(audio.getSample(1,n)),expectedR,3.e-5);
                    }
                }
            }
        }
#endif

        beginTest("Changing quality during prepare updates latency without callback toggles");
        OptoCircuitJuceResearchProbe probe(false);
        probe.prepareToPlay(sr,128);
        expectEquals(probe.getLatencySamples(),0);
        probe.setMagneticForNextPrepare(true);
        probe.prepareToPlay(sr,128);
        expectEquals(probe.getLatencySamples(),32);
        probe.setMagneticForNextPrepare(false);
        probe.prepareToPlay(sr,128);
        expectEquals(probe.getLatencySamples(),0);
        probe.releaseResources();
    }
};
static OptoCircuitJuceResearchHostTests optoCircuitJuceResearchHostTests;
