#include <JuceHeader.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <intrin.h>
#endif

namespace
{
constexpr int kFixtureLatencySamples = 32;

// ── Phase D fault sentinels (test-only; normal audio can never produce NaN) ──
// Quiet NaN payloads. Detection is EXACT BIT LEVEL: exponent all-ones plus a
// nonzero mantissa whose low 16 bits carry the trigger code. Finite audio,
// infinities, signaling NaNs, and other NaN payloads are all ignored.
constexpr std::uint32_t kCrashSentinelBits = 0x7FC0CAFEu;   // NaN payload 0xCAFE → hard crash
constexpr std::uint32_t kHangSentinelBits  = 0x7FC0BEEFu;   // NaN payload 0xBEEF → infinite hang

void triggerHardCrash()
{
    // Deliberate hard native termination INSIDE the sandbox worker process.
    //
    // __fastfail raises an uncatchable fail-fast exception. Unlike a plain
    // access violation, fail-fast exceptions are exempt from the Windows
    // Just-In-Time debugger offer (AeDebug) and terminate the process
    // immediately with a WER report — no dialog can block the worker on a
    // developer machine. The parent still observes an abnormal worker death.
   #if JUCE_WINDOWS
    __fastfail(0xCAFE);
   #else
    std::abort();
   #endif
}

void triggerHardHang()
{
    // processBlock never returns; the worker process stays alive.
    for (;;)
        juce::Thread::sleep(1000);
}

void checkFaultSentinel(float sample)
{
    const auto bits = *reinterpret_cast<const std::uint32_t*>(&sample);
    if ((bits & 0x7F800000u) != 0x7F800000u)   // not exponent all-ones
        return;
    if ((bits & 0x007FFFFFu) == 0u)            // infinity, not NaN
        return;

    const std::uint16_t payload = static_cast<std::uint16_t>(bits & 0xFFFFu);
    if (payload == 0xCAFE)
        triggerHardCrash();
    if (payload == 0xBEEF)
        triggerHardHang();
}

class APEXTestVST3FaultProcessor final : public juce::AudioProcessor
{
public:
    APEXTestVST3FaultProcessor()
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        // Suppress Windows Error Reporting dialogs for the deliberate fault
        // sentinels (fixture process only — the worker must terminate hard
        // without popping a WER dialog during unattended test runs).
       #if JUCE_WINDOWS
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
       #endif
        setLatencySamples(kFixtureLatencySamples);
    }

    const juce::String getName() const override { return "APEX Test VST3 Fault"; }

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
                checkFaultSentinel(input);          // Phase D trigger only
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
    return new APEXTestVST3FaultProcessor();
}
