#pragma once
// APEX Reverie native hosted processor; stable session/automation identity.
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <memory>
#include "ApexReverieEngine.h"

namespace APEX::Reverie
{
class Processor final : public juce::AudioPluginInstance
{
public:
    static constexpr int kUniqueId=0x52565231; // RVR1
    static constexpr const char* kPluginName="APEX Reverie Reverb";
    static constexpr const char* kFileOrIdentifier="APEX::ReverieReverb";
    static constexpr const char* kStateName="apexreveriereverbstate";
    static constexpr const char* kFormatName="APEX Native";
    static constexpr int kParameterCount=13;
    static constexpr std::array<const char*,kParameterCount> parameterIds={{
        "reverie.mode","reverie.era","reverie.predelay","reverie.decay",
        "reverie.size","reverie.damping","reverie.lowcut","reverie.highcut",
        "reverie.mod","reverie.width","reverie.mix","reverie.duck","reverie.bypass"
    }};

    Processor():juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
            .withInput("Input",juce::AudioChannelSet::stereo(),true)
            .withOutput("Output",juce::AudioChannelSet::stereo(),true))
    {
        mode_=choice(parameterIds[0],"REVERB MODE",
                     {"PURE PLATE","VINTAGE PLATE","HALL","CHAMBER","ROOM","AMBIENT"},0);
        era_=choice(parameterIds[1],"COLOR ERA",{"1970s","1980s","MODERN"},2);
        predelay_=number(parameterIds[2],"PRE-DELAY",0.f,250.f,35.f);
        decay_=number(parameterIds[3],"DECAY",0.2f,18.f,2.4f);
        size_=number(parameterIds[4],"SIZE",0.f,100.f,60.f);
        damping_=number(parameterIds[5],"DAMPING",750.f,20000.f,8500.f);
        lowCut_=number(parameterIds[6],"LOW CUT",20.f,1500.f,180.f);
        highCut_=number(parameterIds[7],"HIGH CUT",1500.f,20000.f,14000.f);
        modulation_=number(parameterIds[8],"MODULATION",0.f,100.f,30.f);
        width_=number(parameterIds[9],"STEREO WIDTH",0.f,200.f,100.f);
        mix_=number(parameterIds[10],"DRY / WET",0.f,100.f,25.f);
        duck_=number(parameterIds[11],"VOCAL DUCKING",0.f,100.f,0.f);
        auto b=std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(parameterIds[12],1),"BYPASS",false);
        bypass_=b.get(); addHostedParameter(std::move(b));
        setLatencySamples(0);
    }

    const juce::String getName() const override { return kPluginName; }
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name=kPluginName; d.descriptiveName=kPluginName;
        d.pluginFormatName=kFormatName; d.category="Reverb"; d.manufacturerName="APEX";
        d.version="0.1.0"; d.fileOrIdentifier=kFileOrIdentifier;
        d.uniqueId=kUniqueId; d.deprecatedUid=kUniqueId;
        d.isInstrument=false; d.numInputChannels=2; d.numOutputChannels=2;
    }
    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        const auto input=l.getMainInputChannelSet();
        return input==l.getMainOutputChannelSet() &&
            (input==juce::AudioChannelSet::mono() ||
             input==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double sampleRate,int blockSize) override
    {
        setRateAndBufferSizeDetails(sampleRate,blockSize);
        engine_.prepare(sampleRate);
        engine_.setSettings(settings());
        setLatencySamples(0);
    }
    void releaseResources() override {}
    void reset() override { engine_.reset(); }
    void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int channels=audio.getNumChannels();
        if(channels<1) return;
        engine_.setSettings(settings());
        float* left=audio.getWritePointer(0);
        float* right=channels>1?audio.getWritePointer(1):nullptr;
        for(int i=0;i<audio.getNumSamples();++i)
        {
            float l=left[i],r=right?right[i]:left[i];
            engine_.process(l,r);
            left[i]=right?l:0.5f*(l+r);
            if(right) right[i]=r;
        }
    }
    double getTailLengthSeconds() const override
    {
        return static_cast<double>(decay_->get())+predelay_->get()*0.001+0.5;
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass_; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}

    // Human-unit values for the custom editor, without attachment allocations.
    juce::AudioParameterFloat& floatAt(int index) const noexcept
    {
        return *static_cast<juce::AudioParameterFloat*>(getParameters()[index]);
    }
    juce::AudioParameterChoice& modeParameter() noexcept { return *mode_; }
    juce::AudioParameterChoice& eraParameter() noexcept { return *era_; }
    juce::AudioParameterBool& bypassParameter() noexcept { return *bypass_; }

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        juce::ValueTree node(kStateName);
        node.setProperty("schema",1,nullptr);
        for(int i=0;i<kParameterCount;++i)
            node.setProperty(juce::Identifier(parameterIds[static_cast<size_t>(i)]),
                getParameters()[i]->getValue(),nullptr);
        juce::MemoryOutputStream out(dest,false);
        node.writeToStream(out);
    }
    void setStateInformation(const void* data,int numBytes) override
    {
        if(data==nullptr || numBytes<=0) return;
        const auto node=juce::ValueTree::readFromData(data,static_cast<size_t>(numBytes));
        if(!node.isValid() || !node.hasType(juce::Identifier(kStateName))) return;
        for(int i=0;i<kParameterCount;++i)
        {
            const auto property=juce::Identifier(parameterIds[static_cast<size_t>(i)]);
            if(!node.hasProperty(property)) continue;
            const float n=static_cast<float>(node.getProperty(property));
            if(std::isfinite(n))
                getParameters()[i]->setValue(std::max(0.0f,std::min(1.0f,n)));
        }
    }

private:
    juce::AudioParameterFloat* number(const char* id,const char* name,
                                     float min,float max,float initial)
    {
        auto p=std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(id,1),name,
            juce::NormalisableRange<float>(min,max),initial);
        auto* result=p.get(); addHostedParameter(std::move(p)); return result;
    }
    juce::AudioParameterChoice* choice(const char* id,const char* name,
                                      const juce::StringArray& names,int initial)
    {
        auto p=std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(id,1),name,names,initial);
        auto* result=p.get(); addHostedParameter(std::move(p)); return result;
    }
    Settings settings() const noexcept
    {
        Settings s;
        s.mode=mode_->getIndex(); s.era=era_->getIndex();
        s.predelayMs=predelay_->get(); s.decaySeconds=decay_->get();
        s.size=size_->get()*0.01f; s.dampingHz=damping_->get();
        s.lowCutHz=lowCut_->get(); s.highCutHz=highCut_->get();
        s.modulation=modulation_->get()*0.01f;
        s.width=width_->get()*0.01f; s.mix=mix_->get()*0.01f;
        s.ducking=duck_->get()*0.01f; s.bypass=bypass_->get();
        return s;
    }
    Engine engine_{};
    juce::AudioParameterChoice *mode_=nullptr,*era_=nullptr;
    juce::AudioParameterFloat *predelay_=nullptr,*decay_=nullptr,*size_=nullptr;
    juce::AudioParameterFloat *damping_=nullptr,*lowCut_=nullptr,*highCut_=nullptr;
    juce::AudioParameterFloat *modulation_=nullptr,*width_=nullptr,*mix_=nullptr,*duck_=nullptr;
    juce::AudioParameterBool* bypass_=nullptr;
};
} // namespace APEX::Reverie
#include "ApexReverieEditor.h"
