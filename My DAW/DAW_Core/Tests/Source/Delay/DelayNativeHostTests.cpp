// APEX Bubblegum Delay: host ABI, identity, session/preset, engine integration,
// editor lifecycle and stereo/mono processing contracts. No external Waves code.
#include <JuceHeader.h>
#include "../../../Source/DelayCore/ApexBubblegumDelayProcessor.h"
#include "../../../Source/PluginHostCore/ApexNativePluginFormat.h"
#include "../../../Source/PluginScanCore/PluginScanFormatsCore.h"

#include <cmath>
#include <memory>

namespace
{
class DelayNativeHostTests final : public juce::UnitTest
{
public:
    DelayNativeHostTests() : juce::UnitTest("BubblegumDelay.NativeHost",
                                         "APEX.Delay") {}
    void runTest() override
    {
        using namespace APEX::Delay;
        beginTest("One APEX Native format; unfinished C4 hidden, legacy recall intact");
        DAW::PluginScanFormatsCore registered;
        int nativeCount=0;
        for(const auto& name:registered.getRegisteredFormatNames())
            if(name=="APEX Native") ++nativeCount;
        expectEquals(nativeCount,1);
        DAW::ApexNativePluginFormat native;
        juce::OwnedArray<juce::PluginDescription> results;
        native.findAllTypesForFile(results,{});
        expectEquals(results.size(),5,"G10, Parametric EQ, Opto, Bubblegum Delay and Reverie visible");
        for(const auto* item:results)
            expect(item->fileOrIdentifier!="APEX::C4","unfinished C4 is not advertised");
        juce::PluginDescription description;
        int matches=0;
        for(const auto* item:results)
            if(item->fileOrIdentifier==Processor::kFileOrIdentifier)
            {
                description=*item;
                ++matches;
            }
        expectEquals(matches,1,"Bubblegum Delay has one stable plugin identity");
        if(matches!=1)return;
        expectEquals(description.uniqueId,Processor::kUniqueId);
        expectEquals(description.name,juce::String(Processor::kPluginName));
        expect(native.doesPluginStillExist(description));
        expect(native.fileMightContainThisPluginType(description.fileOrIdentifier));
        expect(!native.fileMightContainThisPluginType("APEX::NotAPlugin"));

        beginTest("Plugin manager instantiates via unified native format");
        juce::AudioPluginFormatManager manager;
        manager.addFormat(std::make_unique<DAW::ApexNativePluginFormat>());
        juce::String error;
        auto instance=manager.createPluginInstance(description,48000,128,error);
        expect(instance!=nullptr,error);
        if(!instance)return;
        auto* processor=dynamic_cast<Processor*>(instance.get());
        expect(processor!=nullptr);
        if(!processor)return;
        expectEquals(instance->getNumParameters(),static_cast<int>(ParameterCount));
        expect(instance->getBypassParameter()!=nullptr);
        juce::StringArray ids;
        for(auto* p:instance->getParameters())
        {
            auto* typed=dynamic_cast<juce::AudioProcessorParameterWithID*>(p);
            expect(typed!=nullptr,"every knob must have a stable host automation ID");
            if(typed)ids.addIfNotAlreadyThere(typed->paramID);
        }
        expectEquals(static_cast<int>(ids.size()),static_cast<int>(ParameterCount),
                     "no duplicate parameter IDs");
        juce::AudioProcessor::BusesLayout mono,stereo;
        mono.inputBuses.add(juce::AudioChannelSet::mono());
        mono.outputBuses.add(juce::AudioChannelSet::mono());
        stereo.inputBuses.add(juce::AudioChannelSet::stereo());
        stereo.outputBuses.add(juce::AudioChannelSet::stereo());
        expect(processor->isBusesLayoutSupported(mono));
        expect(processor->isBusesLayoutSupported(stereo));

        beginTest("Parameter restore preserves stable IDs and exact values");
        processor->getParameterAt(SyncMode).setValue(0);
        processor->getParameterAt(TimeMs).setValue(
            processor->getParameterAt(TimeMs).toNormalised(250.0f));
        processor->getParameterAt(Feedback).setValue(
            processor->getParameterAt(Feedback).toNormalised(65.0f));
        processor->getParameterAt(Mix).setValue(
            processor->getParameterAt(Mix).toNormalised(42.0f));
        processor->getParameterAt(PingPong).setValue(1);
        juce::MemoryBlock state;
        processor->getStateInformation(state);
        expect(state.getSize()>0);
        Processor restored;
        restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        for(int i=0;i<ParameterCount;++i)
            expectWithinAbsoluteError(
                static_cast<double>(restored.getParameterAt(i).getValue()),
                static_cast<double>(processor->getParameterAt(i).getValue()),1.0e-6,
                "state round-trip parameter "+juce::String(i));
        // Corrupt state MUST fail closed rather than reconfigure random controls.
        const float feedbackBefore=restored.getParameterAt(Feedback).getValue();
        constexpr unsigned char garbage[]={0xff,0x00,0x05,0xee,0x03};
        restored.setStateInformation(garbage,static_cast<int>(sizeof(garbage)));
        expectWithinAbsoluteError(
            static_cast<double>(restored.getParameterAt(Feedback).getValue()),
            static_cast<double>(feedbackBefore),1e-6);

        beginTest("Host processing: 250 ms impulse, left/right preservation");
        // The separate ping-pong test covers its alternating routing; this
        // test asserts independent L/R wet routing and first-echo polarity.
        restored.getParameterAt(PingPong).setValue(0.0f);
        restored.prepareToPlay(48000,128);
        expectEquals(restored.getLatencySamples(),0);
        constexpr int frames=13000;
        juce::AudioBuffer<float> buffer(2,128);
        juce::MidiBuffer midi;
        float echoL=0,echoR=0,zeroL=0;
        for(int start=0;start<frames;start+=128)
        {
            const int count=juce::jmin(128,frames-start);
            buffer.clear();
            if(start==0)
            {
                buffer.setSample(0,0,1.0f);
                buffer.setSample(1,0,-0.5f);
            }
            // Process only first 'count' frames. Complete blocks were chosen
            // above; final partial buffer is handled with a separate view.
            if(count==128) restored.processBlock(buffer,midi);
            else
            {
                juce::AudioBuffer<float> tail(2,count);
                tail.clear();
                restored.processBlock(tail,midi);
                continue;
            }
            for(int i=0;i<count;++i)
            {
                if(start+i==0) zeroL=buffer.getSample(0,i);
                if(start+i==12000)
                {
                    echoL=buffer.getSample(0,i);
                    echoR=buffer.getSample(1,i);
                }
            }
        }
        // Input wet=42%, dry=58%; positive left and negative right first echoes.
        expectWithinAbsoluteError(static_cast<double>(zeroL),0.58,0.005);
        expect(echoL>0.20f,"first 250ms echo must reach output");
        expect(echoR<0.0f,"stereo input preserves channel polarity");
        restored.releaseResources();

        beginTest("Editor is genuine, resizable, and retains interactive values");
        auto editor=std::unique_ptr<juce::AudioProcessorEditor>(
            processor->createEditor());
        expect(editor!=nullptr);
        if(editor)
        {
            expect(editor->getWidth()>=760);
            expect(editor->getHeight()>=400);
            expect(editor->getNumChildComponents()>=15,
                   "multi-control touch-safe editor with readout");
            editor->setSize(900,450);
            expectEquals(editor->getWidth(),900);
            juce::Image screenshot(juce::Image::ARGB,900,450,true,
                                    juce::SoftwareImageType{});
            juce::Graphics graphics(screenshot);
            {
                juce::Graphics::ScopedSaveState save(graphics);
                editor->paint(graphics);
            }
            expect(screenshot.getPixelAt(20,20).getAlpha()>0,
                   "editor paints an opaque surface on CPU");
            expect(screenshot.getPixelAt(100,60).getBrightness()>0.01f,
                   "APEX delay chassis must not be fully black");
        }
    }
};
static DelayNativeHostTests delayNativeHostTests;
} // namespace
