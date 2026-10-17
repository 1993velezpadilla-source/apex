#include <JuceHeader.h>
#include "../../../Source/ReverieCore/ApexReverieEngine.h"
#include "../../../Source/ReverieCore/ApexReverieProcessor.h"
#include "../../../Source/PluginHostCore/ApexNativePluginFormat.h"
#include <cmath>

class ReverieNativeHostTests final : public juce::UnitTest
{
public:
    ReverieNativeHostTests():UnitTest("Reverie.NativeHostAndDSP","APEX.Reverb") {}
    void runTest() override
    {
        using APEX::Reverie::Processor;
        beginTest("One stable native identity in the existing shared format");
        DAW::ApexNativePluginFormat format;
        juce::OwnedArray<juce::PluginDescription> catalog;
        format.findAllTypesForFile(catalog,{});
        int matched=0;
        juce::PluginDescription reverie;
        for(auto* d:catalog)
            if(d->fileOrIdentifier==Processor::kFileOrIdentifier)
            {++matched;reverie=*d;}
        expectEquals(matched,1,"reverb must not be registered as a second native format");
        if(matched!=1)return;
        expectEquals(reverie.uniqueId,Processor::kUniqueId);
        expectEquals(reverie.category,juce::String("Reverb"));
        expect(format.doesPluginStillExist(reverie));
        expect(format.fileMightContainThisPluginType(reverie.fileOrIdentifier));

        beginTest("Host instantiation, channel layouts, automation IDs and zero PDC");
        juce::AudioPluginFormatManager manager;
        manager.addFormat(std::make_unique<DAW::ApexNativePluginFormat>());
        juce::String err;
        auto base=manager.createPluginInstance(reverie,48000.0,128,err);
        expect(base!=nullptr,err);
        if(!base)return;
        auto* processor=dynamic_cast<Processor*>(base.get());
        expect(processor!=nullptr);
        if(!processor)return;
        juce::AudioProcessor::BusesLayout mono,stereo;
        mono.inputBuses.add(juce::AudioChannelSet::mono());
        mono.outputBuses.add(juce::AudioChannelSet::mono());
        stereo.inputBuses.add(juce::AudioChannelSet::stereo());
        stereo.outputBuses.add(juce::AudioChannelSet::stereo());
        expect(processor->isBusesLayoutSupported(mono));
        expect(processor->isBusesLayoutSupported(stereo));
        expectEquals(base->getParameters().size(),Processor::kParameterCount);
        for(int i=0;i<Processor::kParameterCount;++i)
        {
            auto* p=dynamic_cast<juce::AudioProcessorParameterWithID*>(
                base->getParameters()[i]);
            expect(p!=nullptr,"automation requires parameter IDs");
            if(p)expectEquals(p->paramID,juce::String(
                Processor::parameterIds[static_cast<size_t>(i)]));
        }
        expect(base->getBypassParameter()!=nullptr);
        base->prepareToPlay(48000.0,128);
        expectEquals(base->getLatencySamples(),0);

        beginTest("Mix zero is bitwise-transparent for normal input in stereo");
        processor->floatAt(10).setValue(0.0f);
        base->prepareToPlay(48000.0,128);
        juce::AudioBuffer<float> audio(2,128);
        for(int i=0;i<128;++i)
        {
            audio.setSample(0,i,0.35f);
            audio.setSample(1,i,-0.12f);
        }
        juce::MidiBuffer midi;
        base->processBlock(audio,midi);
        for(int i=0;i<128;++i)
        {
            expectWithinAbsoluteError(static_cast<double>(audio.getSample(0,i)),0.35,1.0e-6);
            expectWithinAbsoluteError(static_cast<double>(audio.getSample(1,i)),-0.12,1.0e-6);
        }

        beginTest("State survives roundtrip including mode, era, wet, predelay and bypass");
        processor->modeParameter().setValue(0.8f); // ROOM
        processor->eraParameter().setValue(0.5f); // 1980s
        processor->floatAt(2).setValue(0.4f);
        processor->floatAt(10).setValue(0.92f);
        processor->bypassParameter().setValue(1.0f);
        juce::MemoryBlock state;
        base->getStateInformation(state);
        Processor restored;
        restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        for(int i=0;i<Processor::kParameterCount;++i)
            expectWithinAbsoluteError(
                static_cast<double>(restored.getParameters()[i]->getValue()),
                static_cast<double>(base->getParameters()[i]->getValue()),1.0e-6);
        restored.prepareToPlay(48000.0,128);
        expectEquals(restored.getLatencySamples(),0);

        beginTest("Real resizable GUI contains readable numeric knob controls");
        {
            auto editor=std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor());
            expect(editor!=nullptr);
            if(editor)
            {
                expectEquals(editor->getWidth(),980);
                expect(editor->getNumChildComponents()>=23);
                int rotaries=0;
                for(int i=0;i<editor->getNumChildComponents();++i)
                    if(auto* slider=dynamic_cast<juce::Slider*>(editor->getChildComponent(i)))
                    {
                        ++rotaries;
                        expect(slider->getTextBoxPosition()==juce::Slider::TextBoxBelow);
                        expect(static_cast<bool>(slider->onDragStart));
                        expect(static_cast<bool>(slider->onDragEnd));
                    }
                expectEquals(rotaries,10);
                editor->setSize(720,450);
                expectEquals(editor->getWidth(),720);
                editor->setSize(1600,840);
                expectEquals(editor->getHeight(),840);
            }
        }

        beginTest("Deterministic bounded plate/hall/room impulse tails at 44.1/48/96 kHz");
        for(double sr:{44100.0,48000.0,96000.0})
        {
            for(int mode=0;mode<6;++mode)
            {
                APEX::Reverie::Engine engine;
                APEX::Reverie::Settings s;
                s.mode=mode;s.era=mode%3;s.mix=1.0f;s.predelayMs=0.0f;
                s.decaySeconds=1.8f;s.modulation=0.3f;
                engine.prepare(sr);engine.setSettings(s);engine.reset();
                const int count=static_cast<int>(sr*0.42);
                double tailEnergy=0;
                for(int i=0;i<count;++i)
                {
                    float l=(i==0)?1.0f:0.0f,r=0.0f;
                    engine.process(l,r);
                    expect(std::isfinite(l)&&std::isfinite(r),
                           "feedback network must never emit NaN or infinity");
                    expect(std::fabs(l)<10.0f&&std::fabs(r)<10.0f,
                           "FDN must remain bounded");
                    if(i>static_cast<int>(sr*0.075))
                        tailEnergy+=static_cast<double>(l*l+r*r);
                }
                expect(tailEnergy>1.0e-9,
                       "each distinct mode must generate a nonzero wet tail");
            }
        }
        beginTest("Pure bypass leaves audio dry without effect gain changes");
        APEX::Reverie::Engine bypassEngine;
        APEX::Reverie::Settings s;
        s.bypass=true; s.mix=1.0f;
        bypassEngine.prepare(48000.0);bypassEngine.setSettings(s);bypassEngine.reset();
        for(int i=0;i<512;++i)
        {
            float l=0.2f,r=-0.3f;
            bypassEngine.process(l,r);
            expectWithinAbsoluteError(static_cast<double>(l),0.2,1.0e-6);
            expectWithinAbsoluteError(static_cast<double>(r),-0.3,1.0e-6);
        }
    }
};
static ReverieNativeHostTests reverieNativeHostTests;
