#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <cmath>
#include <vector>

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

} // namespace

class ParametricEQDynamicAutomationTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicAutomationTests()
        : UnitTest ("ParametricEQ.DynamicAutomation", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testRangeCrossingZeroDuringActiveReduction();
        testHostileCombinedSweeps();
        testSingleChangeAtStreamStartIsBlockInvariant();
        testAutomationWhileEngineTransitionActive();
    }

private:
    void testRangeCrossingZeroDuringActiveReduction()
    {
        beginTest ("range crossing zero during active reduction stays total");
        const float ranges[] = { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f,
                                 -6.0f, 0.0f, 6.0f, 12.0f };

        // Per-sample continuity oracle: the 1 ms attack legitimately moves
        // several dB per 64-sample block, so click safety is proven on the
        // per-sample state deltas of a 1-sample-block rendering.
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, 1);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                      -40.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 12.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Attack),
                      0.001f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Release),
                      0.020f);

            juce::AudioBuffer<float> block (2, 1);
            block.clear();
            juce::MidiBuffer midi;
            double previous = 0.0;
            double maximumPerSampleJump = 0.0;
            for (int position = 0; position < 9600; ++position)
            {
                setUnits (processor,
                          dynamicParameterIndex (0, DynamicBandOffset::Range),
                          ranges[(position / 64) % 9]);
                const auto value = static_cast<float> (0.7 * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 937.5
                    * position / 48000.0));
                block.setSample (0, 0, value);
                block.setSample (1, 0, value);
                processor.processBlock (block, midi);
                const auto gain = processor.getDynamicGainDbForTesting (0);
                expect (std::isfinite (gain), "per-sample gain stays finite");
                maximumPerSampleJump = std::max (maximumPerSampleJump,
                                                 std::abs (gain - previous));
                previous = gain;
            }
            expect (maximumPerSampleJump < 0.6,
                    "zero-crossing automation must be click-free per sample; max "
                        + juce::String (maximumPerSampleJump, 4) + " dB");
        }

        // Coarse-block rendering: finite and bounded by the legitimate
        // 1 ms attack step (12 * (1 - exp(-64/48)) dB per 64-sample block).
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, 64);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                      -40.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 12.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Attack),
                      0.001f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Release),
                      0.020f);

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            juce::MidiBuffer midi;
            double previous = 0.0;
            double maximumBlockJump = 0.0;
            for (int block = 0; block < 160; ++block)
            {
                setUnits (processor,
                          dynamicParameterIndex (0, DynamicBandOffset::Range),
                          ranges[block % 9]);
                for (int sample = 0; sample < 64; ++sample)
                {
                    const auto value = static_cast<float> (0.7 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 937.5
                        * (block * 64 + sample) / 48000.0));
                    buffer.setSample (0, sample, value);
                    buffer.setSample (1, sample, value);
                }
                processor.processBlock (buffer, midi);
                expect (ParametricEQTest::allFinite (buffer),
                        "finite through zero-crossing automation");
                const auto gain = processor.getDynamicGainDbForTesting (0);
                expect (std::isfinite (gain), "dynamic gain stays finite");
                maximumBlockJump = std::max (maximumBlockJump,
                                             std::abs (gain - previous));
                previous = gain;
            }
            expect (maximumBlockJump < 9.5,
                    "block delta bounded by the legitimate attack step; max "
                        + juce::String (maximumBlockJump, 4) + " dB");
        }
    }

    void testHostileCombinedSweeps()
    {
        beginTest ("hostile combined dynamics automation stays finite and bounded");
        for (const int blockSize : { 1, 31, 512 })
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            for (int band = 0; band < 6; ++band)
                configureBell (processor, band, 200.0f * (band + 1), 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, blockSize);

            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            juce::MidiBuffer midi;
            constexpr int total = 16384;
            int position = 0;
            while (position < total)
            {
                for (int band = 0; band < 6; ++band)
                {
                    setUnits (processor,
                              dynamicParameterIndex (band, DynamicBandOffset::Enable),
                              (position / 100 + band) % 3 == 0 ? 0.0f : 1.0f);
                    setUnits (processor,
                              dynamicParameterIndex (band, DynamicBandOffset::Threshold),
                              -50.0f + 40.0f * std::abs (std::sin (
                                  position * 0.0001 + band)));
                    setUnits (processor,
                              dynamicParameterIndex (band, DynamicBandOffset::Range),
                              24.0f * std::sin (position * 0.0002 + band));
                    setUnits (processor,
                              dynamicParameterIndex (band, DynamicBandOffset::Attack),
                              0.0005f + 0.05f * std::abs (std::sin (
                                  position * 0.0003 + band)));
                    setUnits (processor,
                              dynamicParameterIndex (band, DynamicBandOffset::Release),
                              0.001f + 0.5f * std::abs (std::cos (
                                  position * 0.0004 + band)));
                }
                setUnits (processor, kDynamicDetectorParameter,
                          (position / 500) % 2 == 0 ? 1.0f : 0.0f);
                setUnits (processor, kDynamicLinkParameter,
                          (position / 700) % 2 == 0 ? 1.0f : 0.0f);

                const int count = std::min (blockSize, total - position);
                for (int sample = 0; sample < count; ++sample)
                {
                    const auto value = static_cast<float> (0.4 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 937.5
                        * (position + sample) / 48000.0));
                    buffer.setSample (0, sample, value);
                    buffer.setSample (1, sample, value);
                }
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                               2, 0, count);
                processor.processBlock (view, midi);
                expect (ParametricEQTest::allFinite (buffer),
                        "finite @ block " + juce::String (blockSize));
                position += count;
            }
        }
    }

    void testSingleChangeAtStreamStartIsBlockInvariant()
    {
        beginTest ("a single dynamics change at stream start is sample-exact");
        auto render = [] (int blockSize)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, blockSize);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                      -30.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Attack),
                      0.001f);
            setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Release),
                      0.050f);

            std::vector<float> result;
            constexpr int total = 8192;
            result.reserve (static_cast<std::size_t> (total));
            juce::AudioBuffer<float> block (2, blockSize);
            block.clear();
            juce::MidiBuffer midi;
            int position = 0;
            while (position < total)
            {
                const int count = std::min (blockSize, total - position);
                for (int sample = 0; sample < count; ++sample)
                {
                    const auto value = static_cast<float> (0.3 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 937.5
                        * (position + sample) / 48000.0));
                    block.setSample (0, sample, value);
                    block.setSample (1, sample, value);
                }
                juce::AudioBuffer<float> view (block.getArrayOfWritePointers(),
                                               2, 0, count);
                processor.processBlock (view, midi);
                for (int sample = 0; sample < count; ++sample)
                    result.push_back (block.getSample (0, sample));
                position += count;
            }
            return result;
        };

        const auto one = render (1);
        const auto thirtyOne = render (31);
        const auto fiveTwelve = render (512);
        for (std::size_t sample = 0; sample < one.size(); ++sample)
        {
            expectEquals (one[sample], thirtyOne[sample]);
            expectEquals (one[sample], fiveTwelve[sample]);
        }
    }

    void testAutomationWhileEngineTransitionActive()
    {
        beginTest ("dynamics automation during an active static transition stays safe");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 64);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);

        juce::AudioBuffer<float> buffer (2, 64);
        buffer.clear();
        juce::MidiBuffer midi;
        for (int block = 0; block < 100; ++block)
        {
            if (block % 8 == 0)
                setUnits (processor, parameterIndex (0, BandParameterOffset::Gain),
                          (block / 8) % 2 == 0 ? 12.0f : 3.0f);
            if (block % 5 == 0)
                setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                          -50.0f + 3.0f * (block % 10));
            for (int sample = 0; sample < 64; ++sample)
            {
                const auto value = static_cast<float> (0.6 * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 937.5
                    * (block * 64 + sample) / 48000.0));
                buffer.setSample (0, sample, value);
                buffer.setSample (1, sample, value);
            }
            processor.processBlock (buffer, midi);
            expect (ParametricEQTest::allFinite (buffer),
                    "finite during overlapping transitions");
        }
    }
};

static ParametricEQDynamicAutomationTests parametricEQDynamicAutomationTests;
