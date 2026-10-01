#include "ParametricEQTestUtils.h"

class ParametricEQFilterTests final : public juce::UnitTest
{
public:
    ParametricEQFilterTests() : UnitTest ("ParametricEQ.Filters", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;

        beginTest ("Bell centre gain and reciprocal boost/cut");
        auto boost = ParametricEQTest::preparedEngine();
        auto cut = ParametricEQTest::preparedEngine();
        boost.setBand (0, ParametricEQTest::band (FilterShape::Bell, 3000.0, 18.0, 2.0));
        cut.setBand (0, ParametricEQTest::band (FilterShape::Bell, 3000.0, -18.0, 2.0));
        expectWithinAbsoluteError (boost.getMagnitudeDb (3000.0), 18.0, 1.0e-9);
        expectWithinAbsoluteError (cut.getMagnitudeDb (3000.0), -18.0, 1.0e-9);
        for (const auto frequency : { 50.0, 500.0, 3000.0, 12000.0, 22000.0 })
        {
            const auto product = boost.getResponse (frequency) * cut.getResponse (frequency);
            expectWithinAbsoluteError (std::abs (product), 1.0, 2.0e-10,
                                       "reciprocal response at " + juce::String (frequency));
        }

        beginTest ("Low/high shelves reach specified endpoint gains");
        auto shelves = ParametricEQTest::preparedEngine (192000.0);
        shelves.setBand (0, ParametricEQTest::band (FilterShape::LowShelf,
                                                   1000.0, 12.0, 0.70710678118));
        expectWithinAbsoluteError (shelves.getMagnitudeDb (0.0), 12.0, 1.0e-8);
        expect (std::abs (shelves.getMagnitudeDb (80000.0)) < 0.01);
        shelves.setBand (0, ParametricEQTest::band (FilterShape::HighShelf,
                                                   1000.0, -12.0, 0.70710678118));
        expect (std::abs (shelves.getMagnitudeDb (0.0)) < 0.01);
        expectWithinAbsoluteError (shelves.getMagnitudeDb (96000.0), -12.0, 1.0e-6);

        beginTest ("Notch, band-pass and all-pass contracts");
        auto special = ParametricEQTest::preparedEngine();
        special.setBand (0, ParametricEQTest::band (FilterShape::Notch, 4000.0, 0.0, 10.0));
        expect (special.getMagnitudeDb (4000.0) < -220.0);
        special.setBand (0, ParametricEQTest::band (FilterShape::BandPass, 4000.0, 0.0, 4.0));
        expectWithinAbsoluteError (special.getMagnitudeDb (4000.0), 0.0, 1.0e-9);
        special.setBand (0, ParametricEQTest::band (FilterShape::AllPass, 4000.0, 0.0, 4.0));
        for (const auto frequency : { 0.0, 50.0, 1000.0, 4000.0, 12000.0, 23900.0 })
            expectWithinAbsoluteError (special.getMagnitudeDb (frequency), 0.0, 2.0e-9);
        expect (std::abs (std::arg (special.getResponse (4000.0))) > 0.1,
                "all-pass must alter phase while preserving magnitude");

        beginTest ("APEX Tilt and Flat Tilt have centred opposite endpoints");
        auto tilt = ParametricEQTest::preparedEngine (192000.0);
        tilt.setBand (0, ParametricEQTest::band (FilterShape::Tilt,
                                               1000.0, 12.0, 0.70710678118));
        expectWithinAbsoluteError (tilt.getMagnitudeDb (0.0), -6.0, 1.0e-8);
        expectWithinAbsoluteError (tilt.getMagnitudeDb (96000.0), 6.0, 1.0e-6);
        expect (std::abs (tilt.getMagnitudeDb (1000.0)) < 0.05);
        tilt.setBand (0, ParametricEQTest::band (FilterShape::FlatTilt,
                                               1000.0, 12.0, 0.70710678118));
        expectWithinAbsoluteError (tilt.getMagnitudeDb (0.0), -6.0, 1.0e-8);
        expectWithinAbsoluteError (tilt.getMagnitudeDb (96000.0), 6.0, 1.0e-6);

        beginTest ("Butterworth cut corner and continuous slope boundaries");
        for (const auto slope : { 6.0, 12.0, 18.0, 24.0, 48.0, 96.0 })
        {
            auto lowCut = ParametricEQTest::preparedEngine();
            lowCut.setBand (0, ParametricEQTest::band (FilterShape::LowCut,
                                                      1000.0, 0.0, 1.0, slope));
            expectWithinAbsoluteError (lowCut.getMagnitudeDb (1000.0),
                                       -3.01029995664, 2.0e-8,
                                       "low-cut corner at " + juce::String (slope));
            auto highCut = ParametricEQTest::preparedEngine();
            highCut.setBand (0, ParametricEQTest::band (FilterShape::HighCut,
                                                       1000.0, 0.0, 1.0, slope));
            expectWithinAbsoluteError (highCut.getMagnitudeDb (1000.0),
                                       -3.01029995664, 2.0e-8,
                                       "high-cut corner at " + juce::String (slope));
        }

        auto noCut = ParametricEQTest::preparedEngine();
        noCut.setBand (0, ParametricEQTest::band (FilterShape::LowCut,
                                                 1000.0, 0.0, 1.0, 0.0));
        expectWithinAbsoluteError (noCut.getMagnitudeDb (20.0), 0.0, 1.0e-12);
        auto belowBoundary = ParametricEQTest::preparedEngine();
        auto aboveBoundary = ParametricEQTest::preparedEngine();
        belowBoundary.setBand (0, ParametricEQTest::band (FilterShape::LowCut,
                                                         1000.0, 0.0, 1.0, 5.999999));
        aboveBoundary.setBand (0, ParametricEQTest::band (FilterShape::LowCut,
                                                         1000.0, 0.0, 1.0, 6.000001));
        expect (std::abs (belowBoundary.getMagnitudeDb (200.0)
                        - aboveBoundary.getMagnitudeDb (200.0)) < 1.0e-4,
                "continuous order morph must not jump at six-dB boundaries");
    }
};

static ParametricEQFilterTests parametricEQFilterTests;
