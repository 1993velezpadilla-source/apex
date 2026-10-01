#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <array>
#include <cmath>

namespace
{

using namespace APEX::ParametricEQ;
using ParametricEQTest::db;
using ParametricEQTest::placedBand;

constexpr std::array<double, 6> kRates {
    44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
};

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

void configure (Processor& processor, int band, FilterShape shape,
                ChannelPlacement placement, float frequency, float gain, float q)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (shape));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
    setUnits (processor, placementParameterIndex (band),
              static_cast<float> (placement));
}

std::vector<float> monoReferenceImpulse (const BandSettings& settings,
                                         double sampleRate, int length)
{
    auto engine = ParametricEQTest::preparedEngine (sampleRate, 257, 1);
    engine.setBand (0, settings, DesignMode::Realtime);
    return ParametricEQTest::impulseResponse (engine, length);
}

void expectTransferMatches (juce::UnitTest& test,
                            const std::vector<float>& measured,
                            const std::vector<float>& reference,
                            double sampleRate, double toleranceDb,
                            const juce::String& message)
{
    for (const double frequency : { 40.0, 200.0, 997.0, 4000.0, 12000.0 })
    {
        const auto measuredResponse = ParametricEQTest::responseFromImpulse (
            measured, sampleRate, frequency);
        const auto referenceResponse = ParametricEQTest::responseFromImpulse (
            reference, sampleRate, frequency);
        test.expectWithinAbsoluteError (db (measuredResponse),
                                        db (referenceResponse), toleranceDb,
                                        message + " @ " + juce::String (
                                            frequency, 0) + " Hz");
        test.expectWithinAbsoluteError (
            ParametricEQTest::phaseDifference (measuredResponse, referenceResponse),
            0.0, 0.02, message + " phase @ " + juce::String (frequency, 0) + " Hz");
    }
}

} // namespace

class ParametricEQStereoTests final : public juce::UnitTest
{
public:
    ParametricEQStereoTests()
        : UnitTest ("ParametricEQ.Stereo", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testNeutralUnityAcrossPlacements();
        testLeftRightLeaveUnselectedChannelsUntouched();
        testLeftRightTransfersMatchMonoFilter();
        testMonoEquivalenceAcrossPlacements();
        testPlacementTransitionsAreClickFreeAndBlockInvariant();
        testMixedPlacementStressAtSixRates();
    }

private:
    void testNeutralUnityAcrossPlacements()
    {
        beginTest ("neutral configurations are exact wire at every placement");
        const ChannelPlacement placements[] = {
            ChannelPlacement::Stereo, ChannelPlacement::Left,
            ChannelPlacement::Right, ChannelPlacement::Mid, ChannelPlacement::Side
        };
        for (const auto placement : placements)
        {
            auto engine = ParametricEQTest::preparedEngine (48000.0, 128, 2);
            engine.setBand (0, placedBand (FilterShape::Bell, placement,
                                           1000.0, 0.0, 1.0), DesignMode::Realtime);
            engine.setBand (1, placedBand (FilterShape::LowCut, placement,
                                           120.0, 0.0, 1.0, 0.0), DesignMode::Realtime);
            juce::AudioBuffer<float> buffer (2, 128);
            juce::AudioBuffer<float> expected (2, 128);
            ParametricEQTest::fillDeterministic (buffer);
            expected.makeCopyOf (buffer);
            ParametricEQTest::processBuffer (engine, buffer, 0, 128);
            expect (ParametricEQTest::bitEqual (buffer, expected),
                    "neutral wire at placement "
                        + juce::String (channelPlacementName (placement)));
        }
    }

    void testLeftRightLeaveUnselectedChannelsUntouched()
    {
        beginTest ("left/right bands never touch the unselected physical channel");
        for (const auto rate : kRates)
        {
            for (const auto placement : { ChannelPlacement::Left,
                                          ChannelPlacement::Right })
            {
                auto engine = ParametricEQTest::preparedEngine (rate, 128, 2);
                engine.setBand (0, placedBand (FilterShape::Bell, placement,
                                               1000.0, 18.0, 6.0));
                juce::AudioBuffer<float> buffer (2, 128);
                ParametricEQTest::fillDeterministic (buffer, 0x5EED5EEDu);
                const int untouched = placement == ChannelPlacement::Left ? 1 : 0;
                std::vector<float> original (128);
                std::memcpy (original.data(), buffer.getReadPointer (untouched),
                             128 * sizeof (float));
                ParametricEQTest::processBuffer (engine, buffer, 0, 128);
                expect (std::memcmp (original.data(),
                                     buffer.getReadPointer (untouched),
                                     128 * sizeof (float)) == 0,
                        "unselected channel must remain bit-exact @ "
                            + juce::String (rate, 0));
            }
        }
    }

    void testLeftRightTransfersMatchMonoFilter()
    {
        beginTest ("left/right band matrices equal the mono filter on the selected channel");
        const auto rate = 48000.0;
        const auto settings = placedBand (FilterShape::Bell, ChannelPlacement::Left,
                                          1000.0, 12.0, 2.0);
        const auto reference = monoReferenceImpulse (settings, rate, 8192);
        std::vector<float> identity (8192, 0.0f);
        identity[0] = 1.0f;
        std::vector<float> silence (8192, 0.0f);

        auto engine = ParametricEQTest::preparedEngine (rate, 257, 2);
        engine.setBand (0, settings);
        const auto paths = ParametricEQTest::stereoImpulseResponses (engine, 8192);
        // Left placement transfer is [[H, 0], [0, 1]]: only the left output
        // responds to the left input; every crossfeed and the right lane stay
        // untouched.
        expectTransferMatches (*this, paths[0], reference, rate, 0.05, "Left LL vs mono H");
        expectTransferMatches (*this, paths[1], silence, rate, 0.05, "Left LR wire");
        expectTransferMatches (*this, paths[2], silence, rate, 0.05, "Left RL wire");
        expectTransferMatches (*this, paths[3], identity, rate, 0.05, "Left RR identity");
        for (std::size_t sample = 0; sample < paths[1].size(); ++sample)
            expectEquals (paths[1][sample], 0.0f, "Left LR silence");
        for (std::size_t sample = 0; sample < paths[2].size(); ++sample)
            expectEquals (paths[2][sample], 0.0f, "Left RL silence");
        for (std::size_t sample = 0; sample < paths[3].size(); ++sample)
            expectEquals (paths[3][sample], identity[sample], "Left RR wire");

        auto rightEngine = ParametricEQTest::preparedEngine (rate, 257, 2);
        BandSettings rightBand = settings;
        rightBand.placement = ChannelPlacement::Right;
        rightEngine.setBand (0, rightBand);
        const auto rightPaths = ParametricEQTest::stereoImpulseResponses (
            rightEngine, 8192);
        // Right placement transfer is [[1, 0], [0, H]].
        expectTransferMatches (*this, rightPaths[0], identity, rate, 0.05, "Right LL identity");
        expectTransferMatches (*this, rightPaths[1], silence, rate, 0.05, "Right LR wire");
        expectTransferMatches (*this, rightPaths[2], silence, rate, 0.05, "Right RL wire");
        expectTransferMatches (*this, rightPaths[3], reference, rate, 0.05, "Right RR vs mono H");
    }

    void testMonoEquivalenceAcrossPlacements()
    {
        beginTest ("mono semantics: Stereo/Left/Mid filter, Right/Side are wire");
        const auto rate = 48000.0;
        const auto settings = placedBand (FilterShape::Bell, ChannelPlacement::Stereo,
                                          1500.0, 9.0, 3.0);
        const auto reference = monoReferenceImpulse (settings, rate, 8192);

        for (const auto placement : { ChannelPlacement::Stereo,
                                      ChannelPlacement::Left,
                                      ChannelPlacement::Mid })
        {
            auto engine = ParametricEQTest::preparedEngine (rate, 257, 1);
            auto bandSettings = settings;
            bandSettings.placement = placement;
            engine.setBand (0, bandSettings);
            const auto measured = ParametricEQTest::impulseResponse (engine, 8192);
            expectTransferMatches (*this, measured, reference, rate, 0.05,
                juce::String ("mono ") + channelPlacementName (placement));
        }

        for (const auto placement : { ChannelPlacement::Right,
                                      ChannelPlacement::Side })
        {
            auto engine = ParametricEQTest::preparedEngine (rate, 257, 1);
            auto bandSettings = settings;
            bandSettings.placement = placement;
            engine.setBand (0, bandSettings);
            const auto measured = ParametricEQTest::impulseResponse (engine, 8192);
            expectEquals (measured[0], 1.0f, "mono absent-component impulse");
            for (std::size_t sample = 1; sample < measured.size(); ++sample)
                expectEquals (measured[sample], 0.0f, "mono absent-component tail");
        }
    }

    void testPlacementTransitionsAreClickFreeAndBlockInvariant()
    {
        beginTest ("hostile placement automation is click-free and settles correctly");
        const ChannelPlacement sequence[] = {
            ChannelPlacement::Stereo, ChannelPlacement::Left,
            ChannelPlacement::Mid, ChannelPlacement::Side,
            ChannelPlacement::Right, ChannelPlacement::Stereo,
            ChannelPlacement::Side, ChannelPlacement::Left,
            ChannelPlacement::Mid, ChannelPlacement::Right,
            ChannelPlacement::Left
        };
        constexpr int total = 8192;
        constexpr int stepsPerPlacement = 64;

        auto render = [&] (int blockSize)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            processor.prepareToPlay (48000.0, blockSize);
            configure (processor, 0, FilterShape::Bell, ChannelPlacement::Stereo,
                       1000.0f, 12.0f, 8.0f);

            std::vector<float> result (total);
            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;
            int position = 0;
            int sequenceIndex = 0;
            int step = 0;
            while (position < total)
            {
                if (step % stepsPerPlacement == 0)
                    setUnits (processor, placementParameterIndex (0),
                              static_cast<float> (sequence[static_cast<std::size_t> (
                                  sequenceIndex++ % 11)]));
                ++step;

                const int count = std::min (blockSize, total - position);
                for (int sample = 0; sample < count; ++sample)
                    for (int channel = 0; channel < 2; ++channel)
                        block.setSample (channel, sample, 0.1f * std::sin (
                            2.0 * juce::MathConstants<double>::pi * 997.0
                            * (position + sample) / 48000.0));
                juce::AudioBuffer<float> view (block.getArrayOfWritePointers(),
                                               2, 0, count);
                processor.processBlock (view, midi);
                for (int sample = 0; sample < count; ++sample)
                    result[static_cast<std::size_t> (position + sample)]
                        = block.getSample (0, sample);
                position += count;
            }
            return result;
        };

        for (const int blockSize : { 1, 31, 512 })
        {
            const auto rendered = render (blockSize);
            expect (ParametricEQTest::allFinite (rendered),
                    "finite @ block " + juce::String (blockSize));
            double maximumJump = 0.0;
            for (std::size_t sample = 1; sample < rendered.size(); ++sample)
                maximumJump = std::max (maximumJump,
                                        std::abs (static_cast<double> (rendered[sample])
                                                - rendered[sample - 1]));
            expect (maximumJump < 0.15,
                    "click bound @ block " + juce::String (blockSize) + ": "
                        + juce::String (maximumJump, 6));
        }

        // A single placement change at stream start must be sample-exact
        // across hostile block sizes (identical adoption boundary).
        auto renderOnce = [&] (int blockSize)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            processor.prepareToPlay (48000.0, blockSize);
            configure (processor, 0, FilterShape::Bell, ChannelPlacement::Side,
                       1500.0f, 9.0f, 4.0f);
            std::vector<float> result (total);
            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;
            int position = 0;
            while (position < total)
            {
                const int count = std::min (blockSize, total - position);
                for (int sample = 0; sample < count; ++sample)
                    for (int channel = 0; channel < 2; ++channel)
                        block.setSample (channel, sample, 0.1f * std::sin (
                            2.0 * juce::MathConstants<double>::pi * 997.0
                            * (position + sample) / 48000.0));
                juce::AudioBuffer<float> view (block.getArrayOfWritePointers(),
                                               2, 0, count);
                processor.processBlock (view, midi);
                for (int sample = 0; sample < count; ++sample)
                    result[static_cast<std::size_t> (position + sample)]
                        = block.getSample (0, sample);
                position += count;
            }
            return result;
        };
        const auto one = renderOnce (1);
        const auto thirtyOne = renderOnce (31);
        const auto fiveTwelve = renderOnce (512);
        for (std::size_t sample = 0; sample < one.size(); ++sample)
        {
            expectEquals (one[sample], thirtyOne[sample]);
            expectEquals (one[sample], fiveTwelve[sample]);
        }
    }

    void testMixedPlacementStressAtSixRates()
    {
        beginTest ("24-band mixed placement stress stays finite at six rates");
        for (const auto rate : kRates)
        {
            auto engine = ParametricEQTest::preparedEngine (rate, 256, 2);
            for (int bandIndex = 0; bandIndex < kMaxBands; ++bandIndex)
            {
                const auto placement = static_cast<ChannelPlacement> (
                    bandIndex % kChannelPlacementCount);
                const auto shape = static_cast<FilterShape> (bandIndex % 10);
                auto settings = placedBand (shape, placement,
                    20.0 * std::pow (1.32, bandIndex),
                    (bandIndex & 1) != 0 ? 9.0 : -7.0,
                    0.1 + 0.3 * bandIndex,
                    6.0 + (bandIndex % 16) * 6.0);
                engine.setBand (bandIndex, settings);
            }
            juce::AudioBuffer<float> buffer (2, 8193);
            ParametricEQTest::fillDeterministic (buffer, 0xC0FFEEu);
            ParametricEQTest::processBuffer (engine, buffer, 0,
                                             buffer.getNumSamples());
            expect (ParametricEQTest::allFinite (buffer),
                    "finite @ " + juce::String (rate, 0) + " Hz");
        }
    }
};

static ParametricEQStereoTests parametricEQStereoTests;
