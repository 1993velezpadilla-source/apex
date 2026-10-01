#include "ParametricEQTestUtils.h"

class ParametricEQCompositionTests final : public juce::UnitTest
{
public:
    ParametricEQCompositionTests()
        : UnitTest ("ParametricEQ.Composition", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;

        beginTest ("cascade response is the complex product of every band");
        auto engine = ParametricEQTest::preparedEngine();
        const auto b0 = ParametricEQTest::band (FilterShape::Bell, 1000.0, 12.0, 2.0);
        const auto b1 = ParametricEQTest::band (FilterShape::Bell, 1300.0, -9.0, 3.0);
        const auto b2 = ParametricEQTest::band (FilterShape::HighShelf, 8000.0, 5.0, 0.8);
        engine.setBand (0, b0);
        engine.setBand (1, b1);
        engine.setBand (2, b2);

        auto e0 = ParametricEQTest::preparedEngine();
        auto e1 = ParametricEQTest::preparedEngine();
        auto e2 = ParametricEQTest::preparedEngine();
        e0.setBand (0, b0);
        e1.setBand (0, b1);
        e2.setBand (0, b2);

        for (const auto frequency : { 30.0, 300.0, 1000.0, 1300.0,
                                      5000.0, 12000.0, 22000.0 })
        {
            const auto expected = e0.getResponse (frequency)
                                * e1.getResponse (frequency)
                                * e2.getResponse (frequency);
            expect (std::abs (engine.getResponse (frequency) - expected) < 1.0e-12);
        }

        beginTest ("24 overlapping bands remain finite and analytically composable");
        auto dense = ParametricEQTest::preparedEngine (192000.0, 127, 2);
        for (int index = 0; index < kMaxBands; ++index)
        {
            auto settings = ParametricEQTest::band (
                FilterShape::Bell,
                100.0 * std::pow (1.22, index),
                (index % 3 == 0) ? 9.0 : -4.5,
                0.2 + index * 0.35);
            dense.setBand (index, settings,
                           (index & 1) != 0 ? DesignMode::AnalogMatched
                                            : DesignMode::Realtime);
        }
        for (double frequency = 10.0; frequency < 90000.0; frequency *= 1.07)
        {
            const auto response = dense.getResponse (frequency);
            expect (std::isfinite (response.real()) && std::isfinite (response.imag()));
        }
    }
};

static ParametricEQCompositionTests parametricEQCompositionTests;
