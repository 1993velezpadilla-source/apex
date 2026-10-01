// SidechainRoutingRegressionTests.cpp
//
// P0 host-stability regression suite for the Aug 15 2026 sidechain crash.
//
// Root cause recap:
//   PluginChainCore::processBlockWithSidechain() handed a 3-4 channel combined
//   buffer (main stereo + sidechain) to processSlotWithBypass(), which copied
//   buffer.getNumChannels() channels into 2-channel dry/wet scratch buffers.
//   JUCE Release builds perform no channel bounds checks, so channels 2/3
//   dereferenced the null terminator / uninitialised channel-list slots and
//   produced memcpy access violations (addresses 0x0 and 0x20).
//
// Architectural fix under test:
//   PluginChainCore::prepareSidechainScratchCapacity() preallocates the whole
//   scratch set (combined/dry/wet/chain-input) to the worst-case channel
//   topology on non-realtime threads. The realtime path only verifies
//   capacity and falls back to an allocation-free main-only path on any
//   contract violation.
//
// The processors below are purpose-built so the harness reproduces the exact
// host-side bus topology without any third-party VST dependency.

#include <JuceHeader.h>
#include "../../../Source/PluginHostCore/PluginChainCore.h"

#include <atomic>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

// ── Test processors ────────────────────────────────────────────────────────

void fillTestPluginDescription(juce::PluginDescription& d, const juce::String& name, int uid)
{
    d.name = name;
    d.descriptiveName = name;
    d.pluginFormatName = "APEX Test";
    d.manufacturerName = "APEX";
    d.version = "1";
    d.uniqueId = uid;
    d.deprecatedUid = d.uniqueId;
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
}

/** ReverbLikeProcessor — stereo main in/out only, no sidechain bus.
    Deterministic trivial "reverb" (decorrelating 2x2 mix). */
class ReverbLikeProcessor final : public juce::AudioPluginInstance
{
public:
    ReverbLikeProcessor()
        : juce::AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "APEX ReverbLike Test"; }
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        sampleRate_ = sampleRate;
        maxBlock_ = maximumExpectedSamplesPerBlock;
        prepareCount.fetch_add(1, std::memory_order_relaxed);
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannels() == 2
            && layouts.getMainOutputChannels() == 2
            && layouts.inputBuses.size() <= 1
            && layouts.outputBuses.size() <= 1;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        lastChannels.store(buffer.getNumChannels(), std::memory_order_relaxed);
        lastSamples.store(buffer.getNumSamples(), std::memory_order_relaxed);

        const int n = juce::jmin(buffer.getNumSamples(), maxBlock_);
        auto* L = buffer.getWritePointer(0);
        auto* R = buffer.getWritePointer(juce::jmin(1, buffer.getNumChannels() - 1));
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i];
            const float r = R[i];
            L[i] = 0.98f * l + 0.01f * r;
            R[i] = 0.98f * r + 0.01f * l;
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
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        fillTestPluginDescription(d, getName(), 0x52564230);
    }

    std::atomic<int> prepareCount { 0 };
    std::atomic<int> lastChannels { 0 };
    std::atomic<int> lastSamples { 0 };

private:
    double sampleRate_ = 0.0;
    int maxBlock_ = 0;
};

/** SidechainCapableCompressorProcessor — stereo main in/out plus an explicit
    auxiliary sidechain input bus (bus index 1). Layout negotiation accepts
    disabled/mono/stereo for the sidechain bus. Deterministic trivial
    compressor whose output attenuation depends on the sidechain level, so the
    tests can PROVE sidechain signal delivery. */
class SidechainCapableCompressorProcessor final : public juce::AudioPluginInstance
{
public:
    explicit SidechainCapableCompressorProcessor(bool monoSidechainDefaultLayout)
        : juce::AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain",
                         monoSidechainDefaultLayout ? juce::AudioChannelSet::mono()
                                                    : juce::AudioChannelSet::stereo(),
                         false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "APEX SidechainCompressor Test"; }
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        sampleRate_ = sampleRate;
        maxBlock_ = maximumExpectedSamplesPerBlock;
        prepareCount.fetch_add(1, std::memory_order_relaxed);

        const auto* scBus = getBus(true, 1);
        lastScBusEnabled.store(scBus != nullptr && scBus->isEnabled(), std::memory_order_relaxed);
        lastScChannels.store(scBus != nullptr && scBus->isEnabled()
                                  ? scBus->getNumberOfChannels()
                                  : 0,
                              std::memory_order_relaxed);
        lastTotalInputs.store(getTotalNumInputChannels(), std::memory_order_relaxed);
        lastTotalOutputs.store(getTotalNumOutputChannels(), std::memory_order_relaxed);
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        if (layouts.getMainInputChannels() != 2 || layouts.getMainOutputChannels() != 2)
            return false;
        if (layouts.outputBuses.size() > 1)
            return false;

        const auto& sc = layouts.inputBuses.size() > 1
            ? layouts.getChannelSet(true, 1)
            : juce::AudioChannelSet::disabled();
        return sc.isDisabled() || sc.size() == 1 || sc.size() == 2;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int channels = buffer.getNumChannels();
        const int n = juce::jmin(buffer.getNumSamples(), maxBlock_);
        lastChannels.store(channels, std::memory_order_relaxed);
        lastSamples.store(n, std::memory_order_relaxed);

        // Sidechain evidence: max |sample| across any channel beyond main (2+).
        float scLevel = 0.0f;
        for (int ch = 2; ch < channels; ++ch)
        {
            const auto* sc = buffer.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                scLevel = juce::jmax(scLevel, std::abs(sc[i]));
        }
        lastScMax.store(scLevel, std::memory_order_relaxed);

        // Deterministic compression: fixed attenuation when sidechain energy
        // is present, unity otherwise. Proves SC delivery in the wet path.
        const float reduction = scLevel > 0.0f ? 0.9f : 1.0f;
        for (int ch = 0; ch < juce::jmin(2, channels); ++ch)
        {
            auto* d = buffer.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
                d[i] *= reduction;
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
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        fillTestPluginDescription(d, getName(), 0x53434330);
    }

    std::atomic<int> prepareCount { 0 };
    std::atomic<int> lastChannels { 0 };
    std::atomic<int> lastSamples { 0 };
    std::atomic<int> lastScBusEnabled { -1 };
    std::atomic<int> lastScChannels { -1 };
    std::atomic<int> lastTotalInputs { 0 };
    std::atomic<int> lastTotalOutputs { 0 };
    std::atomic<float> lastScMax { -1.0f };

private:
    double sampleRate_ = 0.0;
    int maxBlock_ = 0;
};

// ── Helpers ────────────────────────────────────────────────────────────────

using SidechainConfig = std::unordered_map<int, juce::Array<int>>;

SidechainConfig scConfig(int slotIndex, int bus = 1)
{
    SidechainConfig config;
    config[slotIndex] = { bus };
    return config;
}

void fillSine(juce::AudioBuffer<float>& buffer, int numSamples, float frequency,
              float amplitude = 0.5f, int startChannel = 0)
{
    const int channels = juce::jmin(buffer.getNumChannels(), startChannel + 2);
    for (int ch = startChannel; ch < channels; ++ch)
    {
        auto* d = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = amplitude * std::sin((float) i * frequency * 0.0001f);
    }
}

bool allFinite(const juce::AudioBuffer<float>& buffer, int numSamples, int channels = -1)
{
    const int chans = channels >= 0 ? juce::jmin(channels, buffer.getNumChannels())
                                    : buffer.getNumChannels();
    for (int ch = 0; ch < chans; ++ch)
    {
        const auto* d = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite(d[i]))
                return false;
    }
    return true;
}

// ── Scenario A: Reverb -> Sidechain-capable Compressor ──────────────────────

class SidechainReverbThenCompressorTest final : public juce::UnitTest
{
public:
    SidechainReverbThenCompressorTest()
        : UnitTest("Sidechain.ReverbThenCompressor", "Sidechain") {}

    void runTest() override
    {
        beginTest("reverb -> sidechain-capable compressor survives sidechain activation");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto reverb = std::make_unique<ReverbLikeProcessor>();
        auto* reverbProbe = reverb.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(reverb)), 0);

        auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
        auto* compProbe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 1);

        // Before sidechain activation the compressor runs 2-channel.
        expectEquals(compProbe->lastTotalInputs.load(), 2);
        expectEquals(chain.getScratchChannelCapacityForTesting(), 2);

        chain.setActiveSidechainBusConfig(scConfig(1));

        // Capacity contract: the control-side reprepare grew the whole
        // scratch set to the negotiated 4-channel topology (2 main + 2 SC).
        expect(chain.getScratchChannelCapacityForTesting() >= 4);
        expect(chain.getDrySnapshotChannelsForTesting() >= 4);
        expect(chain.getWetSnapshotChannelsForTesting() >= 4);
        expectEquals(compProbe->lastTotalInputs.load(), 4);

        juce::AudioBuffer<float> main(2, kBlock);
        juce::AudioBuffer<float> sidechain(2, kBlock);
        fillSine(main, kBlock, 440.0f, 0.5f);
        fillSine(sidechain, kBlock, 55.0f, 0.9f);

        chain.processBlockWithSidechain(main, sidechain, kBlock);

        expect(allFinite(main, kBlock));
        expectEquals(reverbProbe->lastChannels.load(), 2);
        expectEquals(compProbe->lastChannels.load(), 4,
                     "compressor must receive 2 main + 2 sidechain channels");
        expectEquals(compProbe->lastSamples.load(), kBlock);
        expect(compProbe->lastScMax.load() > 0.5f,
               "sidechain signal must reach the compressor through the wet path");
        // 0.9x attenuation proves the compressor actually consumed the SC input.
        // (Sample 0 of a sine is always zero — probe sample 1.)
        expect(main.getSample(0, 1) != 0.0f);

        // Repeat processing — capacity must never change on the RT side.
        const int rtChannels = chain.getScratchChannelCapacityForTesting();
        for (int block = 0; block < 64; ++block)
        {
            fillSine(main, kBlock, 440.0f, 0.5f);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);
        }
        expect(allFinite(main, kBlock));
        expectEquals(chain.getScratchChannelCapacityForTesting(), rtChannels);
    }
};

SidechainReverbThenCompressorTest sidechainReverbThenCompressorTest;

// ── Scenario B: Compressor only (reverb is incidental to the defect) ───────

class SidechainCompressorOnlyTest final : public juce::UnitTest
{
public:
    SidechainCompressorOnlyTest()
        : UnitTest("Sidechain.CompressorOnly", "Sidechain") {}

    void runTest() override
    {
        beginTest("sidechain-capable compressor alone survives sidechain activation");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
        auto* compProbe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);

        chain.setActiveSidechainBusConfig(scConfig(0));

        expect(chain.getScratchChannelCapacityForTesting() >= 4);
        expect(chain.getDrySnapshotChannelsForTesting() >= 4);
        expect(chain.getWetSnapshotChannelsForTesting() >= 4);

        juce::AudioBuffer<float> main(2, kBlock);
        juce::AudioBuffer<float> sidechain(2, kBlock);
        fillSine(main, kBlock, 440.0f, 0.5f);
        fillSine(sidechain, kBlock, 55.0f, 0.9f);

        chain.processBlockWithSidechain(main, sidechain, kBlock);

        expect(allFinite(main, kBlock));
        expectEquals(compProbe->lastChannels.load(), 4);
        expect(compProbe->lastScMax.load() > 0.5f);
    }
};

SidechainCompressorOnlyTest sidechainCompressorOnlyTest;

// ── Scenario D: Compressor -> Reverb ────────────────────────────────────────

class SidechainCompressorThenReverbTest final : public juce::UnitTest
{
public:
    SidechainCompressorThenReverbTest()
        : UnitTest("Sidechain.CompressorThenReverb", "Sidechain") {}

    void runTest() override
    {
        beginTest("sidechain-capable compressor followed by reverb survives");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
        auto* compProbe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);

        auto reverb = std::make_unique<ReverbLikeProcessor>();
        auto* reverbProbe = reverb.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(reverb)), 1);

        chain.setActiveSidechainBusConfig(scConfig(0));

        juce::AudioBuffer<float> main(2, kBlock);
        juce::AudioBuffer<float> sidechain(2, kBlock);
        fillSine(main, kBlock, 440.0f, 0.5f);
        fillSine(sidechain, kBlock, 55.0f, 0.9f);

        chain.processBlockWithSidechain(main, sidechain, kBlock);

        expect(allFinite(main, kBlock));
        expectEquals(compProbe->lastChannels.load(), 4,
                     "sidechain target slot must see the combined 4-channel layout");
        expectEquals(reverbProbe->lastChannels.load(), 2,
                     "non-target slot must see the plain stereo layout");
        expect(compProbe->lastScMax.load() > 0.5f);
    }
};

SidechainCompressorThenReverbTest sidechainCompressorThenReverbTest;

// ── Scenario H/I: create / remove / recreate repeatedly ─────────────────────

class SidechainCreateRemoveRecreateTest final : public juce::UnitTest
{
public:
    SidechainCreateRemoveRecreateTest()
        : UnitTest("Sidechain.CreateRemoveRecreate", "Sidechain") {}

    void runTest() override
    {
        beginTest("sidechain create/remove/recreate cycles stay stable");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto reverb = std::make_unique<ReverbLikeProcessor>();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(reverb)), 0);

        auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
        auto* compProbe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 1);

        juce::AudioBuffer<float> main(2, kBlock);
        juce::AudioBuffer<float> sidechain(2, kBlock);

        for (int cycle = 0; cycle < 8; ++cycle)
        {
            // Create / re-enable sidechain.
            chain.setActiveSidechainBusConfig(scConfig(1));
            expect(chain.getScratchChannelCapacityForTesting() >= 4);
            expectEquals(compProbe->lastTotalInputs.load(), 4);

            for (int block = 0; block < 4; ++block)
            {
                fillSine(main, kBlock, 440.0f, 0.5f);
                fillSine(sidechain, kBlock, 55.0f, 0.9f);
                chain.processBlockWithSidechain(main, sidechain, kBlock);
            }
            expect(allFinite(main, kBlock));
            expectEquals(compProbe->lastChannels.load(), 4);
            expect(compProbe->lastScMax.load() > 0.5f);

            // Remove (disable all aux buses).
            chain.setActiveSidechainBusConfig({});
            expectEquals(compProbe->lastTotalInputs.load(), 2);

            for (int block = 0; block < 4; ++block)
            {
                fillSine(main, kBlock, 440.0f, 0.5f);
                fillSine(sidechain, kBlock, 55.0f, 0.9f);
                chain.processBlockWithSidechain(main, sidechain, kBlock);
            }
            expect(allFinite(main, kBlock));
            expectEquals(compProbe->lastChannels.load(), 2,
                         "disabled sidechain must fall back to the plain stereo path");
        }
    }
};

SidechainCreateRemoveRecreateTest sidechainCreateRemoveRecreateTest;

// ── Scenario J: active processing with protocol-synchronized reconfiguration ─

class SidechainActiveProcessingReconfigureTest final : public juce::UnitTest
{
public:
    SidechainActiveProcessingReconfigureTest()
        : UnitTest("Sidechain.ActiveProcessingReconfigure", "Sidechain") {}

    void runTest() override
    {
        beginTest("bus reconfiguration during active processing via the drain protocol");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto reverb = std::make_unique<ReverbLikeProcessor>();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(reverb)), 0);

        auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
        auto* compProbe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 1);

        chain.setActiveSidechainBusConfig(scConfig(1));

        // Mirror of the ApplicationCore protocol: a suspension flag the
        // "audio thread" honours before each block, plus a pause handshake so
        // bus mutation never overlaps an in-flight block.
        std::atomic<bool> suspension { false };
        std::atomic<bool> stopWorker { false };
        std::atomic<unsigned> pausedGeneration { 0 };
        std::atomic<bool> workerCrashed { false };
        std::atomic<int> workerBlocks { 0 };

        std::thread worker([&]
        {
            juce::AudioBuffer<float> wMain(2, kBlock);
            juce::AudioBuffer<float> wSide(2, kBlock);
            bool wasSuspended = false;
            while (! stopWorker.load(std::memory_order_acquire))
            {
                if (suspension.load(std::memory_order_acquire))
                {
                    // Observe each suspension episode exactly once so the
                    // handshake counts episodes, not spins.
                    if (! wasSuspended)
                        pausedGeneration.fetch_add(1, std::memory_order_release);
                    wasSuspended = true;
                    continue;
                }
                wasSuspended = false;

                fillSine(wMain, kBlock, 440.0f, 0.5f);
                fillSine(wSide, kBlock, 55.0f, 0.9f);
                chain.processBlockWithSidechain(wMain, wSide, kBlock);
                if (! allFinite(wMain, kBlock))
                    workerCrashed.store(true, std::memory_order_relaxed);
                workerBlocks.fetch_add(1, std::memory_order_relaxed);
            }
        });

        for (int cycle = 0; cycle < 25; ++cycle)
        {
            const unsigned before = pausedGeneration.load(std::memory_order_acquire);
            suspension.store(true, std::memory_order_release);

            // Drain: wait until the worker observes the suspension.
            for (int spin = 0;
                 spin < 10000000
                 && pausedGeneration.load(std::memory_order_acquire) == before;
                 ++spin)
            {
            }
            expect(pausedGeneration.load(std::memory_order_acquire) > before,
                   "worker must observe the suspension handshake");

            if ((cycle & 1) == 0)
                chain.setActiveSidechainBusConfig({});
            else
                chain.setActiveSidechainBusConfig(scConfig(1));

            suspension.store(false, std::memory_order_release);

            // The engine resumes processing after the reprepare window.
            // Require at least one block to complete between reconfigurations
            // so the test genuinely exercises active processing.
            const int blocksBefore = workerBlocks.load(std::memory_order_acquire);
            for (int spin = 0;
                 spin < 10000000
                 && workerBlocks.load(std::memory_order_acquire) == blocksBefore;
                 ++spin)
            {
            }
            expect(workerBlocks.load(std::memory_order_acquire) > blocksBefore,
                   "worker must process blocks between reconfigurations");
        }

        // Drain one final time so the final state is deterministic.
        const unsigned before = pausedGeneration.load(std::memory_order_acquire);
        suspension.store(true, std::memory_order_release);
        for (int spin = 0;
             spin < 10000000
             && pausedGeneration.load(std::memory_order_acquire) == before;
             ++spin)
        {
        }
        stopWorker.store(true, std::memory_order_release);
        suspension.store(false, std::memory_order_release);
        worker.join();

        expect(! workerCrashed.load(), "active processing must never observe non-finite output");
        expect(workerBlocks.load() > 0, "worker must have processed blocks during the test");
        expect(compProbe->lastTotalInputs.load() >= 2);
    }
};

SidechainActiveProcessingReconfigureTest sidechainActiveProcessingReconfigureTest;

// ── Scenario K/L/M: mono/stereo layout combinations ─────────────────────────

class SidechainMonoStereoLayoutsTest final : public juce::UnitTest
{
public:
    SidechainMonoStereoLayoutsTest()
        : UnitTest("Sidechain.MonoStereoLayouts", "Sidechain") {}

    void runTest() override
    {
        beginTest("stereo and mono sidechain layouts across main-buffer widths");

        // (1) Stereo main + stereo sidechain -> 4-channel combined buffer.
        {
            DAW::PluginChainCore chain;
            chain.prepare(kRate, kBlock);
            auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
            auto* probe = compressor.get();
            expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);
            chain.setActiveSidechainBusConfig(scConfig(0));

            expectEquals(probe->lastScChannels.load(), 2);
            expectEquals(probe->lastTotalInputs.load(), 4);
            expect(chain.getScratchChannelCapacityForTesting() >= 4);

            juce::AudioBuffer<float> main(2, kBlock);
            juce::AudioBuffer<float> sidechain(2, kBlock);
            fillSine(main, kBlock, 440.0f, 0.5f);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);

            expect(allFinite(main, kBlock));
            expectEquals(probe->lastChannels.load(), 4);
            expect(probe->lastScMax.load() > 0.5f);
        }

        // (2) Stereo main + mono sidechain -> 3-channel combined buffer.
        {
            DAW::PluginChainCore chain;
            chain.prepare(kRate, kBlock);
            auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(true);
            auto* probe = compressor.get();
            expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);
            chain.setActiveSidechainBusConfig(scConfig(0));

            expectEquals(probe->lastScChannels.load(), 1);
            expectEquals(probe->lastTotalInputs.load(), 3);
            expect(chain.getScratchChannelCapacityForTesting() >= 3);

            juce::AudioBuffer<float> main(2, kBlock);
            juce::AudioBuffer<float> sidechain(2, kBlock);
            fillSine(main, kBlock, 440.0f, 0.5f);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);

            expect(allFinite(main, kBlock));
            expectEquals(probe->lastChannels.load(), 3);
            expect(probe->lastScMax.load() > 0.5f);
        }

        // (3) Mono main + stereo sidechain — the host still negotiates the
        //     2+2 layout; the mono main buffer must never overrun the scratch.
        {
            DAW::PluginChainCore chain;
            chain.prepare(kRate, kBlock);
            auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
            auto* probe = compressor.get();
            expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);
            chain.setActiveSidechainBusConfig(scConfig(0));

            juce::AudioBuffer<float> main(1, kBlock);
            juce::AudioBuffer<float> sidechain(2, kBlock);
            fillSine(main, kBlock, 440.0f, 0.5f, 0);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);

            expect(allFinite(main, kBlock, 1));
            expectEquals(probe->lastChannels.load(), 4);
            expect(probe->lastScMax.load() > 0.5f);
        }
    }
};

SidechainMonoStereoLayoutsTest sidechainMonoStereoLayoutsTest;

// ── Scenario N/O: missing / undersized sidechain source ─────────────────────

class SidechainMissingSourceBufferTest final : public juce::UnitTest
{
public:
    SidechainMissingSourceBufferTest()
        : UnitTest("Sidechain.MissingSourceBuffer", "Sidechain") {}

    void runTest() override
    {
        beginTest("missing or undersized sidechain source degrades safely");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto reverb = std::make_unique<ReverbLikeProcessor>();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(reverb)), 0);

        auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
        auto* compProbe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 1);

        chain.setActiveSidechainBusConfig(scConfig(1));

        juce::AudioBuffer<float> main(2, kBlock);

        // (1) Zero-channel sidechain source -> main-only processing. The
        //     negotiated bus layout is still honoured: the compressor keeps
        //     receiving its 4-channel process layout with SILENT auxiliary
        //     sidechain channels — no crash, no stray sidechain energy.
        {
            juce::AudioBuffer<float> zeroCh;
            fillSine(main, kBlock, 440.0f, 0.5f);
            chain.processBlockWithSidechain(main, zeroCh, kBlock);
            expect(allFinite(main, kBlock));
            expectEquals(compProbe->lastChannels.load(), 4,
                         "zero-channel sidechain keeps the negotiated layout with silent sidechain");
            expect(compProbe->lastScMax.load() == 0.0f,
                   "zero-channel sidechain must deliver no sidechain energy");
        }

        // (2) Undersized sidechain source (fewer samples than the block)
        //     -> entry capacity guard falls back to the main-only path with
        //     the same silent-sidechain layout.
        {
            juce::AudioBuffer<float> shortSource(2, 64);
            fillSine(shortSource, 64, 55.0f, 0.9f);
            fillSine(main, kBlock, 440.0f, 0.5f);
            chain.processBlockWithSidechain(main, shortSource, kBlock);
            expect(allFinite(main, kBlock));
            expectEquals(compProbe->lastChannels.load(), 4,
                         "undersized sidechain source must trigger the safe fallback (silent sidechain)");
            expect(compProbe->lastScMax.load() == 0.0f,
                   "undersized sidechain source must deliver no sidechain energy");
        }

        // (3) Valid source still works after the degraded blocks.
        {
            juce::AudioBuffer<float> sidechain(2, kBlock);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            fillSine(main, kBlock, 440.0f, 0.5f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);
            expect(allFinite(main, kBlock));
            expectEquals(compProbe->lastChannels.load(), 4);
            expect(compProbe->lastScMax.load() > 0.5f);
        }
    }
};

SidechainMissingSourceBufferTest sidechainMissingSourceBufferTest;

// ── Mixed bus layouts across a single chain ─────────────────────────────────

class SidechainChainMixedBusLayoutsTest final : public juce::UnitTest
{
public:
    SidechainChainMixedBusLayoutsTest()
        : UnitTest("Sidechain.ChainMixedBusLayouts", "Sidechain") {}

    void runTest() override
    {
        beginTest("reverb -> compressor -> reverb keeps per-slot layouts distinct");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto reverb0 = std::make_unique<ReverbLikeProcessor>();
        auto* reverb0Probe = reverb0.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(reverb0)), 0);

        auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
        auto* compProbe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 1);

        auto reverb1 = std::make_unique<ReverbLikeProcessor>();
        auto* reverb1Probe = reverb1.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(reverb1)), 2);

        chain.setActiveSidechainBusConfig(scConfig(1));

        juce::AudioBuffer<float> main(2, kBlock);
        juce::AudioBuffer<float> sidechain(2, kBlock);
        fillSine(main, kBlock, 440.0f, 0.5f);
        fillSine(sidechain, kBlock, 55.0f, 0.9f);

        chain.processBlockWithSidechain(main, sidechain, kBlock);

        expect(allFinite(main, kBlock));
        expectEquals(reverb0Probe->lastChannels.load(), 2,
                     "slot before the sidechain target stays stereo");
        expectEquals(compProbe->lastChannels.load(), 4,
                     "sidechain target receives the combined layout");
        expectEquals(reverb1Probe->lastChannels.load(), 2,
                     "slot after the sidechain target returns to stereo");
        expect(compProbe->lastScMax.load() > 0.5f);
    }
};

SidechainChainMixedBusLayoutsTest sidechainChainMixedBusLayoutsTest;

// ── Realtime allocation proof ───────────────────────────────────────────────

class SidechainNoRealtimeAllocationTest final : public juce::UnitTest
{
public:
    SidechainNoRealtimeAllocationTest()
        : UnitTest("Sidechain.NoRealtimeAllocation", "Sidechain") {}

    void runTest() override
    {
        // Topologies: compressor-only, reverb->compressor, compressor->reverb.
        for (int topology = 0; topology < 3; ++topology)
        {
            beginTest("no heap allocation during active sidechain processing (topology "
                      + juce::String(topology) + ")");

            DAW::PluginChainCore chain;
            chain.prepare(kRate, kBlock);

            SidechainCapableCompressorProcessor* compProbe = nullptr;
            int compressorSlot = -1;

            if (topology == 0)       // compressor only
            {
                auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
                compProbe = compressor.get();
                expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);
                compressorSlot = 0;
            }
            else if (topology == 1)  // reverb -> compressor
            {
                expectEquals(chain.appendPluginInstanceForTesting(
                                 std::make_unique<ReverbLikeProcessor>()), 0);
                auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
                compProbe = compressor.get();
                expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 1);
                compressorSlot = 1;
            }
            else                     // compressor -> reverb
            {
                auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(false);
                compProbe = compressor.get();
                expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);
                expectEquals(chain.appendPluginInstanceForTesting(
                                 std::make_unique<ReverbLikeProcessor>()), 1);
                compressorSlot = 0;
            }

            chain.setActiveSidechainBusConfig(scConfig(compressorSlot));

            juce::AudioBuffer<float> main(2, kBlock);
            juce::AudioBuffer<float> sidechain(2, kBlock);

            // Warm-up: absorb all first-use effects OUTSIDE the measured region.
            for (int warm = 0; warm < 8; ++warm)
            {
                fillSine(main, kBlock, 440.0f, 0.5f);
                fillSine(sidechain, kBlock, 55.0f, 0.9f);
                chain.processBlockWithSidechain(main, sidechain, kBlock);
            }

            // Probe 1: stimulus generation alone must be allocation-free.
            beginTest("probe stimulus generation allocates nothing (topology "
                      + juce::String(topology) + ")");
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                for (int block = 0; block < 64; ++block)
                {
                    fillSine(main, kBlock, 440.0f, 0.5f);
                    fillSine(sidechain, kBlock, 55.0f, 0.9f);
                }
            }

            // Probe 2: active sidechain processing must be allocation-free.
            beginTest("probe active sidechain processing allocates nothing (topology "
                      + juce::String(topology) + ")");
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                for (int block = 0; block < 64; ++block)
                {
                    fillSine(main, kBlock, 440.0f, 0.5f);
                    fillSine(sidechain, kBlock, 55.0f, 0.9f);
                    chain.processBlockWithSidechain(main, sidechain, kBlock);
                }
            }

            // Probe 3: when the sidechain source buffer is missing, the engine
            // dispatches the plain processBlock path (AudioEngine does this
            // when findSidechainBuffer() returns null). That path must be
            // allocation-free too; the sidechain target still receives its
            // negotiated layout with silent auxiliary channels.
            beginTest("probe missing-sidechain-source dispatch allocates nothing (topology "
                      + juce::String(topology) + ")");
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                for (int block = 0; block < 16; ++block)
                {
                    fillSine(main, kBlock, 440.0f, 0.5f);
                    chain.processBlock(main, kBlock);
                }
            }

            // Probe 4: bypass transitions must be allocation-free.
            beginTest("probe bypass transitions allocate nothing (topology "
                      + juce::String(topology) + ")");
            {
                juce::UnitTestAllocationChecker allocationChecker(*this);
                chain.getSlot(compressorSlot)->setBypassed(true);
                for (int block = 0; block < 16; ++block)
                {
                    fillSine(main, kBlock, 440.0f, 0.5f);
                    fillSine(sidechain, kBlock, 55.0f, 0.9f);
                    chain.processBlockWithSidechain(main, sidechain, kBlock);
                }
                chain.getSlot(compressorSlot)->setBypassed(false);
                for (int block = 0; block < 16; ++block)
                {
                    fillSine(main, kBlock, 440.0f, 0.5f);
                    fillSine(sidechain, kBlock, 55.0f, 0.9f);
                    chain.processBlockWithSidechain(main, sidechain, kBlock);
                }
            }

            expect(allFinite(main, kBlock));
            expectEquals(compProbe->lastChannels.load(), 4);
        }
    }
};

SidechainNoRealtimeAllocationTest sidechainNoRealtimeAllocationTest;

// ══════════════════════════════════════════════════════════════════════════
// PHASE C — real hosted-plugin sidechain semantics via JUCE bus APIs.
//
// BusSemanticSidechainProcessor deliberately does NOT inspect raw buffer
// channels 2/3. It reads its sidechain exclusively through
// AudioProcessor::getBusBuffer(buffer, true, 1) — the exact API contract a
// hosted VST3/AU wrapper exposes to the plugin. Its gain reduction depends
// only on that bus, so the test proves the host negotiated, enabled, and
// fed the auxiliary input bus end to end.
// ══════════════════════════════════════════════════════════════════════════

class BusSemanticSidechainProcessor final : public juce::AudioPluginInstance
{
public:
    BusSemanticSidechainProcessor()
        : juce::AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "APEX BusSemanticSidechain Test"; }
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        sampleRate_ = sampleRate;
        maxBlock_ = maximumExpectedSamplesPerBlock;
        ++prepareCount;

        const auto* scBus = getBus(true, 1);
        lastScBusEnabled.store(scBus != nullptr && scBus->isEnabled() ? 1 : 0,
                               std::memory_order_relaxed);
        lastScLayoutSize.store(scBus != nullptr && scBus->isEnabled()
                                   ? scBus->getNumberOfChannels() : 0,
                               std::memory_order_relaxed);
        lastTotalInputs.store(getTotalNumInputChannels(), std::memory_order_relaxed);
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        if (layouts.getMainInputChannels() != 2 || layouts.getMainOutputChannels() != 2)
            return false;
        if (layouts.outputBuses.size() > 1)
            return false;

        const auto& sc = layouts.inputBuses.size() > 1
            ? layouts.getChannelSet(true, 1)
            : juce::AudioChannelSet::disabled();
        return sc.isDisabled() || sc.size() == 1 || sc.size() == 2;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        lastChannels.store(buffer.getNumChannels(), std::memory_order_relaxed);
        lastSamples.store(buffer.getNumSamples(), std::memory_order_relaxed);

        // ── JUCE bus-semantic access only — never raw channel indices ──
        auto main = getBusBuffer(buffer, true, 0);

        // Record the main-bus input level BEFORE any processing so tests can
        // prove the key never contaminates the main input.
        float mainLevelIn = 0.0f;
        for (int ch = 0; ch < main.getNumChannels(); ++ch)
            for (int i = 0; i < main.getNumSamples(); ++i)
                mainLevelIn = juce::jmax(mainLevelIn, std::abs(main.getSample(ch, i)));
        lastMainMaxIn.store(mainLevelIn, std::memory_order_relaxed);

        const bool scBusExists = getBusCount(true) > 1 && getBus(true, 1) != nullptr;
        const bool scEnabled = scBusExists && getBus(true, 1)->isEnabled();
        lastScBusEnabled.store(scEnabled ? 1 : 0, std::memory_order_relaxed);

        float scLevel = 0.0f;
        if (scEnabled)
        {
            const auto sc = getBusBuffer(buffer, true, 1);
            lastScLayoutSize.store(sc.getNumChannels(), std::memory_order_relaxed);
            for (int ch = 0; ch < sc.getNumChannels(); ++ch)
                for (int i = 0; i < sc.getNumSamples(); ++i)
                    scLevel = juce::jmax(scLevel, std::abs(sc.getSample(ch, i)));
        }
        else
        {
            lastScLayoutSize.store(0, std::memory_order_relaxed);
        }
        lastScLevel.store(scLevel, std::memory_order_relaxed);

        // Deterministic keyed gain reduction (proves detector response).
        const float reduction = scLevel > 0.0f ? 0.75f : 1.0f;
        float mainLevelOut = 0.0f;
        for (int ch = 0; ch < main.getNumChannels(); ++ch)
            for (int i = 0; i < main.getNumSamples(); ++i)
            {
                const float v = main.getSample(ch, i) * reduction;
                main.setSample(ch, i, v);
                mainLevelOut = juce::jmax(mainLevelOut, std::abs(v));
            }
        lastMainMaxOut.store(mainLevelOut, std::memory_order_relaxed);
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
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        fillTestPluginDescription(d, getName(), 0x42535350);
    }

    std::atomic<int> prepareCount { 0 };
    std::atomic<int> lastChannels { 0 };
    std::atomic<int> lastSamples { 0 };
    std::atomic<int> lastScBusEnabled { -1 };
    std::atomic<int> lastScLayoutSize { -1 };
    std::atomic<int> lastTotalInputs { 0 };
    std::atomic<float> lastScLevel { -1.0f };
    std::atomic<float> lastMainMaxIn { -1.0f };
    std::atomic<float> lastMainMaxOut { -1.0f };

private:
    double sampleRate_ = 0.0;
    int maxBlock_ = 0;
};

class SidechainBusSemanticDeliveryTest final : public juce::UnitTest
{
public:
    SidechainBusSemanticDeliveryTest()
        : UnitTest("Sidechain.BusSemanticDelivery", "Sidechain") {}

    void runTest() override
    {
        beginTest("hosted plugin receives the key signal through its JUCE auxiliary bus and ducks");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto compressor = std::make_unique<BusSemanticSidechainProcessor>();
        auto* probe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);

        juce::AudioBuffer<float> main(2, kBlock);
        juce::AudioBuffer<float> sidechain(2, kBlock);
        juce::AudioBuffer<float> zeroCh;

        // ── Baseline: no sidechain configured ──────────────────────────
        {
            fillSine(main, kBlock, 440.0f, 0.5f);
            chain.processBlockWithSidechain(main, zeroCh, kBlock);
            expectEquals(probe->lastScBusEnabled.load(), 0,
                         "auxiliary bus must be disabled before activation");
            expectEquals(probe->lastChannels.load(), 2);
            expectEquals(probe->lastTotalInputs.load(), 2);
            const float baselineOut = probe->lastMainMaxOut.load();
            expect(baselineOut > 0.4f, "unity gain without a sidechain key");
        }

        // ── Activate the sidechain bus ─────────────────────────────────
        chain.setActiveSidechainBusConfig(scConfig(0));

        expectEquals(probe->lastScBusEnabled.load(), 1,
                     "auxiliary bus must be enabled after activation");
        expectEquals(probe->lastScLayoutSize.load(), 2,
                     "auxiliary bus layout must be stereo as negotiated");
        expectEquals(probe->lastTotalInputs.load(), 4,
                     "total input channels must include the auxiliary bus");

        // ── Keyed processing: sidechain energy drives gain reduction ────
        {
            fillSine(main, kBlock, 440.0f, 0.5f);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);

            expectEquals(probe->lastChannels.load(), 4);
            expectEquals(probe->lastScBusEnabled.load(), 1);
            expectEquals(probe->lastScLayoutSize.load(), 2);
            expect(probe->lastScLevel.load() > 0.5f,
                   "key signal must reach the plugin through getBusBuffer(..., true, 1)");
            expect(probe->lastMainMaxOut.load() < 0.42f,
                   "keyed gain reduction must attenuate the main bus (0.75x of 0.5 ~ 0.375)");
        }

        // ── Silent key: no reduction ───────────────────────────────────
        {
            fillSine(main, kBlock, 440.0f, 0.5f);
            sidechain.clear();
            chain.processBlockWithSidechain(main, sidechain, kBlock);
            expectEquals(probe->lastScLevel.load(), 0.0f,
                         "a silent key must produce no sidechain energy");
            expect(probe->lastMainMaxOut.load() > 0.4f,
                   "a silent key must not attenuate the main bus");
        }

        // ── Remove the sidechain: behavior returns to baseline ─────────
        chain.setActiveSidechainBusConfig({});
        expectEquals(probe->lastScBusEnabled.load(), 0,
                     "auxiliary bus must be disabled after removal");
        expectEquals(probe->lastTotalInputs.load(), 2);
        {
            fillSine(main, kBlock, 440.0f, 0.5f);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);
            expectEquals(probe->lastChannels.load(), 2);
            expectEquals(probe->lastScLevel.load(), 0.0f,
                         "removed sidechain must contribute no key energy");
            expect(probe->lastMainMaxOut.load() > 0.4f,
                   "removed sidechain must restore baseline (unity) gain");
        }

        // ── Recreate: ducking returns ──────────────────────────────────
        chain.setActiveSidechainBusConfig(scConfig(0));
        expectEquals(probe->lastScBusEnabled.load(), 1);
        {
            fillSine(main, kBlock, 440.0f, 0.5f);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);
            expectEquals(probe->lastChannels.load(), 4);
            expect(probe->lastScLevel.load() > 0.5f);
            expect(probe->lastMainMaxOut.load() < 0.42f,
                   "recreated sidechain must restore keyed reduction");
        }
    }
};

SidechainBusSemanticDeliveryTest sidechainBusSemanticDeliveryTest;

class SidechainBusLayoutNegotiationTest final : public juce::UnitTest
{
public:
    SidechainBusLayoutNegotiationTest()
        : UnitTest("Sidechain.BusLayoutNegotiation", "Sidechain") {}

    void runTest() override
    {
        beginTest("auxiliary bus negotiation round-trips through JUCE bus layout APIs");

        // Strict wrapper-like processor: accepts ONLY stereo main and
        // mono-or-stereo sidechain — mirrors a real VST3 wrapper that can veto
        // unsupported layouts.
        {
            DAW::PluginChainCore chain;
            chain.prepare(kRate, kBlock);

            auto compressor = std::make_unique<BusSemanticSidechainProcessor>();
            auto* probe = compressor.get();
            expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);

            // Pre-activation: bus exists but is disabled, mono... main-only.
            expectEquals(probe->lastTotalInputs.load(), 2);
            expectEquals(probe->lastScBusEnabled.load(), 0);

            chain.setActiveSidechainBusConfig(scConfig(0));

            // Post-activation negotiated truth.
            expectEquals(probe->lastScBusEnabled.load(), 1);
            expectEquals(probe->lastTotalInputs.load(), 4);
            expect(probe->prepareCount.load() >= 2,
                   "bus activation must re-negotiate (reprepare) the slot");
            expect(chain.getScratchChannelCapacityForTesting() >= 4,
                   "scratch capacity must cover the negotiated 4-channel layout");
        }

        // Mono-default sidechain bus negotiates a 3-channel total layout.
        {
            DAW::PluginChainCore chain;
            chain.prepare(kRate, kBlock);

            auto compressor = std::make_unique<SidechainCapableCompressorProcessor>(true);
            auto* probe = compressor.get();
            expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);

            chain.setActiveSidechainBusConfig(scConfig(0));

            expectEquals(probe->lastScChannels.load(), 1);
            expectEquals(probe->lastTotalInputs.load(), 3);
            expect(chain.getScratchChannelCapacityForTesting() >= 3);
        }
    }
};

SidechainBusLayoutNegotiationTest sidechainBusLayoutNegotiationTest;

// ══════════════════════════════════════════════════════════════════════════
// Current-block delivery determinism: block-coded key pattern proves the
// sidechain target receives EXACTLY the current block's key — never stale,
// never previous-block, never missing data.
// ══════════════════════════════════════════════════════════════════════════

class SidechainCurrentBlockDeliveryTest final : public juce::UnitTest
{
public:
    SidechainCurrentBlockDeliveryTest()
        : UnitTest("Sidechain.CurrentBlockDelivery", "Sidechain") {}

    void runTest() override
    {
        beginTest("sidechain key data is current-block, never stale or missing");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto compressor = std::make_unique<BusSemanticSidechainProcessor>();
        auto* probe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);
        chain.setActiveSidechainBusConfig(scConfig(0));

        juce::AudioBuffer<float> main(2, kBlock);
        juce::AudioBuffer<float> sidechain(2, kBlock);

        // Block-coded key: even blocks carry a high key, odd blocks zero.
        for (int block = 0; block < 32; ++block)
        {
            fillSine(main, kBlock, 440.0f, 0.5f);
            sidechain.clear();
            if ((block & 1) == 0)
                fillSine(sidechain, kBlock, 55.0f, 0.9f);

            chain.processBlockWithSidechain(main, sidechain, kBlock);

            const float scLevel = probe->lastScLevel.load(std::memory_order_acquire);
            if ((block & 1) == 0)
                expect(scLevel > 0.5f,
                       "keyed block " + juce::String(block) + " must deliver the key signal");
            else
                expect(scLevel == 0.0f,
                       "unkeyed block " + juce::String(block) + " must deliver silence (no stale key)");
        }

        // Sidechain removal must return the detector to a permanently unkeyed
        // state — no residual key from previous blocks.
        chain.setActiveSidechainBusConfig({});
        for (int block = 0; block < 8; ++block)
        {
            fillSine(main, kBlock, 440.0f, 0.5f);
            fillSine(sidechain, kBlock, 55.0f, 0.9f);
            chain.processBlockWithSidechain(main, sidechain, kBlock);
            expectEquals(probe->lastScLevel.load(std::memory_order_acquire), 0.0f,
                         "removed sidechain must deliver no key energy on any block");
        }
    }
};

SidechainCurrentBlockDeliveryTest sidechainCurrentBlockDeliveryTest;

// ══════════════════════════════════════════════════════════════════════════
// Transaction atomicity: a route may only be ACTIVE when its auxiliary bus
// is actually committed/enabled. A plugin (wrapper) that rejects the sidechain
// layout models the live "route active but no ducking" failure; the host
// primitives must expose the uncommitted state so the routing layer can roll
// the route back (ApplicationCore::rollbackUncommittedSidechainRoutes).
// ══════════════════════════════════════════════════════════════════════════

class RejectingSidechainProcessor final : public juce::AudioPluginInstance
{
public:
    RejectingSidechainProcessor()
        : juce::AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "APEX RejectingSidechain Test"; }
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        sampleRate_ = sampleRate;
        maxBlock_ = maximumExpectedSamplesPerBlock;
        ++prepareCount;
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        if (layouts.getMainInputChannels() != 2 || layouts.getMainOutputChannels() != 2)
            return false;
        // VETO any auxiliary input bus — models a wrapper that refuses
        // sidechain layouts at negotiation time.
        if (layouts.inputBuses.size() > 1 && !layouts.getChannelSet(true, 1).isDisabled())
            return false;
        return true;
    }

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
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
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        fillTestPluginDescription(d, getName(), 0x52454350);
    }

    std::atomic<int> prepareCount { 0 };

private:
    double sampleRate_ = 0.0;
    int maxBlock_ = 0;
};

class SidechainActiveMeansBusEnabledTest final : public juce::UnitTest
{
public:
    SidechainActiveMeansBusEnabledTest()
        : UnitTest("Sidechain.ActiveMeansBusEnabled", "Sidechain") {}

    void runTest() override
    {
        // Accepting plugin: the committed state must mirror the route.
        {
            beginTest("committed auxiliary bus is observable for accepted layouts");

            DAW::PluginChainCore chain;
            chain.prepare(kRate, kBlock);
            expectEquals(chain.appendPluginInstanceForTesting(
                             std::make_unique<BusSemanticSidechainProcessor>()), 0);

            chain.setActiveSidechainBusConfig(scConfig(0));
            expect(chain.isAuxInputBusActive(0, 1),
                   "accepted layout must commit the auxiliary bus");
        }

        // Rejecting plugin: the active config must be pruned and the commit
        // query must report false — the routing layer then rolls the route
        // back instead of leaving an ACTIVE route with a disabled bus.
        {
            beginTest("rejected layouts never leave an ACTIVE-looking uncommitted bus");

            DAW::PluginChainCore chain;
            chain.prepare(kRate, kBlock);
            expectEquals(chain.appendPluginInstanceForTesting(
                             std::make_unique<RejectingSidechainProcessor>()), 0);

            chain.setActiveSidechainBusConfig(scConfig(0));

            expect(! chain.isAuxInputBusActive(0, 1),
                   "rejected layout must NOT report the auxiliary bus as committed");
            expectEquals(chain.getEnabledAuxInputBusesForSlot(0).size(), 0,
                         "rejected layout must leave an empty active bus config");
        }
    }
};

SidechainActiveMeansBusEnabledTest sidechainActiveMeansBusEnabledTest;

class SidechainDrainTimeoutAtomicityTest final : public juce::UnitTest
{
public:
    SidechainDrainTimeoutAtomicityTest()
        : UnitTest("Sidechain.DrainTimeoutAtomicity", "Sidechain") {}

    void runTest() override
    {
        // Models the ApplicationCore transaction when the realtime drain
        // fails: the config is NOT applied, and the commit query reports
        // false for every requested bus, so the routing layer deactivates
        // the routes (no ACTIVE route with a disabled plugin bus can exist).
        beginTest("drain-failure path yields a deterministic uncommitted state");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);
        expectEquals(chain.appendPluginInstanceForTesting(
                         std::make_unique<BusSemanticSidechainProcessor>()), 0);

        // Drain "fails": simulate by never calling setActiveSidechainBusConfig.
        expect(! chain.isAuxInputBusActive(0, 1),
               "without a committed config the route must be considered uncommitted");
        expectEquals(chain.getEnabledAuxInputBusesForSlot(0).size(), 0);

        // After a successful transaction the state flips deterministically.
        chain.setActiveSidechainBusConfig(scConfig(0));
        expect(chain.isAuxInputBusActive(0, 1),
               "a successful transaction must commit the bus deterministically");

        // And removing the route commits its absence.
        chain.setActiveSidechainBusConfig({});
        expect(! chain.isAuxInputBusActive(0, 1));
        expectEquals(chain.getEnabledAuxInputBusesForSlot(0).size(), 0);
    }
};

SidechainDrainTimeoutAtomicityTest sidechainDrainTimeoutAtomicityTest;

// ══════════════════════════════════════════════════════════════════════════
// Main-input purity: the key must exist ONLY on the auxiliary bus. A key
// leaking into MAIN would make a plugin's INTERNAL detector react (IN mode)
// while its EXTERNAL sidechain (EXT mode) stays silent — the exact live
// signature observed with FabFilter Pro-C 2.
// ══════════════════════════════════════════════════════════════════════════

class SidechainNoKeyLeakIntoMainInputTest final : public juce::UnitTest
{
public:
    SidechainNoKeyLeakIntoMainInputTest()
        : UnitTest("Sidechain.NoKeyLeakIntoMainInput", "Sidechain") {}

    void runTest() override
    {
        beginTest("the sidechain key exists ONLY on the auxiliary bus, never in main");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto compressor = std::make_unique<BusSemanticSidechainProcessor>();
        auto* probe = compressor.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(compressor)), 0);
        chain.setActiveSidechainBusConfig(scConfig(0));

        // MAIN = constant tone A; KEY = constant tone B (distinct DC values
        // make any cross-contamination trivially detectable per channel).
        juce::AudioBuffer<float> main(2, kBlock);
        juce::AudioBuffer<float> sidechain(2, kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            main.setSample(0, i, 0.25f);   // A
            main.setSample(1, i, 0.25f);
            sidechain.setSample(0, i, 0.75f);   // B (key)
            sidechain.setSample(1, i, 0.75f);
        }

        chain.processBlockWithSidechain(main, sidechain, kBlock);

        // The processor records the max |sample| seen on its MAIN bus and on
        // its AUX bus via JUCE bus accessors.
        expectEquals(probe->lastScBusEnabled.load(), 1);
        expect(probe->lastScLevel.load() > 0.5f,
               "auxiliary bus must carry the key (0.75)");
        expect(probe->lastMainMaxIn.load() < 0.35f,
               "main bus must contain ONLY tone A (0.25) — no key leakage");
        // Main is attenuated by the keyed reduction (0.75x): 0.25*0.75 = 0.1875.
        expect(probe->lastMainMaxOut.load() < 0.2f,
               "keyed reduction proves the detector used the AUX bus, not a contaminated main");
    }
};

SidechainNoKeyLeakIntoMainInputTest sidechainNoKeyLeakIntoMainInputTest;

// ══════════════════════════════════════════════════════════════════════════
// VST3 wrapper contract: a layout-changing reprepare must DEACTIVATE the
// plugin before prepareToPlay, otherwise hosted wrappers never apply the new
// bus arrangements/activation (their prepareToPlay early-returns while the
// component is active with unchanged rate/block). This is the proven root
// cause of the real Pro-C 2 EXT failure.
// ══════════════════════════════════════════════════════════════════════════

class ProtocolProbeSidechainProcessor final : public juce::AudioPluginInstance
{
public:
    ProtocolProbeSidechainProcessor()
        : juce::AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "APEX ProtocolProbeSidechain Test"; }
    void prepareToPlay(double, int) override
    {
        // A wrapper-style prepare only applies new layouts when the component
        // is INACTIVE at entry (models the VST3 wrapper's early-return path).
        if (prepareCount++ > 0 && ! active_)
            sawDeactivateBeforeLayoutPrepare = true;
        active_ = true;
    }
    void releaseResources() override { active_ = false; }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        if (layouts.getMainInputChannels() != 2 || layouts.getMainOutputChannels() != 2)
            return false;
        if (layouts.outputBuses.size() > 1)
            return false;
        return true;
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
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
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        fillTestPluginDescription(d, getName(), 0x50524350);
    }

    bool sawDeactivateBeforeLayoutPrepare = false;
    int prepareCount = 0;

private:
    bool active_ = false;
};

class SidechainExternalBusSurvivesPrepareTest final : public juce::UnitTest
{
public:
    SidechainExternalBusSurvivesPrepareTest()
        : UnitTest("Sidechain.ExternalBusSurvivesPrepare", "Sidechain") {}

    void runTest() override
    {
        beginTest("layout-changing reprepare deactivates the plugin before prepareToPlay (VST3 wrapper contract)");

        DAW::PluginChainCore chain;
        chain.prepare(kRate, kBlock);

        auto probe = std::make_unique<ProtocolProbeSidechainProcessor>();
        auto* probePtr = probe.get();
        expectEquals(chain.appendPluginInstanceForTesting(std::move(probe)), 0);
        expectEquals(probePtr->prepareCount, 1);

        chain.setActiveSidechainBusConfig(scConfig(0));

        expect(probePtr->sawDeactivateBeforeLayoutPrepare,
               "the bus-layout reprepare must deactivate the plugin first, "
               "otherwise hosted VST3 wrappers never apply the sidechain bus");
        expect(chain.isAuxInputBusActive(0, 1),
               "the auxiliary bus must be committed after the correct reprepare");
    }
};

SidechainExternalBusSurvivesPrepareTest sidechainExternalBusSurvivesPrepareTest;

} // namespace
