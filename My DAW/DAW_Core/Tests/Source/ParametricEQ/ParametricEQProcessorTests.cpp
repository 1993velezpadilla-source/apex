#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"
#include "../../../Source/ParametricEQCore/ParametricEQSpectralDynamicsCore.h"
#include "../../../Source/ParametricEQCore/ParametricEQSpectralBlend.h"

#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <thread>
#include <vector>

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
        testRapidRealtimeBandEditsNeverMute();
        testBypassContract();
        testOutputStageAndPhase9Abi();
        testRealtimeAllocation();
        testSpectralEngineStandalone();
        testSpectralAlignedBlend();
        testSpectralHostGate();
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

    void testRapidRealtimeBandEditsNeverMute()
    {
        beginTest ("rapid audio-rate GUI retargets preserve finite continuous vocals");
        auto holder = std::make_unique<Processor>();
        auto& processor = *holder;
        configureBell (processor, 0, 1000.0f, 6.0f, 3.0f);
        processor.prepareToPlay (48000.0, 128);
        juce::MidiBuffer midi;

        double minimumRms = 1.0;
        double largestAdjacentStep = 0.0;
        double previous = 0.0;
        int sampleIndex = 0;
        for (int blockIndex = 0; blockIndex < 120; ++blockIndex)
        {
            // Alternate controls faster than the 10 ms coefficient fade.
            // No in-flight filter history may be rewritten under its output.
            setUnits (processor, parameterIndex (
                0, BandParameterOffset::Frequency),
                (blockIndex % 2) == 0 ? 240.0f : 6400.0f);
            setUnits (processor, parameterIndex (
                0, BandParameterOffset::Gain),
                (blockIndex % 4) < 2 ? -12.0f : 12.0f);

            juce::AudioBuffer<float> buffer (2, 128);
            for (int sample = 0; sample < 128; ++sample)
            {
                const auto input = static_cast<float> (
                    0.20 * std::sin (6.283185307179586 * 330.0
                        * static_cast<double> (sampleIndex++) / 48000.0));
                buffer.setSample (0, sample, input);
                buffer.setSample (1, sample, input);
            }
            processor.processBlock (buffer, midi);
            double energy = 0.0;
            for (int sample = 0; sample < 128; ++sample)
            {
                const double output = buffer.getSample (0, sample);
                expect (std::isfinite (output), "non-finite vocal output");
                energy += output * output;
                if (blockIndex > 0 || sample > 0)
                    largestAdjacentStep = std::max (largestAdjacentStep,
                                                    std::abs (output - previous));
                previous = output;
            }
            // A live singer's RMS must not collapse to digital silence while
            // a pointer is held or coefficients are being redesigned.
            minimumRms = std::min (minimumRms, std::sqrt (energy / 128.0));
        }
        expect (minimumRms > 0.005,
                "continuous vocal must not drop out during rapid retargets");
        expect (largestAdjacentStep < 0.30,
                "moving filters must not create a click-sized output step");
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


    void testSpectralEngineStandalone()
    {
        beginTest ("standalone APEX spectral STFT: exact reported delay, "
                   "neutral reconstruction and block independence");
        constexpr int total = 16384;
        const std::array<int, 4> blocks {{64, 128, 256, 512}};
        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            std::vector<float> source (total), output (total);
            for (int i = 0; i < total; ++i)
                source[static_cast<std::size_t> (i)] = static_cast<float> (
                    0.2 * std::sin (2.0 * juce::MathConstants<double>::pi
                        * 1000.0 * i / rate)
                    + 0.07 * std::sin (2.0
                        * juce::MathConstants<double>::pi * 4700.0
                        * i / rate));
            for (int block : blocks)
            {
                auto engine = std::make_unique<SpectralDynamicsCore>();
                engine->prepare (rate);
                std::array<SpectralDynamicsCore::Band, kMaxBands> bands {};
                for (int offset = 0; offset < total; offset += block)
                {
                    const int samples = std::min (block, total - offset);
                    std::copy (source.begin() + offset,
                               source.begin() + offset + samples,
                               output.begin() + offset);
                    float* channels[] { output.data() + offset };
                    engine->process (channels, 1, samples, bands);
                }
                double maxError = 0.0;
                double preDelayPeak = 0.0;
                for (int i = 0; i < total; ++i)
                {
                    expect (std::isfinite (output[static_cast<std::size_t> (i)]),
                            "reconstructed audio must be finite");
                    if (i < SpectralDynamicsCore::kLatencySamples)
                        preDelayPeak = std::max (preDelayPeak, std::abs (
                            static_cast<double> (output[
                                static_cast<std::size_t> (i)])));
                    else
                        maxError = std::max (maxError, std::abs (
                            static_cast<double> (output[
                                static_cast<std::size_t> (i)])
                            - source[static_cast<std::size_t> (
                                i - SpectralDynamicsCore::kLatencySamples)]));
                }
                expect (preDelayPeak < 1.0e-7,
                    "no pre-echo before fixed lookahead latency");
                expect (maxError < 1.0e-4,
                    "sqrt-Hann overlap/add unity reconstruction must "
                    "match precisely for rate " + juce::String (rate)
                    + " block " + juce::String (block));
            }
        }

        beginTest ("selective spectral band attenuates an excited resonance, "
                   "leaving an unrelated frequency essentially unchanged");
        constexpr double rate = 48000.0;
        constexpr int length = 32768;
        std::vector<float> input (length), output (length);
        for (int i = 0; i < length; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            input[static_cast<std::size_t> (i)] = static_cast<float> (
                0.16 * std::sin (2.0 * juce::MathConstants<double>::pi
                    * 1000.0 * t)
                + 0.16 * std::sin (2.0 * juce::MathConstants<double>::pi
                    * 5000.0 * t));
        }
        auto engine = std::make_unique<SpectralDynamicsCore>();
        engine->prepare (rate);
        std::array<SpectralDynamicsCore::Band, kMaxBands> bands {};
        auto& selected = bands[0];
        selected.enabled = true;
        selected.shape = FilterShape::Bell;
        selected.placement = ChannelPlacement::Stereo;
        selected.frequencyHz = 1000.0;
        selected.q = 10.0;
        selected.density = 1.0;
        selected.tilt = false;
        selected.dynamic.enabled = true;
        selected.dynamic.thresholdDb = -36.0;
        selected.dynamic.rangeDb = 18.0;
        selected.dynamic.attackSeconds = 0.002;
        selected.dynamic.releaseSeconds = 0.03;
        output = input;
        for (int offset = 0; offset < length; offset += 128)
        {
            float* channels[] { output.data() + offset };
            engine->process (channels, 1, std::min (128, length - offset), bands);
        }
        auto amplitude = [] (const std::vector<float>& audio, int start,
                             int count, double hz)
        {
            double c = 0.0, s = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const double angle = 2.0
                    * juce::MathConstants<double>::pi * hz * i / 48000.0;
                const double value = audio[
                    static_cast<std::size_t> (start + i)];
                c += value * std::cos (angle);
                s += value * std::sin (angle);
            }
            return (2.0 / count) * std::hypot (c, s);
        };
        constexpr int measure = 4800; // coherent DFT period for both tones
        const double before1 = amplitude (input, 20000, measure, 1000.0);
        const double after1 = amplitude (output, 20000
            + SpectralDynamicsCore::kLatencySamples, measure, 1000.0);
        const double before5 = amplitude (input, 20000, measure, 5000.0);
        const double after5 = amplitude (output, 20000
            + SpectralDynamicsCore::kLatencySamples, measure, 5000.0);
        expect (after1 < before1 * 0.72,
                "narrow spectral action must reduce triggered 1 kHz tone");
        expect (std::abs (after5 / before5 - 1.0) < 0.06,
                "unrelated 5 kHz tone must remain essentially intact");

        beginTest ("stereo Mid/Side frequency masking honors channel placement");
        // Perfectly anti-correlated signal has zero Mid and full Side.
        // A Mid-only spectral band must preserve it, while Side must attenuate.
        auto renderPair = [&selected, &bands] (ChannelPlacement placement)
        {
            constexpr int frames = 16384;
            std::array<float, frames> left {}, right {};
            for (int i = 0; i < frames; ++i)
            {
                const float value = static_cast<float> (
                    0.25 * std::sin (2.0
                        * juce::MathConstants<double>::pi * 1000.0
                        * i / 48000.0));
                left[static_cast<std::size_t> (i)] = value;
                right[static_cast<std::size_t> (i)] = -value;
            }
            selected.placement = placement;
            auto stereoEngine = std::make_unique<SpectralDynamicsCore>();
            stereoEngine->prepare (48000.0);
            for (int offset = 0; offset < frames; offset += 128)
            {
                float* output[] { left.data() + offset, right.data() + offset };
                stereoEngine->process (output, 2, 128, bands);
            }
            double squareSum = 0.0;
            for (int i = 8192; i < frames; ++i)
            {
                const auto value = static_cast<double> (
                    left[static_cast<std::size_t> (i)]);
                squareSum += value * value;
            }
            return std::sqrt (squareSum / (frames - 8192));
        };
        const double midRms = renderPair (ChannelPlacement::Mid);
        const double sideRms = renderPair (ChannelPlacement::Side);
        expect (midRms > 0.15,
            "mid-only spectral attenuation must leave side-only signal intact");
        expect (sideRms < midRms * 0.72,
            "side spectral attenuation must reduce anti-phase content");

        beginTest ("STFT callback has no heap allocation after prepare");
        auto noAllocEngine = std::make_unique<SpectralDynamicsCore>();
        noAllocEngine->prepare (48000.0);
        std::array<float, 128> allocationInput {}, allocationRight {};
        float* allocationChannels[] {
            allocationInput.data(), allocationRight.data()
        };
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int i = 0; i < 40; ++i)
                noAllocEngine->process (allocationChannels, 2, 128, bands);
        }

        beginTest ("spectral STFT guards nonfinite input and is usable in stereo");
        std::array<float, 2048> l {}, r {};
        l[0] = std::numeric_limits<float>::quiet_NaN();
        r[9] = std::numeric_limits<float>::infinity();
        float* stereo[] { l.data(), r.data() };
        auto clean = std::make_unique<SpectralDynamicsCore>();
        clean->prepare (48000.0);
        clean->process (stereo, 2, static_cast<int> (l.size()), bands);
        for (int i = 0; i < static_cast<int> (l.size()); ++i)
            expect (std::isfinite (l[static_cast<std::size_t> (i)])
                && std::isfinite (r[static_cast<std::size_t> (i)]),
                "spectral engine must not publish nonfinite audio");
    }

    void testSpectralAlignedBlend()
    {
        beginTest ("APEX spectral wet/dry stays PDC-aligned across automation, "
                   "giant blocks, and time-varying effect mix");

        constexpr int count = 16384;
        constexpr int latency = SpectralAlignedBlend::kLatencySamples;
        std::vector<float> source (count), neutral (count), wet (count);
        for (int sample = 0; sample < count; ++sample)
            source[static_cast<std::size_t> (sample)] =
                static_cast<float> (0.3 * std::sin (
                    juce::MathConstants<double>::twoPi
                    * 1000.0 * sample / 48000.0));

        std::array<SpectralDynamicsCore::Band, kMaxBands> bands {};
        bands[0].enabled = true;
        bands[0].shape = FilterShape::Bell;
        bands[0].placement = ChannelPlacement::Stereo;
        bands[0].frequencyHz = 1000.0;
        bands[0].q = 4.0;
        bands[0].density = 0.25;
        bands[0].tilt = false;
        bands[0].dynamic.enabled = true;
        bands[0].dynamic.thresholdDb = -36.0;
        bands[0].dynamic.rangeDb = 12.0;
        bands[0].dynamic.attackSeconds = 0.003;
        bands[0].dynamic.releaseSeconds = 0.030;

        auto dryBlend = std::make_unique<SpectralAlignedBlend>();
        dryBlend->prepare (48000.0);
        neutral = source;
        // Deliberately pass one huge block to exercise the ring wrap guard.
        float* dryPointer[] { neutral.data() };
        dryBlend->process (dryPointer, 1, count, bands, false);
        double maxDelayError = 0.0;
        for (int sample = 0; sample < count; ++sample)
        {
            const double delayed = sample >= latency
                ? source[static_cast<std::size_t> (sample - latency)] : 0.0;
            maxDelayError = std::max (maxDelayError,
                std::abs (neutral[static_cast<std::size_t> (sample)]
                          - delayed));
        }
        expect (maxDelayError < 1.0e-7,
                "100% dry spectral-mode bypass must stay *delayed*, "
                "never leak undelayed transient");

        auto activeBlend = std::make_unique<SpectralAlignedBlend>();
        activeBlend->prepare (48000.0);
        wet = source;
        // Switch wet target while the signal is running. Large host blocks
        // are deliberately mixed with small ones.
        int offset = 0;
        constexpr int wetStart = 4096;
        constexpr int wetEnd = 10240;
        while (offset < count)
        {
            const bool enabled = offset >= wetStart && offset < wetEnd;
            const int size = std::min (
                offset == 0 ? wetStart : offset == wetStart
                ? wetEnd - wetStart : count - offset, count - offset);
            float* pointer[] { wet.data() + offset };
            activeBlend->process (pointer, 1, size, bands, enabled);
            offset += size;
        }
        double accumulatedWetChange = 0.0;
        double accumulatedAfterFade = 0.0;
        for (int sample = 6000; sample < 9000; ++sample)
            accumulatedWetChange += std::abs (
                wet[static_cast<std::size_t> (sample)]
                - neutral[static_cast<std::size_t> (sample)]);
        for (int sample = 12000; sample < 15360; ++sample)
            accumulatedAfterFade += std::abs (
                wet[static_cast<std::size_t> (sample)]
                - neutral[static_cast<std::size_t> (sample)]);
        expect (accumulatedWetChange > 12.0,
                "spectral effect must audibly operate during ON window");
        expect (accumulatedAfterFade < 1.0e-4,
                "automated OFF wet mix returns exactly to aligned dry");
        expect (activeBlend->wetMixForTesting() < 0.0001,
                "OFF transition fully settled");

        beginTest ("aligned blend callback allocates no memory, even on "
                   "maximal 24-band spectral runtime");
        std::array<SpectralDynamicsCore::Band, kMaxBands> allBands {};
        for (int b = 0; b < kMaxBands; ++b)
        {
            auto& band = allBands[static_cast<std::size_t> (b)];
            band = bands[0];
            band.frequencyHz = 110.0 * std::pow (1.15, b);
            band.q = 2.0;
        }
        auto rtBlend = std::make_unique<SpectralAlignedBlend>();
        rtBlend->prepare (48000.0);
        std::array<float, 128> left {}, right {};
        float* stereo[] { left.data(), right.data() };
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int frame = 0; frame < 25; ++frame)
                rtBlend->process (stereo, 2, 128, allBands, true);
        }
    }

    void testSpectralHostGate()
    {
        beginTest ("Phase 10 spectral DSP is opt-in, appended ABI and zero "
                   "additional latency by default");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 128);
        expectEquals (kNumParameters, 375);
        expectEquals (processor.getLatencySamples(), 0,
                      "existing sessions never inherit spectral lookahead");
        expect (! processor.isSpectralModeArmedForTesting());
        expect (! processor.isSpectralRepreparePendingForTesting());

        // State v6 knows nothing about Phase 10. A reused processor with
        // leftover spectral values must NOT arm itself on legacy session load.
        setUnits (processor, kSpectralArmParameter, 1.0f);
        setUnits (processor, spectralBandParameterIndex (0), 1.0f);
        setUnits (processor, kSpectralDensityParameter, 0.9f);
        setUnits (processor, kSpectralTiltParameter, 0.0f);
        juce::ValueTree oldSession { juce::Identifier { Processor::kStateTag } };
        oldSession.setProperty (Processor::kVersionProperty, 6, nullptr);
        juce::MemoryBlock oldBytes;
        {
            juce::MemoryOutputStream stream (oldBytes, false);
            oldSession.writeToStream (stream);
        }
        processor.setStateInformation (oldBytes.getData(),
            static_cast<int> (oldBytes.getSize()));
        expect (! processor.getParametricEQParameter (
            kSpectralArmParameter)->getBool(),
            "legacy session must reset spectral ARM to OFF");
        expect (! processor.getParametricEQParameter (
            spectralBandParameterIndex (0))->getBool(),
            "legacy session must reset unrecognized spectral band toggles");
        expectWithinAbsoluteError (
            processor.getParametricEQParameter (
                kSpectralDensityParameter)->getUnitsValue(), 0.5f, 1.0e-6f);
        expect (processor.getParametricEQParameter (
            kSpectralTiltParameter)->getBool());

        // Transport-safe arming: automation never jumps host PDC in flight.
        setUnits (processor, kSpectralArmParameter, 1.0f);
        expect (processor.isSpectralRepreparePendingForTesting());
        juce::AudioBuffer<float> block (2, 128);
        block.clear();
        juce::MidiBuffer midi;
        processor.processBlock (block, midi);
        expectEquals (processor.getLatencySamples(), 0,
                      "host-reported PDC must remain fixed while playing");
        expect (! processor.isSpectralModeArmedForTesting());
        // Reprepare represents host stopping/restarting audio.
        processor.prepareToPlay (48000.0, 128);
        expect (processor.isSpectralModeArmedForTesting());
        expect (! processor.isSpectralRepreparePendingForTesting());
        expectEquals (processor.getLatencySamples(), 1024);

        beginTest ("spectral-arm neutral path is exactly 1024 samples "
                   "and global bypass cannot leak undelayed dry audio");
        std::array<float, 4096> outLeft {}, outRight {};
        outLeft[0] = 0.75f;
        outRight[0] = -0.5f;
        processor.reset();
        for (int start = 0; start < 4096; start += 128)
        {
            block.clear();
            for (int i = 0; i < 128; ++i)
            {
                block.setSample (0, i, outLeft[
                    static_cast<std::size_t> (start + i)]);
                block.setSample (1, i, outRight[
                    static_cast<std::size_t> (start + i)]);
            }
            processor.processBlock (block, midi);
            for (int i = 0; i < 128; ++i)
            {
                outLeft[static_cast<std::size_t> (start + i)] =
                    block.getSample (0, i);
                outRight[static_cast<std::size_t> (start + i)] =
                    block.getSample (1, i);
            }
        }
        double difference = 0.0;
        for (int i = 0; i < 4096; ++i)
        {
            difference = std::max (difference, std::abs (
                static_cast<double> (outLeft[static_cast<std::size_t> (i)])
                    - (i == 1024 ? 0.75 : 0.0)));
            difference = std::max (difference, std::abs (
                static_cast<double> (outRight[static_cast<std::size_t> (i)])
                    - (i == 1024 ? -0.5 : 0.0)));
        }
        expect (difference < 1.0e-5,
                "neutral engine must preserve source exactly on host timeline");

        setUnits (processor, kGlobalBypassParameter, 1.0f);
        processor.reset();
        // Let legacy bypass fade settle so this checks the steady state.
        for (int run = 0; run < 25; ++run)
        {
            block.clear();
            processor.processBlock (block, midi);
        }
        expectEquals (processor.getLatencySamples(), 1024,
                      "global bypass keeps mode lookahead PDC");

        beginTest ("spectral band owns its envelope without double-applying "
                   "ordinary dynamic EQ");
        setUnits (processor, kGlobalBypassParameter, 0.0f);
        configureBell (processor, 0, 1000.0f, 0.0f, 4.0f);
        setUnits (processor, dynamicParameterIndex (
            0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (
            0, DynamicBandOffset::Threshold), -36.0f);
        setUnits (processor, dynamicParameterIndex (
            0, DynamicBandOffset::Range), 18.0f);
        setUnits (processor, spectralBandParameterIndex (0), 1.0f);
        setUnits (processor, kSpectralDensityParameter, 0.25f);
        setUnits (processor, kSpectralTiltParameter, 0.0f);
        processor.prepareToPlay (48000.0, 128);
        for (int iteration = 0; iteration < 96; ++iteration)
        {
            for (int sample = 0; sample < 128; ++sample)
            {
                const int index = iteration * 128 + sample;
                block.setSample (0, sample, static_cast<float> (
                    0.25 * std::sin (
                        juce::MathConstants<double>::twoPi
                        * 1000.0 * index / 48000.0)));
                block.setSample (1, sample, block.getSample (0, sample));
            }
            processor.processBlock (block, midi);
        }
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
            0.0, 1.0e-9,
            "spectral path excludes this band from the ordinary broadband "
            "dynamic envelope");

        // Arm-off will take effect on *next* host reprepare only.
        setUnits (processor, kSpectralArmParameter, 0.0f);
        processor.processBlock (block, midi);
        expectEquals (processor.getLatencySamples(), 1024);
        expect (processor.isSpectralRepreparePendingForTesting());
        processor.prepareToPlay (48000.0, 128);
        expectEquals (processor.getLatencySamples(), 0);
        expect (! processor.isSpectralModeArmedForTesting());
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
