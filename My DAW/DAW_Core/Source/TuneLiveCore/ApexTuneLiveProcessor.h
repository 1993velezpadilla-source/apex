#pragma once
#include <JuceHeader.h>
#include "ApexTuneLiveDspCore.h"
#include <array>
#include <atomic>
#include <cmath>

namespace APEX { namespace TuneLive {

// Stable hosted IDs: keep order and names across future versions for automation.
class Parameter final : public juce::AudioProcessorParameterWithID {
public:
    Parameter(const char* id, const char* name, float initial, int steps = 0)
        : juce::AudioProcessorParameterWithID(juce::ParameterID(id, 1), name),
          default_(initial), steps_(steps), value_(initial) {}
    float getValue() const override { return value_.load(std::memory_order_relaxed); }
    void setValue(float v) override {
        if (std::isfinite(v)) value_.store(juce::jlimit(0.0f, 1.0f, v), std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return default_; }
    juce::String getText(float value, int) const override {
        if (steps_ == 2) return value >= 0.5f ? "On" : "Off";
        if (steps_ > 2) return juce::String(juce::jlimit(0, steps_-1, juce::roundToInt(value*(steps_-1))));
        return juce::String(value, 3);
    }
    float getValueForText(const juce::String& text) const override {
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
    static constexpr int kUniqueId = 0x41544c31; // ATL1
    static constexpr const char* kPluginName = "APEX Tune Live";
    static constexpr const char* kFileOrIdentifier = "APEX::TuneLive";
    static constexpr const char* kVersion = "0.1.0-beta";
    enum Index { kAmount, kSpeed, kKey, kScale, kMix, kBypass,
                 kHumanize, kNoteStability, kPreserveFormants, kNumParams };

    Processor() : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        add(kAmount, "tunelive.amount", "Correction Amount", 0.85f);
        add(kSpeed, "tunelive.speed", "Retune Speed", 0.20f);
        add(kKey, "tunelive.key", "Key (C=0)", 0.0f, 12);
        add(kScale, "tunelive.scale", "Scale (0=Chromatic, 1=Major, 2=Minor)", 0.0f, 3);
        add(kMix, "tunelive.mix", "Wet Mix", 1.0f);
        add(kBypass, "tunelive.bypass", "Bypass", 0.0f, 2);
        // APPEND after V1 stable IDs: existing project automation stays compatible.
        add(kHumanize, "tunelive.humanize", "Humanize", 0.35f);
        add(kNoteStability, "tunelive.noteStability", "Note Stability", 0.40f);
        add(kPreserveFormants, "tunelive.preserveFormants", "Preserve Formants", 1.0f, 2);
    }
    const juce::String getName() const override { return kPluginName; }
    void fillInPluginDescription(juce::PluginDescription& d) const override {
        d.name = d.descriptiveName = kPluginName;
        d.pluginFormatName = "APEX Native";
        d.manufacturerName = "APEX";
        d.category = "Pitch Shift";
        d.version = kVersion;
        d.fileOrIdentifier = kFileOrIdentifier;
        d.uniqueId = d.deprecatedUid = kUniqueId;
        d.isInstrument = false;
        d.numInputChannels = d.numOutputChannels = 2;
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
        dsp_.prepare(rate,block,channels);
        setLatencySamples(dsp_.latencySamples());
        lastHz_.store(0.0f,std::memory_order_relaxed);
    }
    void releaseResources() override {dsp_.reset();}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        const DspSettings settings{
            param(kAmount),
            param(kSpeed),
            juce::jlimit(0,11,juce::roundToInt(param(kKey)*11.0f)),
            juce::jlimit(0,2,juce::roundToInt(param(kScale)*2.0f)),
            param(kMix),
            param(kBypass)>=0.5f,
            param(kHumanize),
            param(kNoteStability),
            param(kPreserveFormants)>=0.5f
        };
        dsp_.process(buffer.getArrayOfWritePointers(),buffer.getNumChannels(),
                     buffer.getNumSamples(),settings);
        lastHz_.store(dsp_.frequencyHz(),std::memory_order_relaxed);
    }
    float detectedFrequencyHz() const noexcept {return lastHz_.load(std::memory_order_relaxed);}
    int getLatencyForDisplay() const noexcept {return dsp_.latencySamples();}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
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
            if (!v.hasProperty(id)) continue;
            float raw = static_cast<float>(v.getProperty(id));
            if (std::isfinite(raw)) params_[static_cast<std::size_t>(i)]->setValue(raw);
        }
    }
private:
    inline static constexpr std::array<const char*,kNumParams> kIds = {
        "tunelive.amount", "tunelive.speed", "tunelive.key", "tunelive.scale",
        "tunelive.mix", "tunelive.bypass", "tunelive.humanize",
        "tunelive.noteStability", "tunelive.preserveFormants"};
    void add(int index, const char* id, const char* name, float value, int steps=0) {
        auto p = std::make_unique<Parameter>(id, name, value, steps);
        params_[static_cast<std::size_t>(index)] = p.get();
        addHostedParameter(std::move(p));
    }
    float param(int index) const noexcept {return params_[static_cast<std::size_t>(index)]->getValue();}
    std::array<Parameter*,kNumParams> params_{};
    DspCore dsp_;
    std::atomic<float> lastHz_{0.0f};
};

}} // namespace APEX::TuneLive
