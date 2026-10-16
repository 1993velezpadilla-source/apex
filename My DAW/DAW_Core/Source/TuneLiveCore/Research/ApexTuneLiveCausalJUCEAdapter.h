#pragma once
// NON-SHIPPING JUCE host adapter. Deliberately not included in any APEX
// native registry, Projucer source group or artist-facing editor.
// Validates the causal harmonic experiment under a real JUCE 8 host.
// Its zero FIXED lookahead PDC does NOT mean a singer is corrected within
// one millisecond: note F0 acquisition / harmonic settling still takes
// approximately tens of milliseconds, plus ASIO/device round-trip.
#include <JuceHeader.h>
#include "ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <cmath>

namespace APEX { namespace TuneLive { namespace Research {

class CausalJUCEAdapter final : public juce::AudioPluginInstance {
public:
    CausalJUCEAdapter()
      :juce::AudioPluginInstance(
          juce::AudioProcessor::BusesProperties()
              .withInput("Input",juce::AudioChannelSet::stereo(),true)
              .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}

    const juce::String getName() const override {
        return "APEX Tune Live Causal Research ONLY";
    }
    void fillInPluginDescription(juce::PluginDescription& d) const override {
        d.name=d.descriptiveName=getName();
        d.pluginFormatName="APEX Research (not registered)";
        d.manufacturerName="APEX";
        d.category="Pitch Shift";
        d.version="0.0.0-research";
        d.fileOrIdentifier="APEX::TuneLive::Research::Causal";
        d.uniqueId=d.deprecatedUid=0x41544352; // ATCR
        d.isInstrument=false;
        d.numInputChannels=d.numOutputChannels=2;
    }
    bool isBusesLayoutSupported(const BusesLayout& buses) const override {
        const auto input=buses.getMainInputChannelSet();
        return (input==juce::AudioChannelSet::mono() ||
                input==juce::AudioChannelSet::stereo())
               && input==buses.getMainOutputChannelSet();
    }
    void prepareToPlay(double rate,int maximumBlockSize) override {
        const double stableRate=std::isfinite(rate) && rate>=8000.0?
                                rate:48000.0;
        const int maxBlock=std::max(1,maximumBlockSize);
        setRateAndBufferSizeDetails(stableRate,maxBlock);
        live_.prepare(stableRate,juce::jlimit(1,2,getTotalNumInputChannels()));
        VoiceSettings settings;
        settings.root=9;
        settings.scale=0;
        settings.correction=1.0f;
        settings.referenceHz=440.0f;
        settings.humanize=0.0f;
        settings.flexTune=0.0f;
        settings.noteHysteresis=0.1f;
        live_.setVoiceSettings(settings);
        live_.enableEarlyOnset(true);
        live_.enableHighRegisterOnset(true);
        live_.enableFastEngagement(true);
        live_.enableExperimentalEarlyTransitions(true);
        live_.enableResidualPreservation(true);
        // No fixed delay/lookahead in this dry plus harmonic-residual
        // resynthesis candidate. Does NOT verify acoustic voice response.
        setLatencySamples(0);
        ready_=true;
    }
    void releaseResources() override {
        ready_=false;
        live_.reset();
    }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override {
        if(!ready_)return;
        live_.process(buffer.getArrayOfWritePointers(),
                      buffer.getNumChannels(),buffer.getNumSamples());
    }
    double researchWetMix() const noexcept {return live_.researchWetMix();}
    int64_t firstVoiceSample() const noexcept {return live_.firstVoiceSample();}
    int64_t firstHighSample() const noexcept {return live_.firstHighSample();}
    int fastTransitionCount() const noexcept {return live_.fastTransitionCount();}
    int64_t lastFastTransitionSample() const noexcept {
        return live_.lastFastTransitionSample();
    }
    int64_t firstHalfWetSample() const noexcept {return live_.firstHalfWetSample();}
    bool hasEditor() const override {return false;}
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 0.0;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Research"; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& state) override {state.reset();}
    void setStateInformation(const void*,int) override {}
private:
    HarmonicLivePrototype live_;
    bool ready_=false;
};

}}} // namespace APEX::TuneLive::Research
