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

void fillSine (juce::AudioBuffer<float>& buffer, double frequency,
               double amplitude, int positionOffset = 0)
{
    buffer.clear();
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto value = static_cast<float> (amplitude * std::sin (
            2.0 * juce::MathConstants<double>::pi * frequency
            * (positionOffset + sample) / 48000.0));
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, sample, value);
    }
}

} // namespace

class ParametricEQDynamicSidechainTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicSidechainTests()
        : UnitTest ("ParametricEQ.DynamicSidechain", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testInternalDetectorReadsProgramInput();
        testExternalKeyDrivesGainWithoutProgramCoupling();
        testExternalKeyAppearingAndDisappearing();
        testSourceSwitchIsDeterministic();
        testBandLimitedDetectorFilterRejectsOutOfBandKey();
    }

private:
    void testInternalDetectorReadsProgramInput()
    {
        beginTest ("internal detector responds to the plugin input level");
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
                  0.020f);
        setUnits (processor, kDynamicSidechainParameter, 0.0f); // Internal

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (int block = 0; block < 8; ++block)
        {
            fillSine (buffer, 937.5, 0.01); // fresh input every block
            processor.processBlock (buffer, midi);
        }
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                   0.0, 0.1, "quiet program keeps unity gain");
        for (int block = 0; block < 8; ++block)
        {
            fillSine (buffer, 937.5, 0.8);
            processor.processBlock (buffer, midi);
        }
        expect (processor.getDynamicGainDbForTesting (0) < -6.0,
                "loud program drives the internal detector into reduction");
    }

    void testExternalKeyDrivesGainWithoutProgramCoupling()
    {
        beginTest ("external key drives reduction independently of program audio");
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
                  0.020f);
        setUnits (processor, kDynamicSidechainParameter, 1.0f); // External

        juce::AudioBuffer<float> quiet (2, 512);
        juce::AudioBuffer<float> key (2, 512);
        juce::MidiBuffer midi;
        for (int block = 0; block < 8; ++block)
        {
            fillSine (quiet, 937.5, 0.01);
            fillSine (key, 937.5, 0.8);
            expect (processor.submitExternalDetectorKey (key),
                    "key submission accepted when prepared");
            processor.processBlock (quiet, midi);
        }
        expect (processor.getDynamicGainDbForTesting (0) < -6.0,
                "loud external key reduces a quiet program band");

        processor.clearExternalDetectorKey();
        for (int block = 0; block < 24; ++block)
        {
            fillSine (quiet, 937.5, 0.01);
            processor.processBlock (quiet, midi);
        }
        // The decay includes the ~10 ms RMS detector window plus the knee
        // plus the 20 ms release: 24 blocks leave < 9 * exp(-(12288-2400)/960).
        expect (std::abs (processor.getDynamicGainDbForTesting (0)) < 0.05,
                "cleared key returns the detector to silence");
    }

    void testExternalKeyAppearingAndDisappearing()
    {
        beginTest ("external key appearance/disappearance is deterministic");
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
                  0.020f);
        setUnits (processor, kDynamicSidechainParameter, 1.0f);

        juce::AudioBuffer<float> program (2, 512);
        fillSine (program, 937.5, 0.4);
        juce::AudioBuffer<float> key (2, 512);
        fillSine (key, 937.5, 0.9);
        juce::MidiBuffer midi;
        for (int block = 0; block < 16; ++block)
        {
            if (block % 4 == 0)
            {
                if (block % 8 == 0)
                    expect (processor.submitExternalDetectorKey (key));
                else
                    processor.clearExternalDetectorKey();
            }
            processor.processBlock (program, midi);
            expect (std::isfinite (processor.getDynamicGainDbForTesting (0)),
                    "key cycling stays finite");
        }
        expect (! processor.submitExternalDetectorKey (key)
                || processor.isExternalKeyValidForTesting(),
                "key state is coherent");
    }

    void testBandLimitedDetectorFilterRejectsOutOfBandKey()
    {
        beginTest ("per-band SC filter focuses Dynamic EQ detection around the band");

        auto measureReduction = [] (double keyFrequency, bool filterEnabled)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBell (processor, 0, 1000.0f, 6.0f, 6.0f);
            processor.prepareToPlay (48000.0, 512);
            setUnits (processor, dynamicParameterIndex (
                0, DynamicBandOffset::Enable), 1.0f);
            setUnits (processor, dynamicParameterIndex (
                0, DynamicBandOffset::Threshold), -30.0f);
            setUnits (processor, dynamicParameterIndex (
                0, DynamicBandOffset::Range), 12.0f);
            setUnits (processor, dynamicParameterIndex (
                0, DynamicBandOffset::Attack), 0.0005f);
            setUnits (processor, dynamicParameterIndex (
                0, DynamicBandOffset::Release), 0.010f);
            setUnits (processor, kDynamicSidechainParameter, 1.0f);
            setUnits (processor, dynamicFilterParameterIndex (0),
                      filterEnabled ? 1.0f : 0.0f);

            juce::AudioBuffer<float> program (2, 512);
            juce::AudioBuffer<float> key (2, 512);
            juce::MidiBuffer midi;
            for (int block = 0; block < 18; ++block)
            {
                fillSine (program, 250.0, 0.005, block * 512);
                fillSine (key, keyFrequency, 0.8, block * 512);
                processor.submitExternalDetectorKey (key);
                processor.processBlock (program, midi);
            }
            return processor.getDynamicGainDbForTesting (0);
        };

        const double focusedNear = measureReduction (1000.0, true);
        const double focusedFar = measureReduction (100.0, true);
        const double broadbandFar = measureReduction (100.0, false);

        expect (focusedNear < -7.0,
                "in-band key must still drive strong reduction");
        expect (std::abs (focusedFar) < 2.0,
                "far-out key must be rejected by the focused detector");
        expect (broadbandFar < -7.0,
                "disabling SC Filter restores broadband detector behaviour");
        expect (focusedNear < focusedFar - 4.0,
                "detector filtering must materially discriminate frequency");
    }

    void testSourceSwitchIsDeterministic()
    {
        beginTest ("internal/external source switching is deterministic");
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
                  0.020f);

        juce::AudioBuffer<float> loud (2, 512);
        fillSine (loud, 937.5, 0.8);
        juce::AudioBuffer<float> quiet (2, 512);
        fillSine (quiet, 937.5, 0.01);
        juce::AudioBuffer<float> key (2, 512);
        fillSine (key, 937.5, 0.8);
        juce::MidiBuffer midi;

        // Internal source with loud program -> reduction.
        setUnits (processor, kDynamicSidechainParameter, 0.0f);
        for (int block = 0; block < 8; ++block)
        {
            fillSine (loud, 937.5, 0.8);
            processor.processBlock (loud, midi);
        }
        expect (processor.getDynamicGainDbForTesting (0) < -6.0);
        // Switch to external with no key -> recovery. The level must fall
        // through the RMS detector window and the knee before the 20 ms
        // release begins; 32 blocks bound the residual far below 0.01 dB.
        setUnits (processor, kDynamicSidechainParameter, 1.0f);
        processor.clearExternalDetectorKey();
        for (int block = 0; block < 32; ++block)
        {
            fillSine (loud, 937.5, 0.8);
            processor.processBlock (loud, midi);
        }
        expect (std::abs (processor.getDynamicGainDbForTesting (0)) < 0.01,
                "external-without-key ignores the program");
        // Provide the key -> reduction returns.
        for (int block = 0; block < 8; ++block)
        {
            fillSine (key, 937.5, 0.8);
            processor.submitExternalDetectorKey (key);
            processor.processBlock (loud, midi);
        }
        expect (processor.getDynamicGainDbForTesting (0) < -6.0);
    }
};

static ParametricEQDynamicSidechainTests parametricEQDynamicSidechainTests;
