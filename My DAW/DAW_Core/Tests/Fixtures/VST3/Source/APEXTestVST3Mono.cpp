#include <JuceHeader.h>

namespace
{
constexpr int kFixtureLatencySamples = 32;

class APEXTestVST3MonoProcessor final : public juce::AudioProcessor
{
public:
    APEXTestVST3MonoProcessor()
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::mono(), true)
              .withOutput("Output", juce::AudioChannelSet::mono(), true))
    {
        setLatencySamples(kFixtureLatencySamples);
    }

    const juce::String getName() const override { return "APEX Test VST3 Mono"; }

    void prepareToPlay(double, int) override
    {
        delay_.setSize(1, kFixtureLatencySamples, false, true, false);
        reset();
    }

    void releaseResources() override
    {
        delay_.setSize(0, 0);
        writePosition_ = 0;
    }

    void reset() override
    {
        delay_.clear();
        writePosition_ = 0;
    }

    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet() == juce::AudioChannelSet::mono()
            && layout.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        jassert(buffer.getNumChannels() >= 1);
        jassert(delay_.getNumChannels() == 1);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            auto* delayChannel = delay_.getWritePointer(0);
            const float input = buffer.getSample(0, sample);
            buffer.setSample(0, sample, delayChannel[writePosition_] * 0.5f);
            delayChannel[writePosition_] = input;

            if (++writePosition_ == kFixtureLatencySamples)
                writePosition_ = 0;
        }
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
    }

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
    void getStateInformation(juce::MemoryBlock& data) override { data.reset(); }
    void setStateInformation(const void*, int) override {}

private:
    juce::AudioBuffer<float> delay_;
    int writePosition_ = 0;
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new APEXTestVST3MonoProcessor();
}
