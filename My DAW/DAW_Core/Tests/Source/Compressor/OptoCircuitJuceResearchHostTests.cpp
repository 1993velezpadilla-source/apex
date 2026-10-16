// APEX Opto 2A — JUCE host-boundary research tests ONLY.
// Intentionally not registered with the APEX Native plugin format.
// In particular, this is not the PR #103 OptoProcessor or its production DSP.
#include <JuceHeader.h>
#include "../../../Source/CompressorCore/ApexOptoCircuitPathResearchCore.h"
#include "../../../Source/AutomationCore/PluginSlotMixCore.h"
#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/PDCCore/ProgrammeRoutePdcJuceResearchBridge.h"
#include "../../../Source/PDCCore/ProgrammeRoutePdcJuceDispatchResearchCore.h"
#include "../../../Source/PDCCore/ProgrammeRoutePdcSnapshotHandoffResearchCore.h"
#if defined(APEX_ENABLE_EXPERIMENTAL_GRAPH_PDC_PREFLIGHT) && APEX_ENABLE_EXPERIMENTAL_GRAPH_PDC_PREFLIGHT
#include "../../../Source/AudioEngineCore/AudioEngine.h"
// Compiles the conditional message-thread graph publication interface too.
#include "../../../Source/AppCore/ApplicationCore.h"
#endif
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
    explicit ExactHostDelayResearchPlugin(int samples=32)
      : AudioPluginInstance(BusesProperties()
            .withInput("Input",juce::AudioChannelSet::stereo(),true)
            .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
        latency_(samples) { jassert(samples==16 || samples==32); }
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
        setLatencySamples(latency_);
    }
    void releaseResources() override {}
    void reset() override {for(auto& v:delay_)v.fill(0.f);index_=0;}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override {
        if(b.getNumChannels()<2)return;
        ++processCalls;
        float* l=b.getWritePointer(0);
        float* r=b.getWritePointer(1);
        for(int n=0;n<b.getNumSamples();++n) {
            const float oldL=delay_[0][index_],oldR=delay_[1][index_];
            delay_[0][index_]=l[n];delay_[1][index_]=r[n];
            l[n]=oldL;r[n]=oldR;
            index_=(index_+1)%static_cast<std::size_t>(latency_);
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
    int processCalls=0; // research test probe for actual 64-frame slicing
private:
    std::array<std::array<float,32>,2> delay_{};
    std::size_t index_=0;
    int latency_=32;
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
        beginTest("Serial 32+16 plugin latencies keep chain dry aligned to 48 samples");
        // Both slot mixes are 50% equal-power and the overall chain mix is
        // also 50%. With identical delayed wet and dry, each slot multiplies
        // amplitude by sqrt(2), and the chain multiplies by (1+2)/sqrt(2).
        for(const double sr:{44100.0,48000.0}) {
            for(const int frames:{64,128}) {
                DAW::PluginChainCore chain;
                chain.prepare(sr,frames);
                expectEquals(chain.appendPluginInstanceForTesting(
                    std::make_unique<ExactHostDelayResearchPlugin>(32)),0);
                expectEquals(chain.appendPluginInstanceForTesting(
                    std::make_unique<ExactHostDelayResearchPlugin>(16)),1);
                expectEquals(chain.totalLatencySamples(),48);
                chain.setSlotMix(0,.5f);
                chain.setSlotMix(1,.5f);
                chain.setChainOutputMix(.5f);
                constexpr double expectedFactor=3.0*0.7071067811865475244;
                constexpr int total=512;
                for(int start=0;start<total;start+=frames) {
                    juce::AudioBuffer<float> audio(2,frames);
                    for(int i=0;i<frames;++i) {
                        const int at=start+i;
                        audio.setSample(0,i,static_cast<float>(
                            .26*std::sin(.057*at)+.1*std::cos(.011*at)));
                        audio.setSample(1,i,static_cast<float>(
                            -.31*std::cos(.041*at)));
                    }
                    chain.processBlock(audio,frames);
                    for(int i=0;i<frames;++i) {
                        const int at=start+i;
                        const int src=at-48;
                        const double left=src<0?0.0:expectedFactor*(
                            .26*std::sin(.057*src)+.1*std::cos(.011*src));
                        const double right=src<0?0.0:
                            expectedFactor*(-.31*std::cos(.041*src));
                        expectWithinAbsoluteError(
                            static_cast<double>(audio.getSample(0,i)),left,3.e-5);
                        expectWithinAbsoluteError(
                            static_cast<double>(audio.getSample(1,i)),right,3.e-5);
                    }
                }
            }
        }

        beginTest("Actual host stereo dry-PDC parity across four processing entrypoints");
        // Normal audio, first-slot MIDI, explicit sidechain and automation
        // unsliced paths all call the same PDC-enabled host mixing contract.
        // Uneven 31-frame callbacks also exercise ring wrap independently
        // of the configured host block length.
        for(const double sr:{44100.0,48000.0}) {
            for(const int frames:{31,64,128}) {
                for(int mode=0;mode<4;++mode) {
                    DAW::PluginChainCore chain;
                    chain.prepare(sr,frames);
                    expectEquals(chain.appendPluginInstanceForTesting(
                        std::make_unique<ExactHostDelayResearchPlugin>(32)),0);
                    chain.setSlotMix(0,.5f);
                    chain.setChainOutputMix(.5f);
                    constexpr int total=512;
                    constexpr double factor=1.0+0.7071067811865475244;
                    juce::MidiBuffer midi;
                    for(int start=0;start<total;start+=frames) {
                        const int count=std::min(frames,total-start);
                        juce::AudioBuffer<float> audio(2,count);
                        for(int n=0;n<count;++n) {
                            const int at=start+n;
                            audio.setSample(0,n,static_cast<float>(
                                .24*std::sin(.029*at)+.07*std::cos(.091*at)));
                            audio.setSample(1,n,static_cast<float>(
                                -.19*std::cos(.037*at)));
                        }
                        switch(mode) {
                            case 0: chain.processBlock(audio,count);break;
                            case 1: chain.processBlockWithMidi(audio,midi,count);break;
                            case 2: {
                                juce::AudioBuffer<float> sidechain(2,count);
                                sidechain.clear();
                                chain.processBlockWithSidechain(audio,sidechain,count);
                                break;
                            }
                            default:
                                chain.processBlockWithAutomation(
                                    audio,nullptr,nullptr,DAW::TrackID{},
                                    nullptr,start,sr,120.0,count);
                                break;
                        }
                        for(int n=0;n<count;++n) {
                            const int at=start+n,src=at-32;
                            const double l=src<0?0.0:factor*(
                                .24*std::sin(.029*src)+.07*std::cos(.091*src));
                            const double r=src<0?0.0:
                                factor*(-.19*std::cos(.037*src));
                            expectWithinAbsoluteError(
                                static_cast<double>(audio.getSample(0,n)),l,3.e-5);
                            expectWithinAbsoluteError(
                                static_cast<double>(audio.getSample(1,n)),r,3.e-5);
                        }
                    }
                }
            }
        }

        beginTest("Moving slot-mix automation: 256 sample sliced callback matches four real 64 sample callbacks");
        // This is the real AutomationSnapshot lane, not synthetic knob
        // changes. The 256-frame host path MUST call the delay processor
        // exactly 4 times (kAutomationSliceSamples=64); the reference host
        // receives four genuine device callbacks of 64 frames.
        for(const double rate:{44100.0,48000.0}) {
            for(const int mode:{0,1,2}) { // no MIDI, MIDI, silent sidechain
                const DAW::TrackID id("apex.opto.pdc.sliced");
                DAW::AutomationSnapshot automation;
                DAW::AutomationSnapshot::LaneSnapshot lane;
                lane.trackId=id;
                lane.parameterId=DAW::AutomationManagerCore::makePluginSlotMixId(0);
                lane.enabled=true;
                lane.points={{0,0.1f},{127,0.95f},{255,0.2f}};
                automation.lanes.push_back(std::move(lane));

                DAW::PluginChainCore sliced, reference;
                sliced.prepare(rate,256);
                reference.prepare(rate,64);
                auto slicedPlugin=std::make_unique<ExactHostDelayResearchPlugin>();
                auto refPlugin=std::make_unique<ExactHostDelayResearchPlugin>();
                auto* probeSliced=slicedPlugin.get();
                auto* probeRef=refPlugin.get();
                expectEquals(sliced.appendPluginInstanceForTesting(
                    std::move(slicedPlugin)),0);
                expectEquals(reference.appendPluginInstanceForTesting(
                    std::move(refPlugin)),0);
                sliced.setChainOutputMix(.5f);
                reference.setChainOutputMix(.5f);

                juce::AudioBuffer<float> full(2,256),ref(2,256);
                for(int n=0;n<256;++n) {
                    full.setSample(0,n,static_cast<float>(
                        .27*std::sin(.061*n)+.1*std::cos(.017*n)));
                    full.setSample(1,n,static_cast<float>(
                        -.21*std::cos(.042*n)));
                }
                for(int ch=0;ch<2;++ch)
                    ref.copyFrom(ch,0,full,ch,0,256);
                juce::MidiBuffer midi;
                juce::AudioBuffer<float> sideFull(2,256),sideSmall(2,64);
                sideFull.clear();sideSmall.clear();
                sliced.processBlockWithAutomation(
                    full,mode==1?&midi:nullptr,mode==2?&sideFull:nullptr,
                    id,&automation,0,rate,120.0,256);
                for(int start=0;start<256;start+=64) {
                    float* channels[]={ref.getWritePointer(0)+start,
                                       ref.getWritePointer(1)+start};
                    juce::AudioBuffer<float> chunk(channels,2,64);
                    reference.processBlockWithAutomation(
                        chunk,mode==1?&midi:nullptr,
                        mode==2?&sideSmall:nullptr,
                        id,&automation,start,rate,120.0,64);
                }
                expectEquals(probeSliced->processCalls,4,
                    "the 256 frame callback must be split into four");
                expectEquals(probeRef->processCalls,4,
                    "reference is four genuine 64-frame host blocks");
                for(int ch=0;ch<2;++ch)
                    for(int n=0;n<256;++n)
                        expectWithinAbsoluteError(
                            static_cast<double>(full.getSample(ch,n)),
                            static_cast<double>(ref.getSample(ch,n)),2.e-6);
                expectWithinAbsoluteError(
                    static_cast<double>(sliced.getSlot(0)->getSlotMixNormalized()),
                    static_cast<double>(reference.getSlot(0)->getSlotMixNormalized()),
                    2.e-6);
            }
        }

        beginTest("Real PluginChainCore clears slot+chain dry FIFOs on reset");
        for(const double rate:{44100.0,48000.0}) {
            DAW::PluginChainCore chain;
            chain.prepare(rate,64);
            expectEquals(chain.appendPluginInstanceForTesting(
                std::make_unique<ExactHostDelayResearchPlugin>()),0);
            chain.setSlotMix(0,.5f);
            chain.setChainOutputMix(.5f);
            // Populate history on all three delay lines.
            for(int block=0;block<8;++block) {
                juce::AudioBuffer<float> audio(2,64);
                for(int ch=0;ch<2;++ch)
                    for(int n=0;n<64;++n)
                        audio.setSample(ch,n,ch==0?.42f:-.29f);
                chain.processBlock(audio,64);
            }
            chain.reset();
            juce::AudioBuffer<float> impulse(2,64);
            impulse.clear();
            impulse.setSample(0,0,.40f);
            impulse.setSample(1,0,-.20f);
            chain.processBlock(impulse,64);
            for(int i=0;i<64;++i) {
                const double factor=1.0+0.7071067811865475244;
                const double left=i==32?factor*.4:0;
                const double right=i==32?factor*(-.2):0;
                expectWithinAbsoluteError(
                    static_cast<double>(impulse.getSample(0,i)),left,3.e-5);
                expectWithinAbsoluteError(
                    static_cast<double>(impulse.getSample(1,i)),right,3.e-5);
            }
        }

        beginTest("Real host PDC retains dry history when slot and chain are fully wet");
        for(const double rate:{44100.0,48000.0}) {
            DAW::PluginChainCore chain;
            chain.prepare(rate,64);
            expectEquals(chain.appendPluginInstanceForTesting(
                std::make_unique<ExactHostDelayResearchPlugin>()),0);
            chain.setSlotMix(0,1.0f);
            chain.setChainOutputMix(1.0f);
            // Must record dry history despite no audible dry gain.
            for(int b=0;b<8;++b) {
                juce::AudioBuffer<float> audio(2,64);
                for(int i=0;i<64;++i) {
                    audio.setSample(0,i,.33f);
                    audio.setSample(1,i,-.19f);
                }
                chain.processBlock(audio,64);
            }
            chain.setSlotMix(0,0.0f);
            chain.setChainOutputMix(0.0f);
            juce::AudioBuffer<float> audio(2,64);
            audio.clear();
            chain.processBlock(audio,64);
            for(int i=0;i<32;++i) {
                expectWithinAbsoluteError(
                    static_cast<double>(audio.getSample(0,i)),.33,1.e-6);
                expectWithinAbsoluteError(
                    static_cast<double>(audio.getSample(1,i)),-.19,1.e-6);
            }
            for(int i=32;i<64;++i) {
                expectWithinAbsoluteError(
                    static_cast<double>(audio.getSample(0,i)),0.0,1.e-6);
                expectWithinAbsoluteError(
                    static_cast<double>(audio.getSample(1,i)),0.0,1.e-6);
            }
        }

        beginTest("Reprepare 44.1 to 48k resets chain PDC without stale audio");
        {
            DAW::PluginChainCore chain;
            chain.prepare(44100.0,64);
            expectEquals(chain.appendPluginInstanceForTesting(
                std::make_unique<ExactHostDelayResearchPlugin>()),0);
            chain.setSlotMix(0,.5f);
            chain.setChainOutputMix(.5f);
            for(int b=0;b<6;++b) {
                juce::AudioBuffer<float> warm(2,64);
                for(int ch=0;ch<2;++ch)
                    for(int n=0;n<64;++n)
                        warm.setSample(ch,n,ch==0?.45f:-.3f);
                chain.processBlock(warm,64);
            }
            chain.prepare(48000.0,64);
            expectEquals(chain.totalLatencySamples(),32);
            juce::AudioBuffer<float> impulse(2,64);
            impulse.clear();
            impulse.setSample(0,0,.25f);
            impulse.setSample(1,0,-.125f);
            chain.processBlock(impulse,64);
            const double factor=1.0+0.7071067811865475244;
            for(int n=0;n<64;++n) {
                const double left=n==32?factor*.25:0;
                const double right=n==32?factor*(-.125):0;
                expectWithinAbsoluteError(
                    static_cast<double>(impulse.getSample(0,n)),left,3.e-5);
                expectWithinAbsoluteError(
                    static_cast<double>(impulse.getSample(1,n)),right,3.e-5);
            }
        }

        beginTest("Real host PDC bypass after fade stays aligned after reset");
        for(const double rate:{44100.0,48000.0}) {
            DAW::PluginChainCore chain;
            chain.prepare(rate,64);
            expectEquals(chain.appendPluginInstanceForTesting(
                std::make_unique<ExactHostDelayResearchPlugin>()),0);
            chain.setSlotMix(0,.5f);
            chain.setChainOutputMix(.5f);
            chain.setSlotBypassed(0,true);
            // Fade length ~5 ms and must settle before the impulse.
            for(int b=0;b<8;++b) {
                juce::AudioBuffer<float> zeros(2,64);
                zeros.clear();
                chain.processBlock(zeros,64);
            }
            chain.reset();
            juce::AudioBuffer<float> impulse(2,64);
            impulse.clear();
            impulse.setSample(0,0,.3f);
            impulse.setSample(1,0,-.15f);
            chain.processBlock(impulse,64);
            const double factor=1.4142135623730951; // dry plus dry at 50% chain
            for(int i=0;i<64;++i) {
                const double left=i==32?.3*factor:0.0;
                const double right=i==32?-.15*factor:0.0;
                expectWithinAbsoluteError(
                    static_cast<double>(impulse.getSample(0,i)),left,3.e-5);
                expectWithinAbsoluteError(
                    static_cast<double>(impulse.getSample(1,i)),right,3.e-5);
            }
        }
#endif

        beginTest("Immutable RoutingSnapshot + plugin-latency map produces correct send/bus PDC plan");
        {
            using Planner=DAW::PdcResearch::ProgrammeRoutePdcPlannerResearchCore;
            using Bridge=DAW::PdcResearch::ProgrammeRoutePdcJuceResearchBridge;
            DAW::RoutingSnapshot graph;
            graph.nodes={
                {"RN_voice","vocal",DAW::RoutingNodeType::Track,true},
                {"RN_bass","bass",DAW::RoutingNodeType::Track,true},
                {"RN_aux","aux",DAW::RoutingNodeType::Bus,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            graph.processingOrder={"RN_voice","RN_bass","RN_aux","master"};
            const auto edge=[](const char* id,const char* src,
                               const char* dst,DAW::ConnectionType type) {
                DAW::RoutingSnapshot::EdgeSnapshot e;
                e.id=id; e.sourceNodeId=src;e.destNodeId=dst;e.type=type;
                return e;
            };
            graph.edges={
                edge("voice-main","RN_voice","master",DAW::ConnectionType::Direct),
                edge("voice-send","RN_voice","RN_aux",DAW::ConnectionType::Send),
                edge("bass-send","RN_bass","RN_aux",DAW::ConnectionType::PreSend),
                edge("aux-main","RN_aux","master",DAW::ConnectionType::Direct),
                edge("bass-main","RN_bass","master",DAW::ConnectionType::Direct),
                edge("bass-key","RN_bass","RN_voice",DAW::ConnectionType::Sidechain)};
            // The normal AudioEngine snapshot stores the authoritative track
            // IDs, not mutable PluginChainCore instances.
            std::map<DAW::TrackID,int> publishedLatencies={
                {"vocal",32},{"bass",0},{"aux",16}};
            Planner planned;
            expect(Bridge::build(graph,&publishedLatencies,planned));
            expect(planned.valid());
            const auto requiresDelay=[&](const char* id,int samples) {
                const auto* found=planned.findEdge(id);
                expect(found!=nullptr,id);
                if(found)expectEquals(found->delaySamples,samples);
            };
            requiresDelay("voice-main",16); // voice total 32 vs wet path 48
            requiresDelay("voice-send",0);
            requiresDelay("bass-send",32);  // align BEFORE the aux compressor
            requiresDelay("aux-main",0);
            requiresDelay("bass-main",48);
            expect(planned.findEdge("bass-key")==nullptr,
                   "Sidechain key must not be treated as parallel audio");
            const auto* aux=planned.findNode("RN_aux");
            expect(aux!=nullptr);
            if(aux)expectEquals(aux->outputLatency,48);
            // Inactive send disappears and stops affecting the aux merge.
            graph.edges[2].active=false;
            expect(Bridge::build(graph,&publishedLatencies,planned));
            expect(planned.findEdge("bass-send")==nullptr);
            expectEquals(planned.findEdge("voice-main")->delaySamples,16);
            // Corrupt topological order must fail closed.
            graph.processingOrder={"RN_aux","RN_voice","RN_bass","master"};
            expect(!Bridge::build(graph,&publishedLatencies,planned));
            expect(!planned.valid());
        }

        beginTest("Actual JUCE RoutingSnapshot indices render 32+16-sample parallel sends aligned with master");
        for(double sr:{44100.0,48000.0}) {
            for(int frames:{31,64,128}) {
                using Dispatch=DAW::PdcResearch::ProgrammeRoutePdcJuceDispatchResearchCore;
                DAW::RoutingSnapshot graph;
                graph.version=207;
                graph.nodes={
                    {"vocal-node","vocal",DAW::RoutingNodeType::Track,true},
                    {"aux-node","aux",DAW::RoutingNodeType::Bus,true},
                    {"master","master",DAW::RoutingNodeType::Master,true}};
                graph.processingOrder={"vocal-node","aux-node","master"};
                const auto makeEdge=[](const char* id,const char* src,
                                       const char* dst,DAW::ConnectionType type) {
                    DAW::RoutingSnapshot::EdgeSnapshot e;
                    e.id=id;e.sourceNodeId=src;e.destNodeId=dst;e.type=type;
                    return e;
                };
                graph.edges={
                    makeEdge("dry","vocal-node","master",DAW::ConnectionType::Direct),
                    makeEdge("send","vocal-node","aux-node",DAW::ConnectionType::Send),
                    makeEdge("wet","aux-node","master",DAW::ConnectionType::Direct),
                    makeEdge("key","aux-node","vocal-node",DAW::ConnectionType::Sidechain)};
                std::map<DAW::TrackID,int> latencies={
                    {"vocal",32},{"aux",16}};
                Dispatch dispatcher;
                expect(dispatcher.prepare(graph,&latencies,2));
                expectEquals(dispatcher.version(),static_cast<std::uint64_t>(207));
                expectEquals(dispatcher.edgeDelay(0),16);
                expectEquals(dispatcher.edgeDelay(1),0);
                expectEquals(dispatcher.edgeDelay(2),0);
                expectEquals(dispatcher.edgeDelay(3),-1);
                expect(dispatcher.coversAllActiveProgrammeEdges(graph),
                       "all active programme edges are prepared; sidechain excluded");
                DAW::PluginParallelDryDelayResearchCore vocalFx,auxFx;
                expect(vocalFx.prepare(2,32));
                expect(auxFx.prepare(2,16));
                constexpr int total=512;
                for(int start=0;start<total;start+=frames) {
                    const int n=std::min(frames,total-start);
                    juce::AudioBuffer<float> vocal(2,n),dry(2,n),wet(2,n);
                    for(int i=0;i<n;++i) {
                        const int at=start+i;
                        vocal.setSample(0,i,static_cast<float>(
                            .22*std::sin(at*.037)+.07*std::cos(at*.091)));
                        vocal.setSample(1,i,static_cast<float>(
                            -.3*std::cos(at*.021)));
                    }
                    vocalFx.process(vocal.getArrayOfWritePointers(),2,n);
                    for(int ch=0;ch<2;++ch) {
                        dry.copyFrom(ch,0,vocal,ch,0,n);
                        wet.copyFrom(ch,0,vocal,ch,0,n);
                    }
                    expect(!dispatcher.processEdge(
                        206,0,dry.getArrayOfWritePointers(),2,n));
                    expect(dispatcher.processEdge(
                        207,0,dry.getArrayOfWritePointers(),2,n));
                    expect(dispatcher.processEdge(
                        207,1,wet.getArrayOfWritePointers(),2,n));
                    auxFx.process(wet.getArrayOfWritePointers(),2,n);
                    expect(dispatcher.processEdge(
                        207,2,wet.getArrayOfWritePointers(),2,n));
                    for(int i=0;i<n;++i) {
                        const int source=start+i-48;
                        const double expL=source<0?0.0:2.*(
                            .22*std::sin(source*.037)+.07*std::cos(source*.091));
                        const double expR=source<0?0.0:
                            -0.6*std::cos(source*.021);
                        expectWithinAbsoluteError(
                            static_cast<double>(dry.getSample(0,i))+
                            static_cast<double>(wet.getSample(0,i)),
                            expL,2.e-6);
                        expectWithinAbsoluteError(
                            static_cast<double>(dry.getSample(1,i))+
                            static_cast<double>(wet.getSample(1,i)),
                            expR,2.e-6);
                    }
                }
                float left=1.f,right=-.5f;
                float* pointers[]={&left,&right};
                expect(!dispatcher.processEdge(207,3,pointers,2,1));
                expectEquals(left,1.f);
                graph.edges[1].active=false;
                graph.version=208;
                expect(dispatcher.prepare(graph,&latencies,2));
                expectEquals(dispatcher.edgeDelay(0),0);
                expectEquals(dispatcher.edgeDelay(1),-1);
                expect(!dispatcher.processEdge(207,0,pointers,2,1),
                       "old immutable snapshot generation must not dispatch");
                graph.processingOrder={"master","vocal-node","aux-node"};
                graph.version=209;
                expect(!dispatcher.prepare(graph,&latencies,2));
                expectEquals(dispatcher.version(),static_cast<std::uint64_t>(208));
                expectEquals(dispatcher.edgeDelay(0),0);
                // An active edge targeting an inactive aux has no prepared
                // delay. Reject the ENTIRE experimental block rather than
                // compensating direct but accidentally sending aux dry.
                graph.processingOrder={"vocal-node","aux-node","master"};
                graph.nodes[1].active=false;
                graph.edges[1].active=true;
                graph.version=210;
                expect(dispatcher.prepare(graph,&latencies,2));
                expect(!dispatcher.coversAllActiveProgrammeEdges(graph));
                graph.edges[1].active=false;
                graph.edges[2].active=false;
                graph.version=211;
                expect(dispatcher.prepare(graph,&latencies,2));
                expect(dispatcher.coversAllActiveProgrammeEdges(graph));
            }
        }

#if defined(APEX_ENABLE_EXPERIMENTAL_GRAPH_PDC_PREFLIGHT) && APEX_ENABLE_EXPERIMENTAL_GRAPH_PDC_PREFLIGHT
        beginTest("AudioEngine research opt-in dispatch aligns direct + send + bus exactly once");
        for(const double rate:{44100.0,48000.0})
        {
            for(const int frames:{31,64,128})
            {
                DAW::AudioEngine engine;
                // Match the engine device format before staging a PDC bank.
                // The realtime safety gate rejects banks prepared for any
                // other sample rate; a default-constructed engine is 44.1k.
                engine.prepare(rate,frames);
                // No Release audio rendering is enabled: runtime opt-in
                // exists exclusively in the APEXTests preprocessor build.
                expectEquals(static_cast<int>(
                    engine.getMonitoringPdcMode()),
                    static_cast<int>(DAW::LiveMonitoringBypassCore::Mode::Bypass));
                engine.setExperimentalGraphPdcRenderEnabledForTests(true);
                DAW::RoutingSnapshot graph;
                graph.version=610;
                graph.nodes={
                    {"vocal-node","vocal",DAW::RoutingNodeType::Track,true},
                    {"aux-node","aux",DAW::RoutingNodeType::Bus,true},
                    {"master","master",DAW::RoutingNodeType::Master,true}};
                graph.processingOrder={"vocal-node","aux-node","master"};
                DAW::RoutingSnapshot::EdgeSnapshot direct,send,bus;
                direct.id="direct";direct.sourceNodeId="vocal-node";
                direct.destNodeId="master";direct.type=DAW::ConnectionType::Direct;
                send.id="send";send.sourceNodeId="vocal-node";
                send.destNodeId="aux-node";send.type=DAW::ConnectionType::Send;
                bus.id="bus";bus.sourceNodeId="aux-node";
                bus.destNodeId="master";bus.type=DAW::ConnectionType::Direct;
                graph.edges={direct,send,bus};
                std::map<DAW::TrackID,int> delays={{"vocal",32},{"aux",16}};
                expect(engine.stageProgrammeGraphPdcPreflightOnControlThread(
                    graph,&delays,2,rate));
                DAW::PluginParallelDryDelayResearchCore vocalFx,auxFx;
                expect(vocalFx.prepare(2,32));
                expect(auxFx.prepare(2,16));
                constexpr int total=499;
                for(int start=0;start<total;start+=frames)
                {
                    const int n=std::min(frames,total-start);
                    juce::AudioBuffer<float> dry(2,n),wet(2,n);
                    for(int i=0;i<n;++i)
                    {
                        const int at=start+i;
                        dry.setSample(0,i,static_cast<float>(.14*std::sin(at*.057)));
                        dry.setSample(1,i,static_cast<float>(.18*std::cos(at*.041)));
                    }
                    vocalFx.process(dry.getArrayOfWritePointers(),2,n);
                    for(int ch=0;ch<2;++ch)
                        wet.copyFrom(ch,0,dry,ch,0,n);
                    expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,dry));
                    expect(engine.renderOnePreparedGraphEdgeForTests(graph,1,wet));
                    auxFx.process(wet.getArrayOfWritePointers(),2,n);
                    expect(engine.renderOnePreparedGraphEdgeForTests(graph,2,wet));
                    for(int i=0;i<n;++i)
                    {
                        const int at=start+i-48;
                        const double expectedL=at<0?0.0:2.*.14*std::sin(at*.057);
                        const double expectedR=at<0?0.0:2.*.18*std::cos(at*.041);
                        expectWithinAbsoluteError(
                            static_cast<double>(dry.getSample(0,i)+wet.getSample(0,i)),
                            expectedL,2.e-6);
                        expectWithinAbsoluteError(
                            static_cast<double>(dry.getSample(1,i)+wet.getSample(1,i)),
                            expectedR,2.e-6);
                    }
                }
                juce::AudioBuffer<float> check(2,31);
                check.clear();
                graph.version=611;
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check),
                       "stale graph cannot dispatch a mismatched PDC bank");
                // A same-version snapshot may still change its sends:
                // RoutingGraph::publishSnapshotOnly() does not bump version.
                // Never apply a bank prepared for a different active route.
                graph.version=610;
                graph.edges[1].active=false;
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                graph.edges[1].active=true;
                graph.edges[0].gain=.42f;
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                graph.edges[0].retiring=true;
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                graph.edges[0].retiring=false;
                graph.edges[0].tapPoint=DAW::TapPoint::PostFX;
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                graph.edges[0].tapPoint=DAW::TapPoint::PreFX;
                graph.version=611;
                // A format reprepare must replace histories atomically.
                expect(engine.stageProgrammeGraphPdcPreflightOnControlThread(
                    graph,&delays,2,rate));
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                // A device-rate switch rejects the old DSP bank even when
                // the routing snapshot is otherwise still identical.
                const double nextRate=rate==44100.0?48000.0:44100.0;
                engine.prepare(nextRate,frames);
                expect(!engine.renderOnePreparedGraphEdgeForTests(graph,0,check),
                       "device rate change must reject stale PDC history");
                expect(engine.stageProgrammeGraphPdcPreflightOnControlThread(
                    graph,&delays,2,nextRate));
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,check));
                // A SAME-rate restart is a new signal timeline: the old
                // 16-sample direct-path impulse cannot spill into playback.
                juce::AudioBuffer<float> seed(2,1);
                seed.setSample(0,0,1.f);
                seed.setSample(1,0,-.5f);
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,seed));
                engine.prepare(nextRate,frames);
                juce::AudioBuffer<float> cleared(2,64);
                cleared.clear();
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,cleared));
                for(int ch=0;ch<2;++ch)
                    for(int i=0;i<cleared.getNumSamples();++i)
                        expectEquals(cleared.getSample(ch,i),0.f,
                            "device restart must clear stale programme PDC");
                // The offline-render barrier likewise starts an empty history.
                seed.setSample(0,0,.8f);seed.setSample(1,0,-.4f);
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,seed));
                engine.resetOfflineRenderBuffers(frames);
                cleared.clear();
                expect(engine.renderOnePreparedGraphEdgeForTests(graph,0,cleared));
                for(int ch=0;ch<2;++ch)
                    for(int i=0;i<cleared.getNumSamples();++i)
                        expectEquals(cleared.getSample(ch,i),0.f,
                            "offline export must clear stale programme PDC");
                expect(engine.collectGraphPdcRetiredOnControlThread()>0);
            }
        }

        beginTest("Actual AudioEngine stages immutable programme PDC at control boundary without changing renderer");
        {
            // Compile and instantiate the ACTUAL engine under the APEXTests
            // flag. This merely stages a bank; the normal MasterPdcCore
            // retains sole production routing/audio authority.
            DAW::AudioEngine engine;
            DAW::RoutingSnapshot graph;
            graph.version=1103;
            graph.nodes={
                {"voice","vocal",DAW::RoutingNodeType::Track,true},
                {"aux","aux",DAW::RoutingNodeType::Bus,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            graph.processingOrder={"voice","aux","master"};
            const auto edge=[](const char* id,const char* src,
                               const char* dst,DAW::ConnectionType kind) {
                DAW::RoutingSnapshot::EdgeSnapshot e;
                e.id=id;e.sourceNodeId=src;e.destNodeId=dst;e.type=kind;
                return e;
            };
            graph.edges={
                edge("dry","voice","master",DAW::ConnectionType::Direct),
                edge("send","voice","aux",DAW::ConnectionType::Send),
                edge("wet","aux","master",DAW::ConnectionType::Direct)};
            std::map<DAW::TrackID,int> delays={{"vocal",32},{"aux",16}};
            expect(engine.stageProgrammeGraphPdcPreflightOnControlThread(
                graph,&delays,2,48000.0));
            graph.version=1104;
            graph.edges[1].active=false;
            expect(engine.stageProgrammeGraphPdcPreflightOnControlThread(
                graph,&delays,2,48000.0));
            expectEquals(engine.collectGraphPdcRetiredOnControlThread(),
                         static_cast<std::size_t>(1));
            const auto before=engine.getGraphPdcPreflightCounters();
            expectEquals(before.matchingBlocks,static_cast<std::uint64_t>(0));
            expectEquals(before.rejectedBlocks,static_cast<std::uint64_t>(0));
            graph.processingOrder={"master","voice","aux"};
            expect(!engine.stageProgrammeGraphPdcPreflightOnControlThread(
                graph,&delays,2,48000.0));
        }
#endif

        beginTest("Gain-only QuickSend republish retains PDC generation, latency changes invalidate it");
        {
            using Owner=DAW::PdcResearch::ProgrammeRoutePdcSnapshotHandoffResearchCore;
            DAW::RoutingSnapshot graph;
            graph.version=901;
            graph.nodes={
                {"voice","voice",DAW::RoutingNodeType::Track,true},
                {"bus","bus",DAW::RoutingNodeType::Bus,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            graph.processingOrder={"voice","bus","master"};
            DAW::RoutingSnapshot::EdgeSnapshot dry,send,wet;
            dry.id="direct";dry.sourceNodeId="voice";
            dry.destNodeId="master";dry.type=DAW::ConnectionType::Direct;
            send.id="send";send.sourceNodeId="voice";
            send.destNodeId="bus";send.type=DAW::ConnectionType::Send;
            wet.id="wet";wet.sourceNodeId="bus";
            wet.destNodeId="master";wet.type=DAW::ConnectionType::Direct;
            graph.edges={dry,send,wet};
            std::map<DAW::TrackID,int> latencies={{"voice",32},{"bus",16}};
            Owner owner;
            expect(owner.publishPrepared(graph,&latencies,2,48000.0));
            expectEquals(owner.latestPublicationOnControlThread(),
                         static_cast<std::uint64_t>(1));
            graph.edges[1].gain=.25f;
            // RoutingGraph republishes gain without increasing version.
            expect(owner.publishPrepared(graph,&latencies,2,48000.0));
            expectEquals(owner.latestPublicationOnControlThread(),
                         static_cast<std::uint64_t>(1));
            // Leave a nonzero impulse in the previously published ring.
            // A changed absolute source timing must not replay it later.
            {
                auto original=owner.readRealtime();
                float oldL[1]={.6f},oldR[1]={-.3f};
                float* oldStereo[]={oldL,oldR};
                expect(original->dispatch.processEdge(
                    graph.version,0,oldStereo,2,1));
                expectEquals(oldL[0],0.0f);
                expectEquals(oldR[0],0.0f);
            }
            // A new insertion latency changes absolute source timing even
            // when direct-route PDC compensation is still exactly 16.
            latencies["voice"]=48;
            expect(owner.publishPrepared(graph,&latencies,2,48000.0));
            expectEquals(owner.latestPublicationOnControlThread(),
                         static_cast<std::uint64_t>(2));
            {
                auto prepared=owner.readRealtime();
                expectEquals(prepared->dispatch.edgeDelay(0),16);
                float l[32]{},r[32]{};
                float* pair[]={l,r};
                expect(prepared->dispatch.processEdge(
                    graph.version,0,pair,2,32));
                for(int n=0;n<32;++n)
                    expect(l[n]==0.0f && r[n]==0.0f);
            }
            expectEquals(owner.collectRetiredOnControlThread(),
                         static_cast<std::size_t>(1));
            graph.edges[1].retiring=true;
            auto newest=owner.readRealtime();
            expect(!Owner::sameProgrammeRoutingRealtime(
                graph,newest->graph));
        }

        beginTest("Control-published RoutingSnapshot and PDC bank remain coherent across concurrent generations");
        {
            using Owner=DAW::PdcResearch::ProgrammeRoutePdcSnapshotHandoffResearchCore;
            DAW::RoutingSnapshot g;
            g.version=500;
            g.nodes={
                {"vocal-node","vocal",DAW::RoutingNodeType::Track,true},
                {"aux-node","aux",DAW::RoutingNodeType::Bus,true},
                {"master","master",DAW::RoutingNodeType::Master,true}};
            g.processingOrder={"vocal-node","aux-node","master"};
            const auto edge=[](const char* id,const char* src,const char* dst,
                               DAW::ConnectionType type) {
                DAW::RoutingSnapshot::EdgeSnapshot e;
                e.id=id;e.sourceNodeId=src;e.destNodeId=dst;e.type=type;
                return e;
            };
            g.edges={
                edge("dry","vocal-node","master",DAW::ConnectionType::Direct),
                edge("to-aux","vocal-node","aux-node",DAW::ConnectionType::Send),
                edge("wet","aux-node","master",DAW::ConnectionType::Direct)};
            std::map<DAW::TrackID,int> latencies={
                {"vocal",32},{"aux",16}};
            Owner owner;
            expect(owner.publishPrepared(g,&latencies,2));
            expectEquals(owner.latestPublicationOnControlThread(),
                         static_cast<std::uint64_t>(1));
            {
                auto old=owner.readRealtime();
                expect(static_cast<bool>(old));
                expectEquals(old->graph.version,static_cast<std::uint64_t>(500));
                expectEquals(old->dispatch.edgeDelay(0),16);
                // Commit a new routing generation while the prior audio
                // reader is STILL using its original snapshot and bank.
                g.edges[1].active=false;
                g.version=501;
                expect(owner.publishPrepared(g,&latencies,2));
                expectEquals(owner.latestPublicationOnControlThread(),
                             static_cast<std::uint64_t>(2));
                expectEquals(owner.retainedGenerationsOnControlThread(),
                             static_cast<std::size_t>(1));
                expectEquals(owner.collectRetiredOnControlThread(),
                             static_cast<std::size_t>(0));
                expect(!owner.resetPreparedHistoriesAfterReadersDrainOnControlThread(),
                       "never reset a delay ring while an RT reader is alive");
                {
                    auto fresh=owner.readRealtime();
                    expect(static_cast<bool>(fresh));
                    expectEquals(fresh->graph.version,
                                 static_cast<std::uint64_t>(501));
                    expectEquals(fresh->dispatch.edgeDelay(0),0);
                    expectEquals(fresh->dispatch.edgeDelay(1),-1);
                    float l[64]{},r[64]{};
                    l[0]=.4f;r[0]=-.2f;
                    float* data[]={l,r};
                    expect(fresh->dispatch.processEdge(
                        fresh->graph.version,0,data,2,64));
                    expectWithinAbsoluteError(static_cast<double>(l[0]),
                                              .4,1.e-6);
                    expectWithinAbsoluteError(static_cast<double>(r[0]),
                                              -.2,1.e-6);
                    expect(!fresh->dispatch.processEdge(
                        old->graph.version,0,data,2,64));
                }
                float l[64]{},r[64]{};
                l[0]=.35f;r[0]=-.15f;
                float* data[]={l,r};
                expect(old->dispatch.processEdge(
                    old->graph.version,0,data,2,64));
                expectWithinAbsoluteError(static_cast<double>(l[16]),
                                          .35,1.e-6);
                expectWithinAbsoluteError(static_cast<double>(r[16]),
                                          -.15,1.e-6);
                expectEquals(old->graph.version,static_cast<std::uint64_t>(500));
                expectEquals(old->dispatch.edgeDelay(0),16);
            }
            expectEquals(owner.collectRetiredOnControlThread(),
                         static_cast<std::size_t>(1));
            expect(owner.resetPreparedHistoriesAfterReadersDrainOnControlThread(),
                   "after RT quiescence a timeline reset is safe");
            g.processingOrder={"master","vocal-node","aux-node"};
            expect(!owner.publishPrepared(g,&latencies,2));
            expectEquals(owner.latestPublicationOnControlThread(),
                         static_cast<std::uint64_t>(2));
            {
                auto unchanged=owner.readRealtime();
                expectEquals(unchanged->graph.version,
                             static_cast<std::uint64_t>(501));
                expectEquals(unchanged->dispatch.edgeDelay(0),0);
            }
        }

        beginTest("Stable route survives graph publication with uninterrupted 16-sample PDC history");
        for(const int frames:{31,64}) {
            for(const double rate:{44100.0,48000.0}) {
                using Owner=DAW::PdcResearch::ProgrammeRoutePdcSnapshotHandoffResearchCore;
                DAW::RoutingSnapshot graph;
                graph.version=701;
                graph.nodes={
                    {"vocal","track-vocal",DAW::RoutingNodeType::Track,true},
                    {"aux","track-aux",DAW::RoutingNodeType::Bus,true},
                    {"master","master",DAW::RoutingNodeType::Master,true}};
                graph.processingOrder={"vocal","aux","master"};
                DAW::RoutingSnapshot::EdgeSnapshot dry,send,wet;
                dry.id="stable-dry";dry.sourceNodeId="vocal";
                dry.destNodeId="master";dry.type=DAW::ConnectionType::Direct;
                send.id="stable-send";send.sourceNodeId="vocal";
                send.destNodeId="aux";send.type=DAW::ConnectionType::Send;
                wet.id="stable-wet";wet.sourceNodeId="aux";
                wet.destNodeId="master";wet.type=DAW::ConnectionType::Direct;
                graph.edges={dry,send,wet};
                std::map<DAW::TrackID,int> delays={
                    {"track-vocal",32},{"track-aux",16}};
                Owner owner;
                expect(owner.publishPrepared(graph,&delays,2,rate));
                auto run=[&](int first,int count,bool expectContinuous) {
                    auto scope=owner.readRealtime();
                    expect(static_cast<bool>(scope));
                    expectEquals(scope->dispatch.edgeDelay(0),16);
                    juce::AudioBuffer<float> stereo(2,count);
                    for(int i=0;i<count;++i) {
                        stereo.setSample(0,i,static_cast<float>((first+i+1)*.001));
                        stereo.setSample(1,i,static_cast<float>(-(first+i+1)*.002));
                    }
                    expect(scope->dispatch.processEdge(
                        scope->graph.version,0,stereo.getArrayOfWritePointers(),2,count));
                    for(int i=0;i<count;++i) {
                        const int source=first+i-16;
                        const double left=source<0||(!expectContinuous&&i<16)
                            ?0.0:(source+1)*.001;
                        const double right=source<0||(!expectContinuous&&i<16)
                            ?0.0:-(source+1)*.002;
                        expectWithinAbsoluteError(
                            static_cast<double>(stereo.getSample(0,i)),left,1.e-5);
                        expectWithinAbsoluteError(
                            static_cast<double>(stereo.getSample(1,i)),right,1.e-5);
                    }
                };
                run(0,frames,true);
                // Unchanged route identity + delay MUST reuse the exact ring
                // history instead of inserting 16 new silent samples.
                graph.version=702;
                expect(owner.publishPrepared(graph,&delays,2,rate));
                run(frames,frames,true);
                // Changing the TAP identity on a route forces a fresh bank
                // for this edge even though its ID and delay are the same.
                // This intentionally gates old, semantically unrelated audio.
                graph.edges[0].tapPoint=DAW::TapPoint::PostFX;
                graph.version=703;
                expect(owner.publishPrepared(graph,&delays,2,rate));
                run(2*frames,frames,false);
                // Publishing generation 703 reclaimed generation 701
                // before retiring 702 because no readers were active.
                // Exactly ONE retired generation remains, not two.
                expectEquals(owner.collectRetiredOnControlThread(),
                             static_cast<std::size_t>(1));
                // >64 control-plane edits WITHOUT an audio reader must not
                // consume the bounded RCU-retirement quota. Regression for
                // accidentally retaining a control-local Reader while
                // calling publishPrepared().
                for(std::uint64_t generation=704;generation<=800;++generation) {
                    graph.version=generation;
                    expect(owner.publishPrepared(graph,&delays,2,rate));
                }
                expectEquals(owner.latestPublicationOnControlThread(),
                             static_cast<std::uint64_t>(100));
                expectEquals(owner.retainedGenerationsOnControlThread(),
                             static_cast<std::size_t>(1));
                expectEquals(owner.collectRetiredOnControlThread(),
                             static_cast<std::size_t>(1));
                // Format changes invalidate old sample history even when
                // the same route ID, tap and integer delay remain stable.
                graph.version=801;
                const double changedRate=rate==44100.0?48000.0:44100.0;
                expect(owner.publishPrepared(graph,&delays,2,changedRate));
                run(3*frames,frames,false);
                expect(!owner.publishPrepared(graph,&delays,2,0.0));
                expect(!owner.publishPrepared(graph,&delays,2,-44100.0));
                expect(!owner.publishPrepared(graph,&delays,2,500000.0));
                expectEquals(owner.latestPublicationOnControlThread(),
                             static_cast<std::uint64_t>(101));
                {
                    auto current=owner.readRealtime();
                    expectEquals(current->sampleRate,changedRate);
                    expectEquals(current->graph.version,
                                 static_cast<std::uint64_t>(801));
                }
            }
        }

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
