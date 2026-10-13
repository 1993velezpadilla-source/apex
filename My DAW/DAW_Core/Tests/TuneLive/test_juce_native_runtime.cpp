// Uses linked JUCE 8 libraries, not mock headers: runtime insert,
// prepare/process/state/editor. A real Windows GUI+audio smoke without
// requiring an Apollo Solo on GitHub runners.
#include "../../Source/TuneLiveCore/ApexTuneLiveProcessor.h"
#include "../../Source/PluginHostCore/ApexNativePluginFormat.h"
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
    const auto desc=DAW::ApexNativePluginFormat::createTuneLiveDescription();
    if(!DAW::ApexNativePluginFormat::isTuneLiveDescription(desc))
        return fail("intrinsic plugin identifier not registered");
    if(desc.fileOrIdentifier!=APEX::TuneLive::Processor::kFileOrIdentifier)
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
    if(editor->getWidth()<620 || editor->getHeight()<560)
        return fail("native editor touch-safe bounds too small");
    if(editor->getNumChildComponents()<12)
        return fail("editor has fewer controls than expected");

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
    }
    const float hz=plugin.detectedFrequencyHz();
    if(hz<430.0f || hz>470.0f)
        return fail("live input was not pitch-tracked by native insert");

    editor.reset();
    plugin.releaseResources();
    std::cout<<"PASS: JUCE 8 native runtime plugin description, 11 hosted params, "
                "GUI controls, state, 48k/128 stereo realtime processing "
                "and pitch detection. F0="<<hz<<"Hz\n";
    return 0;
}
