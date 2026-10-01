#include "ParametricEQTestUtils.h"

class ParametricEQBlockSizeTests final : public juce::UnitTest
{
public:
    ParametricEQBlockSizeTests()
        : UnitTest ("ParametricEQ.BlockSizes", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;
        const int blockSizes[] = { 1, 2, 3, 7, 16, 31, 32, 63, 64, 127,
                                   128, 255, 256, 511, 512, 1024, 4097 };

        beginTest ("hostile fixed block sizes are finite and complete");
        for (const auto blockSize : blockSizes)
        {
            auto engine = ParametricEQTest::preparedEngine (96000.0, blockSize, 2);
            engine.setBand (0, ParametricEQTest::band (FilterShape::LowCut,
                                                      17.0, 0.0, 1.0, 95.9));
            engine.setBand (1, ParametricEQTest::band (FilterShape::Bell,
                                                      39000.0, 30.0, 80.0));
            juce::AudioBuffer<float> buffer (2, blockSize);
            ParametricEQTest::fillDeterministic (buffer,
                                                 static_cast<std::uint32_t> (blockSize));
            ParametricEQTest::processBuffer (engine, buffer, 0, blockSize);
            expect (ParametricEQTest::allFinite (buffer),
                    "non-finite at block " + juce::String (blockSize));
        }

        beginTest ("random irregular partitions are sample-order invariant");
        auto single = ParametricEQTest::preparedEngine (48000.0, 1, 1);
        auto irregular = ParametricEQTest::preparedEngine (48000.0, 1024, 1);
        const auto configure = [] (Engine& engine)
        {
            engine.setBand (0, ParametricEQTest::band (FilterShape::LowCut,
                                                      30.0, 0.0, 1.0, 23.4));
            engine.setBand (1, ParametricEQTest::band (FilterShape::Bell,
                                                      1700.0, 17.0, 9.0));
            engine.setBand (2, ParametricEQTest::band (FilterShape::FlatTilt,
                                                      900.0, -8.0, 0.6));
        };
        configure (single);
        configure (irregular);

        juce::AudioBuffer<float> reference (1, 10007);
        ParametricEQTest::fillDeterministic (reference);
        juce::AudioBuffer<float> partitioned;
        partitioned.makeCopyOf (reference);
        for (int sample = 0; sample < reference.getNumSamples(); ++sample)
            ParametricEQTest::processBuffer (single, reference, sample, 1);

        const int pattern[] = { 3, 1, 127, 7, 512, 2, 31, 1024, 63 };
        int offset = 0;
        int patternIndex = 0;
        while (offset < partitioned.getNumSamples())
        {
            const auto count = std::min (pattern[patternIndex++ % 9],
                                         partitioned.getNumSamples() - offset);
            ParametricEQTest::processBuffer (irregular, partitioned, offset, count);
            offset += count;
        }

        expect (ParametricEQTest::bitEqual (reference, partitioned),
                "static recurrence must be bit-identical for any partition");
    }
};

static ParametricEQBlockSizeTests parametricEQBlockSizeTests;
