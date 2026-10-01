#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQResponseCore.h"

using namespace APEX::ParametricEQ;

class ParametricEQResponseTests final : public juce::UnitTest
{
public:
    ParametricEQResponseTests() : UnitTest ("ParametricEQ.Response", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testScalarTransferAgainstImpulse();
        testAnalogMatchedBell();
        testPlacementMatrixAgainstMeasuredImpulses();
    }

private:
    void testScalarTransferAgainstImpulse()
    {
        beginTest ("display response is the exact processing transfer function");
        auto engine = ParametricEQTest::preparedEngine (48000.0, 31, 1);
        engine.setBand (0, ParametricEQTest::band (FilterShape::LowCut,
                                                  45.0, 0.0, 1.0, 21.3));
        engine.setBand (1, ParametricEQTest::band (FilterShape::Bell,
                                                  350.0, 7.0, 0.8));
        engine.setBand (2, ParametricEQTest::band (FilterShape::Bell,
                                                  4200.0, -13.0, 7.0));
        engine.setBand (3, ParametricEQTest::band (FilterShape::HighShelf,
                                                  12000.0, 5.0, 0.75));

        const auto impulse = ParametricEQTest::impulseResponse (engine, 131072, 113);
        expect (ParametricEQTest::allFinite (impulse));
        for (const auto frequency : { 20.0, 45.0, 100.0, 350.0, 1000.0,
                                      4200.0, 10000.0, 18000.0, 22000.0 })
        {
            const auto measured = ParametricEQTest::responseFromImpulse (
                impulse, 48000.0, frequency);
            const auto displayed = engine.getResponse (frequency);
            expect (std::abs (ParametricEQTest::db (measured)
                            - ParametricEQTest::db (displayed)) < 0.015,
                    "magnitude mismatch at " + juce::String (frequency)
                        + " Hz: measured=" + juce::String (ParametricEQTest::db (measured), 5)
                        + " analytic=" + juce::String (ParametricEQTest::db (displayed), 5));
            expect (ParametricEQTest::phaseDifference (measured, displayed) < 0.003,
                    "phase mismatch at " + juce::String (frequency));
        }
    }

    void testAnalogMatchedBell()
    {
        beginTest ("analog-matched bell reduces near-Nyquist analog-shape error");
        constexpr double rate = 48000.0;
        constexpr double centre = 18000.0;
        constexpr double gain = 12.0;
        constexpr double q = 3.0;
        const auto rbj = FilterDesigner::designBand (
            ParametricEQTest::band (FilterShape::Bell, centre, gain, q),
            rate, DesignMode::Realtime);
        const auto matched = FilterDesigner::designBand (
            ParametricEQTest::band (FilterShape::Bell, centre, gain, q),
            rate, DesignMode::AnalogMatched);
        expect (matched.usedAnalogMatchedDesign,
                "candidate benchmark must exercise the matched design");

        double rbjSquaredError = 0.0;
        double matchedSquaredError = 0.0;
        int points = 0;
        for (double frequency = 8000.0; frequency <= 23000.0; frequency += 250.0)
        {
            const auto targetDb = 20.0 * std::log10 (
                ParametricEQTest::analogPeakingMagnitude (frequency, centre, q, gain));
            const auto rbjError = ParametricEQTest::db (rbj.response (frequency, rate)) - targetDb;
            const auto matchedError = ParametricEQTest::db (
                matched.response (frequency, rate)) - targetDb;
            rbjSquaredError += rbjError * rbjError;
            matchedSquaredError += matchedError * matchedError;
            ++points;
        }
        const auto rbjRms = std::sqrt (rbjSquaredError / points);
        const auto matchedRms = std::sqrt (matchedSquaredError / points);
        expect (matchedRms < rbjRms * 0.75,
                "matched RMS=" + juce::String (matchedRms, 4)
                    + " dB, RBJ RMS=" + juce::String (rbjRms, 4) + " dB");
    }

    void testPlacementMatrixAgainstMeasuredImpulses()
    {
        beginTest ("2x2 response authority matches measured impulse paths");
        const auto rate = 48000.0;
        std::array<BandSettings, kMaxBands> settings {};
        settings[0] = ParametricEQTest::placedBand (
            FilterShape::Bell, ChannelPlacement::Stereo, 120.0, 5.0, 1.2);
        settings[1] = ParametricEQTest::placedBand (
            FilterShape::Bell, ChannelPlacement::Mid, 1000.0, 8.0, 4.0);
        settings[2] = ParametricEQTest::placedBand (
            FilterShape::HighShelf, ChannelPlacement::Side, 6000.0, -6.0, 0.9);
        settings[3] = ParametricEQTest::placedBand (
            FilterShape::Notch, ChannelPlacement::Left, 2500.0, 0.0, 12.0);
        settings[4] = ParametricEQTest::placedBand (
            FilterShape::Bell, ChannelPlacement::Right, 400.0, -9.0, 2.5);
        settings[5] = ParametricEQTest::placedBand (
            FilterShape::LowCut, ChannelPlacement::Mid, 60.0, 0.0, 0.7, 36.0);

        auto engine = ParametricEQTest::preparedEngine (rate, 257, 2);
        for (int index = 0; index < kMaxBands; ++index)
            engine.setBand (index, settings[static_cast<std::size_t> (index)]);
        const auto paths = ParametricEQTest::stereoImpulseResponses (engine, 16384);

        for (const double frequency : { 40.0, 120.0, 1000.0, 2500.0, 6000.0 })
        {
            const auto matrix = ResponseCore::wetTransfer (
                settings, DesignMode::Realtime, rate, frequency);
            const std::complex<double> measured[] = {
                ParametricEQTest::responseFromImpulse (paths[0], rate, frequency),
                ParametricEQTest::responseFromImpulse (paths[1], rate, frequency),
                ParametricEQTest::responseFromImpulse (paths[2], rate, frequency),
                ParametricEQTest::responseFromImpulse (paths[3], rate, frequency)
            };
            const std::complex<double> analytic[] = {
                matrix.ll, matrix.lr, matrix.rl, matrix.rr
            };
            for (int path = 0; path < 4; ++path)
            {
                expectWithinAbsoluteError (
                    ParametricEQTest::db (measured[path]),
                    ParametricEQTest::db (analytic[path]), 0.05,
                    "matrix magnitude path " + juce::String (path) + " @ "
                        + juce::String (frequency, 0) + " Hz");
                expectWithinAbsoluteError (
                    ParametricEQTest::phaseDifference (measured[path],
                                                       analytic[path]),
                    0.0, 0.02,
                    "matrix phase path " + juce::String (path) + " @ "
                        + juce::String (frequency, 0) + " Hz");
            }
        }

        // Projection authority: a pure Mid band projects onto Mid = H and
        // Side = 1; a pure Side band projects onto Side = H and Mid = 1.
        const auto midFrame = settings[1];
        const auto h = ResponseCore::bandResponse (midFrame, DesignMode::Realtime,
                                                   rate, 1000.0);
        const auto midBand = ResponseCore::bandTransfer (midFrame,
                                                         DesignMode::Realtime,
                                                         rate, 1000.0);
        expectWithinAbsoluteError (std::abs (midBand.midProjection()),
                                   std::abs (h), 1.0e-12);
        expectWithinAbsoluteError (std::abs (midBand.sideProjection()), 1.0, 1.0e-12);

        const auto sideFrame = settings[2];
        const auto sideBand = ResponseCore::bandTransfer (sideFrame,
                                                          DesignMode::Realtime,
                                                          rate, 6000.0);
        expectWithinAbsoluteError (std::abs (sideBand.sideProjection()),
                                   std::abs (ResponseCore::bandResponse (
                                       sideFrame, DesignMode::Realtime,
                                       rate, 6000.0)), 1.0e-12);
        expectWithinAbsoluteError (std::abs (sideBand.midProjection()), 1.0, 1.0e-12);
    }
};

static ParametricEQResponseTests parametricEQResponseTests;

