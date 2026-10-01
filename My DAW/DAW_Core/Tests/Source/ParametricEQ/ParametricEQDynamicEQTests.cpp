#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <array>
#include <cmath>

namespace
{

using namespace APEX::ParametricEQ;

constexpr std::array<double, 6> kRates {
    44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
};

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

void configureBell (Processor& processor, int band, float frequency, float gain,
                    float q, ChannelPlacement placement = ChannelPlacement::Stereo)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (FilterShape::Bell));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
    setUnits (processor, placementParameterIndex (band),
              static_cast<float> (placement));
}

void configureDynamics (Processor& processor, int band, bool enabled,
                        float threshold, float range, float attack, float release)
{
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Enable),
              enabled ? 1.0f : 0.0f);
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Threshold),
              threshold);
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Range),
              range);
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Attack),
              attack);
    setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Release),
              release);
}

void processBlocks (Processor& processor, int blocks, int blockSize = 512)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
        processor.processBlock (buffer, midi);
}

double rmsOfLastBlock (Processor& processor, double frequency, double amplitude,
                       int channel, int blockSize = 512, int blocks = 8)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    int position = 0;
    double last = 0.0;
    for (int block = 0; block < blocks; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = static_cast<float> (amplitude * std::sin (
                2.0 * juce::MathConstants<double>::pi * frequency
                * (position + sample) / 48000.0));
            buffer.setSample (0, sample, value);
            buffer.setSample (1, sample, value);
        }
        processor.processBlock (buffer, midi);
        position += blockSize;
        double sum = 0.0;
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = buffer.getSample (channel, sample);
            sum += static_cast<double> (value) * value;
        }
        last = std::sqrt (sum / blockSize);
    }
    return last;
}

} // namespace

class ParametricEQDynamicEQTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicEQTests()
        : UnitTest ("ParametricEQ.DynamicEQ", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testRangeLawUnitContract();
        testZeroRangeIsStaticBitExact();
        testPositiveRangeCutsAboveThreshold();
        testNegativeRangeBoostsBelowThreshold();
        testKneeAndThresholdBoundaries();
        testSignedLawUsesAttackTimingForEngagement();
        testDynamicBandAudioAtSixRates();
        testBlockInvarianceAndResetDeterminism();
        testSilenceAndBurstRecovery();
        testDynamicDisabledMatchesStaticPath();
    }

private:
    void testRangeLawUnitContract()
    {
        beginTest ("dynamic range law: positive/negative/zero with soft knee");
        DynamicBandParameters p;
        p.enabled = true;
        p.thresholdDb = -20.0;
        p.rangeDb = 6.0;

        expectWithinAbsoluteError (dynamicGainDbForLevel (-40.0, p), 0.0, 1.0e-12,
                                   "positive range, far below: no change");
        // At the threshold the 6 dB soft knee is half travelled: t = 0.5,
        // activation = 0.25, so a 6 dB range yields -1.5 dB.
        expectWithinAbsoluteError (dynamicGainDbForLevel (-20.0, p), -1.5, 1.0e-12,
                                   "positive range at threshold: quarter knee");
        expectWithinAbsoluteError (dynamicGainDbForLevel (0.0, p), -6.0, 1.0e-12,
                                   "positive range, far above: full -6 dB");

        p.rangeDb = -6.0;
        expectWithinAbsoluteError (dynamicGainDbForLevel (-40.0, p), 6.0, 1.0e-12,
                                   "negative range, far below: full +6 dB");
        expectWithinAbsoluteError (dynamicGainDbForLevel (0.0, p), 0.0, 1.0e-12,
                                   "negative range, far above: no change");

        p.rangeDb = 0.0;
        expectWithinAbsoluteError (dynamicGainDbForLevel (-40.0, p), 0.0, 1.0e-12);
        expectWithinAbsoluteError (dynamicGainDbForLevel (20.0, p), 0.0, 1.0e-12,
                                   "zero range is exactly static");

        // Continuity while the range approaches zero from both sides.
        double previousPositive = 0.0;
        for (double range = 6.0; range >= 1.0e-9; range *= 0.5)
        {
            p.rangeDb = range;
            const auto value = dynamicGainDbForLevel (10.0, p);
            expect (std::isfinite (value) && value <= 0.0 && value >= -6.0);
            previousPositive = value;
        }
        expect (std::abs (previousPositive) < 1.0e-6,
                "positive branch vanishes continuously at zero range");
        double previousNegative = 0.0;
        for (double range = -6.0; range <= -1.0e-9; range *= 0.5)
        {
            p.rangeDb = range;
            const auto value = dynamicGainDbForLevel (10.0, p);
            expect (std::isfinite (value) && value >= 0.0 && value <= 6.0);
            previousNegative = value;
        }
        expect (std::abs (previousNegative) < 1.0e-6,
                "negative branch vanishes continuously at zero range");
    }

    void testZeroRangeIsStaticBitExact()
    {
        beginTest ("zero dynamic range behaves as deterministic static EQ");
        for (const auto rate : kRates)
        {
            auto dynamicStorage = std::make_unique<Processor>();
            auto staticStorage = std::make_unique<Processor>();
            auto& dynamic = *dynamicStorage;
            auto& staticProcessor = *staticStorage;
            for (auto* processor : { &dynamic, &staticProcessor })
            {
                configureBell (*processor, 0, 1000.0f, 6.0f, 2.0f);
                processor->prepareToPlay (rate, 512);
            }
            configureDynamics (dynamic, 0, true, -24.0f, 0.0f, 0.010f, 0.100f);

            juce::AudioBuffer<float> a (2, 8193);
            juce::AudioBuffer<float> b (2, 8193);
            ParametricEQTest::fillDeterministic (a, 0xD17A71Cu);
            b.makeCopyOf (a);
            juce::MidiBuffer midi;
            dynamic.processBlock (a, midi);
            staticProcessor.processBlock (b, midi);
            expect (ParametricEQTest::bitEqual (a, b),
                    "zero-range dynamic must be bit-identical to static @ "
                        + juce::String (rate, 0));
        }
    }

    void testPositiveRangeCutsAboveThreshold()
    {
        beginTest ("positive range attenuates the band as level rises");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        configureDynamics (processor, 0, true, -30.0f, 8.0f, 0.002f, 0.020f);
        processor.prepareToPlay (48000.0, 512);
        processBlocks (processor, 4);

        // Residual-law reference: with the base +6 dB bell, the dynamic gain
        // g scales the residual contribution, out = x + g * (H - 1).
        BandSettings reference;
        reference.enabled = true;
        reference.shape = FilterShape::Bell;
        reference.placement = ChannelPlacement::Stereo;
        reference.frequencyHz = 1000.0;
        reference.gainDb = 6.0;
        reference.q = 2.0;
        const auto h = FilterDesigner::designBand (reference, 48000.0,
                                                   DesignMode::Realtime)
                          .response (937.5, 48000.0);
        const auto expectedQuietDb = 20.0 * std::log10 (std::max (1.0e-12,
                                                                  std::abs (h)));
        const auto gLoud = std::pow (10.0, -8.0 / 20.0);
        const auto expectedLoudDb = 20.0 * std::log10 (
            std::max (1.0e-12, std::abs (1.0 + gLoud * (h - 1.0))));

        const auto quiet = rmsOfLastBlock (processor, 937.5, 0.02, 0);
        const auto loud = rmsOfLastBlock (processor, 937.5, 0.8, 0);
        const auto quietDb = 20.0 * std::log10 (quiet / (0.02 / std::sqrt (2.0)));
        const auto loudDb = 20.0 * std::log10 (loud / (0.8 / std::sqrt (2.0)));
        expectWithinAbsoluteError (quietDb, expectedQuietDb, 0.25,
                                   "quiet input keeps the base filter gain");
        expectWithinAbsoluteError (loudDb, expectedLoudDb, 0.25,
                                   "loud input scales the residual by the range");
        expect (quietDb - loudDb > 2.0,
                "positive range reduces the band contribution; quiet="
                    + juce::String (quietDb, 2) + " loud=" + juce::String (loudDb, 2));
    }

    void testNegativeRangeBoostsBelowThreshold()
    {
        beginTest ("negative range boosts the band as level falls");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        configureDynamics (processor, 0, true, -30.0f, -8.0f, 0.002f, 0.020f);
        processor.prepareToPlay (48000.0, 512);
        processBlocks (processor, 4);

        BandSettings reference;
        reference.enabled = true;
        reference.shape = FilterShape::Bell;
        reference.placement = ChannelPlacement::Stereo;
        reference.frequencyHz = 1000.0;
        reference.gainDb = 6.0;
        reference.q = 2.0;
        const auto h = FilterDesigner::designBand (reference, 48000.0,
                                                   DesignMode::Realtime)
                          .response (937.5, 48000.0);
        const auto gQuiet = std::pow (10.0, 8.0 / 20.0);
        const auto expectedQuietDb = 20.0 * std::log10 (
            std::max (1.0e-12, std::abs (1.0 + gQuiet * (h - 1.0))));
        const auto expectedLoudDb = 20.0 * std::log10 (std::max (1.0e-12,
                                                                  std::abs (h)));

        const auto quiet = rmsOfLastBlock (processor, 937.5, 0.02, 0);
        const auto loud = rmsOfLastBlock (processor, 937.5, 0.8, 0);
        const auto quietDb = 20.0 * std::log10 (quiet / (0.02 / std::sqrt (2.0)));
        const auto loudDb = 20.0 * std::log10 (loud / (0.8 / std::sqrt (2.0)));
        expectWithinAbsoluteError (quietDb, expectedQuietDb, 0.35,
                                   "quiet input receives the residual-scaled boost");
        expectWithinAbsoluteError (loudDb, expectedLoudDb, 0.25,
                                   "loud input keeps the base filter gain");
        expect (quietDb - loudDb > 2.0,
                "negative range boosts below threshold; quiet="
                    + juce::String (quietDb, 2) + " loud=" + juce::String (loudDb, 2));
    }

    void testKneeAndThresholdBoundaries()
    {
        beginTest ("knee and threshold boundaries are continuous in processor use");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        configureDynamics (processor, 0, true, -30.0f, 6.0f, 0.002f, 0.020f);
        processor.prepareToPlay (48000.0, 512);

        // Sweep the input level smoothly across the threshold with small,
        // constant per-block dB steps so the smoothed dynamic gain tracks
        // without jumps.
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        int position = 0;
        double previous = 0.0;
        double maximumJump = 0.0;
        constexpr int blocks = 400;
        for (int block = 0; block < blocks; ++block)
        {
            const double amplitude = 0.001 * std::pow (900.0,
                static_cast<double> (block) / (blocks - 1));
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto value = static_cast<float> (amplitude * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 937.5
                    * (position + sample) / 48000.0));
                buffer.setSample (0, sample, value);
                buffer.setSample (1, sample, value);
            }
            processor.processBlock (buffer, midi);
            position += 512;
            const auto dynamicGain = processor.getDynamicGainDbForTesting (0);
            maximumJump = std::max (maximumJump, std::abs (dynamicGain - previous));
            previous = dynamicGain;
            expect (std::isfinite (dynamicGain), "dynamic gain stays finite");
        }
        expect (maximumJump < 1.5,
                "threshold sweep must not jump; max per-block delta "
                    + juce::String (maximumJump, 4) + " dB");
    }

    void testSignedLawUsesAttackTimingForEngagement()
    {
        beginTest ("signed product law maps through the magnitude envelope with correct timing");
        // The reusable APEX::Dynamics envelope operates on non-negative
        // effect magnitude (attack = target > state). A signed -9 dB cut
        // target must therefore ENGAGE with attack timing, never the release
        // branch. This pins the integration convention permanently.
        const auto rate = 48000.0;

        {
            // Positive range: engagement uses the 1 ms attack.
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (rate, 512);
            configureDynamics (processor, 0, true, -30.0f, 9.0f, 0.001f, 0.050f);

            juce::AudioBuffer<float> block (2, 1);
            block.clear();
            juce::MidiBuffer midi;
            for (int sample = 0; sample < 96; ++sample)
            {
                const auto value = static_cast<float> (0.8 * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 937.5 * sample / rate));
                block.setSample (0, 0, value);
                block.setSample (1, 0, value);
                processor.processBlock (block, midi);
            }
            // 96 samples = 2 ms: with a 1 ms attack the reduction is nearly
            // complete (>= 7 of 9 dB). Release timing (50 ms) would leave it
            // below ~0.4 dB. This distinguishes the two branches decisively.
            expect (processor.getDynamicGainDbForTesting (0) < -6.5,
                    "negative targets must engage via attack, not release");
        }

        {
            // Negative range: engagement (level falling) also uses attack.
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (rate, 512);
            configureDynamics (processor, 0, true, -30.0f, -9.0f, 0.001f, 0.050f);

            // Drive loud first to reach zero boost, then silence: the boost
            // must engage with attack timing.
            juce::AudioBuffer<float> block (2, 1);
            block.clear();
            juce::MidiBuffer midi;
            for (int sample = 0; sample < 4800; ++sample)
            {
                const auto value = static_cast<float> (0.8 * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 937.5 * sample / rate));
                block.setSample (0, 0, value);
                block.setSample (1, 0, value);
                processor.processBlock (block, midi);
            }
            expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                       0.0, 0.2, "loud input holds zero boost");
            // Silence: the ~10 ms RMS detector window and the soft knee must
            // first decay before the boost target rises to +9 dB. At 3200
            // silent samples the target is ~8.7 dB; a 1 ms attack tracks it
            // closely (> 7 dB), while release timing lags near 6.4 dB.
            for (int sample = 0; sample < 3200; ++sample)
            {
                block.setSample (0, 0, 0.0f);
                block.setSample (1, 0, 0.0f);
                processor.processBlock (block, midi);
            }
            expect (processor.getDynamicGainDbForTesting (0) > 7.0,
                    "negative-range boosts must engage via attack, not release");
        }
    }

    void testDynamicBandAudioAtSixRates()
    {
        beginTest ("dynamic bands stay finite and bounded at six rates");
        for (const auto rate : kRates)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBell (processor, 0, 1000.0f, 8.0f, 2.0f);
            configureBell (processor, 1, 4000.0f, -6.0f, 1.5f,
                           ChannelPlacement::Mid);
            configureDynamics (processor, 0, true, -24.0f, 10.0f, 0.001f, 0.050f);
            configureDynamics (processor, 1, true, -24.0f, -8.0f, 0.001f, 0.050f);
            processor.prepareToPlay (rate, 512);

            juce::AudioBuffer<float> buffer (2, 8193);
            ParametricEQTest::fillDeterministic (buffer, 0xDD7714u);
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);
            expect (ParametricEQTest::allFinite (buffer),
                    "finite @ " + juce::String (rate, 0));
        }
    }

    void testBlockInvarianceAndResetDeterminism()
    {
        beginTest ("dynamic processing is block-size invariant and reset deterministic");
        auto render = [] (int blockSize)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            configureDynamics (processor, 0, true, -30.0f, 9.0f, 0.001f, 0.050f);
            processor.prepareToPlay (48000.0, blockSize);
            std::vector<float> result;
            constexpr int total = 16384;
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
        const auto again = render (512);
        for (std::size_t sample = 0; sample < one.size(); ++sample)
            expectEquals (fiveTwelve[sample], again[sample],
                          "reset/reprepare determinism");
    }

    void testSilenceAndBurstRecovery()
    {
        beginTest ("silence and bursts recover without NaN or stale reduction");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        configureDynamics (processor, 0, true, -30.0f, 12.0f, 0.001f, 0.050f);
        processor.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        for (int block = 0; block < 32; ++block)
            processor.processBlock (buffer, midi);
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6,
                                   "silence recovers to zero dynamic gain");

        for (int block = 0; block < 16; ++block)
        {
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto value = static_cast<float> (std::sin (
                    2.0 * juce::MathConstants<double>::pi * 937.5
                    * sample / 48000.0));
                buffer.setSample (0, sample, value);
                buffer.setSample (1, sample, value);
            }
            processor.processBlock (buffer, midi);
        }
        expect (processor.getDynamicGainDbForTesting (0) < -8.0,
                "full-scale input drives the reduction near its range");

        buffer.clear();
        for (int block = 0; block < 48; ++block)
        {
            buffer.clear(); // fresh silence every block: no output feedback
            processor.processBlock (buffer, midi);
        }
        // Release bound includes the ~10 ms RMS detector window: the
        // effective decay starts after ~2400 detector samples, then follows
        // the 50 ms release. 48 blocks: 12 * exp(-(24576-2400)/2400) dB.
        expect (std::abs (processor.getDynamicGainDbForTesting (0)) < 0.01,
                "release decays within the mathematically derived bound");
    }

    void testDynamicDisabledMatchesStaticPath()
    {
        beginTest ("disabled dynamics are bit-identical to the static path");
        auto dynamicStorage = std::make_unique<Processor>();
        auto staticStorage = std::make_unique<Processor>();
        auto& dynamic = *dynamicStorage;
        auto& staticProcessor = *staticStorage;
        for (auto* processor : { &dynamic, &staticProcessor })
        {
            configureBell (*processor, 0, 1000.0f, 6.0f, 2.0f);
            configureBell (*processor, 1, 4000.0f, -6.0f, 1.5f,
                           ChannelPlacement::Side);
            processor->prepareToPlay (48000.0, 512);
        }
        configureDynamics (dynamic, 0, false, -24.0f, 9.0f, 0.001f, 0.050f);
        configureDynamics (dynamic, 1, false, -24.0f, -9.0f, 0.001f, 0.050f);

        juce::AudioBuffer<float> a (2, 8193);
        juce::AudioBuffer<float> b (2, 8193);
        ParametricEQTest::fillDeterministic (a, 0xD15A81Eu);
        b.makeCopyOf (a);
        juce::MidiBuffer midi;
        dynamic.processBlock (a, midi);
        staticProcessor.processBlock (b, midi);
        expect (ParametricEQTest::bitEqual (a, b),
                "disabled dynamics must equal the static path bitwise");
    }
};

static ParametricEQDynamicEQTests parametricEQDynamicEQTests;
