#include "ParametricEQTestUtils.h"

class ParametricEQRtAllocationTests final : public juce::UnitTest
{
public:
    ParametricEQRtAllocationTests()
        : UnitTest ("ParametricEQ.RtAllocation", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;
        const int blockSizes[] = { 1, 3, 31, 128, 333, 1024 };

        for (const auto blockSize : blockSizes)
        {
            beginTest ("zero allocations: neutral, block " + juce::String (blockSize));
            auto neutral = ParametricEQTest::preparedEngine (48000.0, blockSize, 2);
            juce::AudioBuffer<float> neutralBuffer (2, blockSize);
            ParametricEQTest::fillDeterministic (neutralBuffer);
            auto neutralChannels = neutralBuffer.getArrayOfWritePointers();
            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int iteration = 0; iteration < 2000; ++iteration)
                    neutral.process (neutralChannels, 2, blockSize);
            }

            beginTest ("zero allocations: 24 active mixed shapes, block "
                       + juce::String (blockSize));
            auto dense = ParametricEQTest::preparedEngine (192000.0, blockSize, 2);
            for (int index = 0; index < kMaxBands; ++index)
            {
                const FilterShape shape = index % 6 == 0 ? FilterShape::LowCut
                                        : index % 6 == 1 ? FilterShape::Bell
                                        : index % 6 == 2 ? FilterShape::Notch
                                        : index % 6 == 3 ? FilterShape::LowShelf
                                        : index % 6 == 4 ? FilterShape::AllPass
                                                         : FilterShape::FlatTilt;
                dense.setBand (index, ParametricEQTest::band (
                    shape, 15.0 * std::pow (1.35, index),
                    (index & 1) != 0 ? 18.0 : -12.0,
                    0.1 + index * 0.5,
                    6.0 + (index % 16) * 6.0));
            }
            juce::AudioBuffer<float> denseBuffer (2, blockSize);
            ParametricEQTest::fillDeterministic (denseBuffer, 0x31415926u);
            auto denseChannels = denseBuffer.getArrayOfWritePointers();
            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int iteration = 0; iteration < 64; ++iteration)
                    dense.process (denseChannels, 2, blockSize);
            }
        }

        beginTest ("active numSamples never touches backing-buffer tail");
        constexpr int prepared = 512;
        constexpr int active = 73;
        auto engine = ParametricEQTest::preparedEngine (48000.0, prepared, 2);
        engine.setBand (0, ParametricEQTest::band (FilterShape::LowCut,
                                                  80.0, 0.0, 1.0, 47.5));
        engine.setBand (1, ParametricEQTest::band (FilterShape::Bell,
                                                  3000.0, 24.0, 30.0));
        juce::AudioBuffer<float> backing (2, prepared);
        ParametricEQTest::fillDeterministic (backing);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = active; sample < prepared; ++sample)
                backing.setSample (channel, sample,
                                   channel == 0 ? 123.5f : -77.25f);
        juce::AudioBuffer<float> before;
        before.makeCopyOf (backing);
        auto channels = backing.getArrayOfWritePointers();
        engine.process (channels, 2, active);
        bool intact = true;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = active; sample < prepared; ++sample)
                intact = intact && backing.getSample (channel, sample)
                                   == before.getSample (channel, sample);
        expect (intact);

        beginTest ("zero allocations: all five placements, hostile blocks");
        for (const auto placement : { ChannelPlacement::Stereo,
                                      ChannelPlacement::Left,
                                      ChannelPlacement::Right,
                                      ChannelPlacement::Mid,
                                      ChannelPlacement::Side })
        {
            auto placed = ParametricEQTest::preparedEngine (192000.0, 128, 2);
            for (int index = 0; index < kMaxBands; ++index)
            {
                const FilterShape shape = index % 7 == 0 ? FilterShape::LowCut
                                        : index % 7 == 1 ? FilterShape::HighCut
                                        : index % 7 == 2 ? FilterShape::Bell
                                        : index % 7 == 3 ? FilterShape::LowShelf
                                        : index % 7 == 4 ? FilterShape::Notch
                                        : index % 7 == 5 ? FilterShape::Tilt
                                                         : FilterShape::FlatTilt;
                placed.setBand (index, ParametricEQTest::placedBand (
                    shape, placement, 15.0 * std::pow (1.35, index),
                    (index & 1) != 0 ? 15.0 : -9.0,
                    0.1 + index * 0.5,
                    6.0 + (index % 16) * 6.0));
            }
            for (const int hostile : { 1, 3, 8193 })
            {
                juce::AudioBuffer<float> buffer (2, hostile);
                ParametricEQTest::fillDeterministic (buffer, 0xABCDu);
                auto bufferChannels = buffer.getArrayOfWritePointers();
                {
                    juce::UnitTestAllocationChecker checker (*this);
                    for (int iteration = 0; iteration < 16; ++iteration)
                        placed.process (bufferChannels, 2, hostile);
                }
                expect (ParametricEQTest::allFinite (buffer),
                        "placement " + juce::String (channelPlacementName (placement))
                            + " block " + juce::String (hostile));
            }
        }
    }
};

static ParametricEQRtAllocationTests parametricEQRtAllocationTests;
