#include "ParametricEQTestUtils.h"

class ParametricEQNeutralPathTests final : public juce::UnitTest
{
public:
    ParametricEQNeutralPathTests()
        : UnitTest ("ParametricEQ.NeutralPath", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0,
                                 176400.0, 192000.0 };

        beginTest ("no active bands is bit-exact at every supported rate");
        for (const auto rate : rates)
        {
            auto engine = ParametricEQTest::preparedEngine (rate, 7, 2);
            juce::AudioBuffer<float> input (2, 4099);
            ParametricEQTest::fillDeterministic (input, static_cast<std::uint32_t> (rate));
            juce::AudioBuffer<float> output;
            output.makeCopyOf (input);
            ParametricEQTest::processBuffer (engine, output, 0, output.getNumSamples());
            expect (ParametricEQTest::bitEqual (input, output),
                    "neutral mismatch at " + juce::String (rate));
        }

        beginTest ("zero-gain gain shapes, bypassed bands and zero-slope cuts are exact wire");
        auto engine = ParametricEQTest::preparedEngine (48000.0, 16, 2);
        engine.setBand (0, ParametricEQTest::band (FilterShape::Bell, 1000.0, 0.0, 100.0));
        engine.setBand (1, ParametricEQTest::band (FilterShape::LowShelf, 200.0, 0.0, 0.025));
        engine.setBand (2, ParametricEQTest::band (FilterShape::HighShelf, 18000.0, 0.0, 100.0));
        engine.setBand (3, ParametricEQTest::band (FilterShape::Tilt, 1000.0, 0.0, 1.0));
        engine.setBand (4, ParametricEQTest::band (FilterShape::FlatTilt, 1000.0, 0.0, 1.0));
        engine.setBand (5, ParametricEQTest::band (FilterShape::LowCut, 1000.0, 0.0, 1.0, 0.0));
        auto bypassed = ParametricEQTest::band (FilterShape::Bell, 5000.0, 36.0, 100.0);
        bypassed.bypassed = true;
        engine.setBand (6, bypassed);

        juce::AudioBuffer<float> input (2, 8193);
        ParametricEQTest::fillDeterministic (input);
        juce::AudioBuffer<float> output;
        output.makeCopyOf (input);
        ParametricEQTest::processBuffer (engine, output, 0, output.getNumSamples());
        expect (ParametricEQTest::bitEqual (input, output));
    }
};

static ParametricEQNeutralPathTests parametricEQNeutralPathTests;
