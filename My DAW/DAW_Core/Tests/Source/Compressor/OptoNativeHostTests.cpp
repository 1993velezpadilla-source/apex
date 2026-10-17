#include <JuceHeader.h>
#include "../../../Source/PluginHostCore/ApexNativePluginFormat.h"
#include "../../../Source/CompressorCore/ApexOptoLevelerProcessor.h"
#include "../../../Source/PluginHostCore/PluginChainCore.h"
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
        // JUCE AudioProcessor exposes this virtual method as protected; our
        // OptoProcessor override is public and safe to inspect directly.
        expect(processor->isBusesLayoutSupported(monoLayout));
        expect(processor->isBusesLayoutSupported(stereoLayout));
        expectEquals(base->getParameters().size(),(int)ParameterCount);

        beginTest("Stable parameter IDs for APEX automation and dry/wet contract");
        for (int i=0;i<ParameterCount;++i)
        {
            auto* named=dynamic_cast<juce::AudioProcessorParameterWithID*>(
                base->getParameters()[i]);
            expect(named != nullptr, "all nine params support stable host IDs");
        }
        beginTest("A single optical plugin exposes three host-automatable voicings");
        auto& voice=processor->getParameterAt(APEX::Compressor::Voicing);
        expectEquals(voice.getNumSteps(),3);
        expect(voice.isDiscrete());
        expectEquals(voice.getText(0.f,0),juce::String("FAST"));
        expectEquals(voice.getText(.5f,0),juce::String("CLASSIC"));
        expectEquals(voice.getText(1.f,0),juce::String("SLOW"));
        voice.setValue(.5f);
        expectWithinAbsoluteError(static_cast<double>(voice.getValue()),.5,1.e-6);
        voice.setValue(.8f);
        expectWithinAbsoluteError(static_cast<double>(voice.getValue()),1.,1.e-6,
                                  "continuous host data must quantize to the nearest voice");
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

        beginTest("Rack GUI constructs, scales and renders nonempty hardware face");
        {
            auto editor=std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor());
            expect(editor!=nullptr,"A real JUCE rack editor must be constructed");
            if(editor)
            {
                expectEquals(editor->getWidth(),1040);
                expectEquals(editor->getHeight(),380);
                expect(editor->getNumChildComponents()>=10,
                       "Gain/Peak/VU selector/Limit/Power/parallel/key/color controls");
                // JUCE 8 defaults to GPU-backed Direct2D native images on Windows.
                // Headless GitHub runner has no render peer; queued GPU draw
                // commands produced a fully transparent 1040x380 PNG.
                // Force CPU-backed image for deterministic screenshot tests.
                juce::Image screenshot(juce::Image::ARGB,1040,380,true,
                                        juce::SoftwareImageType{});
                juce::Graphics graphics(screenshot);
                // This editor is deliberately NOT attached to a desktop
                // window during the headless Windows CI run. JUCE suppresses
                // paintEntireComponent for components without a showing peer:
                // even setVisible(true) yielded a fully transparent PNG.
                // Paint the editor and each direct visible child explicitly
                // using the exact production paint callbacks, with each
                // child's original local bounds and clip. This yields an
                // authentic deterministic offscreen proof without a window.
                editor->paint(graphics);
                for (int childIndex = 0; childIndex < editor->getNumChildComponents(); ++childIndex)
                {
                    auto* child = editor->getChildComponent(childIndex);
                    if (child == nullptr || !child->isVisible()) continue;
                    const auto area = child->getBounds();
                    juce::Graphics::ScopedSaveState childState(graphics);
                    graphics.reduceClipRegion(area);
                    graphics.addTransform(juce::AffineTransform::translation(
                        static_cast<float>(area.getX()), static_cast<float>(area.getY())));
                    child->paint(graphics);
                }
                const auto metal=screenshot.getPixelAt(195,148);
                const auto lcd=screenshot.getPixelAt(512,110);
                const auto strip=screenshot.getPixelAt(25,344);
                expect(metal.getBrightness()>0.32f,"rack must have silver metal chassis");
                expect(lcd.getRed()>lcd.getBlue(),"VU meter must have amber glass");
                expect(strip.getBrightness()<0.30f,"APEX extra controls remain separate");
                const auto screenshots=juce::File::getCurrentWorkingDirectory()
                    .getChildFile("evidence").getChildFile("opto2a_gui");
                if(screenshots.createDirectory().wasOk())
                {
                    const auto name=screenshots.getChildFile("apex_opto2a_reference_face_1040x380.png");
                    auto stream=name.createOutputStream();
                    if(stream!=nullptr)
                    {
                        juce::PNGImageFormat format;
                        expect(format.writeImageToStream(screenshot,*stream),
                               "Render rack screenshot artifact");
                        stream->flush();
                    }
                }
                editor->setSize(780,330);
                expectEquals(editor->getWidth(),780);
                expect(editor->getNumChildComponents()>=10,"responsive resize keeps real controls");
            }
        }

        beginTest("Session state roundtrip preserves normalized parameter values");
        processor->getParameterAt(APEX::Compressor::Mix).setValue(0.32f);
        processor->getParameterAt(APEX::Compressor::Gain).setValue(0.25f);
        processor->getParameterAt(APEX::Compressor::Emphasis).setValue(0.81f);
        processor->getParameterAt(APEX::Compressor::Limit).setValue(1.0f);
        processor->setMeterMode(2);
        processor->getParameterAt(APEX::Compressor::Voicing).setValue(1.0f);
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
        expectEquals(restored.getMeterMode(),2,"VU selector persists as UI state");
        expectEquals(static_cast<int>(restored.getParameterAt(
            APEX::Compressor::Voicing).getUnits()),2,
            "FAST/CLASSIC/SLOW must survive session save-close-reopen");

        beginTest("Actual APEX PluginChain insert/process/save/reopen with native Opto 2A");
        {
            DAW::PluginChainCore trackChain;
            trackChain.prepare(48000.0,128);
            juce::String chainError;
            const int slot=trackChain.appendPlugin(opto,manager,chainError);
            expectEquals(slot,0,"APEX mixer must insert the native compressor");
            expect(chainError.isEmpty(),chainError);
            if(slot==0 && trackChain.getSlot(0)!=nullptr)
            {
                auto* live=dynamic_cast<OptoProcessor*>(
                    trackChain.getSlot(0)->getProcessor());
                expect(live!=nullptr,"actual mixer slot must own OptoProcessor");
                if(live!=nullptr)
                {
                    live->getParameterAt(APEX::Compressor::PeakReduction).setValue(.8f);
                    live->getParameterAt(APEX::Compressor::Gain).setValue(0.f);
                    live->getParameterAt(APEX::Compressor::ColorEnabled).setValue(0.f);
                    live->getParameterAt(APEX::Compressor::Voicing).setValue(1.f);
                    trackChain.setSlotMix(0,1.f);
                    trackChain.setChainOutputMix(1.f);
                    juce::AudioBuffer<float> routed(2,128);
                    for(int block=0;block<128;++block)
                    {
                        for(int n=0;n<128;++n)
                        {
                            routed.setSample(0,n,.7f);
                            routed.setSample(1,n,.35f);
                        }
                        trackChain.processBlock(routed,128);
                    }
                    expect(live->getGainReductionMeterDb()>1.f,
                           "inserted native compressor must actually reduce gain");
                    expect(routed.getSample(0,127)<.7f,
                           "track chain must route processed wet audio, not unprocessed dry");
                    expectWithinAbsoluteError(
                        static_cast<double>(routed.getSample(0,127)/.7f),
                        static_cast<double>(routed.getSample(1,127)/.35f),
                        1.e-4,"stereo channels must remain compression-linked");

                    juce::ValueTree saved;
                    juce::String saveError;
                    const bool captured=trackChain.captureStateForPersistence(
                        saved,saveError);
                    expect(captured,saveError);
                    if(captured)
                    {
                        DAW::PluginChainCore reopened;
                        reopened.prepare(48000.0,128);
                        juce::String restoreError;
                        const bool restoredOk=reopened.restoreState(
                            saved,manager,restoreError);
                        expect(restoredOk,restoreError);
                        expectEquals(reopened.getNumSlots(),1);
                        if(restoredOk && reopened.getSlot(0)!=nullptr)
                        {
                            auto* p=dynamic_cast<OptoProcessor*>(
                                reopened.getSlot(0)->getProcessor());
                            expect(p!=nullptr,"saved native plugin must resolve by stable identifier");
                            if(p)
                            {
                                expectEquals(static_cast<int>(
                                    p->getParameterAt(APEX::Compressor::Voicing).getUnits()),
                                    2,"SLOW voicing survives full mixer session restore");
                                expectWithinAbsoluteError(static_cast<double>(
                                    p->getParameterAt(APEX::Compressor::PeakReduction)
                                        .getValue()),.8,1.e-5);
                            }
                        }
                    }
                }
            }
        }

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
