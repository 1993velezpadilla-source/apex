#include <JuceHeader.h>

namespace
{
constexpr int kDenseParameterCount = 129;

class DenseParameter final : public juce::AudioProcessorParameterWithID
{
public:
    explicit DenseParameter(int index)
        : juce::AudioProcessorParameterWithID(
              "param_" + juce::String(index),
              "Param " + juce::String(index),
              "Param " + juce::String(index)),
          index_(index)
    {
    }

    float getValue() const override
    {
        return value_.load(std::memory_order_relaxed);
    }

    void setValue(float newValue) override
    {
        value_.store(juce::jlimit(0.0f, 1.0f, newValue),
                     std::memory_order_relaxed);
    }

    float getDefaultValue() const override { return 0.5f; }

    juce::String getName(int) const override
    {
        return "Param " + juce::String(index_);
    }

    juce::String getLabel() const override { return {}; }

    float getValueForText(const juce::String& text) const override
    {
        return juce::jlimit(0.0f, 1.0f, text.getFloatValue());
    }

    int getNumSteps() const override
    {
        return juce::AudioProcessor::getDefaultNumParameterSteps();
    }

private:
    const int index_;
    std::atomic<float> value_ { 0.5f };
};

class APEXTestVST3DenseE2BProcessor final : public juce::AudioProcessor
{
public:
    APEXTestVST3DenseE2BProcessor()
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        // Construction order is the stable worker ordinal contract. The IDs
        // are deterministic and are created only during plugin construction,
        // never from the realtime processing path.
        for (int index = 0; index < kDenseParameterCount; ++index)
            addParameter(new DenseParameter(index));

        setLatencySamples(0);
    }

    const juce::String getName() const override { return "APEX Dense E2B"; }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void reset() override {}

    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        // Parameter transport and metadata identity are the observable E2B
        // contract. Keep DSP trivial so no unrelated signal-path behavior is
        // coupled to the density and ordinal tests.
        juce::ignoreUnused(buffer, midi);
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ignoreUnused(midi);
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
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new APEXTestVST3DenseE2BProcessor();
}
