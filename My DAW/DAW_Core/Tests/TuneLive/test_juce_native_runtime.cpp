// Uses linked JUCE 8 libraries, not mock headers: runtime insert,
// prepare/process/state/editor. A real Windows GUI+audio smoke without
// requiring an Apollo Solo on GitHub runners.
#include "../../Source/TuneLiveCore/ApexTuneLiveProcessor.h"
#include <cmath>
#include <iostream>
#include <memory>

static int fail(const char* reason) {
    std::cerr << "FAIL native JUCE runtime: " << reason << "\n";
    return 1;
}
int main() {
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    APEX::TuneLive::Processor plugin;
    // The unified APEX Native registry is compiled by the separate strict
    // JUCE syntax gate. Linking ALL G10/C4/ParametricEQ factories into a
    // TuneLive-only console executable would require the entire DAW's .cpp
    // dependency graph, so test the processor's own host description here.
    juce::PluginDescription desc;
    plugin.fillInPluginDescription(desc);
    if(desc.pluginFormatName!="APEX Native" || desc.manufacturerName!="APEX")
        return fail("wrong intrinsic native plug-in format");
    if(desc.fileOrIdentifier!=APEX::TuneLive::Processor::kFileOrIdentifier
       || desc.uniqueId!=APEX::TuneLive::Processor::kUniqueId)
        return fail("unstable native plugin identifier");
    if(plugin.getParameters().size()!=APEX::TuneLive::Processor::kNumParams)
        return fail("expected 11 hosted/automation-safe parameters");

    plugin.prepareToPlay(48000.0,128);
    if(plugin.getLatencySamples()!=1920)
        return fail("48k/40ms PDC disclosure is not stable");

    // Verify that editing a hosted parameter is serialised/restored through
    // actual JUCE state callbacks. No DAW-level session promise implied.
    auto* amount=plugin.getParameters()[APEX::TuneLive::Processor::kAmount];
    amount->setValueNotifyingHost(0.52f);
    juce::MemoryBlock state;
    plugin.getStateInformation(state);
    if(state.getSize()==0) return fail("empty plugin state");
    amount->setValueNotifyingHost(0.12f);
    plugin.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    if(std::abs(amount->getValue()-0.52f)>0.001f)
        return fail("host parameter state round-trip");

    // Construct the custom editor inside the JUCE message-thread context,
    // and ensure it presents more than generic parameter controls.
    std::unique_ptr<juce::AudioProcessorEditor> editor(plugin.createEditor());
    if(!editor) return fail("native editor unavailable");
    if(editor->getWidth()!=1200 || editor->getHeight()!=750)
        return fail("premium native editor is not 1200x750 reference size");
    if(editor->getNumChildComponents()<12)
        return fail("editor has fewer controls than expected");

    // Test actual dropdown interactions; Ab and Bb map onto the existing
    // automated 12-semitone parameter. Never create duplicate tuning keys.
    juce::ComboBox* keyBox=nullptr;
    for(int child=0;child<editor->getNumChildComponents();++child)
        if(auto* combo=dynamic_cast<juce::ComboBox*>(editor->getChildComponent(child)))
            if(combo->getNumItems()==17)keyBox=combo;
    if(!keyBox)return fail("expected 17 selectable C#/Db...G#/Ab...A#/Bb spellings");
    auto* key=plugin.getParameters()[APEX::TuneLive::Processor::kKey];
    keyBox->setSelectedId(13,juce::sendNotificationSync);
    if(keyBox->getText()!="Ab" || std::abs(key->getValue()-8.0f/11.0f)>0.001f)
        return fail("Ab key does not map to G# parameter semitone 8");
    keyBox->setSelectedId(16,juce::sendNotificationSync);
    if(keyBox->getText()!="Bb" || std::abs(key->getValue()-10.0f/11.0f)>0.001f)
        return fail("Bb key does not map to A# parameter semitone 10");
    keyBox->setSelectedId(1,juce::sendNotificationSync);
    if(std::abs(key->getValue())>0.001f)
        return fail("GUI key reset to C failed");

    // Screenshot-only instrumentation: this captures the EXACT, existing
    // JUCE production editor. Does not change the GUI or any DSP control.
    // It is intentionally generated inside the real linked Windows JUCE
    // harness, rather than a generated mockup or design concept.
    editor->setVisible(true);
    const juce::Image screenshot=editor->createComponentSnapshot(editor->getLocalBounds());
    if(!screenshot.isValid() || screenshot.getWidth()!=1200 || screenshot.getHeight()!=750)
        return fail("actual Tune Live editor screenshot invalid dimensions");
    const auto screenshotFile=juce::File::getCurrentWorkingDirectory()
        .getChildFile("APEX_Tune_Live_Actual_GUI_Windows.png");
    {
        auto stream=screenshotFile.createOutputStream();
        if(!stream) return fail("cannot create actual native GUI screenshot");
        juce::PNGImageFormat png;
        if(!png.writeImageToStream(screenshot,*stream))
            return fail("failed writing actual native GUI PNG");
        stream->flush();
    }
    if(screenshotFile.getSize()<4096)
        return fail("captured native GUI PNG unexpectedly small");
    std::cout<<"ACTUAL_TUNE_LIVE_GUI_SCREENSHOT "<<screenshotFile.getFullPathName()
             <<" "<<screenshot.getWidth()<<"x"<<screenshot.getHeight()
             <<" bytes="<<screenshotFile.getSize()<<"\\n";

    // V5 responsive fidelity gate: both ends of the supported touch-safe
    // artboard must keep every real JUCE control inside the editor viewport.
    for(const auto size : {juce::Point<int>(960,600),juce::Point<int>(1600,1000)}) {
        editor->setSize(size.x,size.y);
        if(editor->getWidth()!=size.x || editor->getHeight()!=size.y)
            return fail("responsive native editor rejected a supported size");
        for(int child=0;child<editor->getNumChildComponents();++child) {
            const auto* component=editor->getChildComponent(child);
            if(component->isVisible() && !editor->getLocalBounds().contains(component->getBounds()))
                return fail("resize clipped a native control");
        }
        if(size.x==960) {
            const auto compact=editor->createComponentSnapshot(editor->getLocalBounds());
            if(!compact.isValid() || compact.getWidth()!=960 || compact.getHeight()!=600)
                return fail("touch-safe real editor compact screenshot invalid");
            const auto compactFile=juce::File::getCurrentWorkingDirectory()
                .getChildFile("APEX_Tune_Live_Actual_GUI_Windows_Compact.png");
            auto stream=compactFile.createOutputStream();
            if(!stream || !juce::PNGImageFormat().writeImageToStream(compact,*stream))
                return fail("cannot write compact real editor PNG");
            stream->flush();
            if(compactFile.getSize()<4096)
                return fail("compact actual JUCE screenshot unexpectedly small");
        }
    }
    editor->setSize(1200,750);

    // The preset highlights and profile selection must control real hosted
    // parameters, while a user's custom values are never reset by a resize.
    juce::TextButton* modern=nullptr;
    for(int i=0;i<editor->getNumChildComponents();++i)
        if(auto* button=dynamic_cast<juce::TextButton*>(editor->getChildComponent(i)))
            if(button->getButtonText()=="MODERN") modern=button;
    if(!modern || !modern->onClick)
        return fail("MODERN native preset button/callback is missing");
    modern->onClick(); // direct GUI action; no synthetic DSP state
    const auto& modernProfile=APEX::TuneLive::kVocalStyles[1];
    if(!modern->getToggleState()
       || std::abs(amount->getValue()-modernProfile.amount)>0.001f
       || std::abs(plugin.getParameters()[APEX::TuneLive::Processor::kSpeed]
                  ->getValue()-modernProfile.retune)>0.001f)
        return fail("MODERN visual preset failed to drive hosted parameters");
    plugin.getParameters()[APEX::TuneLive::Processor::kSpeed]
          ->setValueNotifyingHost(0.37f); // changed by host; now custom
    editor->setSize(960,600);
    editor->setSize(1200,750);
    if(std::abs(plugin.getParameters()[APEX::TuneLive::Processor::kSpeed]
                ->getValue()-0.37f)>0.001f)
        return fail("GUI rescaling changed artist's custom automated setting");

    // Force precise Hard Tune in the JUCE host, not just in the portable
    // processor tester. In particular Flex Tune must be zero here.
    amount->setValueNotifyingHost(1.0f);
    plugin.getParameters()[APEX::TuneLive::Processor::kSpeed]
          ->setValueNotifyingHost(0.0f);
    plugin.getParameters()[APEX::TuneLive::Processor::kHumanize]
          ->setValueNotifyingHost(0.0f);
    plugin.getParameters()[APEX::TuneLive::Processor::kFlexTune]
          ->setValueNotifyingHost(0.0f);
    APEX::TuneLive::PitchTracker outputPitch;
    outputPitch.prepare(48000.0);

    // Pump identical stereo microphone signals through the actual JUCE
    // AudioPluginInstance processBlock; verify finite audio and detected F0.
    juce::AudioBuffer<float> audio(2,128);
    juce::MidiBuffer midi;
    constexpr int total=48000;
    for(int i=0;i<total;i+=128) {
        for(int n=0;n<128;++n) {
            const double time=static_cast<double>(i+n)/48000.0;
            const float v=static_cast<float>(0.30*std::sin(
                6.283185307179586*447.69*time));
            audio.setSample(0,n,v);
            audio.setSample(1,n,v);
        }
        plugin.processBlock(audio,midi);
        for(int c=0;c<2;++c)
            for(int n=0;n<128;++n)
                if(!std::isfinite(audio.getSample(c,n)))
                    return fail("non-finite native audio output");
        if(i>=24000)
            for(int n=0;n<128;++n)
                outputPitch.push(audio.getSample(0,n));
    }
    const float hz=plugin.detectedFrequencyHz();
    if(hz<430.0f || hz>470.0f)
        return fail("live input was not pitch-tracked by native insert");

    const auto wet=outputPitch.latest();
    const double cents=wet.voiced
       ? 1200.0*std::log2(wet.hz/440.0) : 9999.0;
    if(!wet.voiced || std::abs(cents)>18.0)
        return fail("hosted insert did not correct actual processed audio to A4");

    // APEX records live solo microphones in mono. The portable DSP
    // stereo test is not enough: the JUCE hosted bus itself must switch to
    // mono before prepare(), then process a 1-channel AudioBuffer.
    plugin.releaseResources();
    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
    if(!plugin.setBusesLayout(monoLayout))
        return fail("APEX native mono mic bus layout rejected");
    plugin.prepareToPlay(48000.0,128);
    if(plugin.getLatencySamples()!=1920)
        return fail("mono host PDC differs from stereo");
    juce::AudioBuffer<float> monoAudio(1,128);
    APEX::TuneLive::PitchTracker monoOut;
    monoOut.prepare(48000.0);
    for(int i=0;i<48000;i+=128) {
        for(int n=0;n<128;++n) {
            const double t=static_cast<double>(i+n)/48000.0;
            monoAudio.setSample(0,n,static_cast<float>(
                0.32*std::sin(6.283185307179586*447.69*t)));
        }
        plugin.processBlock(monoAudio,midi);
        if(i>=24000)
            for(int n=0;n<128;++n) {
                const float x=monoAudio.getSample(0,n);
                if(!std::isfinite(x))return fail("mono host emitted NaN");
                monoOut.push(x);
            }
    }
    const auto monoPitch=monoOut.latest();
    const double monoCents=monoPitch.voiced
                        ?1200.0*std::log2(monoPitch.hz/440.0):9999.0;
    if(!monoPitch.voiced || std::abs(monoCents)>18.0)
        return fail("mono host failed to actually pitch-correct Apollo-like mic");

    editor.reset();
    plugin.releaseResources();
    std::cout<<"PASS: JUCE 8 native runtime plugin description, 11 hosted params, "
                "GUI controls, state, 48k/128 stereo realtime processing "
                "and pitch correction. Input F0="<<hz
             <<"Hz, stereo wet cents from A4="<<cents
             <<", mono wet cents from A4="<<monoCents<<"\n";
    return 0;
}
