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

class ParametricEQDynamicStereoTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicStereoTests()
        : UnitTest ("ParametricEQ.DynamicStereo", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testStereoPlacementDetectorDomain();
        testLeftPlacementDetectorDomain();
        testRightPlacementDetectorDomain();
        testSimultaneousMixedPlacements();
        testMonoPlacementDomains();
    }

private:
    double settledGain (Processor& processor, int band)
    {
        return processor.getDynamicGainDbForTesting (band);
    }

    void testStereoPlacementDetectorDomain()
    {
        beginTest ("stereo placement detects the linked stereo domain");
        for (const bool linked : { true, false })
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, ChannelPlacement::Stereo, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);
            configureDynamics (processor, 0, -30.0f, 9.0f);
            setUnits (processor, kDynamicLinkParameter, linked ? 1.0f : 0.0f);

            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            juce::MidiBuffer midi;

            // Left-only loud: linked stereo reduces.
            feedStereo (processor, buffer, midi, 0.8, 1.0, 0.0, 8);
            expect (settledGain (processor, 0) < -6.0,
                    "left-only loud signal engages stereo dynamics");
            // Silence recovers (fresh silence every block: no output feedback).
            for (int block = 0; block < 48; ++block)
            {
                buffer.clear();
                processor.processBlock (buffer, midi);
            }
            expect (std::abs (settledGain (processor, 0)) < 0.05,
                    "stereo dynamics release to unity");
        }
    }

    void testLeftPlacementDetectorDomain()
    {
        beginTest ("left placement detects only the physical left channel");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, ChannelPlacement::Left, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        configureDynamics (processor, 0, -30.0f, 9.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;

        // Right-only loud: the left detector sees silence -> no reduction.
        feedStereo (processor, buffer, midi, 0.8, 0.0, 1.0, 8);
        expect (std::abs (settledGain (processor, 0)) < 0.05,
                "right-only signal must not drive a left-domain detector");
        // Left-only loud: reduction engages.
        feedStereo (processor, buffer, midi, 0.8, 1.0, 0.0, 8);
        expect (settledGain (processor, 0) < -6.0,
                "left-only signal drives the left-domain detector");
    }

    void testRightPlacementDetectorDomain()
    {
        beginTest ("right placement detects only the physical right channel");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, ChannelPlacement::Right, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        configureDynamics (processor, 0, -30.0f, 9.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        feedStereo (processor, buffer, midi, 0.8, 1.0, 0.0, 8);
        expect (std::abs (settledGain (processor, 0)) < 0.05,
                "left-only signal must not drive a right-domain detector");
        feedStereo (processor, buffer, midi, 0.8, 0.0, 1.0, 8);
        expect (settledGain (processor, 0) < -6.0,
                "right-only signal drives the right-domain detector");
    }

    void testSimultaneousMixedPlacements()
    {
        beginTest ("simultaneous dynamic bands with different placements stay finite");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, ChannelPlacement::Stereo, 500.0f, 6.0f, 2.0f);
        configureBand (processor, 1, ChannelPlacement::Left, 1500.0f, 6.0f, 2.0f);
        configureBand (processor, 2, ChannelPlacement::Right, 3000.0f, 6.0f, 2.0f);
        configureBand (processor, 3, ChannelPlacement::Mid, 6000.0f, 6.0f, 2.0f);
        configureBand (processor, 4, ChannelPlacement::Side, 9000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        for (int band = 0; band < 5; ++band)
            configureDynamics (processor, band, -30.0f, 9.0f);

        juce::AudioBuffer<float> buffer (2, 8193);
        ParametricEQTest::fillDeterministic (buffer, 0x50DE5u);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        expect (ParametricEQTest::allFinite (buffer),
                "mixed-placement dynamic processing stays finite");
        for (int band = 0; band < 5; ++band)
            expect (std::isfinite (settledGain (processor, band)),
                    "band " + juce::String (band) + " gain finite");
    }

    void testMonoPlacementDomains()
    {
        beginTest ("mono dynamics use the mono stream for Stereo/Left/Mid");
        for (const auto placement : { ChannelPlacement::Stereo,
                                      ChannelPlacement::Left,
                                      ChannelPlacement::Mid })
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, placement, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);
            configureDynamics (processor, 0, -30.0f, 9.0f);

            juce::AudioBuffer<float> mono (1, 512);
            juce::MidiBuffer midi;
            for (int block = 0; block < 8; ++block)
            {
                for (int sample = 0; sample < 512; ++sample)
                {
                    const auto value = static_cast<float> (0.8 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 937.5
                        * (block * 512 + sample) / 48000.0));
                    mono.setSample (0, sample, value);
                }
                processor.processBlock (mono, midi);
            }
            expect (settledGain (processor, 0) < -6.0,
                    juce::String (channelPlacementName (placement))
                        + " mono domain detects the mono stream");
        }
    }
};

static ParametricEQDynamicStereoTests parametricEQDynamicStereoTests;
