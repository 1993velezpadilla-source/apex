// APEX Bubblegum Delay: host ABI, identity, session/preset, engine integration,
// editor lifecycle and stereo/mono processing contracts. No external Waves code.
#include <JuceHeader.h>
#include "../../../Source/DelayCore/ApexBubblegumDelayProcessor.h"
#include "../../../Source/PluginHostCore/ApexNativePluginFormat.h"
#include "../../../Source/PluginScanCore/PluginScanFormatsCore.h"
#include "../../../Source/PluginHostCore/HostPlayheadCore.h"

#include <cmath>
#include <memory>
#include <vector>

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

// Screenshot gate: render the ACTUAL compiled APEX Limiter JUCE editor.
// This is not a mock, generated image, or a drawing reconstructed from code.
// The graphics tree is rendered via paintEntireComponent, including children.
class LimiterNativePreviewTests final : public juce::UnitTest
{
public:
    LimiterNativePreviewTests() : juce::UnitTest("BubblegumLimiter.RealGUI", "APEX.LimiterPreview") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        // Verify the visible history is anchored to audio TIME, not the
        // host's callback block size. Two callbacks sizes must report the
        // same 100 frames over one second and matching real DSP telemetry.
        beginTest("Live history is deterministic across 128/1024 sample host blocks");
        APEX::Limiter::Processor processor128,processor1024;
        processor128.prepareToPlay(48000.0,128);
        processor1024.prepareToPlay(48000.0,1024);
        juce::MidiBuffer midi;
        const auto exercise=[&](APEX::Limiter::Processor& p,int blockSize)
        {
            for(int offset=0;offset<48000;offset+=blockSize)
            {
                const int n=juce::jmin(blockSize,48000-offset);
                juce::AudioBuffer<float> input(2,n);
                for(int i=0;i<n;++i)
                {
                    const double time=(offset+i)/48000.0;
                    const double envelope=0.4+0.5*std::abs(
                        std::sin(2.0*juce::MathConstants<double>::pi*1.7*time));
                    const float wave=static_cast<float>(envelope*(
                        std::sin(2.0*juce::MathConstants<double>::pi*220.0*time)
                       +0.3*std::sin(2.0*juce::MathConstants<double>::pi*1760.0*time)));
                    input.setSample(0,i,wave);
                    input.setSample(1,i,wave*0.72f);
                }
                p.processBlock(input,midi);
            }
        };
        exercise(processor128,128);
        exercise(processor1024,1024);
        expectEquals(static_cast<int>(processor128.displayFramesPublished()),100,
                     "48kHz / 100 history Hz = exactly 100 frames");
        expectEquals(static_cast<int>(processor1024.displayFramesPublished()),100,
                     "larger host blocks must not compress the graph time axis");
        for(const size_t age:{size_t{0},size_t{1},size_t{9},size_t{34},size_t{99}})
        {
            const auto a=processor128.displayFrame(age),b=processor1024.displayFrame(age);
            expectWithinAbsoluteError(static_cast<double>(a.input),static_cast<double>(b.input),1e-5,
                                      "input peak must match across host buffer sizes");
            expectWithinAbsoluteError(static_cast<double>(a.output),static_cast<double>(b.output),1e-5,
                                      "output peak must match across host buffer sizes");
            expectWithinAbsoluteError(static_cast<double>(a.reduction),static_cast<double>(b.reduction),1e-5,
                                      "gain reduction must match across host buffer sizes");
        }
        processor128.releaseResources();
        processor1024.releaseResources();

        beginTest("True 1x-32x native host mode, fixed PDC, state recall and no realtime resize");
        // Audio oversampling is deliberately selected at prepare(), not
        // dynamically on the audio callback; PDC changes require a restart.
        for(int quality=0;quality<=5;++quality)
        {
            APEX::Limiter::Processor hosted;
            hosted.getLimiterParameter(APEX::Limiter::AudioOversampling)
                  .setValueNotifyingHost(static_cast<float>(quality)/5.0f);
            hosted.prepareToPlay(48000.0,128);
            const int factor=1<<quality;
            expectEquals(hosted.activeAudioOversampling(),factor,
                         "real audio oversampling starts at selected factor");
            expect(!hosted.audioOversamplingChangePending(),
                   "selected factor becomes active on prepare");
            const int pdc=hosted.getLatencySamples();
            expect(pdc>=950 && pdc<=1070,
                   "host reports approximately 20ms Core + FIR latency");
            const int frames=3072;
            std::vector<float> output(static_cast<size_t>(frames),0.0f);
            for(int offset=0;offset<frames;offset+=73)
            {
                const int n=juce::jmin(73,frames-offset);
                juce::AudioBuffer<float> audio(2,n);
                for(int k=0;k<n;++k)
                {
                    const int frame=offset+k;
                    const float sine=0.32f*static_cast<float>(std::sin(
                        2.0*juce::MathConstants<double>::pi*439.0*frame/48000.0));
                    audio.setSample(0,k,sine);
                    audio.setSample(1,k,sine*0.7f);
                }
                hosted.processBlock(audio,midi);
                for(int k=0;k<n;++k)
                {
                    const auto v=audio.getSample(0,k);
                    expect(std::isfinite(v) && std::abs(v)<2.0f,
                           "host-rate processed audio is finite and bounded");
                    output[static_cast<size_t>(offset+k)]=v;
                }
            }
            float energy=0.0f;
            for(int i=2400;i<frames;++i)
                energy+=output[static_cast<size_t>(i)]*
                        output[static_cast<size_t>(i)];
            expect(energy>0.02f,"native host executes real high-rate audio path");
            hosted.releaseResources();
        }
        APEX::Limiter::Processor staged;
        staged.prepareToPlay(48000.0,128);
        const int originalPdc=staged.getLatencySamples();
        staged.getLimiterParameter(APEX::Limiter::AudioOversampling)
              .setValueNotifyingHost(1.0f);
        expect(staged.audioOversamplingChangePending(),
               "runtime user change is queued until audio engine restarts");
        expectEquals(staged.activeAudioOversampling(),1,
                     "running 1x DSP cannot be reconfigured on audio callback");
        expectEquals(staged.getLatencySamples(),originalPdc,
                     "queued selector cannot silently change host PDC");
        juce::MemoryBlock state;
        staged.getStateInformation(state);
        APEX::Limiter::Processor recalled;
        recalled.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        expect(recalled.audioOversamplingChangePending(),
               "32x choice survived a complete host state roundtrip");
        recalled.prepareToPlay(48000.0,128);
        expectEquals(recalled.activeAudioOversampling(),32,
                     "restored 32x choice activates on prepare");
        expect(!recalled.audioOversamplingChangePending(),
               "restored high-rate mode is no longer pending");

        beginTest("Capture real hosted limiter editor including live JUCE controls");
        APEX::Limiter::Processor processor;
        processor.prepareToPlay(48000.0, 128);
        juce::AudioBuffer<float> block(2, 128);
        // Six seconds of actual DSP processing: shaped stereo musical tones
        // and periodic sharp transients, not invented pixels or faked meters.
        // The screenshot must demonstrate an animated, varied history.
        for (int iteration=0; iteration<2250; ++iteration)
        {
            for (int i=0; i<block.getNumSamples(); ++i)
            {
                const double time=(iteration*128+i)/48000.0;
                const double beat=std::exp(-22.0*std::fmod(time,0.53));
                const double volume=0.20+0.62*(0.5+0.5*std::sin(
                    2.0*juce::MathConstants<double>::pi*0.7*time));
                const double rest=std::fmod(time,2.13)<0.20?0.035:1.0;
                const float wave=static_cast<float>(rest*(
                    volume*(0.75*std::sin(2.0*juce::MathConstants<double>::pi*220.0*time)
                           +0.32*std::sin(2.0*juce::MathConstants<double>::pi*990.0*time))
                    +beat*1.15*std::sin(2.0*juce::MathConstants<double>::pi*3110.0*time)));
                block.setSample(0,i,wave);
                block.setSample(1,i,wave*static_cast<float>(
                    0.74+0.12*std::sin(2.0*juce::MathConstants<double>::pi*0.3*time)));
            }
            processor.processBlock(block,midi);
        }
        expectEquals(static_cast<int>(processor.displayFramesPublished()),600,
                     "six seconds must publish 600 real 10-ms envelope frames");

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        expect(editor!=nullptr, "actual AudioProcessorEditor is instantiated");
        if (!editor) return;

        const juce::String overrideDir=juce::SystemStats::getEnvironmentVariable(
            "APEX_LIMITER_PREVIEW_DIR", {});
        const juce::File folder=overrideDir.isNotEmpty()
            ? juce::File(overrideDir)
            : juce::File::getCurrentWorkingDirectory().getChildFile(
                "evidence/renderpacks/APEX_LIMITER_NATIVE");
        expect(folder.createDirectory() || folder.isDirectory(), "can create screenshot directory");
        // Let timer read real DSP meters onto the real UI (no synthetic UI values).
        juce::MessageManager::getInstance()->runDispatchLoopUntil(75);

        auto capture=[&](int width,int height,const char* filename)
        {
            editor->setSize(width,height);
            juce::Image image(juce::Image::ARGB,width,height,true);
            {
                juce::Graphics graphics(image);
                editor->paintEntireComponent(graphics,false);
            }
            auto destination=folder.getChildFile(filename);
            destination.deleteFile();
            juce::FileOutputStream stream(destination);
            juce::PNGImageFormat png;
            const bool saved=stream.openedOk() && png.writeImageToStream(image,stream);
            stream.flush();
            expect(saved && destination.getSize()>10000,
                   juce::String("must write actual rendered image: ")+destination.getFullPathName());
            logMessage("APEX LIMITER REAL JUCE SCREENSHOT: "+destination.getFullPathName()
                       +" ("+juce::String(width)+"x"+juce::String(height)+")");
        };
        auto* visual=dynamic_cast<APEX::Limiter::Editor*>(editor.get());
        expect(visual!=nullptr,"real mastering GUI uses APEX native Editor");
        for(const auto size : {juce::Point<int>{820,520},juce::Point<int>{1060,640},
                              juce::Point<int>{1440,800}})
        {
            editor->setSize(size.x,size.y);
            if(visual)
            {
                const auto graph=visual->getGraphRegionForTest();
                const auto meter=visual->getMeterRegionForTest();
                const auto knobs=visual->getAdvancedRegionForTest();
                const auto peakValues=visual->getPeakReadoutRegionForTest();
                const auto loudness=visual->getLoudnessReadoutRegionForTest();
                expect(meter.contains(peakValues),"peak values stay inside the meter");
                expect(meter.contains(loudness),"GR/LUFS/LRA stay inside the meter");
                expect(!peakValues.intersects(loudness),
                       "peak readout must not collide with the GR/LUFS summary");
                expect(!graph.intersects(meter),"waveform graph never intersects meter");
                expect(!graph.intersects(knobs),"waveform graph never intersects controls");
                expect(!meter.intersects(knobs),"meters never cover advanced controls");
                expect(editor->getLocalBounds().contains(graph),"waveform inside host window");
                expect(editor->getLocalBounds().contains(meter),"meter inside host window");
                expect(editor->getLocalBounds().contains(knobs),"controls inside host window");
            }
        }
        capture(1060,640,"01_APEX_Bubblegum_Limiter_REAL_1060x640.png");
        capture(820,520,"02_APEX_Bubblegum_Limiter_REAL_820x520.png");
        capture(1440,800,"03_APEX_Bubblegum_Limiter_REAL_1440x800.png");
        // This fourth real JUCE capture uses the *actual* live 32x DSP,
        // not a mockup or detector-only fake selector. Reprepare on the
        // test/message thread, then stream audio and read atomics.
        beginTest("Capture actual native 32x oversampling state and PDC");
        processor.getLimiterParameter(APEX::Limiter::AudioOversampling)
                 .setValueNotifyingHost(1.0f);
        expect(processor.audioOversamplingChangePending(),
               "OS parameter change is staged before engine restart");
        processor.prepareToPlay(48000.0,128);
        expectEquals(processor.activeAudioOversampling(),32,
                     "32x audio path is running in actual JUCE editor");
        for(int iteration=0;iteration<120;++iteration)
        {
            for(int i=0;i<block.getNumSamples();++i)
            {
                const double t=(iteration*128+i)/48000.0;
                const float wave=static_cast<float>(
                    0.45*std::sin(2.0*juce::MathConstants<double>::pi*880.0*t)
                    +0.15*std::sin(2.0*juce::MathConstants<double>::pi*1760.0*t));
                block.setSample(0,i,wave);
                block.setSample(1,i,wave*0.73f);
            }
            processor.processBlock(block,midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(75);
        capture(1060,640,"04_APEX_Bubblegum_Limiter_REAL_32x_1060x640.png");
        processor.releaseResources();
    }
};
static LimiterNativePreviewTests limiterNativePreviewTests;
static DelayNativeHostTests delayNativeHostTests;
} // namespace
