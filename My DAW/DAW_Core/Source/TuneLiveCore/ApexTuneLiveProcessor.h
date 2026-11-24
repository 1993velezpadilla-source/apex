#pragma once
#include <JuceHeader.h>
#include "ApexTuneLiveDspCore.h"
#include "ApexTuneLiveRealtimeCore.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace APEX { namespace TuneLive {

// Stable hosted IDs: keep order and names across future versions for automation.
class TuneLiveParameter final : public juce::AudioProcessorParameterWithID {
public:
    TuneLiveParameter(const char* id, const char* name, float initial, int steps = 0)
        : juce::AudioProcessorParameterWithID(juce::ParameterID(id, 1), name),
          default_(initial), steps_(steps), value_(initial) {}
    float getValue() const override { return value_.load(std::memory_order_relaxed); }
    void setValue(float v) override {
        if (std::isfinite(v)) value_.store(juce::jlimit(0.0f, 1.0f, v), std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return default_; }
    juce::String getText(float value, int) const override {
        if (paramID == "tunelive.vocalRange")
            return rangeInfo(static_cast<VocalRange>(juce::roundToInt(value * (kVocalRanges.size() - 1)))).label;
        if (steps_ == 2) return value >= 0.5f ? "On" : "Off";
        if (steps_ > 2) return juce::String(juce::jlimit(0, steps_-1, juce::roundToInt(value*(steps_-1))));
        return juce::String(value, 3);
    }
    float getValueForText(const juce::String& text) const override {
        if (paramID == "tunelive.vocalRange") {
            for (std::size_t i = 0; i < kVocalRanges.size(); ++i)
                if (text.equalsIgnoreCase(kVocalRanges[i].label))
                    return static_cast<float>(i) / static_cast<float>(kVocalRanges.size() - 1);
        }
        if (steps_ == 2) return text.equalsIgnoreCase("on") || text.getFloatValue() >= 0.5f ? 1.0f : 0.0f;
        if (steps_ > 2) return juce::jlimit(0.0f,1.0f,text.getFloatValue()/static_cast<float>(steps_-1));
        return juce::jlimit(0.0f, 1.0f, text.getFloatValue());
    }
    int getNumSteps() const override { return steps_ > 0 ? steps_ : juce::AudioProcessorParameter::getDefaultNumParameterSteps(); }
    bool isDiscrete() const override { return steps_ > 0; }
    bool isBoolean() const override { return steps_ == 2; }
private:
    const float default_;
    const int steps_;
    std::atomic<float> value_;
};

class Processor final : public juce::AudioPluginInstance {
public:
    enum class Engine { Studio, Tracking };

    static constexpr int kUniqueId = 0x41544c31; // ATL1
    static constexpr const char* kPluginName = "APEX Tune Live";
    static constexpr const char* kFileOrIdentifier = "APEX::TuneLive";
    static constexpr int kTrackingUniqueId = 0x41544c32; // ATL2
    static constexpr const char* kTrackingName = "APEX Tune Live Tracking";
    static constexpr const char* kTrackingIdentifier = "APEX::TuneLiveTracking";
    static constexpr const char* kVersion = "0.2.0-beta";
    enum Index { kAmount, kSpeed, kKey, kScale, kMix, kBypass,
                 kHumanize, kNoteStability, kPreserveFormants, kFlexTune, kA4Reference,
                 kVocalRange, kVibratoPreserve, kExperimentalDeclick, kNumParams };

    explicit Processor(Engine engine = Engine::Studio) : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        tracking_(engine == Engine::Tracking ? std::make_unique<RealtimeCore>() : nullptr) {
        add(kAmount, "tunelive.amount", "Correction Amount", 0.85f);
        add(kSpeed, "tunelive.speed", "Retune Speed", 0.20f);
        add(kKey, "tunelive.key", "Key (C=0)", 0.0f, 12);
        add(kScale, "tunelive.scale", "Scale (0=Chromatic, 1=Major, 2=Minor)", 0.0f, 3);
        add(kMix, "tunelive.mix", "Wet Mix", 1.0f);
        add(kBypass, "tunelive.bypass", "Bypass", 0.0f, 2);
        // APPEND after V1 stable IDs: existing project automation stays compatible.
        add(kHumanize, "tunelive.humanize", "Humanize", 0.70f);
        add(kNoteStability, "tunelive.noteStability", "Note Stability", 0.40f);
        add(kPreserveFormants, "tunelive.preserveFormants", "Preserve Formants", 1.0f, 2);
        add(kFlexTune, "tunelive.flexTune", "Flex Tune", 0.35f);
        add(kA4Reference, "tunelive.a4Reference", "A4 Reference (430-450 Hz)", 0.50f);
        add(kVocalRange, "tunelive.vocalRange", "Vocal Range", 0.0f, static_cast<int>(kVocalRanges.size()));
        add(kVibratoPreserve, "tunelive.vibratoPreserve", "Vibrato Preservation", isTracking() ? 0.85f : 0.0f);
        // Appended ID keeps existing APEX projects and automations stable.
        // OFF by default; evaluated on two licensed human singers and not
        // promoted to the regular renderer without Android monitoring QA.
        add(kExperimentalDeclick, "tunelive.experimentalDeclick", "Anti-Click A/B Research", 0.0f, 2);
    }
    bool isTracking() const noexcept { return tracking_ != nullptr; }
    const juce::String getName() const override { return isTracking() ? kTrackingName : kPluginName; }
    static juce::PluginDescription descriptionFor(Engine engine) {
        const bool tracking = engine == Engine::Tracking;
        juce::PluginDescription d;
        d.name = d.descriptiveName = tracking ? kTrackingName : kPluginName;
        d.pluginFormatName = "APEX Native";
        d.manufacturerName = "APEX";
        d.category = "Pitch Shift";
        d.version = kVersion;
        d.fileOrIdentifier = tracking ? kTrackingIdentifier : kFileOrIdentifier;
        d.uniqueId = d.deprecatedUid = tracking ? kTrackingUniqueId : kUniqueId;
        d.isInstrument = false;
        d.numInputChannels = d.numOutputChannels = 2;
        return d;
    }
    void fillInPluginDescription(juce::PluginDescription& d) const override {
        d = descriptionFor(isTracking() ? Engine::Tracking : Engine::Studio);
    }
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        const auto in = l.getMainInputChannelSet();
        return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
            && in == l.getMainOutputChannelSet();
    }
    void prepareToPlay(double sampleRate, int maximumBlockSize) override {
        const double rate=std::isfinite(sampleRate) && sampleRate>8000.0?sampleRate:48000.0;
        const int block=juce::jmax(1, maximumBlockSize);
        const int channels=juce::jlimit(1,2,getTotalNumInputChannels());
        setRateAndBufferSizeDetails(rate,block);
        int latency = 0;
        if (tracking_) {
            tracking_->prepare(rate, channels);
            tracking_->setResearchPhaseSlew(param(kExperimentalDeclick) >= 0.5f);
            latency = tracking_->latencySamples();
        } else {
            dsp_.prepare(rate, block, channels);
            latency = dsp_.latencySamples();
        }
        latencyForDisplay_.store(latency, std::memory_order_relaxed);
        setLatencySamples(latency);
        lastHz_.store(0.0f, std::memory_order_relaxed);
        lastShift_.store(0.0f, std::memory_order_relaxed);
    }
    void releaseResources() override { reset(); }
    void reset() override {
        if (tracking_)
            tracking_->reset();
        else
            dsp_.reset();
        lastHz_.store(0.0f, std::memory_order_relaxed);
        lastShift_.store(0.0f, std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        render(buffer, false);
    }
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        render(buffer, true);
    }
    float detectedFrequencyHz() const noexcept { return lastHz_.load(std::memory_order_relaxed); }
    float appliedShiftSemitones() const noexcept { return lastShift_.load(std::memory_order_relaxed); }
    int getLatencyForDisplay() const noexcept { return latencyForDisplay_.load(std::memory_order_relaxed); }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override; // APEX native custom UI below
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override { return params_[kBypass]; }
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::ValueTree v("ApexTuneLiveState");
        v.setProperty("schema", 1, nullptr);
        for (int i=0; i<kNumParams; ++i)
            v.setProperty(juce::Identifier(kIds[static_cast<std::size_t>(i)]), param(i), nullptr);
        juce::MemoryOutputStream stream(dest, false);
        v.writeToStream(stream);
    }
    void setStateInformation(const void* data, int size) override {
        if (!data || size <= 0) return;
        auto v = juce::ValueTree::readFromData(data, static_cast<size_t>(size));
        if (!v.hasType("ApexTuneLiveState")) return;
        for (int i=0;i<kNumParams;++i) {
            juce::Identifier id(kIds[static_cast<std::size_t>(i)]);
            if (!v.hasProperty(id)) {
                if (i >= kVocalRange)
                    params_[static_cast<std::size_t>(i)]->setValue(params_[static_cast<std::size_t>(i)]->getDefaultValue());
                continue;
            }
            float raw = static_cast<float>(v.getProperty(id));
            if (std::isfinite(raw)) params_[static_cast<std::size_t>(i)]->setValue(raw);
        }
    }
private:
    void render(juce::AudioBuffer<float>& buffer, bool hostBypass) {
        const DspSettings settings{
            param(kAmount),
            430.0f+20.0f*param(kA4Reference),
            param(kSpeed),
            juce::roundToInt(param(kKey)*11.0f),
            juce::roundToInt(param(kScale)*2.0f),
            param(kMix),
            hostBypass || param(kBypass)>=0.5f,
            param(kHumanize),
            param(kFlexTune),
            param(kNoteStability),
            param(kPreserveFormants)>=0.5f,
            static_cast<VocalRange>(juce::roundToInt(param(kVocalRange)*(kVocalRanges.size()-1))),
            param(kVibratoPreserve)
        };
        auto* data = buffer.getArrayOfWritePointers();
        const int channels = buffer.getNumChannels();
        const int samples = buffer.getNumSamples();
        if (tracking_) {
            // JUCE parameter read uses relaxed atomic load, no heap work on
            // the audio callback. Switching modes is for controlled A/B;
            // the released/default path remains unchanged when OFF.
            tracking_->setResearchPhaseSlew(param(kExperimentalDeclick) >= 0.5f);
            tracking_->process(data, channels, samples, settings);
            lastHz_.store(tracking_->frequencyHz(), std::memory_order_relaxed);
            lastShift_.store(static_cast<float>(tracking_->appliedShiftSemitones()), std::memory_order_relaxed);
        } else {
            dsp_.process(data, channels, samples, settings);
            lastHz_.store(dsp_.frequencyHz(), std::memory_order_relaxed);
            lastShift_.store(dsp_.debugAppliedShiftSemitones(), std::memory_order_relaxed);
        }
    }
    inline static constexpr std::array<const char*,kNumParams> kIds = {
        "tunelive.amount", "tunelive.speed", "tunelive.key", "tunelive.scale",
        "tunelive.mix", "tunelive.bypass", "tunelive.humanize",
        "tunelive.noteStability", "tunelive.preserveFormants",
        "tunelive.flexTune", "tunelive.a4Reference", "tunelive.vocalRange",
        "tunelive.vibratoPreserve", "tunelive.experimentalDeclick"};
    void add(int index, const char* id, const char* name, float value, int steps=0) {
        auto p = std::make_unique<TuneLiveParameter>(id, name, value, steps);
        params_[static_cast<std::size_t>(index)] = p.get();
        addHostedParameter(std::move(p));
    }
    float param(int index) const noexcept {return params_[static_cast<std::size_t>(index)]->getValue();}
    std::array<TuneLiveParameter*,kNumParams> params_{};
    // Engine choice is a stable native insert identity, fixed before prepare.
    DspCore dsp_;
    const std::unique_ptr<RealtimeCore> tracking_;
    std::atomic<float> lastHz_{0.0f}, lastShift_{0.0f};
    std::atomic<int> latencyForDisplay_{0};
};

}} // namespace APEX::TuneLive

#include "../TuneLiveUI/ApexTuneLiveEditor.h"
inline juce::AudioProcessorEditor* APEX::TuneLive::Processor::createEditor()
{
    return new APEX::TuneLive::Editor (*this);
}
