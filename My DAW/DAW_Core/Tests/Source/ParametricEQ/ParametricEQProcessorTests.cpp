#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <thread>

namespace
{

using namespace APEX::ParametricEQ;

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    jassert (parameter != nullptr);
    parameter->setValue (parameter->toNormalised (units));
}

void configureBell (Processor& processor, int band, float frequency,
                    float gain, float q)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (FilterShape::Bell));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
}

juce::MemoryBlock serialise (Processor& processor)
{
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    return state;
}

} // namespace

class ParametricEQProcessorTests final : public juce::UnitTest
{
public:
    ParametricEQProcessorTests()
        : UnitTest ("ParametricEQ.Processor", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testIdentityAndParameterAbi();
        testMappingsAndAtomicPublication();
        testLifecycleAndBuses();
        testStateRoundTripAndTolerance();
        testProcessorResponseAndReset();
        testAutomationTransitionAndBlockIndependence();
        testBypassContract();
        testOutputStageAndPhase9Abi();
        testRealtimeAllocation();
    }

private:
    void testIdentityAndParameterAbi()
    {
        beginTest ("identity and permanent 24-slot parameter ABI");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        juce::PluginDescription description;
        processor.fillInPluginDescription (description);

        expectEquals (processor.getName(), juce::String (Processor::kPluginName));
        expectEquals (description.fileOrIdentifier,
                      juce::String (Processor::kFileOrIdentifier));
        expectEquals (description.uniqueId, Processor::kUniqueId);
        expectEquals (processor.getParameters().size(), kNumParameters);
        expect (processor.getBypassParameter()
                == processor.getParametricEQParameter (kGlobalBypassParameter));

        std::set<std::string> ids;
        for (int index = 0; index < kNumParameters; ++index)
        {
            auto* parameter = processor.getParametricEQParameter (index);
            expect (parameter != nullptr, "parameter " + juce::String (index));
            if (parameter == nullptr)
                continue;
            expect (parameter->isStorageLockFree(),
                    "atomic storage must be lock-free: " + parameter->paramID);
            expect (parameter->paramID == Processor::getParameterId (index),
                    "stable ID mismatch at " + juce::String (index));
            expect (ids.insert (parameter->paramID.toStdString()).second,
                    "duplicate stable ID " + parameter->paramID);
        }

        expectEquals (Processor::getParameterId (0),
                      juce::String ("peq.band01.enabled"));
        expectEquals (Processor::getParameterId (parameterIndex (
                          23, BandParameterOffset::Slope)),
                      juce::String ("peq.band24.slope"));
        expectEquals (Processor::getParameterId (kDesignModeParameter),
                      juce::String ("peq.design"));
        expectEquals (Processor::getParameterId (kGlobalBypassParameter),
                      juce::String ("peq.bypass"));
        expectEquals (Processor::getParameterId (kOutputGainParameter),
                      juce::String ("peq.output.gain"));
        expectEquals (Processor::getParameterId (kOutputPanParameter),
                      juce::String ("peq.output.pan"));
        expectEquals (Processor::getParameterId (kOutputPhaseInvertParameter),
                      juce::String ("peq.output.phaseInvert"));
        expectEquals (Processor::getParameterId (kOutputAutoGainParameter),
                      juce::String ("peq.output.autoGain"));
        expectEquals (Processor::getParameterId (kOutputGainScaleParameter),
                      juce::String ("peq.output.gainScale"));
        expectWithinAbsoluteError (
            processor.getParametricEQParameter (kOutputGainParameter)->getUnitsValue(),
            0.0f, 0.0f);
        expectWithinAbsoluteError (
            processor.getParametricEQParameter (kOutputPanParameter)->getUnitsValue(),
            0.0f, 0.0f);
        expect (! processor.getParametricEQParameter (
            kOutputPhaseInvertParameter)->getBool());
        expect (! processor.getParametricEQParameter (
            kOutputAutoGainParameter)->getBool());
        expectWithinAbsoluteError (
            processor.getParametricEQParameter (kOutputGainScaleParameter)->getUnitsValue(),
            1.0f, 0.0f);

        for (int band = 0; band < kMaxBands; ++band)
        {
            expect (! processor.getParametricEQParameter (parameterIndex (
                band, BandParameterOffset::Enabled))->getBool());
            expectWithinAbsoluteError (
                processor.getParametricEQParameter (parameterIndex (
                    band, BandParameterOffset::Frequency))->getUnitsValue(),
                1000.0f, 0.01f);
            expectWithinAbsoluteError (
                processor.getParametricEQParameter (parameterIndex (
                    band, BandParameterOffset::Q))->getUnitsValue(), 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (
                processor.getParametricEQParameter (parameterIndex (
                    band, BandParameterOffset::Slope))->getUnitsValue(),
                12.0f, 1.0e-5f);
        }
    }

    void testMappingsAndAtomicPublication()
    {
        beginTest ("parameter laws, choices and concurrent atomic publication");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        auto* frequency = processor.getParametricEQParameter (parameterIndex (
            0, BandParameterOffset::Frequency));
        auto* q = processor.getParametricEQParameter (parameterIndex (
            0, BandParameterOffset::Q));
        auto* gain = processor.getParametricEQParameter (parameterIndex (
            0, BandParameterOffset::Gain));
        auto* shape = processor.getParametricEQParameter (parameterIndex (
            0, BandParameterOffset::Shape));

        for (const float value : { 5.0f, 20.0f, 1000.0f, 20000.0f, 40000.0f })
            expectWithinAbsoluteError (
                frequency->fromNormalised (frequency->toNormalised (value)),
                value, std::max (0.01f, value * 2.0e-6f));
        for (const float value : { 0.025f, 0.1f, 1.0f, 10.0f, 100.0f })
            expectWithinAbsoluteError (q->fromNormalised (q->toNormalised (value)),
                                       value, std::max (1.0e-6f, value * 2.0e-6f));
        expectWithinAbsoluteError (
            frequency->fromNormalised (frequency->getValueForText ("2.5 kHz")),
            2500.0f, 0.1f);
        expectWithinAbsoluteError (
            gain->fromNormalised (gain->getValueForText ("-18.5 dB")),
            -18.5f, 1.0e-4f);
        expectEquals (shape->getText (shape->getValueForText ("Flat Tilt"), 32),
                      juce::String ("Flat Tilt"));

        std::atomic<bool> stop { false };
        std::atomic<bool> badRead { false };
        std::thread writer ([&]
        {
            for (int iteration = 0; iteration < 200000; ++iteration)
                frequency->setValue (static_cast<float> (iteration & 1023) / 1023.0f);
            stop.store (true, std::memory_order_release);
        });
        while (! stop.load (std::memory_order_acquire))
        {
            const auto value = frequency->getValue();
            if (! std::isfinite (value) || value < 0.0f || value > 1.0f)
                badRead.store (true, std::memory_order_relaxed);
        }
        writer.join();
        expect (! badRead.load(), "atomic reader observed a torn/invalid value");
    }

    void testLifecycleAndBuses()
    {
        beginTest ("prepare/reset lifecycle, mono/stereo buses, latency and tail");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 257);
        expectWithinAbsoluteError (processor.getSampleRate(), 48000.0, 0.0);
        expectEquals (processor.getLatencySamples(), 0);
        expectWithinAbsoluteError (processor.getTailLengthSeconds(), 0.0, 0.0);
        expect (! processor.acceptsMidi() && ! processor.producesMidi());
        expect (processor.hasEditor());
        {
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (
                processor.createEditor());
            expect (editor != nullptr);
            editor.reset();
        }

        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        expect (processor.isBusesLayoutSupported (mono));
        juce::AudioProcessor::BusesLayout stereo;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        expect (processor.isBusesLayoutSupported (stereo));
        juce::AudioProcessor::BusesLayout mismatch;
        mismatch.inputBuses.add (juce::AudioChannelSet::mono());
        mismatch.outputBuses.add (juce::AudioChannelSet::stereo());
        expect (! processor.isBusesLayoutSupported (mismatch));

        processor.reset();
        processor.releaseResources();
        processor.prepareToPlay (96000.0, 31);
        expectWithinAbsoluteError (processor.getSampleRate(), 96000.0, 0.0);
    }

    void testStateRoundTripAndTolerance()
    {
        beginTest ("versioned state round-trip and tolerant transactional fields");
        auto sourceStorage = std::make_unique<Processor>();
        auto& source = *sourceStorage;
        configureBell (source, 0, 1234.0f, 8.5f, 4.0f);
        setUnits (source, parameterIndex (7, BandParameterOffset::Enabled), 1.0f);
        setUnits (source, parameterIndex (7, BandParameterOffset::Shape),
                  static_cast<float> (FilterShape::LowCut));
        setUnits (source, parameterIndex (7, BandParameterOffset::Frequency), 72.0f);
        setUnits (source, parameterIndex (7, BandParameterOffset::Slope), 47.5f);
        setUnits (source, kDesignModeParameter, 1.0f);
        setUnits (source, kOutputGainParameter, 4.5f);
        setUnits (source, kOutputPanParameter, -0.25f);
        setUnits (source, kOutputPhaseInvertParameter, 1.0f);
        setUnits (source, kOutputAutoGainParameter, 1.0f);
        setUnits (source, kOutputGainScaleParameter, 0.75f);
        source.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Pre);

        const auto block = serialise (source);
        expect (block.getSize() > 0);
        auto restoredStorage = std::make_unique<Processor>();
        auto& restored = *restoredStorage;
        restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        for (int index = 0; index < kNumParameters; ++index)
            expectWithinAbsoluteError (
                restored.getParametricEQParameter (index)->getValue(),
                source.getParametricEQParameter (index)->getValue(), 0.0f,
                "state mismatch at " + Processor::getParameterId (index));
        expect (restored.getAnalyzerMode() == APEX::Analysis::SpectrumTapMode::Pre);
        restored.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Closed);
        source.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Closed);

        juce::ValueTree tolerant (Processor::kStateTag);
        tolerant.setProperty (Processor::kVersionProperty, 99, nullptr);
        tolerant.setProperty ("future.unknown", "ignored", nullptr);
        tolerant.setProperty (Processor::getParameterId (parameterIndex (
                                  0, BandParameterOffset::Gain)),
                              -999.0f, nullptr);
        tolerant.setProperty (Processor::getParameterId (parameterIndex (
                                  0, BandParameterOffset::Q)),
                              juce::var (std::numeric_limits<float>::quiet_NaN()),
                              nullptr);
        juce::MemoryBlock tolerantBlock;
        juce::MemoryOutputStream tolerantStream (tolerantBlock, false);
        tolerant.writeToStream (tolerantStream);

        auto targetStorage = std::make_unique<Processor>();
        auto& target = *targetStorage;
        const auto qDefault = target.getParametricEQParameter (parameterIndex (
            0, BandParameterOffset::Q))->getValue();
        target.setStateInformation (tolerantBlock.getData(),
                                    static_cast<int> (tolerantBlock.getSize()));
        expectEquals (target.getParametricEQParameter (parameterIndex (
                          0, BandParameterOffset::Gain))->getValue(), 0.0f);
        expectEquals (target.getParametricEQParameter (parameterIndex (
                          0, BandParameterOffset::Q))->getValue(), qDefault);

        juce::ValueTree foreign ("foreignstate");
        foreign.setProperty (Processor::getParameterId (kGlobalBypassParameter),
                             1.0f, nullptr);
        juce::MemoryBlock foreignBlock;
        juce::MemoryOutputStream foreignStream (foreignBlock, false);
        foreign.writeToStream (foreignStream);
        target.setStateInformation (foreignBlock.getData(),
                                    static_cast<int> (foreignBlock.getSize()));
        expect (! target.getParametricEQParameter (kGlobalBypassParameter)->getBool(),
                "foreign schema must not mutate processor state");
    }

    void testProcessorResponseAndReset()
    {
        beginTest ("processor adopts state before prepare and reset clears histories");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 128);
        expectWithinAbsoluteError (
            processor.getCurrentEngineForTesting().getMagnitudeDb (1000.0),
            6.0, 0.02);

        juce::AudioBuffer<float> impulse (1, 128);
        juce::MidiBuffer midi;
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        processor.processBlock (impulse, midi);
        std::array<float, 128> first {};
        for (int sample = 0; sample < 128; ++sample)
            first[static_cast<std::size_t> (sample)] = impulse.getSample (0, sample);

        processor.reset();
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        processor.processBlock (impulse, midi);
        bool same = true;
        for (int sample = 0; sample < 128; ++sample)
            same &= impulse.getSample (0, sample)
                 == first[static_cast<std::size_t> (sample)];
        expect (same, "reset must reproduce the prepared impulse response exactly");
    }

    static std::vector<float> renderAutomation (int blockSize)
    {
        constexpr int total = 4096;
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, blockSize);
        configureBell (processor, 0, 1000.0f, 12.0f, 8.0f);

        std::vector<float> result (total);
        juce::AudioBuffer<float> block (1, blockSize);
        juce::MidiBuffer midi;
        int position = 0;
        while (position < total)
        {
            const int count = std::min (blockSize, total - position);
            for (int sample = 0; sample < count; ++sample)
                block.setSample (0, sample, 0.1f * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 997.0
                    * (position + sample) / 48000.0));
            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(),
                                           1, 0, count);
            processor.processBlock (view, midi);
            for (int sample = 0; sample < count; ++sample)
                result[static_cast<std::size_t> (position + sample)]
                    = block.getSample (0, sample);
            position += count;
        }
        return result;
    }

    void testAutomationTransitionAndBlockIndependence()
    {
        beginTest ("coefficient changes crossfade without resets and are block independent");
        const auto one = renderAutomation (1);
        const auto thirtyOne = renderAutomation (31);
        const auto fiveTwelve = renderAutomation (512);
        std::size_t firstDifference = one.size();
        double maximumDifference = 0.0;
        for (std::size_t sample = 0; sample < one.size(); ++sample)
        {
            const auto difference31 = std::abs (
                static_cast<double> (one[sample]) - thirtyOne[sample]);
            const auto difference512 = std::abs (
                static_cast<double> (one[sample]) - fiveTwelve[sample]);
            const auto difference = std::max (difference31, difference512);
            if (difference > 0.0 && firstDifference == one.size())
                firstDifference = sample;
            maximumDifference = std::max (maximumDifference, difference);
        }
        expect (firstDifference == one.size(),
                "automation transition must be sample-order invariant; first="
                    + juce::String (static_cast<int> (firstDifference))
                    + " max=" + juce::String (maximumDifference, 12));
        expect (ParametricEQTest::allFinite (one));

        double maximumJump = 0.0;
        for (std::size_t sample = 1; sample < one.size(); ++sample)
            maximumJump = std::max (maximumJump,
                                    std::abs (static_cast<double> (one[sample])
                                            - one[sample - 1]));
        expect (maximumJump < 0.15,
                "transition contains a click-sized discontinuity: "
                    + juce::String (maximumJump, 6));
    }

    void testBypassContract()
    {
        beginTest ("global bypass crossfades then becomes bit-exact dry");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 18.0f, 5.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, kGlobalBypassParameter, 1.0f);

        juce::AudioBuffer<float> transition (2, 512);
        ParametricEQTest::fillDeterministic (transition);
        juce::MidiBuffer midi;
        processor.processBlock (transition, midi);
        expectWithinAbsoluteError (processor.getBypassMixForTesting(), 1.0, 1.0e-12);

        juce::AudioBuffer<float> dry (2, 333);
        ParametricEQTest::fillDeterministic (dry, 0x77331122u);
        juce::AudioBuffer<float> expected;
        expected.makeCopyOf (dry);
        processor.processBlock (dry, midi);
        expect (ParametricEQTest::bitEqual (dry, expected),
                "settled bypass must be exact dry wire");

        setUnits (processor, kGlobalBypassParameter, 0.0f);
        processor.processBlock (dry, midi);
        expect (processor.getBypassMixForTesting() < 1.0,
                "un-bypass must begin from the current dry state");
    }

    void testOutputStageAndPhase9Abi()
    {
        beginTest ("Phase 9 output stage is neutral by default and applies gain/pan/polarity");

        auto neutralStorage = std::make_unique<Processor>();
        auto& neutral = *neutralStorage;
        neutral.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> neutralBuffer (2, 256);
        ParametricEQTest::fillDeterministic (neutralBuffer, 0xA901u);
        juce::AudioBuffer<float> neutralExpected;
        neutralExpected.makeCopyOf (neutralBuffer);
        juce::MidiBuffer midi;
        neutral.processBlock (neutralBuffer, midi);
        expect (ParametricEQTest::bitEqual (neutralBuffer, neutralExpected),
                "default Phase 9 output controls must be bit-neutral");

        auto outputStorage = std::make_unique<Processor>();
        auto& output = *outputStorage;
        output.prepareToPlay (48000.0, 256);
        setUnits (output, kOutputGainParameter, 6.0f);
        setUnits (output, kOutputPanParameter, 1.0f);
        setUnits (output, kOutputPhaseInvertParameter, 1.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        ParametricEQTest::fillDeterministic (buffer, 0xA902u);
        juce::AudioBuffer<float> expected;
        expected.makeCopyOf (buffer);
        const float gain = juce::Decibels::decibelsToGain (6.0f);
        output.processBlock (buffer, midi);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            expectWithinAbsoluteError (buffer.getSample (0, sample), 0.0f, 1.0e-7f);
            expectWithinAbsoluteError (
                buffer.getSample (1, sample),
                -expected.getSample (1, sample) * gain, 2.0e-6f);
        }

        auto scaledStorage = std::make_unique<Processor>();
        auto& scaled = *scaledStorage;
        configureBell (scaled, 0, 1000.0f, 6.0f, 2.0f);
        setUnits (scaled, kOutputGainScaleParameter, 0.5f);
        scaled.prepareToPlay (48000.0, 128);
        expectWithinAbsoluteError (
            scaled.getCurrentEngineForTesting().getMagnitudeDb (1000.0),
            3.0, 0.03,
            "50% gain scale must halve static EQ gain before coefficient design");
    }

    void testRealtimeAllocation()
    {
        beginTest ("processBlock allocates zero memory during target adoption and transitions");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 128);
        for (int band = 0; band < kMaxBands; ++band)
        {
            setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
            setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
                      static_cast<float> (band % 10));
            setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
                      20.0f * std::pow (1.32f, static_cast<float> (band)));
            setUnits (processor, parameterIndex (band, BandParameterOffset::Gain),
                      (band & 1) != 0 ? 12.0f : -9.0f);
            setUnits (processor, parameterIndex (band, BandParameterOffset::Q),
                      0.1f + 0.4f * band);
            setUnits (processor, parameterIndex (band, BandParameterOffset::Slope),
                      6.0f + (band % 16) * 6.0f);
        }

        juce::AudioBuffer<float> buffer (2, 128);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 64; ++iteration)
                processor.processBlock (buffer, midi);
        }
    }
};

static ParametricEQProcessorTests parametricEQProcessorTests;
