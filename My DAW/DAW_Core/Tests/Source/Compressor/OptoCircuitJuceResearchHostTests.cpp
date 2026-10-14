// APEX Opto 2A — JUCE host-boundary research tests ONLY.
// Intentionally not registered with the APEX Native plugin format.
// In particular, this is not the PR #103 OptoProcessor or its production DSP.
#include <JuceHeader.h>
#include "../../../Source/CompressorCore/ApexOptoCircuitPathResearchCore.h"
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

        beginTest("Host-style external 50% wet/dry references a delayed dry signal");
        for(int block:{64,128}) {
            OptoCircuitJuceResearchProbe probe(true);
            APEX::Compressor::ApexOptoCircuitResearchParameters p;
            p.peakReduction=55.0;p.mix=1.0;
            probe.setResearchParameters(p);
            probe.prepareToPlay(sr,block);
            TestHostDryDelay hostLeft,hostRight;
            hostLeft.prepare(probe.getLatencySamples());
            hostRight.prepare(probe.getLatencySamples());
            bool nonSilentWet=false;
            for(int start=0;start<1024;start+=block) {
                juce::AudioBuffer<float> buffer(2,block);
                std::array<float,128> inL{},inR{};
                for(int i=0;i<block;++i) {
                    const int n=start+i;
                    inL[static_cast<std::size_t>(i)]=static_cast<float>(
                        .4*std::sin(n*.031)+.04*std::cos(n*.119));
                    inR[static_cast<std::size_t>(i)]=static_cast<float>(
                        .3*std::cos(n*.023));
                    buffer.setSample(0,i,inL[static_cast<std::size_t>(i)]);
                    buffer.setSample(1,i,inR[static_cast<std::size_t>(i)]);
                }
                probe.processBlock(buffer,midi);
                for(int i=0;i<block;++i) {
                    const float wetL=buffer.getSample(0,i);
                    const float wetR=buffer.getSample(1,i);
                    const float delayedL=hostLeft.process(inL[static_cast<std::size_t>(i)]);
                    const float delayedR=hostRight.process(inR[static_cast<std::size_t>(i)]);
                    const float externalMixL=.5f*delayedL+.5f*wetL;
                    const float externalMixR=.5f*delayedR+.5f*wetR;
                    expect(std::isfinite(externalMixL) && std::isfinite(externalMixR));
                    // This is the host compensation contract to enforce in
                    // PluginChainCore BEFORE a real x4 insert is registered.
                    if(start+i<32) {
                        expectWithinAbsoluteError(static_cast<double>(externalMixL),0.0,1.e-7);
                        expectWithinAbsoluteError(static_cast<double>(externalMixR),0.0,1.e-7);
                    }
                    if(start+i>64 && std::abs(wetL)>.001f)nonSilentWet=true;
                }
            }
            expect(nonSilentWet,"test DSP must produce a non-silent wet path");
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
