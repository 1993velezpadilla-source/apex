#include <JuceHeader.h>

#include <cstdint>
#include <cstring>

#if JUCE_WINDOWS && defined(APEX_TEST_VST3_STATE_HANG_SEAM) \
    && APEX_TEST_VST3_STATE_HANG_SEAM
namespace
{
constexpr std::uint32_t kHangSentinelBits = 0x7FC0BEEFu;

bool isHangSentinel(float sample) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &sample, sizeof(bits));
    return bits == kHangSentinelBits;
}

[[noreturn]] void triggerHardHang()
{
    // Test fixture only: the worker process remains alive while the plugin
    // callback is deliberately non-returning, allowing the parent watchdog to
    // prove progress-based hang detection and recovery.
    for (;;)
        juce::Thread::sleep(1000);
}
} // namespace
#endif

namespace
{
constexpr int kFixtureLatencySamples = 32;
constexpr char kStateMagic[4] = { 'A', 'P', 'S', 'T' };
constexpr std::uint32_t kStateVersion = 1;
constexpr int kStateHeaderBytes = 8;   // magic(4) + version(4)

class APEXTestVST3StateGainParameter final : public juce::AudioProcessorParameterWithID
{
public:
    APEXTestVST3StateGainParameter()
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
    void setFromState(float value) noexcept
    {
        gain_.store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_relaxed);
    }

private:
    std::atomic<float> gain_ { 0.5f };
};

class APEXTestVST3StateProcessor final : public juce::AudioProcessor
{
public:
    APEXTestVST3StateProcessor()
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter(gainParameter_ = new APEXTestVST3StateGainParameter());
        setLatencySamples(kFixtureLatencySamples);
    }

    const juce::String getName() const override { return "APEX Test VST3 State"; }

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

       #if JUCE_WINDOWS && defined(APEX_TEST_VST3_STATE_HANG_SEAM) \
           && APEX_TEST_VST3_STATE_HANG_SEAM
        if (buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0
            && isHangSentinel(buffer.getSample(0, 0)))
            triggerHardHang();
       #endif

        const float gain = gainParameter_ != nullptr
            ? gainParameter_->currentValue() : 0.5f;

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
            {
                auto* delayChannel = delay_.getWritePointer(channel);
                const float input = buffer.getSample(channel, sample);
                buffer.setSample(channel, sample, delayChannel[writePosition_] * gain);
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

    // ── Phase E1 state chunk: magic + version + gain ──
    void getStateInformation(juce::MemoryBlock& data) override
    {
        juce::MemoryOutputStream stream(data, true);
        stream.write(kStateMagic, 4);
        stream.writeInt(static_cast<int>(kStateVersion));
        stream.writeFloat(gainParameter_ != nullptr
            ? gainParameter_->currentValue() : 0.5f);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        if (data == nullptr || sizeInBytes != kStateHeaderBytes + 4)
            return;   // invalid: preserve current valid state

        const auto* bytes = static_cast<const char*>(data);
        if (std::memcmp(bytes, kStateMagic, 4) != 0)
            return;   // bad magic: preserve current valid state

        juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        stream.readByte(); stream.readByte(); stream.readByte(); stream.readByte();
        const std::uint32_t version = static_cast<std::uint32_t>(stream.readInt());
        const float gain = stream.readFloat();
        if (version != kStateVersion || ! std::isfinite(gain)
            || gain < 0.0f || gain > 1.0f)
            return;   // invalid version/value: preserve current valid state

        if (gainParameter_ != nullptr)
            gainParameter_->setFromState(gain);
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

private:
    APEXTestVST3StateGainParameter* gainParameter_ = nullptr;
    juce::AudioBuffer<float> delay_;
    int writePosition_ = 0;
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new APEXTestVST3StateProcessor();
}
