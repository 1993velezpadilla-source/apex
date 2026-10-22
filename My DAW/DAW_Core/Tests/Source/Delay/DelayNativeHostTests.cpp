// APEX Bubblegum Delay: host ABI, identity, session/preset, engine integration,
// editor lifecycle and stereo/mono processing contracts. No external Waves code.
#include <JuceHeader.h>
#include "../../../Source/DelayCore/ApexBubblegumDelayProcessor.h"
#include "../../../Source/PluginHostCore/ApexNativePluginFormat.h"
#include "../../../Source/PluginScanCore/PluginScanFormatsCore.h"
#include "../../../Source/PluginHostCore/HostPlayheadCore.h"

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
        expectEquals(results.size(),6,"G10, Parametric EQ, Opto, Limiter, Bubblegum Delay and Reverie visible");
        int limiterMatches=0;
        for(const auto* item:results)
        {
            expect(item->fileOrIdentifier!="APEX::C4","unfinished C4 is not advertised");
            if (item->fileOrIdentifier=="APEX::BubblegumLimiter") ++limiterMatches;
        }
        expectEquals(limiterMatches,1,"new limiter is advertised exactly once");
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

        beginTest("HOST sync follows APEX project BPM, manual and MS modes remain independent");
        {
            DAW::HostPlayheadCore hostClock;
            DAW::PluginTransportSnapshot clockSnapshot;
            clockSnapshot.sampleRate=48000.0;
            clockSnapshot.bpm=100.0;
            hostClock.updateSnapshot(clockSnapshot);
            Processor tempoDelay;
            tempoDelay.setPlayHead(&hostClock);
            tempoDelay.prepareToPlay(48000.0,128);
            juce::AudioBuffer<float> scratch(2,128);
            juce::MidiBuffer empty;
            scratch.clear();
            expect(!tempoDelay.hasHostTempo(),"host BPM must not be reported before audio callback");
            tempoDelay.processBlock(scratch,empty);
            expect(tempoDelay.hasHostTempo(),"APEX HostPlayheadCore supplies real project BPM");
            expectWithinAbsoluteError(tempoDelay.getHostTempoForDisplay(),100.0,0.01);
            expectWithinAbsoluteError(tempoDelay.getEffectiveDelayMsForDisplay(),450.0,0.05,
                                      "factory dotted-eighth at 100 BPM equals 450ms");

            clockSnapshot.bpm=150.0;
            hostClock.updateSnapshot(clockSnapshot);
            scratch.clear();
            tempoDelay.processBlock(scratch,empty);
            expectWithinAbsoluteError(tempoDelay.getHostTempoForDisplay(),150.0,0.01);
            expectWithinAbsoluteError(tempoDelay.getEffectiveDelayMsForDisplay(),300.0,0.05,
                                      "tempo changes automatically retime HOST delay");

            // Two bars at 120 BPM are 4000 ms; at 30 BPM they are 16000 ms.
            // Both MUST follow transport tempo instead of the 3500ms MS cap.
            tempoDelay.getParameterAt(Note).setValue(
                tempoDelay.getParameterAt(Note).toNormalised(12.0f));
            clockSnapshot.bpm=120.0;
            hostClock.updateSnapshot(clockSnapshot);
            scratch.clear();
            tempoDelay.processBlock(scratch,empty);
            expectWithinAbsoluteError(tempoDelay.getEffectiveDelayMsForDisplay(),4000.0,0.1);
            clockSnapshot.bpm=30.0;
            hostClock.updateSnapshot(clockSnapshot);
            scratch.clear();
            tempoDelay.processBlock(scratch,empty);
            expectWithinAbsoluteError(tempoDelay.getEffectiveDelayMsForDisplay(),16000.0,0.1);
            tempoDelay.getParameterAt(Note).setValue(
                tempoDelay.getParameterAt(Note).toNormalised(5.0f));

            tempoDelay.getParameterAt(SyncMode).setValue(
                tempoDelay.getParameterAt(SyncMode).toNormalised(2.0f));
            tempoDelay.getParameterAt(ManualBpm).setValue(
                tempoDelay.getParameterAt(ManualBpm).toNormalised(90.0f));
            expectWithinAbsoluteError(tempoDelay.getEffectiveDelayMsForDisplay(),500.0,0.05,
                                      "manual BPM ignores project BPM until HOST selected");

            tempoDelay.getParameterAt(SyncMode).setValue(0.0f);
            tempoDelay.getParameterAt(TimeMs).setValue(
                tempoDelay.getParameterAt(TimeMs).toNormalised(246.0f));
            expectWithinAbsoluteError(tempoDelay.getEffectiveDelayMsForDisplay(),246.0,0.05,
                                      "MS timing is independent of project BPM");
            tempoDelay.releaseResources();
            tempoDelay.setPlayHead(nullptr);
        }

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
            // Validate the photo-matched responsive JUCE geometry. The two
            // hero knobs must sit above the compact seven-knob lower band.
            if(editor->getNumChildComponents()>=9)
            {
                auto* hero=dynamic_cast<juce::Slider*>(editor->getChildComponent(0));
                auto* feedback=dynamic_cast<juce::Slider*>(editor->getChildComponent(1));
                auto* firstLower=dynamic_cast<juce::Slider*>(editor->getChildComponent(2));
                auto* lastLower=dynamic_cast<juce::Slider*>(editor->getChildComponent(8));
                expect(hero!=nullptr && feedback!=nullptr && firstLower!=nullptr
                       && lastLower!=nullptr);
                if(hero && feedback && firstLower && lastLower)
                {
                    expect(hero->getBottom()<firstLower->getY(),
                           "top and lower knob rows do not overlap");
                    expect(firstLower->getBottom()<=437,
                           "knob value plates never overlap footer buttons");
                    expect(lastLower->getRight()<editor->getWidth(),
                           "manual BPM knob stays inside reference design");
                    expect(hero->getX()<feedback->getX());
                }
            }
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
