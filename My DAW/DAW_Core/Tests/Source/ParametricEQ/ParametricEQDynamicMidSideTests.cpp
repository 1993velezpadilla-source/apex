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

void configureBand (Processor& processor, int band, ChannelPlacement placement,
                    float frequency, float gain, float q)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (FilterShape::Bell));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
    setUnits (processor, placementParameterIndex (band),
              static_cast<float> (placement));
}

void configureDynamics (Processor& processor, int band, float threshold, float range)
{
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Enable), 1.0f);
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Threshold),
              threshold);
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Range), range);
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Attack),
              0.001f);
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Release),
              0.050f);
}

void feedStereo (Processor& processor, juce::AudioBuffer<float>& buffer,
                 juce::MidiBuffer& midi, double amplitude, double leftGain,
                 double rightGain, int blocks)
{
    for (int block = 0; block < blocks; ++block)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto value = static_cast<float> (amplitude * std::sin (
                2.0 * juce::MathConstants<double>::pi * 937.5
                * (block * buffer.getNumSamples() + sample) / 48000.0));
            buffer.setSample (0, sample, value * static_cast<float> (leftGain));
            buffer.setSample (1, sample, value * static_cast<float> (rightGain));
        }
        processor.processBlock (buffer, midi);
    }
}

} // namespace

class ParametricEQDynamicMidSideTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicMidSideTests()
        : UnitTest ("ParametricEQ.DynamicMidSide", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testMidPlacementDetectorDomain();
        testSidePlacementDetectorDomain();
        testMixedMidSideDynamicBands();
        testTwentyFourBandMixedPlacementStress();
    }

private:
    double settledGain (Processor& processor, int band)
    {
        return processor.getDynamicGainDbForTesting (band);
    }

    void testMidPlacementDetectorDomain()
    {
        beginTest ("mid placement detects the mid component only");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, ChannelPlacement::Mid, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        configureDynamics (processor, 0, -30.0f, 9.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;

        // Anti-correlated (pure side): mid = 0 -> no reduction.
        feedStereo (processor, buffer, midi, 0.8, 1.0, -1.0, 8);
        expect (std::abs (settledGain (processor, 0)) < 0.05,
                "anti-correlated input must not drive a mid-domain detector");
        // Correlated (pure mid): reduction engages.
        feedStereo (processor, buffer, midi, 0.8, 1.0, 1.0, 8);
        expect (settledGain (processor, 0) < -6.0,
                "correlated input drives the mid-domain detector");
        // Dual mono also engages.
        buffer.clear();
        for (int block = 0; block < 48; ++block)
            processor.processBlock (buffer, midi);
        feedStereo (processor, buffer, midi, 0.8, 1.0, 1.0, 8);
        expect (settledGain (processor, 0) < -6.0,
                "dual mono drives the mid-domain detector");
    }

    void testSidePlacementDetectorDomain()
    {
        beginTest ("side placement detects the side component only");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, ChannelPlacement::Side, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        configureDynamics (processor, 0, -30.0f, 9.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        feedStereo (processor, buffer, midi, 0.8, 1.0, 1.0, 8);
        expect (std::abs (settledGain (processor, 0)) < 0.05,
                "correlated input must not drive a side-domain detector");
        feedStereo (processor, buffer, midi, 0.8, 1.0, -1.0, 8);
        expect (settledGain (processor, 0) < -6.0,
                "anti-correlated input drives the side-domain detector");
    }

    void testMixedMidSideDynamicBands()
    {
        beginTest ("mixed mid and side dynamic bands stay finite and independent");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, ChannelPlacement::Mid, 500.0f, 6.0f, 2.0f);
        configureBand (processor, 1, ChannelPlacement::Side, 3000.0f, 6.0f, 2.0f);
        configureBand (processor, 2, ChannelPlacement::Stereo, 6000.0f, 6.0f, 2.0f);
        configureBand (processor, 3, ChannelPlacement::Left, 9000.0f, 6.0f, 2.0f);
        configureBand (processor, 4, ChannelPlacement::Right, 12000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        for (int band = 0; band < 5; ++band)
            configureDynamics (processor, band, -30.0f, 9.0f);

        juce::AudioBuffer<float> buffer (2, 8193);
        ParametricEQTest::fillDeterministic (buffer, 0x51DE5u);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        expect (ParametricEQTest::allFinite (buffer),
                "mixed mid/side/stereo/l/r dynamics stay finite");
        for (int band = 0; band < 5; ++band)
            expect (std::isfinite (settledGain (processor, band)),
                    "band " + juce::String (band) + " gain finite");
    }

    void testTwentyFourBandMixedPlacementStress()
    {
        beginTest ("24-band mixed placement dynamic stress stays finite");
        for (const auto rate : { 48000.0, 96000.0, 192000.0 })
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            for (int band = 0; band < kMaxBands; ++band)
            {
                configureBand (processor, band,
                               static_cast<ChannelPlacement> (band % 5),
                               50.0f * (band + 1), 6.0f, 1.0f);
                configureDynamics (processor, band, -30.0f, (band & 1) != 0 ? 9.0f : -9.0f);
            }
            processor.prepareToPlay (rate, 512);

            juce::AudioBuffer<float> buffer (2, 8193);
            ParametricEQTest::fillDeterministic (buffer, 0xD17A71Cu);
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);
            expect (ParametricEQTest::allFinite (buffer),
                    "finite @ " + juce::String (rate, 0));
            for (int band = 0; band < kMaxBands; ++band)
                expect (std::isfinite (settledGain (processor, band)),
                        "band " + juce::String (band) + " gain finite @ "
                            + juce::String (rate, 0));
        }
    }
};

static ParametricEQDynamicMidSideTests parametricEQDynamicMidSideTests;
