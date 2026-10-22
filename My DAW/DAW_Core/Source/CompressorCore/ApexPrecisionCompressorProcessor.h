#pragma once

// APEX Precision C3: separate native dynamics insert, never replaces Opto 2A.
// All parameters are host-visible with stable IDs for session persistence and
// APEX automation. Editor is compiled inline at the bottom of this header.
#include <JuceHeader.h>
#include "ApexPrecisionCompressorCore.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace APEX::PrecisionC3
{
enum ParameterIndex : int
{
    Threshold = 0, Ratio, Knee, Range, Attack, Release, Hold, Input, Wet,
    Dry, Output, SidechainHP, SidechainLP, StereoLink, Drive, Style,
    Character, AutoThreshold, AutoRelease, AutoGain, Bypass, ParameterCount
};

class PrecisionC3HostedParameter final : public juce::AudioProcessorParameterWithID
{
public:
    PrecisionC3HostedParameter (const char* id, const char* name, float lo, float hi,
                     float initial, int steps = 0, const char* unit = "")
        : AudioProcessorParameterWithID (juce::ParameterID (id, 1), name),
          low_(lo), high_(hi), initial_(initial), steps_(steps), unit_(unit)
    {
        normalised_.store(toNormalised(initial), std::memory_order_relaxed);
    }
    float getValue() const override { return normalised_.load(std::memory_order_relaxed); }
    void setValue(float value) override
    {
        if (!std::isfinite(value)) return;
        value = juce::jlimit(0.0f, 1.0f, value);
        if (steps_ > 1) value = std::round(value * (steps_ - 1)) / (steps_ - 1);
        normalised_.store(value, std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return toNormalised(initial_); }
    float toNormalised (float value) const noexcept
    {
        return juce::jlimit(0.0f, 1.0f, (value - low_) / (high_ - low_));
    }
    float fromNormalised (float normal) const noexcept
    {
        return low_ + (high_ - low_) * normal;
    }
    float getUnits() const noexcept { return fromNormalised(getValue()); }
    juce::String getText (float normal, int) const override
    {
        if (steps_ == 2) return normal >= 0.5f ? "On" : "Off";
        const int index = steps_ > 1
            ? juce::jlimit(0, steps_ - 1,
                static_cast<int>(std::lround(normal * (steps_ - 1)))) : 0;
        if (steps_ == 14) return kStyleNames[static_cast<std::size_t>(index)];
        if (steps_ == 4)
        {
            constexpr const char* names[] {"Off", "Tube", "Diode", "Bright"};
            return names[index];
        }
        return juce::String(fromNormalised(normal), 2) + unit_;
    }
    float getValueForText (const juce::String& value) const override
    {
        if (steps_ == 2) return value.equalsIgnoreCase("On") || value == "1" ? 1.0f : 0.0f;
        if (steps_ == 14)
            for (int i = 0; i < 14; ++i)
                if (value.equalsIgnoreCase(kStyleNames[static_cast<std::size_t>(i)]))
                    return static_cast<float>(i) / 13.0f;
        if (steps_ == 4)
        {
            constexpr const char* names[] {"Off", "Tube", "Diode", "Bright"};
            for (int i = 0; i < 4; ++i)
                if (value.equalsIgnoreCase(names[i])) return i / 3.0f;
        }
        return toNormalised(value.getFloatValue());
    }
    int getNumSteps() const override
    {
        return steps_ > 1 ? steps_ : juce::AudioProcessorParameter::getDefaultNumParameterSteps();
    }
    bool isDiscrete() const override { return steps_ > 1; }
    bool isBoolean() const override { return steps_ == 2; }
    juce::String getLabel() const override { return unit_; }
private:
    const float low_, high_, initial_;
    const int steps_;
    const juce::String unit_;
    std::atomic<float> normalised_ {0.0f};
};
static_assert(std::atomic<float>::is_always_lock_free,
              "APEX Precision C3 requires lock-free hosted parameter reads");

class Processor final : public juce::AudioPluginInstance
{
public:
    static constexpr int kUniqueId = 0x41504333; // APC3
    static constexpr const char* kPluginName = "APEX Precision C3";
    static constexpr const char* kFormatName = "APEX Native";
    static constexpr const char* kFileOrIdentifier = "APEX::PrecisionC3";
    static constexpr const char* kCategory = "Dynamics";
    static constexpr const char* kManufacturer = "APEX";
    static constexpr const char* kVersion = "0.1.0-preview";
    static constexpr const char* kStateTag = "apex-precision-c3-v1";

    Processor()
        : juce::AudioPluginInstance(juce::AudioProcessor::BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        add(Threshold,     "c3.threshold", "Threshold", -60, 0, -18, 0, " dB");
        add(Ratio,         "c3.ratio", "Ratio", 1, 40, 4, 0, ":1");
        add(Knee,          "c3.knee", "Knee", 0, 72, 12, 0, " dB");
        add(Range,         "c3.range", "Range", 0, 60, 60, 0, " dB");
        add(Attack,        "c3.attack", "Attack", .005f, 250, 10, 0, " ms");
        add(Release,       "c3.release", "Release", 5, 3000, 120, 0, " ms");
        add(Hold,          "c3.hold", "Hold", 0, 500, 0, 0, " ms");
        add(Input,         "c3.input", "Input", -24, 24, 0, 0, " dB");
        add(Wet,           "c3.wet", "Wet Gain", -24, 24, 0, 0, " dB");
        add(Dry,           "c3.dry", "Dry", 0, 100, 0, 0, " %");
        add(Output,        "c3.output", "Output", -24, 24, 0, 0, " dB");
        add(SidechainHP,   "c3.schp", "SC High-pass", 0, 2000, 0, 0, " Hz");
        add(SidechainLP,   "c3.sclp", "SC Low-pass", 100, 20000, 20000, 0, " Hz");
        add(StereoLink,    "c3.link", "Stereo Link", 0, 100, 100, 0, " %");
        add(Drive,         "c3.drive", "Drive", 0, 100, 0, 0, " %");
        add(Style,         "c3.style", "Style", 0, 13, 0, 14);
        add(Character,     "c3.character", "Character", 0, 3, 0, 4);
        add(AutoThreshold, "c3.autothr", "Auto Threshold", 0, 1, 0, 2);
        add(AutoRelease,   "c3.autorel", "Auto Release", 0, 1, 0, 2);
        add(AutoGain,      "c3.autogain", "Auto Gain", 0, 1, 0, 2);
        add(Bypass,        "c3.bypass", "Bypass", 0, 1, 0, 2);
    }

    const juce::String getName() const override { return kPluginName; }
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name=kPluginName; d.descriptiveName=kPluginName;
        d.pluginFormatName=kFormatName; d.category=kCategory;
        d.manufacturerName=kManufacturer; d.version=kVersion;
        d.fileOrIdentifier=kFileOrIdentifier; d.uniqueId=kUniqueId;
        d.deprecatedUid=kUniqueId; d.isInstrument=false;
        d.numInputChannels=2; d.numOutputChannels=2;
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto input=l.getMainInputChannelSet();
        return input == l.getMainOutputChannelSet()
            && (input == juce::AudioChannelSet::mono()
                || input == juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double sampleRate, int blockSize) override
    {
        setRateAndBufferSizeDetails(sampleRate, blockSize);
        engine_.prepare(sampleRate);
        engine_.setParameters(readParameters());
        meterInput_.store(-120.0f, std::memory_order_relaxed);
        meterOutput_.store(-120.0f, std::memory_order_relaxed);
        meterGR_.store(0.0f, std::memory_order_relaxed);
        meterHistoryCounter_.store(0, std::memory_order_release);
        setLatencySamples(0); // no hidden lookahead/PDC in this preview
    }
    void releaseResources() override {}
    void reset() override { engine_.reset(); }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int channels=buffer.getNumChannels();
        if (channels < 1 || buffer.getNumSamples() <= 0) return;
        engine_.setParameters(readParameters());
        engine_.process(buffer.getWritePointer(0),
                        channels > 1 ? buffer.getWritePointer(1) : nullptr,
                        buffer.getNumSamples());
        meterInput_.store(static_cast<float>(engine_.getInputPeakDb()),
                          std::memory_order_relaxed);
        meterOutput_.store(static_cast<float>(engine_.getOutputPeakDb()),
                           std::memory_order_relaxed);
        meterGR_.store(static_cast<float>(engine_.getGainReductionDb()),
                       std::memory_order_relaxed);
        // One atomic frame per processed buffer. No allocation or locking.
        const auto write = meterHistoryCounter_.load(std::memory_order_relaxed);
        const auto cell = static_cast<std::size_t>(write % kMeterHistoryLength);
        meterHistoryInput_[cell].store(inputLevel(), std::memory_order_relaxed);
        meterHistoryOutput_[cell].store(outputLevel(), std::memory_order_relaxed);
        meterHistoryGr_[cell].store(gainReduction(), std::memory_order_relaxed);
        meterHistoryCounter_.store(write + 1, std::memory_order_release);
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return precisionParameters_[Bypass]; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    PrecisionC3HostedParameter& getC3Parameter(int index) noexcept { return *precisionParameters_[static_cast<std::size_t>(index)]; }
    float gainReduction() const noexcept { return meterGR_.load(std::memory_order_relaxed); }
    float inputLevel() const noexcept { return meterInput_.load(std::memory_order_relaxed); }
    float outputLevel() const noexcept { return meterOutput_.load(std::memory_order_relaxed); }

    // Read-only GUI history of the ACTUAL compressor DSP meters. Each atomic
    // cell can be read without blocking the audio callback.
    static constexpr std::size_t kMeterHistoryLength = 512;
    std::uint32_t meterHistoryCount() const noexcept
    {
        return meterHistoryCounter_.load(std::memory_order_acquire);
    }
    void meterHistoryFrame(std::uint32_t sequence, float& in,
                           float& out, float& gr) const noexcept
    {
        const auto cell=static_cast<std::size_t>(sequence % kMeterHistoryLength);
        in=meterHistoryInput_[cell].load(std::memory_order_relaxed);
        out=meterHistoryOutput_[cell].load(std::memory_order_relaxed);
        gr=meterHistoryGr_[cell].load(std::memory_order_relaxed);
    }

    void getStateInformation (juce::MemoryBlock& destination) override
    {
        juce::ValueTree state(kStateTag);
        state.setProperty("schema", 1, nullptr);
        for (int i=0; i<ParameterCount; ++i)
            state.setProperty(juce::Identifier(parameterIds_[static_cast<std::size_t>(i)]),
                precisionParameters_[static_cast<std::size_t>(i)]->getValue(), nullptr);
        juce::MemoryOutputStream stream(destination, false);
        state.writeToStream(stream);
    }
    void setStateInformation (const void* data, int numBytes) override
    {
        if (data == nullptr || numBytes <= 0) return;
        const auto state=juce::ValueTree::readFromData(data, static_cast<std::size_t>(numBytes));
        if (!state.isValid() || !state.hasType(juce::Identifier(kStateTag))) return;
        for (int i=0; i<ParameterCount; ++i)
        {
            const auto id=juce::Identifier(parameterIds_[static_cast<std::size_t>(i)]);
            if (!state.hasProperty(id)) continue;
            const auto normalised=static_cast<float>(state.getProperty(id));
            if (std::isfinite(normalised))
                precisionParameters_[static_cast<std::size_t>(i)]->setValue(normalised);
        }
    }
private:
    void add (int index, const char* id, const char* name, float lo, float hi,
              float initial, int steps=0, const char* unit="")
    {
        auto p=std::make_unique<PrecisionC3HostedParameter>(id,name,lo,hi,initial,steps,unit);
        precisionParameters_[static_cast<std::size_t>(index)]=p.get();
        addHostedParameter(std::move(p));
    }
    Parameters readParameters() const noexcept
    {
        auto value=[this](int index) noexcept
        {
            return static_cast<double>(
                precisionParameters_[static_cast<std::size_t>(index)]->getUnits());
        };
        Parameters p;
        p.thresholdDb=value(Threshold);
        p.ratio=value(Ratio);
        p.kneeDb=value(Knee);
        p.rangeDb=value(Range);
        p.attackMs=value(Attack);
        p.releaseMs=value(Release);
        p.holdMs=value(Hold);
        p.inputDb=value(Input);
        p.wetDb=value(Wet);
        p.dryPercent=value(Dry);
        p.outputDb=value(Output);
        p.sidechainHighpassHz=value(SidechainHP);
        p.sidechainLowpassHz=value(SidechainLP);
        p.stereoLinkPercent=value(StereoLink);
        p.drive=value(Drive);
        p.style=static_cast<int>(std::lround(value(Style)));
        p.character=static_cast<int>(std::lround(value(Character)));
        p.autoThreshold=value(AutoThreshold)>=0.5;
        p.autoRelease=value(AutoRelease)>=0.5;
        p.autoGain=value(AutoGain)>=0.5;
        p.bypass=value(Bypass)>=0.5;
        return p;
    }
    static constexpr std::array<const char*,ParameterCount> parameterIds_ {{
        "c3.threshold","c3.ratio","c3.knee","c3.range","c3.attack",
        "c3.release","c3.hold","c3.input","c3.wet","c3.dry",
        "c3.output","c3.schp","c3.sclp","c3.link","c3.drive",
        "c3.style","c3.character","c3.autothr","c3.autorel",
        "c3.autogain","c3.bypass"
    }};
    std::array<PrecisionC3HostedParameter*,ParameterCount> precisionParameters_ {};
    Core engine_;
    std::atomic<float> meterGR_ {0.0f};
    std::atomic<float> meterInput_ {-120.0f};
    std::atomic<float> meterOutput_ {-120.0f};
    std::array<std::atomic<float>,kMeterHistoryLength> meterHistoryInput_ {};
    std::array<std::atomic<float>,kMeterHistoryLength> meterHistoryOutput_ {};
    std::array<std::atomic<float>,kMeterHistoryLength> meterHistoryGr_ {};
    std::atomic<std::uint32_t> meterHistoryCounter_ {0};
};
} // namespace APEX::PrecisionC3

#include "ApexPrecisionCompressorEditor.h"
