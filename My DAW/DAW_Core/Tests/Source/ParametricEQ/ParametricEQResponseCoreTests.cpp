#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQResponseCore.h"

class ParametricEQResponseCoreTests final : public juce::UnitTest
{
public:
    ParametricEQResponseCoreTests()
        : UnitTest ("ParametricEQ.ResponseCore", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;

        beginTest ("response frame uses the exact engine transfer and crossfade law");
        ResponseFrame frame;
        frame.sampleRate = 48000.0;
        frame.currentBands[0] = ParametricEQTest::band (
            FilterShape::Bell, 1000.0, 6.0, 2.0);
        frame.targetBands = frame.currentBands;
        frame.targetBands[0].gainDb = -6.0;

        auto current = ParametricEQTest::preparedEngine();
        current.setBand (0, frame.currentBands[0]);
        auto target = ParametricEQTest::preparedEngine();
        target.setBand (0, frame.targetBands[0]);

        for (const double frequency : { 20.0, 100.0, 1000.0, 4000.0, 18000.0 })
        {
            frame.transitionMix = 0.0;
            expect (std::abs (ResponseCore::response (frame, frequency)
                              - current.getResponse (frequency)) < 1.0e-12,
                    "current response at " + juce::String (frequency));
            frame.transitionMix = 1.0;
            expect (std::abs (ResponseCore::response (frame, frequency)
                              - target.getResponse (frequency)) < 1.0e-12,
                    "target response at " + juce::String (frequency));
            frame.transitionMix = 0.25;
            const auto expected = current.getResponse (frequency)
                                + 0.25 * (target.getResponse (frequency)
                                        - current.getResponse (frequency));
            expect (std::abs (ResponseCore::response (frame, frequency)
                              - expected) < 1.0e-12,
                    "parallel transition response at " + juce::String (frequency));
        }

        frame.transitionMix = 0.5;
        frame.bypassMix = 1.0;
        expect (std::abs (ResponseCore::response (frame, 1000.0)
                          - std::complex<double> { 1.0, 0.0 }) < 1.0e-12,
                "settled bypass response is exact identity");

        beginTest ("fractional cut response matches Engine");
        ResponseFrame cutFrame;
        cutFrame.sampleRate = 96000.0;
        cutFrame.currentBands[3] = ParametricEQTest::band (
            FilterShape::LowCut, 83.0, 0.0, 1.0, 47.5);
        cutFrame.targetBands = cutFrame.currentBands;
        auto cutEngine = ParametricEQTest::preparedEngine (96000.0);
        cutEngine.setBand (3, cutFrame.currentBands[3]);
        for (const double frequency : { 20.0, 83.0, 100.0, 1000.0 })
            expect (std::abs (ResponseCore::response (cutFrame, frequency)
                              - cutEngine.getResponse (frequency)) < 1.0e-12);

        beginTest ("fixed SPSC response publication is coherent and epoch-aware");
        ResponseCore core;
        const auto epoch = core.beginPrepareEpoch();
        ResponseFrame queued;
        queued.prepareEpoch = epoch;
        queued.generation = 17;
        queued.sampleRate = 192000.0;
        queued.currentBands[9] = ParametricEQTest::band (
            FilterShape::HighShelf, 12000.0, 9.0, 0.7);
        queued.targetBands = queued.currentBands;
        expect (core.push (queued));
        ResponseFrame consumed;
        expect (core.consumeLatest (consumed));
        expectEquals (static_cast<juce::int64> (consumed.generation),
                      static_cast<juce::int64> (17));
        expectWithinAbsoluteError (consumed.sampleRate, 192000.0, 0.0);

        queued.prepareEpoch = epoch;
        queued.generation = 18;
        expect (core.push (queued));
        core.beginPrepareEpoch();
        expect (! core.consumeLatest (consumed),
                "old-rate response must be rejected after prepare invalidation");

        beginTest ("response overflow drops visualization, never overwrites unread data");
        ResponseCore full;
        const auto fullEpoch = full.beginPrepareEpoch();
        queued.prepareEpoch = fullEpoch;
        for (int index = 0; index < ResponseCore::kCapacity; ++index)
        {
            queued.generation = static_cast<std::uint64_t> (index);
            expect (full.push (queued));
        }
        expect (! full.push (queued));
        expectEquals (full.getDropCount(), 1);
        expect (full.consumeLatest (consumed));
        expectEquals (static_cast<juce::int64> (consumed.generation),
                      static_cast<juce::int64> (ResponseCore::kCapacity - 1));
    }
};

static ParametricEQResponseCoreTests parametricEQResponseCoreTests;
