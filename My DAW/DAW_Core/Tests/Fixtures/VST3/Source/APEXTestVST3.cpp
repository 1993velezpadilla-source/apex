#include <JuceHeader.h>

namespace
{
constexpr int kFixtureLatencySamples = 32;
constexpr int kFixtureEditorWidth = 320;
constexpr int kFixtureEditorHeight = 180;

class APEXTestVST3Editor final : public juce::AudioProcessorEditor
{
public:
    explicit APEXTestVST3Editor(juce::AudioProcessor& processor)
        : juce::AudioProcessorEditor(&processor)
    {
        setOpaque(true);
        setSize(kFixtureEditorWidth, kFixtureEditorHeight);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colours::darkslategrey);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(18.0f));
        graphics.drawFittedText("APEX TEST EDITOR",
                                getLocalBounds().reduced(16),
                                juce::Justification::centred,
                                1);
    }

    void resized() override {}
};

class APEXTestVST3Processor final : public juce::AudioProcessor
{
public:
    APEXTestVST3Processor()
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        setLatencySamples(kFixtureLatencySamples);
    }

    const juce::String getName() const override { return "APEX Test VST3"; }

    void prepareToPlay(double, int) override
    {
        delay_.setSize(2, kFixtureLatencySamples, false, true, false);
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
        return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        jassert(buffer.getNumChannels() == 2);
        jassert(delay_.getNumChannels() == 2);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
            {
                auto* delayChannel = delay_.getWritePointer(channel);
                const float input = buffer.getSample(channel, sample);
                buffer.setSample(channel, sample, delayChannel[writePosition_] * 0.5f);
                delayChannel[writePosition_] = input;
            }

            if (++writePosition_ == kFixtureLatencySamples)
                writePosition_ = 0;
        }
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override
    {
        return new APEXTestVST3Editor(*this);
    }
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
    return new APEXTestVST3Processor();
}
