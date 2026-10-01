#include <JuceHeader.h>
#include "../../../Source/PluginHostCore/PluginChainCore.h"

#include <cmath>
#include <limits>

namespace
{
struct BufferEvidence
{
    float maxAbs = 0.0f;
    double rms = 0.0;
    int firstNonZero = -1;
    int channels = 0;
    int samples = 0;
    bool hasNonFinite = false;
};

BufferEvidence inspect(const juce::AudioBuffer<float>& buffer)
{
    BufferEvidence evidence;
    evidence.channels = buffer.getNumChannels();
    evidence.samples = buffer.getNumSamples();
    double sumSquares = 0.0;
    int ordinal = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* samples = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i, ++ordinal)
        {
            const float value = samples[i];
            if (! std::isfinite(value))
                evidence.hasNonFinite = true;
            else
            {
                evidence.maxAbs = juce::jmax(evidence.maxAbs, std::abs(value));
                sumSquares += (double) value * (double) value;
            }

            if (evidence.firstNonZero < 0 && value != 0.0f)
                evidence.firstNonZero = ordinal;
        }
    }

    const auto count = (double) juce::jmax(1, evidence.channels * evidence.samples);
    evidence.rms = std::sqrt(sumSquares / count);
    return evidence;
}

juce::AudioBuffer<float> makeActiveView(juce::AudioBuffer<float>& storage, int numSamples)
{
    return { storage.getArrayOfWritePointers(), storage.getNumChannels(), numSamples };
}

class ZeroInputSilentPlugin final : public juce::AudioPluginInstance
{
public:
    ZeroInputSilentPlugin()
        : juce::AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void expectNextBlockSize(int samples) noexcept { expectedNextSamples_ = samples; }
    int lastReceivedBlockSize() const noexcept { return lastReceivedSamples_; }
    BufferEvidence lastInputEvidence() const noexcept { return lastInput_; }

    const juce::String getName() const override { return "APEX Zero Input Silent"; }
    void prepareToPlay(double, int maximumExpectedSamplesPerBlock) override
    {
        maximumExpectedSamplesPerBlock_ = maximumExpectedSamplesPerBlock;
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        lastReceivedSamples_ = buffer.getNumSamples();
        lastInput_ = inspect(buffer);

        // A correctly hosted zero-input-silent insert is a no-op. Make a host
        // block-contract violation deterministic instead of relying on audible
        // behaviour from any third-party plugin.
        if (buffer.getNumSamples() != expectedNextSamples_
            || buffer.getNumSamples() > maximumExpectedSamplesPerBlock_)
        {
            buffer.setSample(0, 0, 0.25f);
        }
    }
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destinationData) override { destinationData.reset(); }
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& description) const override
    {
        description.name = getName();
        description.descriptiveName = getName();
        description.pluginFormatName = "APEX Test";
        description.manufacturerName = "APEX";
        description.version = "1";
        description.uniqueId = 0x41504558;
        description.deprecatedUid = description.uniqueId;
        description.numInputChannels = 2;
        description.numOutputChannels = 2;
    }

private:
    int maximumExpectedSamplesPerBlock_ = 0;
    int expectedNextSamples_ = 0;
    int lastReceivedSamples_ = 0;
    BufferEvidence lastInput_;
};

class PluginEmptyTrackSilenceTests final : public juce::UnitTest
{
public:
    PluginEmptyTrackSilenceTests()
        : juce::UnitTest("plugin.empty_track.digital_silence.v1", "PluginHost")
    {
    }

    void runTest() override
    {
        beginTest("empty monitored-off track remains silent through insert lifecycle");

        constexpr int maximumBlock = 1024;
        constexpr int shortBlock = 480;
        constexpr int storageSamples = 8192;

        DAW::PluginChainCore chain;
        chain.prepare(48000.0, maximumBlock);

        auto plugin = std::make_unique<ZeroInputSilentPlugin>();
        auto* probe = plugin.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(plugin)), 0);

        juce::AudioBuffer<float> trackStorage(2, storageSamples);

        // Warm the reusable host scratch with non-zero audio using a legal
        // maximum-sized block. This exposes stale samples on the next block.
        trackStorage.clear();
        for (int ch = 0; ch < trackStorage.getNumChannels(); ++ch)
            trackStorage.applyGain(ch, 0, maximumBlock, 0.5f);
        for (int ch = 0; ch < trackStorage.getNumChannels(); ++ch)
            for (int i = 0; i < maximumBlock; ++i)
                trackStorage.setSample(ch, i, 0.5f);
        probe->expectNextBlockSize(maximumBlock);
        chain.processBlock(trackStorage, maximumBlock);

        // Exact requested setup: no clip, no recording, monitoring/arm/sends/
        // sidechain/automation/hardware contribution all absent. The active
        // track-source, clip-mixer, and pre-insert range is explicitly cleared.
        for (int ch = 0; ch < trackStorage.getNumChannels(); ++ch)
            trackStorage.clear(ch, 0, shortBlock);
        auto preInsert = makeActiveView(trackStorage, shortBlock);
        const auto preEvidence = inspect(preInsert);
        expectEquals(preEvidence.channels, 2);
        expectEquals(preEvidence.firstNonZero, -1);
        expectEquals(preEvidence.maxAbs, 0.0f);
        expect(! preEvidence.hasNonFinite);

        probe->expectNextBlockSize(shortBlock);
        chain.processBlock(trackStorage, shortBlock);
        auto postInsert = makeActiveView(trackStorage, shortBlock);
        const auto postEvidence = inspect(postInsert);
        const auto pluginInput = probe->lastInputEvidence();

        expectEquals(probe->lastReceivedBlockSize(), shortBlock,
                     "processBlock must receive the callback frame count, not buffer capacity");
        expectEquals(pluginInput.samples, shortBlock);
        expectEquals(pluginInput.firstNonZero, -1,
                     "no inactive-range stale samples may enter the plugin call");
        expectEquals(pluginInput.maxAbs, 0.0f);
        expect(! pluginInput.hasNonFinite);
        expectEquals(postEvidence.firstNonZero, -1,
                     "first unexpected non-zero must not appear at plugin output");
        expectEquals(postEvidence.maxAbs, 0.0f);
        expect(! postEvidence.hasNonFinite);

        for (int repetition = 0; repetition < 32; ++repetition)
        {
            for (int ch = 0; ch < trackStorage.getNumChannels(); ++ch)
            {
                trackStorage.clear(ch, 0, shortBlock);
                for (int i = shortBlock; i < storageSamples; ++i)
                    trackStorage.setSample(ch, i, (repetition & 1) == 0 ? 0.75f : -0.75f);
            }
            probe->expectNextBlockSize(shortBlock);
            chain.processBlock(trackStorage, shortBlock);
            const auto repeatedEvidence = inspect(makeActiveView(trackStorage, shortBlock));
            expectEquals(repeatedEvidence.maxAbs, 0.0f);
            expectEquals(repeatedEvidence.firstNonZero, -1);
            expect(! repeatedEvidence.hasNonFinite);
        }

        chain.removePlugin(0);
        expectEquals(chain.getNumActiveSlots(), 0);

        auto replacement = std::make_unique<ZeroInputSilentPlugin>();
        auto* replacementProbe = replacement.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(replacement)), 0);
        for (int ch = 0; ch < trackStorage.getNumChannels(); ++ch)
            trackStorage.clear(ch, 0, shortBlock);
        replacementProbe->expectNextBlockSize(shortBlock);
        chain.processBlock(trackStorage, shortBlock);
        const auto reinsertEvidence = inspect(makeActiveView(trackStorage, shortBlock));
        expectEquals(reinsertEvidence.maxAbs, 0.0f);
        expectEquals(reinsertEvidence.firstNonZero, -1);
        expect(! reinsertEvidence.hasNonFinite);
    }
};

PluginEmptyTrackSilenceTests pluginEmptyTrackSilenceTests;
}
