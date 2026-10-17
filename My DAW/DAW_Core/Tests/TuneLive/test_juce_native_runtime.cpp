// Uses linked JUCE 8 libraries, not mock headers: runtime insert,
// prepare/process/state/editor. A real Windows GUI+audio smoke without
// requiring an Apollo Solo on GitHub runners.
#include "../../Source/TuneLiveCore/ApexTuneLiveProcessor.h"
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveCausalJUCEAdapter.h"
#include <array>
#include <chrono>
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

    // A completely separate JUCE 8 AudioPluginInstance executes the
    // causal/residual LIVE experiment in real linked Windows JUCE code.
    // NOT registered with APEX Native; never changes HQ40 by itself.
    // Zero FIXED lookahead metadata is not a claim of 1ms note detection.
    APEX::TuneLive::Research::CausalJUCEAdapter candidate;
    candidate.prepareToPlay(48000.0,128);
    if(candidate.getLatencySamples()!=0)
        return fail("causal research host added fixed plugin lookahead");
    juce::AudioBuffer<float> researchAudio(2,128);
    APEX::TuneLive::PitchTracker researchPitch;
    researchPitch.prepare(48000.0);
    for(int i=0;i<48000;i+=128) {
        for(int n=0;n<128;++n) {
            const double t=static_cast<double>(i+n)/48000.0;
            const float sample=static_cast<float>(0.27*std::sin(
                6.283185307179586*447.691*t));
            researchAudio.setSample(0,n,sample);
            researchAudio.setSample(1,n,sample);
        }
        candidate.processBlock(researchAudio,midi);
        for(int c=0;c<2;++c)
            for(int n=0;n<128;++n)
                if(!std::isfinite(researchAudio.getSample(c,n)))
                    return fail("causal research JUCE returned invalid audio");
        if(i>=24000)
            for(int n=0;n<128;++n)
                researchPitch.push(researchAudio.getSample(0,n));
    }
    const auto researchF0=researchPitch.latest();
    const double researchCents=researchF0.voiced
        ?1200.0*std::log2(researchF0.hz/440.0):9999.0;
    if(!researchF0.voiced || std::abs(researchCents)>18.0 ||
       candidate.researchWetMix()<0.95)
        return fail("causal research hosted wet audio was not pitch-corrected");
    if(candidate.firstVoiceSample()<0 || candidate.firstHalfWetSample()<0 ||
       candidate.firstHalfWetSample()<candidate.firstVoiceSample())
        return fail("causal research hosted F0 onset telemetry invalid");
    if(candidate.firstUltraSample()<0 ||
       candidate.firstUltraSample()>384 ||
       candidate.firstHalfWetSample()>528)
        return fail("research JUCE ultra-high F0 exceeded 8ms or first half-wet 11ms");
    std::cout<<"CAUSAL_JUCE_RESEARCH fixed_pdc_samples="
             <<candidate.getLatencySamples()
             <<" firstF0_ms="<<1000.0*candidate.firstVoiceSample()/48000.0
             <<" ultraRegisterF0_ms="<<1000.0*candidate.firstUltraSample()/48000.0
             <<" firstWet50_ms="<<1000.0*candidate.firstHalfWetSample()/48000.0
             <<" late_wet_cents="<<researchCents
             <<" NO_1MS_FIRST_NOTE_RESPONSE_CLAIM\\n";
    candidate.releaseResources();

    // Real JUCE 8 streaming-host test for the guarded early octave seed.
    // We generate phase-continuous A3+30c -> A4-30c vocal-like harmonics;
    // no known pitch is handed to the processor.
    candidate.prepareToPlay(48000.0,128);
    APEX::TuneLive::PitchTracker researchLeapPitch;
    researchLeapPitch.prepare(48000.0);
    double sourcePhase=0.0;
    for(int i=0;i<48000;i+=128) {
        for(int n=0;n<128;++n) {
            const double hzIn=(i+n)<21600
                ?220.0*std::pow(2.0,30.0/1200.0)
                :440.0*std::pow(2.0,-30.0/1200.0);
            sourcePhase+=6.283185307179586*hzIn/48000.0;
            const float sample=static_cast<float>(
                0.24*std::sin(sourcePhase)
                +0.045*std::sin(2.0*sourcePhase+0.18)
                +0.018*std::sin(3.0*sourcePhase+0.24));
            researchAudio.setSample(0,n,sample);
            researchAudio.setSample(1,n,sample);
        }
        candidate.processBlock(researchAudio,midi);
        for(int n=0;n<128;++n) {
            const float sample=researchAudio.getSample(0,n);
            if(!std::isfinite(sample) ||
               !std::isfinite(researchAudio.getSample(1,n)))
                return fail("causal JUCE early leap generated invalid audio");
            if(i>=36000)researchLeapPitch.push(sample);
        }
    }
    const double leapTimeMs=1000.0*candidate.lastFastTransitionSample()/48000.0;
    if(candidate.getLatencySamples()!=0 ||
       candidate.fastTransitionCount()<1 ||
       leapTimeMs<450.0 || leapTimeMs>470.0)
        return fail("real JUCE host failed guarded early A3-A4 switch");
    const auto leapWet=researchLeapPitch.latest();
    const double leapCents=leapWet.voiced
        ?1200.0*std::log2(leapWet.hz/440.0):9999.0;
    if(!leapWet.voiced || std::abs(leapCents)>18.0 ||
       candidate.researchWetMix()<0.95)
        return fail("causal JUCE fast-leap wet audio lost tonal accuracy");
    std::cout<<"CAUSAL_JUCE_FAST_LEAP fixed_pdc_samples="
             <<candidate.getLatencySamples()
             <<" first_octave_seed_ms="<<leapTimeMs
             <<" fast_jump_count="<<candidate.fastTransitionCount()
             <<" final_wet_cents="<<leapCents
             <<" NO_1MS_PITCH_DECISION_CLAIM\\n";
    candidate.releaseResources();

    // Stress EIGHT isolated causal JUCE8 AudioPluginInstance inserts, not
    // merely an unhosted DSP object. Production HQ40 remains unchanged.
    // CPU timing is from an Actions Windows VM, NOT Apollo Solo hardware.
    constexpr int kLiveInserts=8;
    std::array<APEX::TuneLive::Research::CausalJUCEAdapter,kLiveInserts> liveInserts{};
    std::array<APEX::TuneLive::PitchTracker,kLiveInserts> checkedInserts{};
    for(int id=0;id<kLiveInserts;++id) {
        liveInserts[static_cast<std::size_t>(id)].prepareToPlay(48000.0,128);
        checkedInserts[static_cast<std::size_t>(id)].prepare(48000.0);
        if(liveInserts[static_cast<std::size_t>(id)].getLatencySamples()!=0)
            return fail("eight JUCE LIVE inserts report nonzero fixed lookahead");
    }
    const auto stressStart=std::chrono::steady_clock::now();
    for(int frame=0;frame<48000;frame+=128) {
        for(int id=0;id<kLiveInserts;++id) {
            for(int i=0;i<128;++i) {
                const double radians=6.283185307179586*447.691*
                                     (frame+i)/48000.0;
                const float value=static_cast<float>(
                    0.24*std::sin(radians)+
                    0.05*std::sin(2.0*radians+0.21));
                researchAudio.setSample(0,i,value);
                researchAudio.setSample(1,i,value);
            }
            auto& insert=liveInserts[static_cast<std::size_t>(id)];
            insert.processBlock(researchAudio,midi);
            for(int i=0;i<128;++i) {
                const auto l=researchAudio.getSample(0,i);
                const auto r=researchAudio.getSample(1,i);
                if(!std::isfinite(l)||!std::isfinite(r))
                    return fail("eight hosted LIVE inserts emitted NaN");
                if(frame>=24000)
                    checkedInserts[static_cast<std::size_t>(id)].push(l);
            }
        }
    }
    const double eightSeconds=
        std::chrono::duration<double>(std::chrono::steady_clock::now()-
                                      stressStart).count();
    for(int id=0;id<kLiveInserts;++id) {
        auto& insert=liveInserts[static_cast<std::size_t>(id)];
        const auto pitch=checkedInserts[static_cast<std::size_t>(id)].latest();
        const double centsOutput=pitch.voiced && pitch.hz>0.0f
            ?1200.0*std::log2(pitch.hz/440.0):9999.0;
        if(std::abs(centsOutput)>18.0 ||
           insert.researchWetMix()<0.95 ||
           insert.firstUltraSample()<0 ||
           insert.firstUltraSample()>384)
            return fail("eight real JUCE LIVE inserts lost high-register wet tuning");
        insert.releaseResources();
    }
    std::cout<<"CAUSAL_JUCE_8_INSTANCES count="<<kLiveInserts
             <<" sample_rate=48000 buffer=128"
             <<" total_audio_s=1 total_wall_s="<<eightSeconds
             <<" fixed_lookahead_samples=0"
             <<" independently_wet_tuned=8/8"
             <<" CI_VM_CPU_IS_NOT_APOLLO_OR_ASIO=1\\n";

    // A/B SAME-SAMPLE acoustic impulse proof through TWO independently
    // linked JUCE8 instances with their correction already 100% wet.
    // Unlike getLatencySamples()==0, this probes emitted PCM output and
    // stereo crosstalk on the actual research wet/residual audio path.
    // It is NOT 1ms F0 acquisition or an ASIO roundtrip measurement.
    APEX::TuneLive::Research::CausalJUCEAdapter baselineImpulse;
    APEX::TuneLive::Research::CausalJUCEAdapter alteredImpulse;
    baselineImpulse.prepareToPlay(48000.0,128);
    alteredImpulse.prepareToPlay(48000.0,128);
    juce::AudioBuffer<float> unchanged(2,128),injected(2,128);
    constexpr int impulseAt=32000;
    constexpr float injection=0.25f;
    double immediateDelta=0.0;
    bool preImpulseIdentical=true,stereoNoLeak=true;
    for(int frame=0;frame<38400;frame+=128) {
        for(int n=0;n<128;++n) {
            const int sampleIndex=frame+n;
            const double radians=6.283185307179586*
                                 447.691*sampleIndex/48000.0;
            const float dry=static_cast<float>(
                0.24*std::sin(radians)+
                0.05*std::sin(2.0*radians+0.2));
            unchanged.setSample(0,n,dry);
            unchanged.setSample(1,n,dry);
            injected.setSample(0,n,dry+(sampleIndex==impulseAt?injection:0.0f));
            injected.setSample(1,n,dry);
        }
        baselineImpulse.processBlock(unchanged,midi);
        alteredImpulse.processBlock(injected,midi);
        for(int n=0;n<128;++n) {
            const int sampleIndex=frame+n;
            const double leftDelta=static_cast<double>(
                injected.getSample(0,n)-unchanged.getSample(0,n));
            const double rightDelta=static_cast<double>(
                injected.getSample(1,n)-unchanged.getSample(1,n));
            if(sampleIndex<impulseAt)
                preImpulseIdentical &=leftDelta==0.0 && rightDelta==0.0;
            if(sampleIndex==impulseAt)immediateDelta=leftDelta/injection;
            if(sampleIndex<=impulseAt)stereoNoLeak &=rightDelta==0.0;
        }
    }
    if(!preImpulseIdentical||!stereoNoLeak||
       !std::isfinite(immediateDelta)||
       immediateDelta<0.70||immediateDelta>1.30||
       baselineImpulse.researchWetMix()<0.95||
       alteredImpulse.researchWetMix()<0.95)
        return fail("real linked JUCE causal wet impulse path is delayed, lost or crossfed");
    std::cout<<"CAUSAL_JUCE_WET_IMPULSE sr=48000 wet=1"
             <<" injected_at_sample="<<impulseAt
             <<" heard_at_sample="<<impulseAt
             <<" impulse_immediate_gain="<<immediateDelta
             <<" host_fixed_pdc="<<alteredImpulse.getLatencySamples()
             <<" no_right_channel_crosstalk="<<stereoNoLeak
             <<" singer_note_detection_not_1ms=1\\n";
    baselineImpulse.releaseResources();
    alteredImpulse.releaseResources();

    // Mid-range fast LIVE test through a REAL linked JUCE 8 instance.
    // C#4 (277.1826 Hz) +30 cents is actual chromatic tuning target.
    // This verifies emitted WET audio independently; zero fixed
    // lookahead does NOT assert a 1ms first-note pitch response.
    APEX::TuneLive::Research::CausalJUCEAdapter midJUCE;
    midJUCE.prepareToPlay(48000.0,128);
    APEX::TuneLive::PitchTracker midOutputPitch;
    midOutputPitch.prepare(48000.0);
    constexpr double midTargetHz=277.18263;
    constexpr double midInputHz=midTargetHz*1.0174796921026863;
    for(int frame=0;frame<48000;frame+=128) {
        for(int i=0;i<128;++i) {
            const double phase=6.283185307179586*
                               midInputHz*(frame+i)/48000.0;
            const float value=static_cast<float>(
                0.24*std::sin(phase)+
                0.045*std::sin(2.0*phase+0.18)+
                0.018*std::sin(3.0*phase+0.24));
            researchAudio.setSample(0,i,value);
            researchAudio.setSample(1,i,value);
        }
        midJUCE.processBlock(researchAudio,midi);
        for(int i=0;i<128;++i) {
            const float y=researchAudio.getSample(0,i);
            if(!std::isfinite(y))
                return fail("JUCE mid-register LIVE emitted invalid PCM");
            if(frame>=28000)midOutputPitch.push(y);
        }
    }
    const auto midTuned=midOutputPitch.latest();
    const double midCents=midTuned.voiced?
        1200.0*std::log2(midTuned.hz/midTargetHz):9999.0;
    if(midJUCE.getLatencySamples()!=0 ||
       midJUCE.firstMidSample()<0 ||
       midJUCE.firstMidSample()>static_cast<int64_t>(48000.0*0.014) ||
       midJUCE.firstHighSample()>=0 ||
       midJUCE.firstUltraSample()>=0 ||
       midJUCE.firstHalfWetSample()>static_cast<int64_t>(48000.0*0.019) ||
       !midTuned.voiced || std::abs(midCents)>18.0 ||
       midJUCE.researchWetMix()<0.95)
        return fail("linked JUCE 8 guarded mid F0 / wet onset or pitch gate");
    std::cout<<"CAUSAL_JUCE_MID_REGISTER fixed_lookahead_samples="
             <<midJUCE.getLatencySamples()
             <<" first_mid_ms="<<1000.0*midJUCE.firstMidSample()/48000.0
             <<" first_half_wet_ms="
             <<1000.0*midJUCE.firstHalfWetSample()/48000.0
             <<" corrected_cents="<<midCents
             <<" no_1ms_new_note_claim=1\\n";
    midJUCE.releaseResources();

    // FASTEST guarded half-cycle mode is exposed ONLY via the separate
    // unregistered JUCE research adapter. This is a genuine hosted Audio-
    // PluginInstance path, not just isolated portable DSP, so verify actual
    // JUCE PDC metadata, emitted PCM tuning and sample-synchronous attacks.
    // None of this qualifies as 1ms unknown-note recognition or shipping.
    APEX::TuneLive::Research::CausalJUCEAdapter fastHosted;
    fastHosted.enableGuardedHalfCycleResearch(true);
    fastHosted.prepareToPlay(48000.0,128);
    if(fastHosted.getLatencySamples()!=0)
        return fail("guarded half-cycle research JUCE reported fixed lookahead");
    juce::PluginDescription fastDesc;
    fastHosted.fillInPluginDescription(fastDesc);
    if(fastDesc.pluginFormatName=="APEX Native" ||
       fastDesc.fileOrIdentifier==APEX::TuneLive::Processor::kFileOrIdentifier)
        return fail("experimental half-cycle adapter aliased shipping plugin identity");
    APEX::TuneLive::PitchTracker fastOutput;
    fastOutput.prepare(48000.0);
    for(int frame=0;frame<48000;frame+=128) {
        for(int i=0;i<128;++i) {
            const double t=(frame+i)/48000.0;
            const float sample=static_cast<float>(
                0.26*std::sin(6.283185307179586*447.691*t)+
                0.05*std::sin(6.283185307179586*2.0*447.691*t+0.31)+
                0.018*std::sin(6.283185307179586*3.0*447.691*t+0.13));
            researchAudio.setSample(0,i,sample);
            researchAudio.setSample(1,i,sample);
        }
        fastHosted.processBlock(researchAudio,midi);
        for(int i=0;i<128;++i) {
            const float out=researchAudio.getSample(0,i);
            if(!std::isfinite(out) ||
               !std::isfinite(researchAudio.getSample(1,i)))
                return fail("guarded half-cycle JUCE host emitted nonfinite audio");
            if(frame>=24000)fastOutput.push(out);
        }
    }
    const auto fastResult=fastOutput.latest();
    const double fastCents=fastResult.voiced && fastResult.hz>0.0f
        ?1200.0*std::log2(fastResult.hz/440.0):9999.0;
    const double fastFirstF0Ms=1000.0*fastHosted.firstVoiceSample()/48000.0;
    const double fastHalfWetMs=1000.0*fastHosted.firstHalfWetSample()/48000.0;
    if(!fastResult.voiced || std::abs(fastCents)>18.0 ||
       fastHosted.researchWetMix()<0.95 ||
       fastHosted.firstUltraSample()<0 ||
       fastFirstF0Ms<6.0 || fastFirstF0Ms>7.3 ||
       fastHalfWetMs<fastFirstF0Ms || fastHalfWetMs>9.0)
        return fail("fast guarded research JUCE first note/wet control or F0 outside measured fixture");
    if(plugin.getLatencySamples()!=1920)
        return fail("hosted research altered shipping HQ40 PDC");
    std::cout<<"CAUSAL_JUCE_GUARDED_HALFCYCLE_RESEARCH"
             <<" fixed_lookahead_samples="<<fastHosted.getLatencySamples()
             <<" first_f0_ms="<<fastFirstF0Ms
             <<" first_wet50_control_ms="<<fastHalfWetMs
             <<" corrected_output_cents="<<fastCents
             <<" research_only_no_1ms_pitch_recognition_or_artist_quality=1\n";
    fastHosted.releaseResources();

    editor.reset();
    plugin.releaseResources();
    std::cout<<"PASS: JUCE 8 native runtime plugin description, 11 hosted params, "
                "GUI controls, state, 48k/128 stereo realtime processing "
                "and pitch correction. Input F0="<<hz
             <<"Hz, stereo wet cents from A4="<<cents
             <<", mono wet cents from A4="<<monoCents<<"\n";
    return 0;
}
