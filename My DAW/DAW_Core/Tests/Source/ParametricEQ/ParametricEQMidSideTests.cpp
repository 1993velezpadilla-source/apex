#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"
#include "../../../Source/ParametricEQCore/ParametricEQResponseCore.h"

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
    }
}

void expectMatrixMatchesEngine (juce::UnitTest& test,
    const std::array<BandSettings, kMaxBands>& settings, DesignMode mode,
    double sampleRate, int length, double toleranceDb, const juce::String& label)
{
    auto engine = ParametricEQTest::preparedEngine (sampleRate, 257, 2);
    for (int bandIndex = 0; bandIndex < kMaxBands; ++bandIndex)
        engine.setBand (bandIndex, settings[static_cast<std::size_t> (bandIndex)],
                        mode);
    const auto paths = ParametricEQTest::stereoImpulseResponses (engine, length);

    for (const double frequency : { 80.0, 500.0, 2000.0, 8000.0 })
    {
        const auto matrix = ResponseCore::wetTransfer (settings, mode, sampleRate,
                                                       frequency);
        const auto ll = ParametricEQTest::responseFromImpulse (
            paths[0], sampleRate, frequency);
        const auto lr = ParametricEQTest::responseFromImpulse (
            paths[1], sampleRate, frequency);
        const auto rl = ParametricEQTest::responseFromImpulse (
            paths[2], sampleRate, frequency);
        const auto rr = ParametricEQTest::responseFromImpulse (
            paths[3], sampleRate, frequency);
        const auto suffix = label + " @ " + juce::String (frequency, 0) + " Hz";
        test.expectWithinAbsoluteError (db (ll), db (matrix.ll), toleranceDb,
                                        "LL " + suffix);
        test.expectWithinAbsoluteError (db (lr), db (matrix.lr), toleranceDb,
                                        "LR " + suffix);
        test.expectWithinAbsoluteError (db (rl), db (matrix.rl), toleranceDb,
                                        "RL " + suffix);
        test.expectWithinAbsoluteError (db (rr), db (matrix.rr), toleranceDb,
                                        "RR " + suffix);
    }
}

} // namespace

class ParametricEQMidSideTests final : public juce::UnitTest
{
public:
    ParametricEQMidSideTests()
        : UnitTest ("ParametricEQ.MidSide", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testEncodeDecodeUnityAndConvention();
        testNeutralEncodeDecodeIsExactWire();
        testMidSideComponentSemantics();
        testMixedPlacementCompositionMatchesMatrix();
        testPlacementOrderMatters();
        testOverlappingMidSideBandsAndStress();
    }

private:
    void testEncodeDecodeUnityAndConvention()
    {
        beginTest ("matching peak-safe M/S convention reconstructs exactly");
        std::uint32_t state = 0x51DE0001u;
        for (int iteration = 0; iteration < 10000; ++iteration)
        {
            state = state * 1664525u + 1013904223u;
            const auto left = static_cast<double> (state) / 4294967296.0 * 4.0 - 2.0;
            state = state * 1664525u + 1013904223u;
            const auto right = static_cast<double> (state) / 4294967296.0 * 4.0 - 2.0;
            double mid = 0.0;
            double side = 0.0;
            encodeMidSide (left, right, mid, side);
            double decodedLeft = 0.0;
            double decodedRight = 0.0;
            decodeMidSide (mid, side, decodedLeft, decodedRight);
            expectWithinAbsoluteError (decodedLeft, left,
                                       std::max (1.0e-15, std::abs (left) * 1.0e-15));
            expectWithinAbsoluteError (decodedRight, right,
                                       std::max (1.0e-15, std::abs (right) * 1.0e-15));
        }

        const double value = 0.25;
        double mid = 0.0, side = 0.0;
        encodeMidSide (value, value, mid, side);
        expectWithinAbsoluteError (mid, value, 1.0e-15);
        expectWithinAbsoluteError (side, 0.0, 1.0e-15);
        encodeMidSide (value, -value, mid, side);
        expectWithinAbsoluteError (mid, 0.0, 1.0e-15);
        expectWithinAbsoluteError (side, value, 1.0e-15);
        encodeMidSide (value, 0.0, mid, side);
        expectWithinAbsoluteError (mid, 0.5 * value, 1.0e-15);
        expectWithinAbsoluteError (side, 0.5 * value, 1.0e-15);
        encodeMidSide (0.0, value, mid, side);
        expectWithinAbsoluteError (mid, 0.5 * value, 1.0e-15);
        expectWithinAbsoluteError (side, -0.5 * value, 1.0e-15);
    }

    void testNeutralEncodeDecodeIsExactWire()
    {
        beginTest ("identity bands do not introduce encode/decode rounding");
        for (const auto placement : { ChannelPlacement::Mid, ChannelPlacement::Side })
        {
            auto engine = ParametricEQTest::preparedEngine (48000.0, 128, 2);
            engine.setBand (0, placedBand (FilterShape::Bell, placement,
                                           1000.0, 0.0, 1.0));
            engine.setBand (1, placedBand (FilterShape::LowCut, placement,
                                           80.0, 0.0, 1.0, 0.0));
            juce::AudioBuffer<float> buffer (2, 128);
            juce::AudioBuffer<float> expected (2, 128);
            ParametricEQTest::fillDeterministic (buffer, 0x0DDBA11u);
            expected.makeCopyOf (buffer);
            ParametricEQTest::processBuffer (engine, buffer, 0, 128);
            expect (ParametricEQTest::bitEqual (buffer, expected),
                    "identity M/S bands must be exact wire");
        }
    }

    void testMidSideComponentSemantics()
    {
        beginTest ("mid/side process exactly their own component");
        const auto rate = 48000.0;
        const auto midSettings = placedBand (FilterShape::Bell,
                                             ChannelPlacement::Mid,
                                             1500.0, 9.0, 3.0);
        const auto reference = monoReferenceImpulse (midSettings, rate, 8192);

        {
            // Dual-mono input: M = x, S = 0. Mid band filters both channels;
            // Side band is exact wire.
            auto fresh = ParametricEQTest::preparedEngine (rate, 257, 2);
            fresh.setBand (0, midSettings);
            auto paths = ParametricEQTest::dualMonoImpulseResponses (fresh, 8192);
            expectTransferMatches (*this, paths[0], reference, rate, 0.05, "Mid LL dual-mono");
            expectTransferMatches (*this, paths[1], reference, rate, 0.05, "Mid LR dual-mono");
            expectTransferMatches (*this, paths[2], reference, rate, 0.05, "Mid RL dual-mono");
            expectTransferMatches (*this, paths[3], reference, rate, 0.05, "Mid RR dual-mono");

            BandSettings sideSettings = midSettings;
            sideSettings.placement = ChannelPlacement::Side;
            auto sideEngine = ParametricEQTest::preparedEngine (rate, 257, 2);
            sideEngine.setBand (0, sideSettings);
            auto sidePaths = ParametricEQTest::dualMonoImpulseResponses (
                sideEngine, 8192);
            for (int path = 0; path < 4; ++path)
            {
                expectEquals (sidePaths[static_cast<std::size_t> (path)][0], 1.0f,
                              "Side dual-mono impulse wire");
                for (std::size_t sample = 1; sample < sidePaths[
                         static_cast<std::size_t> (path)].size(); ++sample)
                    expectEquals (sidePaths[static_cast<std::size_t> (path)][sample],
                                  0.0f, "Side dual-mono tail wire");
            }
        }

        {
            // Anti-correlated input: M = 0, S = x. Side band filters the
            // component; Mid band is exact wire.
            BandSettings sideSettings = midSettings;
            sideSettings.placement = ChannelPlacement::Side;
            auto sideEngine = ParametricEQTest::preparedEngine (rate, 257, 2);
            sideEngine.setBand (0, sideSettings);

            const int length = 8192;
            std::array<std::vector<float>, 4> paths;
            for (int path = 0; path < 4; ++path)
            {
                auto copy = sideEngine;
                copy.reset();
                auto& measured = paths[static_cast<std::size_t> (path)];
                measured.assign (static_cast<std::size_t> (length), 0.0f);
                int offset = 0;
                while (offset < length)
                {
                    const int count = std::min (257, length - offset);
                    std::vector<float> left (static_cast<std::size_t> (count), 0.0f);
                    std::vector<float> right (static_cast<std::size_t> (count), 0.0f);
                    if (offset == 0)
                    {
                        const float impulse = path / 2 == 0 ? 1.0f : -1.0f;
                        left[0] = impulse;
                        right[0] = -impulse;
                    }
                    float* channels[] = { left.data(), right.data() };
                    copy.process (channels, 2, count);
                    const auto& lane = path % 2 == 0 ? left : right;
                    std::copy (lane.begin(), lane.end(),
                               measured.begin() + offset);
                    offset += count;
                }
            }
            // LL: input +1/-1 -> L' = H(1) = H impulse.
            expectTransferMatches (*this, paths[0], reference, rate, 0.05, "Side LL anti-phase");
            expectTransferMatches (*this, paths[3], reference, rate, 0.05, "Side RR anti-phase");
            expectTransferMatches (*this, paths[2], reference, rate, 0.05, "Side RL anti-phase");
            expectTransferMatches (*this, paths[1], reference, rate, 0.05, "Side LR anti-phase");
        }
    }

    void testMixedPlacementCompositionMatchesMatrix()
    {
        beginTest ("mixed placement chains match the analytic 2x2 matrix");
        const auto rate = 48000.0;
        std::array<BandSettings, kMaxBands> settings {};
        settings[0] = placedBand (FilterShape::Bell, ChannelPlacement::Stereo,
                                  120.0, 5.0, 1.2);
        settings[1] = placedBand (FilterShape::Bell, ChannelPlacement::Mid,
                                  1000.0, 8.0, 4.0);
        settings[2] = placedBand (FilterShape::HighShelf, ChannelPlacement::Side,
                                  6000.0, -6.0, 0.9);
        settings[3] = placedBand (FilterShape::Notch, ChannelPlacement::Left,
                                  2500.0, 0.0, 12.0);
        settings[4] = placedBand (FilterShape::Bell, ChannelPlacement::Right,
                                  400.0, -9.0, 2.5);
        settings[5] = placedBand (FilterShape::LowCut, ChannelPlacement::Mid,
                                  60.0, 0.0, 0.7, 36.0);
        settings[6] = placedBand (FilterShape::HighCut, ChannelPlacement::Side,
                                  12000.0, 0.0, 0.7, 48.0);
        expectMatrixMatchesEngine (*this, settings, DesignMode::Realtime, rate, 16384,
                                   0.05, "mixed chain Realtime");
        expectMatrixMatchesEngine (*this, settings, DesignMode::AnalogMatched, rate,
                                   16384, 0.05, "mixed chain AnalogMatched");
    }

    void testPlacementOrderMatters()
    {
        beginTest ("left/mid placement order is audible and non-commutative");
        const auto rate = 48000.0;
        std::array<BandSettings, kMaxBands> orderA {};
        std::array<BandSettings, kMaxBands> orderB {};
        orderA[0] = placedBand (FilterShape::Bell, ChannelPlacement::Left,
                                1200.0, 10.0, 3.0);
        orderA[1] = placedBand (FilterShape::Bell, ChannelPlacement::Mid,
                                2400.0, 10.0, 3.0);
        orderB[0] = orderA[1];
        orderB[1] = orderA[0];

        auto measure = [&] (const std::array<BandSettings, kMaxBands>& order)
        {
            auto engine = ParametricEQTest::preparedEngine (rate, 257, 2);
            for (int index = 0; index < 2; ++index)
                engine.setBand (index, order[static_cast<std::size_t> (index)]);
            return ParametricEQTest::stereoImpulseResponses (engine, 16384);
        };
        const auto a = measure (orderA);
        const auto b = measure (orderB);
        const auto matrixA = ResponseCore::wetTransfer (orderA, DesignMode::Realtime,
                                                        rate, 1200.0);
        const auto matrixB = ResponseCore::wetTransfer (orderB, DesignMode::Realtime,
                                                        rate, 1200.0);
        expect (std::abs (matrixA.lr - matrixB.lr) > 1.0e-3
                    || std::abs (matrixA.rl - matrixB.rl) > 1.0e-3
                    || std::abs (matrixA.ll - matrixB.ll) > 1.0e-3,
                "left/mid order must produce a different transfer");
        double maximumDifference = 0.0;
        for (std::size_t sample = 0; sample < a[0].size(); ++sample)
            maximumDifference = std::max (maximumDifference,
                std::abs (static_cast<double> (a[1][sample]) - b[1][sample]));
        expect (maximumDifference > 1.0e-4,
                "measured LR paths must differ when order differs");
    }

    void testOverlappingMidSideBandsAndStress()
    {
        beginTest ("several overlapping M/S bands compose correctly at six rates");
        for (const auto rate : kRates)
        {
            std::array<BandSettings, kMaxBands> settings {};
            const ChannelPlacement placements[] = {
                ChannelPlacement::Mid, ChannelPlacement::Side,
                ChannelPlacement::Mid, ChannelPlacement::Side,
                ChannelPlacement::Stereo, ChannelPlacement::Left,
                ChannelPlacement::Right
            };
            for (std::size_t index = 0;
                 index < sizeof (placements) / sizeof (placements[0]); ++index)
                settings[index] = placedBand (
                    FilterShape::Bell, placements[index],
                    150.0 * std::pow (1.6, static_cast<int> (index)),
                    (static_cast<int> (index) & 1) != 0 ? 7.0 : -7.0,
                    0.5 + 0.6 * index);
            expectMatrixMatchesEngine (*this, settings, DesignMode::Realtime, rate,
                                       16384, 0.08,
                                       "overlapping M/S @ "
                                           + juce::String (rate, 0));
        }

        beginTest ("24-band alternating placement stress stays finite");
        for (const auto rate : kRates)
        {
            auto engine = ParametricEQTest::preparedEngine (rate, 256, 2);
            for (int index = 0; index < kMaxBands; ++index)
                engine.setBand (index, placedBand (
                    FilterShape::Bell,
                    static_cast<ChannelPlacement> (index % kChannelPlacementCount),
                    20.0 * std::pow (1.32, index),
                    (index & 1) != 0 ? 8.0 : -8.0, 0.1 + 0.3 * index));
            juce::AudioBuffer<float> buffer (2, 8193);
            ParametricEQTest::fillDeterministic (buffer, 0xA11CE5u);
            ParametricEQTest::processBuffer (engine, buffer, 0,
                                             buffer.getNumSamples());
            expect (ParametricEQTest::allFinite (buffer),
                    "finite @ " + juce::String (rate, 0) + " Hz");
        }
    }
};

static ParametricEQMidSideTests parametricEQMidSideTests;
