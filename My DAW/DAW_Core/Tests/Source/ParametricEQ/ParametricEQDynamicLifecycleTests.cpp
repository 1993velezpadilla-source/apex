#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <cmath>

namespace
{

using namespace APEX::ParametricEQ;

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

void configureBell (Processor& processor, int band, float frequency, float gain, float q)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (FilterShape::Bell));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
}

void drive (Processor& processor, int blocks, double amplitude)
{
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
    {
        for (int sample = 0; sample < 512; ++sample)
        {
            const auto value = static_cast<float> (amplitude * std::sin (
                2.0 * juce::MathConstants<double>::pi * 937.5 * sample / 48000.0));
            buffer.setSample (0, sample, value);
            buffer.setSample (1, sample, value);
        }
        processor.processBlock (buffer, midi);
    }
}

} // namespace

class ParametricEQDynamicLifecycleTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicLifecycleTests()
        : UnitTest ("ParametricEQ.DynamicLifecycle", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testEnableEdgeResetsDetectorAndEnvelope();
        testDisableEnableCycleIsClean();
        testResetAndReprepareClearDynamicState();
        testStateRestoreLeavesDeterministicDynamicState();
        testRepeatedLifecycleCycles();
    }

private:
    void testEnableEdgeResetsDetectorAndEnvelope()
    {
        beginTest ("enable edges start from a known detector/envelope state");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 0.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                  -30.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Attack),
                  0.001f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Release),
                  0.050f);
        drive (processor, 4, 0.01);
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6,
                                   "disabled dynamics never reduce");

        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi); // adopt the enable edge
        drive (processor, 2, 0.8);
        expect (processor.getDynamicGainDbForTesting (0) < -6.0,
                "fresh engagement reduces from a clean start");
    }

    void testDisableEnableCycleIsClean()
    {
        beginTest ("disable/enable cycles leave no stale reduction");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                  -30.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Attack),
                  0.001f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Release),
                  0.050f);

        drive (processor, 4, 0.8);
        expect (processor.getDynamicGainDbForTesting (0) < -6.0);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 0.0f);
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6,
                                   "disabling clears the reduction immediately");
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        buffer.clear(); // fresh silence: never reuse a previous block's output
        processor.processBlock (buffer, midi);
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6,
                                   "re-enabling starts clean, not reduced");
    }

    void testResetAndReprepareClearDynamicState()
    {
        beginTest ("reset and reprepare clear detector and envelope state");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                  -30.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Attack),
                  0.001f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Release),
                  0.050f);
        drive (processor, 4, 0.8);
        expect (processor.getDynamicGainDbForTesting (0) < -6.0);

        processor.reset();
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6, "reset clears the reduction");

        drive (processor, 4, 0.8);
        expect (processor.getDynamicGainDbForTesting (0) < -6.0);
        processor.prepareToPlay (96000.0, 256);
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6, "reprepare clears the reduction");
    }

    void testStateRestoreLeavesDeterministicDynamicState()
    {
        beginTest ("state restore preserves parameters and resets runtime");
        auto sourceStorage = std::make_unique<Processor>();
        auto& source = *sourceStorage;
        configureBell (source, 0, 1000.0f, 6.0f, 2.0f);
        source.prepareToPlay (48000.0, 512);
        setUnits (source, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (source, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        drive (source, 4, 0.8);
        expect (source.getDynamicGainDbForTesting (0) < -6.0);

        juce::MemoryBlock state;
        source.getStateInformation (state);
        auto restoredStorage = std::make_unique<Processor>();
        auto& restored = *restoredStorage;
        restored.setStateInformation (state.getData(),
                                     static_cast<int> (state.getSize()));
        restored.prepareToPlay (48000.0, 512);
        expectWithinAbsoluteError (restored.getDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6,
                                   "restored processor starts with clean runtime");
        expect (restored.getParametricEQParameter (dynamicParameterIndex (
                    0, DynamicBandOffset::Enable))->getBool(),
                "restored parameters keep the enable state");
    }

    void testRepeatedLifecycleCycles()
    {
        beginTest ("repeated prepare/reset cycles stay deterministic");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        for (int cycle = 0; cycle < 8; ++cycle)
        {
            processor.prepareToPlay (48000.0 + cycle * 100.0, 256 + cycle);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable),
                      (cycle & 1) != 0 ? 1.0f : 0.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range),
                      9.0f);
            drive (processor, 2, 0.5);
            expect (std::isfinite (processor.getDynamicGainDbForTesting (0)),
                    "cycle " + juce::String (cycle) + " stays finite");
            processor.reset();
            expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                       0.0, 1.0e-6);
        }
    }
};

static ParametricEQDynamicLifecycleTests parametricEQDynamicLifecycleTests;
