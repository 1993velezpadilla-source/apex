#include <JuceHeader.h>

namespace
{
class APEXTestVST3AutomationGainParameter final : public juce::AudioProcessorParameterWithID
{
public:
    APEXTestVST3AutomationGainParameter()
        : juce::AudioProcessorParameterWithID("gain", "Gain", "Gain") {}

    float getValue() const override { return gain_.load(std::memory_order_relaxed); }
    void setValue(float newValue) override
    {
        gain_.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName(int) const override { return "Gain"; }
    juce::String getLabel() const override { return {}; }
    float getValueForText(const juce::String& text) const override
    {
        return juce::jlimit(0.0f, 1.0f, text.getFloatValue());
    }
    int getNumSteps() const override { return juce::AudioProcessor::getDefaultNumParameterSteps(); }

    float currentValue() const noexcept { return gain_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> gain_ { 0.5f };
};

class APEXTestVST3AutomationParam1 final : public juce::AudioProcessorParameterWithID
{
public:
    APEXTestVST3AutomationParam1()
        : juce::AudioProcessorParameterWithID("param_1", "Param 1", "Param 1") {}

    float getValue() const override { return value_.load(std::memory_order_relaxed); }
    void setValue(float newValue) override
    {
        value_.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_relaxed);
    }
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName(int) const override { return "Param 1"; }
    juce::String getLabel() const override { return {}; }
    float getValueForText(const juce::String& text) const override
    {
        return juce::jlimit(0.0f, 1.0f, text.getFloatValue());
    }
    int getNumSteps() const override { return juce::AudioProcessor::getDefaultNumParameterSteps(); }

    float currentValue() const noexcept { return value_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> value_ { 0.5f };
};

class APEXTestVST3AutomationProcessor final : public juce::AudioProcessor
{
public:
    APEXTestVST3AutomationProcessor()
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter(gainParameter_ = new APEXTestVST3AutomationGainParameter());
        // The second parameter is deliberately neutral at its default so all
        // existing param_0/E2A fixture expectations remain unchanged. It
        // independently scales the right channel when automated, making
        // ordinal-1 delivery observable without changing the gain contract.
        addParameter(param1_ = new APEXTestVST3AutomationParam1());
        // Zero plugin latency: the sample-accurate boundary is directly
        // observable in the worker output without latency arithmetic.
        setLatencySamples(0);
    }

    const juce::String getName() const override { return "APEX Test VST3 Automation"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void reset() override {}

    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const float gain = gainParameter_ != nullptr
            ? gainParameter_->currentValue() : 0.5f;
        const float param1Scale = param1_ != nullptr
            ? 2.0f * param1_->currentValue() : 1.0f;

        for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            const float channelGain = channel == 1 ? gain * param1Scale : gain;
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                data[sample] *= channelGain;
        }
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
    }

    void getStateInformation(juce::MemoryBlock& data) override { data.reset(); }
    void setStateInformation(const void*, int) override {}

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

private:
    APEXTestVST3AutomationGainParameter* gainParameter_ = nullptr;
    APEXTestVST3AutomationParam1* param1_ = nullptr;
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new APEXTestVST3AutomationProcessor();
}
