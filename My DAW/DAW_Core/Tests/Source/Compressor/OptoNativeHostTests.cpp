#include <JuceHeader.h>
#include "../../../Source/PluginHostCore/ApexNativePluginFormat.h"
#include "../../../Source/CompressorCore/ApexOptoLevelerProcessor.h"
#include <cmath>

class OptoNativeHostTests final : public juce::UnitTest
{
public:
    OptoNativeHostTests() : UnitTest("Opto2A.Host", "APEX.Compressor") {}

    void runTest() override
    {
        using APEX::Compressor::OptoProcessor;
        using APEX::Compressor::ParameterCount;

        beginTest("Shared APEX Native format includes four distinct plugin identities");
        DAW::ApexNativePluginFormat format;
        juce::OwnedArray<juce::PluginDescription> catalog;
        format.findAllTypesForFile(catalog,{});
        expect(catalog.size() >= 4, "G10/C4/Parametric EQ/Opto coexist");
        juce::PluginDescription opto;
        bool found=false;
        for(auto* desc:catalog)
            if(desc->fileOrIdentifier == OptoProcessor::kFileOrIdentifier)
            {
                opto=*desc;
                found=true;
            }
        expect(found, "APEX Opto 2A discoverable via unified plugin format");
        if(!found) return;
        expectEquals(opto.uniqueId,OptoProcessor::kUniqueId);
        expect(format.doesPluginStillExist(opto));
        expect(format.fileMightContainThisPluginType(opto.fileOrIdentifier));
        expect(!format.fileMightContainThisPluginType("Fake::Plugin"));

        beginTest("JUCE AudioPluginFormatManager synchronously instantiates plugin");
        juce::AudioPluginFormatManager manager;
        manager.addFormat(std::make_unique<DAW::ApexNativePluginFormat>());
        juce::String error;
        auto base=manager.createPluginInstance(opto,48000.0,128,error);
        expect(base!=nullptr,error);
        if(!base) return;
        auto* processor=dynamic_cast<OptoProcessor*>(base.get());
        expect(processor!=nullptr);
        if(!processor) return;
        expectEquals(base->getSampleRate(),48000.0);
        expectEquals(base->getBlockSize(),128);
        expect(base->getBypassParameter()!=nullptr);
        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        juce::AudioProcessor::BusesLayout stereoLayout;
        stereoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
        stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());
        expect(base->isBusesLayoutSupported(monoLayout));
        expect(base->isBusesLayoutSupported(stereoLayout));
        expectEquals(base->getParameters().size(),(int)ParameterCount);

        beginTest("Stable parameter IDs for APEX automation and dry/wet contract");
        for (int i=0;i<ParameterCount;++i)
        {
            auto* named=dynamic_cast<juce::AudioProcessorParameterWithID*>(
                base->getParameters()[i]);
            expect(named != nullptr, "all eight params support stable host IDs");
        }
        // Force dry at first preparation, so initial state is exact.
        processor->getParameterAt(APEX::Compressor::Mix).setValue(0.0f);
        processor->getParameterAt(APEX::Compressor::PeakReduction).setValue(1.0f);
        base->prepareToPlay(48000.0,128);
        juce::AudioBuffer<float> stereo(2,128);
        stereo.clear();
        for (int sample=0;sample<128;++sample)
        {
            stereo.setSample(0,sample,0.67f);
            stereo.setSample(1,sample,-0.24f);
        }
        juce::MidiBuffer midi;
        base->processBlock(stereo,midi);
        for (int sample=0;sample<128;++sample)
        {
            expectWithinAbsoluteError((double)stereo.getSample(0,sample),0.67,1.e-5);
            expectWithinAbsoluteError((double)stereo.getSample(1,sample),-0.24,1.e-5);
        }
        base->releaseResources();

        beginTest("Session state roundtrip preserves normalized parameter values");
        processor->getParameterAt(APEX::Compressor::Mix).setValue(0.32f);
        processor->getParameterAt(APEX::Compressor::Gain).setValue(0.25f);
        processor->getParameterAt(APEX::Compressor::Emphasis).setValue(0.81f);
        processor->getParameterAt(APEX::Compressor::Limit).setValue(1.0f);
        juce::MemoryBlock state;
        base->getStateInformation(state);
        expect(state.getSize()>0);
        OptoProcessor restored;
        restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        expectWithinAbsoluteError((double)restored.getParameterAt(APEX::Compressor::Mix).getValue(),
                                  0.32,1.e-5);
        expectWithinAbsoluteError((double)restored.getParameterAt(APEX::Compressor::Gain).getValue(),
                                  0.25,1.e-5);
        expectWithinAbsoluteError((double)restored.getParameterAt(APEX::Compressor::Emphasis).getValue(),
                                  0.81,1.e-5);
        expect(restored.getParameterAt(APEX::Compressor::Limit).getUnits()>0.5f);

        beginTest("Stereo key link lowers both channels equally, no dry-source printing");
        OptoProcessor comp;
        comp.getParameterAt(APEX::Compressor::PeakReduction).setValue(0.8f);
        comp.getParameterAt(APEX::Compressor::ColorEnabled).setValue(0.0f);
        comp.getParameterAt(APEX::Compressor::Gain).setValue(0.0f);
        comp.prepareToPlay(48000,128);
        juce::AudioBuffer<float> signal(2,128);
        for (int block=0;block<200;++block)
        {
            for (int n=0;n<128;++n)
            {
                signal.setSample(0,n,0.75f);
                signal.setSample(1,n,0.25f);
            }
            comp.processBlock(signal,midi);
        }
        expect(comp.getGainReductionMeterDb()>1.0f);
        expect(signal.getSample(0,127)<0.75f);
        expectWithinAbsoluteError(
            (double)signal.getSample(0,127)/0.75,
            (double)signal.getSample(1,127)/0.25, 1.e-5);
        comp.releaseResources();
    }
};
static OptoNativeHostTests optoNativeHostTests;
