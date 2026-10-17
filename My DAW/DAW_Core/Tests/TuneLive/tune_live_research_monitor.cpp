// APEX Tune Live Research Monitor — intentionally separate Windows GUI EXE.
// No APEX Native registration or HQ40 changes. Mono input -> dual wet output.
// This prototype is NOT an Apollo ASIO or <1ms correction certification.
//
// SAFETY: audio passthrough is opt-in, explicit headphone acknowledgement;
// neither OS default recording nor direct monitoring are disabled here.
// No file I/O, allocations or audio-device control inside the audio callback.
// Stop callback before exporting the original DRY PCM take to WAV.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace {
using APEX::TuneLive::Research::HarmonicLivePrototype;

APEX::TuneLive::VoiceSettings hardTuneSettings() {
    APEX::TuneLive::VoiceSettings s;
    s.root=9; // A chromatic, demo-only; not a configurable DAW pitch preset.
    s.scale=0;
    s.correction=1.0f;
    s.referenceHz=440.0f;
    s.humanize=0.0f;
    s.flexTune=0.0f;
    s.noteHysteresis=0.10f;
    return s;
}
void configure(HarmonicLivePrototype& engine,double rate) {
    engine.prepare(rate,1);
    engine.setVoiceSettings(hardTuneSettings());
    engine.enableEarlyOnset(true);
    engine.enableHighRegisterOnset(true);
    engine.enableUltraHighRegisterOnset(true);
    engine.enableUltraHighFastHopResearch(true);
    engine.enableUltraLeapConsensusResearch(true);
    engine.enableMidRegisterOnset(true);
    engine.enableExperimentalEarlyTransitions(true);
    engine.enableFastEngagement(true);
    engine.enableHalfPeriodEngagementResearch(true);
    engine.enableResidualPreservation(true);
}

// Standalone deterministic CI test; NEVER requires/discovers a microphone.
int selfTest() {
    constexpr int rate=48000;
    HarmonicLivePrototype engine;
    configure(engine,rate);
    APEX::TuneLive::PitchTracker independent;
    independent.prepare(rate);
    constexpr int samples=rate;
    constexpr int block=128;
    std::array<float,block> audio{};
    bool finite=true;
    const double source=440.0*std::pow(2.0,30.0/1200.0);
    for(int frame=0;frame<samples;frame+=block) {
        for(int i=0;i<block;++i) {
            const double phase=6.283185307179586*source*(frame+i)/rate;
            audio[static_cast<std::size_t>(i)]=static_cast<float>(
                0.26*std::sin(phase)+0.050*std::sin(2.0*phase+0.31)+
                0.018*std::sin(3.0*phase+0.13));
        }
        float* ptr[1]={audio.data()};
        engine.process(ptr,1,block);
        for(int i=0;i<block;++i) {
            finite &=std::isfinite(audio[static_cast<std::size_t>(i)]);
            if(frame>=24000)independent.push(audio[static_cast<std::size_t>(i)]);
        }
    }
    const auto pitch=independent.latest();
    const double cents=pitch.voiced && pitch.hz>0.0f
       ?1200.0*std::log2(pitch.hz/440.0):9999.0;
    const double f0Ms=1000.0*engine.firstVoiceSample()/rate;
    const double wet50Ms=1000.0*engine.firstHalfWetSample()/rate;
    const bool passed=finite && std::abs(cents)<18.0 &&
        engine.researchWetMix()>0.95 &&
        f0Ms>=6.0 && f0Ms<8.0 &&
        wet50Ms>f0Ms && wet50Ms<9.2;
    juce::Logger::writeToLog(
       "APEX_RESEARCH_MONITOR_SELFTEST pass="+juce::String(passed?1:0)+
       " f0_ms="+juce::String(f0Ms,3)+
       " control_wet50_ms="+juce::String(wet50Ms,3)+
       " corrected_output_cents="+juce::String(cents,3)+
       " NO_1MS_TOTAL_VOICE_CLAIM=1");
    return passed?0:2;
}

class LiveCallback final : public juce::AudioIODeviceCallback {
public:
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        armed_.store(false,std::memory_order_release);
        if(!device)return;
        sampleRate_=device->getCurrentSampleRate();
        const int rate=juce::roundToInt(sampleRate_);
        if(rate<8000||rate>192000)return;
        const std::size_t capacity=static_cast<std::size_t>(
            std::max(8192,device->getCurrentBufferSizeSamples()));
        scratch_.assign(capacity,0.0f);
        // Deliberately capped to 60s to avoid unbounded memory use.
        const std::size_t maxTake=static_cast<std::size_t>(rate)*60U;
        dryTake_.assign(maxTake,0.0f);
        recorded_.store(0,std::memory_order_relaxed);
        configure(engine_,sampleRate_);
        armed_.store(true,std::memory_order_release);
    }
    void audioDeviceStopped() override {
        armed_.store(false,std::memory_order_release);
    }
    void audioDeviceIOCallbackWithContext(
        const float* const* input,int inChannels,
        float* const* output,int outChannels,int frames,
        const juce::AudioIODeviceCallbackContext&) override {
        if(frames<=0)return;
        for(int c=0;c<outChannels;++c)
            if(output && output[c])
                std::fill_n(output[c],frames,0.0f);
        if(!armed_.load(std::memory_order_acquire) ||
           !input || inChannels<1 || !input[0] ||
           !output || outChannels<1 ||
           static_cast<std::size_t>(frames)>scratch_.size())
            return;
        const std::size_t first=recorded_.load(std::memory_order_relaxed);
        for(int i=0;i<frames;++i) {
            const float raw=input[0][i];
            const float dry=std::isfinite(raw)?std::clamp(raw,-1.0f,1.0f):0.0f;
            scratch_[static_cast<std::size_t>(i)]=dry;
            const auto pos=first+static_cast<std::size_t>(i);
            if(pos<dryTake_.size())dryTake_[pos]=dry;
        }
        recorded_.store(std::min(first+static_cast<std::size_t>(frames),
                                 dryTake_.size()),std::memory_order_release);
        float* mono[1]={scratch_.data()};
        engine_.process(mono,1,frames);
        for(int c=0;c<outChannels;++c) {
            if(!output[c])continue;
            for(int i=0;i<frames;++i) {
                // Conservative headphone monitor gain; not a limiter.
                const float sample=scratch_[static_cast<std::size_t>(i)]*0.35f;
                output[c][i]=std::isfinite(sample)?std::clamp(sample,-1.0f,1.0f):0.0f;
            }
        }
    }
    // Caller MUST have removed this callback from the AudioDeviceManager.
    juce::String exportDryWav() {
        const auto count=recorded_.load(std::memory_order_acquire);
        if(count==0 || sampleRate_<8000.0)return "No microphone audio captured.";
        auto folder=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                       .getChildFile("APEX Tune Live Research Takes");
        if(!folder.createDirectory())return "Cannot create Documents take directory.";
        const auto filename="DRY_original_"+juce::Time::getCurrentTime()
           .formatted("%Y%m%d_%H%M%S")+".wav";
        const auto target=folder.getNonexistentChildFile(
             filename.upToLastOccurrenceOf(".",false,false),".wav");
        auto stream=std::unique_ptr<juce::FileOutputStream>(target.createOutputStream());
        if(!stream)return "Could not open DRY take for writing.";
        juce::WavAudioFormat wave;
        const auto hz=static_cast<double>(juce::roundToInt(sampleRate_));
        auto writer=std::unique_ptr<juce::AudioFormatWriter>(
            wave.createWriterFor(stream.get(),hz,1,24,{},0));
        if(!writer)return "Cannot create PCM24 WAV writer.";
        stream.release(); // AudioFormatWriter assumes ownership of stream.
        const float* channels[]={dryTake_.data()};
        if(!writer->writeFromFloatArrays(channels,1,
                  static_cast<int>(count)))return "Failed to write complete DRY take.";
        writer->flush();
        writer.reset();
        recorded_.store(0,std::memory_order_relaxed);
        return "Saved original DRY WAV: "+target.getFullPathName();
    }
    int capturedSeconds() const noexcept {
        if(sampleRate_<8000.0)return 0;
        return static_cast<int>(recorded_.load(std::memory_order_acquire)/
                                sampleRate_);
    }
private:
    HarmonicLivePrototype engine_;
    std::vector<float> scratch_,dryTake_;
    std::atomic<bool> armed_{false};
    std::atomic<std::size_t> recorded_{0};
    double sampleRate_=0.0;
};

class PreviewPanel final : public juce::Component {
public:
    PreviewPanel()
      :selector_(devices_,1,2,1,2,false,false,true,false) {
        devices_.initialise(1,2,nullptr,true);
        addAndMakeVisible(title_);
        title_.setText("APEX TUNE LIVE  |  RESEARCH MONITOR",
                       juce::dontSendNotification);
        title_.setFont(juce::Font(22.0f,juce::Font::bold));
        title_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(warning_);
        warning_.setText(
          "EXPERIMENTAL / NOT APEX DAW. HEADPHONES ONLY. Disable Apollo direct "
          "monitoring to avoid dry + tuned echo. This preview may produce "
          "artifacts. Begin at low headphone volume. No ASIO or 1ms guarantee.",
          juce::dontSendNotification);
        warning_.setColour(juce::Label::textColourId,juce::Colours::orange);
        warning_.setJustificationType(juce::Justification::topLeft);
        warning_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(selector_);
        addAndMakeVisible(headphones_);
        headphones_.setButtonText("Headphones connected; direct monitoring disabled");
        headphones_.onClick=[this]{start_.setEnabled(
            headphones_.getToggleState() && !listening_);};
        addAndMakeVisible(start_);
        start_.setButtonText("START: MONITOR TUNED + CAPTURE DRY");
        start_.setEnabled(false);
        start_.onClick=[this]{startListening();};
        addAndMakeVisible(stop_);
        stop_.setButtonText("STOP + SAVE ORIGINAL DRY WAV");
        stop_.setEnabled(false);
        stop_.onClick=[this]{stopListening();};
        addAndMakeVisible(status_);
        status_.setText("STOPPED. Choose device and input/output channels first.",
                        juce::dontSendNotification);
        status_.setJustificationType(juce::Justification::topLeft);
        setSize(900,690);
    }
    ~PreviewPanel() override {
        devices_.removeAudioCallback(&callback_);
    }
    void resized() override {
        auto area=getLocalBounds().reduced(18);
        title_.setBounds(area.removeFromTop(40));
        warning_.setBounds(area.removeFromTop(76));
        area.removeFromTop(8);
        auto bottom=area.removeFromBottom(148);
        selector_.setBounds(area);
        headphones_.setBounds(bottom.removeFromTop(34));
        auto buttons=bottom.removeFromTop(45);
        start_.setBounds(buttons.removeFromLeft(buttons.getWidth()/2).reduced(3));
        stop_.setBounds(buttons.reduced(3));
        status_.setBounds(bottom);
    }
private:
    void startListening() {
        if(listening_ || !headphones_.getToggleState())return;
        const auto* device=devices_.getCurrentAudioDevice();
        if(!device) {
            status_.setText("ERROR: select and enable a duplex audio device.",
                            juce::dontSendNotification);
            return;
        }
        // The callback only turns on after the explicit consent toggle.
        devices_.addAudioCallback(&callback_);
        listening_=true;
        start_.setEnabled(false);
        stop_.setEnabled(true);
        headphones_.setEnabled(false);
        selector_.setEnabled(false);
        status_.setText(
             "LIVE RESEARCH MONITOR ON. Saves up to 60 seconds original DRY "
             "on Stop. Never enable physical direct monitor simultaneously.",
             juce::dontSendNotification);
    }
    void stopListening() {
        if(!listening_)return;
        devices_.removeAudioCallback(&callback_);
        listening_=false;
        headphones_.setEnabled(true);
        selector_.setEnabled(true);
        start_.setEnabled(headphones_.getToggleState());
        stop_.setEnabled(false);
        status_.setText(callback_.exportDryWav(),juce::dontSendNotification);
    }
    juce::AudioDeviceManager devices_;
    LiveCallback callback_;
    juce::AudioDeviceSelectorComponent selector_;
    juce::Label title_,warning_,status_;
    juce::ToggleButton headphones_;
    juce::TextButton start_,stop_;
    bool listening_=false;
};

class PreviewApp final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {
        return "APEX Tune Live Research Monitor";
    }
    const juce::String getApplicationVersion() override {return "0.1-research";}
    bool moreThanOneInstanceAllowed() override {return false;}
    void initialise(const juce::String& args) override {
        if(args.contains("--self-test")) {
            setApplicationReturnValue(selfTest());
            quit();
            return;
        }
        window_=std::make_unique<Window>();
    }
    void shutdown() override {window_.reset();}
private:
    class Window final : public juce::DocumentWindow {
    public:
        Window() :juce::DocumentWindow("APEX Tune Live - RESEARCH ONLY",
            juce::Colours::darkslategrey,juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new PreviewPanel(),true);
            setResizable(false,false);
            centreWithSize(getWidth(),getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<Window> window_;
};
}

START_JUCE_APPLICATION(PreviewApp)
