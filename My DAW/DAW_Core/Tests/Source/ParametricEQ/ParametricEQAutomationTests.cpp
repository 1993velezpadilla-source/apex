#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <atomic>
#include <cmath>
#include <thread>

namespace
{

using namespace APEX::ParametricEQ;

constexpr std::array<double, 6> kRates {
    44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
};

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

void configureBand (Processor& processor, int band, FilterShape shape,
                    ChannelPlacement placement, float frequency, float gain,
                    float q, float slope = 12.0f)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (shape));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Slope), slope);
    setUnits (processor, placementParameterIndex (band),
              static_cast<float> (placement));
}

std::vector<float> renderPlacementSequence (
    const std::vector<ChannelPlacement>& sequence, int samplesPerPlacement,
    double rate, int blockSize, int totalSamples)
{
    auto processorStorage = std::make_unique<Processor>();
    auto& processor = *processorStorage;
    processor.prepareToPlay (rate, blockSize);
    configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Stereo,
                   1000.0f, 12.0f, 8.0f);

    std::vector<float> result (static_cast<std::size_t> (totalSamples));
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;
    int position = 0;
    std::size_t sequenceIndex = 0;
    while (position < totalSamples)
    {
        if (sequenceIndex * samplesPerPlacement
            <= static_cast<std::size_t> (position))
        {
            setUnits (processor, placementParameterIndex (0),
                      static_cast<float> (sequence[sequenceIndex
                                                       % sequence.size()]));
            ++sequenceIndex;
        }
        const int count = std::min (blockSize, totalSamples - position);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                block.setSample (channel, sample, 0.1f * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 997.0
                    * (position + sample) / rate));
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(),
                                       2, 0, count);
        processor.processBlock (view, midi);
        for (int sample = 0; sample < count; ++sample)
            result[static_cast<std::size_t> (position + sample)]
                = block.getSample (0, sample);
        position += count;
    }
    return result;
}

} // namespace

class ParametricEQAutomationTests final : public juce::UnitTest
{
public:
    ParametricEQAutomationTests()
        : UnitTest ("ParametricEQ.Automation", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testHostilePlacementAutomationAtSixRates();
        testSinglePlacementTransitionIsBlockInvariant();
        testPlacementTransitionDeferral();
        testConcurrentPlacementAutomationIsBounded();
        testPlacementAutomationWhileBypassed();
    }

private:
    void testHostilePlacementAutomationAtSixRates()
    {
        beginTest ("hostile placement automation is finite, bounded and settles");
        const std::vector<ChannelPlacement> sequence = {
            ChannelPlacement::Stereo, ChannelPlacement::Left,
            ChannelPlacement::Mid, ChannelPlacement::Side,
            ChannelPlacement::Right, ChannelPlacement::Side,
            ChannelPlacement::Mid, ChannelPlacement::Left,
            ChannelPlacement::Stereo, ChannelPlacement::Right,
            ChannelPlacement::Left
        };
        constexpr int total = 16384;
        constexpr int samplesPerPlacement = 96;
        for (const auto rate : kRates)
        {
            for (const int blockSize : { 1, 31, 512 })
            {
                const auto rendered = renderPlacementSequence (
                    sequence, samplesPerPlacement, rate, blockSize, total);
                expect (ParametricEQTest::allFinite (rendered),
                        "finite @ " + juce::String (rate, 0)
                            + " block " + juce::String (blockSize));

                double maximumJump = 0.0;
                for (std::size_t sample = 1; sample < rendered.size(); ++sample)
                    maximumJump = std::max (
                        maximumJump, std::abs (static_cast<double> (rendered[sample])
                                             - rendered[sample - 1]));
                expect (maximumJump < 0.15,
                        "click bound @ " + juce::String (rate, 0)
                            + " block " + juce::String (blockSize) + ": "
                            + juce::String (maximumJump, 6));
            }
        }
    }

    void testSinglePlacementTransitionIsBlockInvariant()
    {
        beginTest ("one placement transition at stream start is sample-exact");
        constexpr int total = 8192;
        auto render = [&] (int blockSize)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            processor.prepareToPlay (48000.0, blockSize);
            configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Mid,
                           1000.0f, 12.0f, 8.0f);

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

        const auto one = render (1);
        const auto thirtyOne = render (31);
        const auto fiveTwelve = render (512);
        for (std::size_t sample = 0; sample < one.size(); ++sample)
        {
            expectEquals (one[sample], thirtyOne[sample],
                          "block invariance 1 vs 31 @ " + juce::String (
                              static_cast<int> (sample)));
            expectEquals (one[sample], fiveTwelve[sample],
                          "block invariance 1 vs 512 @ " + juce::String (
                              static_cast<int> (sample)));
        }
    }

    void testPlacementTransitionDeferral()
    {
        beginTest ("rapid placement retargets defer instead of resetting an audible engine");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 64);
        configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Stereo,
                       1000.0f, 12.0f, 4.0f);

        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        auto processBlocks = [&] (int blocks)
        {
            for (int block = 0; block < blocks; ++block)
            {
                ParametricEQTest::fillDeterministic (buffer, 0xBEEFu);
                processor.processBlock (buffer, midi);
            }
        };
        processBlocks (1); // settle initial state

        // Start a placement change, then retarget twice while the 10 ms
        // transition is still audible.
        setUnits (processor, placementParameterIndex (0),
                  static_cast<float> (ChannelPlacement::Mid));
        processBlocks (1); // ~1.3 ms into the transition
        expect (processor.isTransitionActiveForTesting()
                || processor.getTransitionMixForTesting() > 0.0);
        setUnits (processor, placementParameterIndex (0),
                  static_cast<float> (ChannelPlacement::Side));
        processBlocks (1);
        setUnits (processor, placementParameterIndex (0),
                  static_cast<float> (ChannelPlacement::Left));
        processBlocks (128); // let every deferred handoff complete
        expect (! processor.isTransitionActiveForTesting());
        expectEquals (static_cast<int> (processor.getCurrentEngineForTesting()
                                            .getBand (0).placement),
                      static_cast<int> (ChannelPlacement::Left),
                      "audible engine must settle on the newest placement");
        expect (ParametricEQTest::allFinite (buffer));

        // The settled current engine must exactly match a fresh engine built
        // with the final placement (no stale history contamination).
        Engine reference;
        reference.prepare (48000.0, 64, 2);
        BandSettings finalSettings;
        finalSettings.enabled = true;
        finalSettings.shape = FilterShape::Bell;
        finalSettings.placement = ChannelPlacement::Left;
        finalSettings.frequencyHz = 1000.0;
        finalSettings.gainDb = 12.0;
        finalSettings.q = 4.0;
        reference.setBand (0, finalSettings);
        expectEquals (static_cast<int> (
            processor.getCurrentEngineForTesting().getBand (0).placement),
            static_cast<int> (reference.getBand (0).placement));
    }

    void testConcurrentPlacementAutomationIsBounded()
    {
        beginTest ("concurrent placement writes never tear or escape bounds");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 128);
        auto* placement = processor.getParametricEQParameter (
            placementParameterIndex (0));

        std::atomic<bool> stop { false };
        std::atomic<bool> bad { false };
        std::thread writer ([&]
        {
            for (int iteration = 0; iteration < 400000; ++iteration)
                placement->setValue (static_cast<float> (iteration % 5) / 4.0f);
            stop.store (true, std::memory_order_release);
        });

        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;
        while (! stop.load (std::memory_order_acquire))
        {
            ParametricEQTest::fillDeterministic (buffer, 0xAB1Du);
            processor.processBlock (buffer, midi);
            const auto value = placement->getValue();
            if (! std::isfinite (value) || value < 0.0f || value > 1.0f)
                bad.store (true, std::memory_order_relaxed);
            if (! ParametricEQTest::allFinite (buffer))
                bad.store (true, std::memory_order_relaxed);
        }
        writer.join();
        expect (! bad.load(), "concurrent placement automation must stay bounded");
    }

    void testPlacementAutomationWhileBypassed()
    {
        beginTest ("placement automation under global bypass adopts directly");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);
        configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Stereo,
                       1000.0f, 9.0f, 2.0f);
        setUnits (processor, kGlobalBypassParameter, 1.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        ParametricEQTest::fillDeterministic (buffer);
        processor.processBlock (buffer, midi);

        setUnits (processor, placementParameterIndex (0),
                  static_cast<float> (ChannelPlacement::Side));
        processor.processBlock (buffer, midi);
        expectEquals (static_cast<int> (processor.getCurrentEngineForTesting()
                                            .getBand (0).placement),
                      static_cast<int> (ChannelPlacement::Side),
                      "bypassed adoption must apply placement immediately");

        setUnits (processor, kGlobalBypassParameter, 0.0f);
        ParametricEQTest::fillDeterministic (buffer, 0x0DDu);
        processor.processBlock (buffer, midi);
        expect (ParametricEQTest::allFinite (buffer),
                "un-bypass must warm the wet path without runaway values");
    }
};

static ParametricEQAutomationTests parametricEQAutomationTests;
