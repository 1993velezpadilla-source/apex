#include "ParametricEQTestUtils.h"

class ParametricEQSampleRateTests final : public juce::UnitTest
{
public:
    ParametricEQSampleRateTests()
        : UnitTest ("ParametricEQ.SampleRates", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0,
                                 176400.0, 192000.0 };

        beginTest ("centre/corner accuracy across the six-rate matrix");
        for (const auto rate : rates)
        {
            auto bell = ParametricEQTest::preparedEngine (rate);
            bell.setBand (0, ParametricEQTest::band (FilterShape::Bell,
                                                    10000.0, 15.0, 6.0));
            expectWithinAbsoluteError (bell.getMagnitudeDb (10000.0), 15.0, 2.0e-8,
                                       "bell at " + juce::String (rate));

            auto cut = ParametricEQTest::preparedEngine (rate);
            cut.setBand (0, ParametricEQTest::band (FilterShape::HighCut,
                                                   10000.0, 0.0, 1.0, 36.0));
            expectWithinAbsoluteError (cut.getMagnitudeDb (10000.0),
                                       -3.01029995664, 3.0e-8,
                                       "cut at " + juce::String (rate));
        }

        beginTest ("frequency is safely clamped to active-rate Nyquist margin");
        for (const auto rate : rates)
        {
            auto engine = ParametricEQTest::preparedEngine (rate);
            engine.setBand (0, ParametricEQTest::band (FilterShape::Bell,
                                                      1000000.0, 12.0, 4.0));
            expectWithinAbsoluteError (
                engine.getBand (0).frequencyHz,
                maximumUsableFrequency (rate), 1.0e-9,
                "clamp at " + juce::String (rate));
            expect (std::isfinite (engine.getMagnitudeDb (rate * 0.499)));
        }
    }
};

static ParametricEQSampleRateTests parametricEQSampleRateTests;
