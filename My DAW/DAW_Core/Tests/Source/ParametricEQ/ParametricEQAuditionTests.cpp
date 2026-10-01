#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <cmath>

namespace
{

using namespace APEX::ParametricEQ;
using APEX::Analysis::SpectrumTapMode;

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

void configureBand (Processor& processor, int band, FilterShape shape,
                    ChannelPlacement placement, float frequency, float gain,
                    float q, float slope = 12.0f)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (shape));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Slope), slope);
    setUnits (processor, placementParameterIndex (band),
              static_cast<float> (placement));
}

// Feed a stereo sine pair for `blocks` blocks and return the RMS of the last
// block on the requested channel.
double settledRms (Processor& processor, double frequency, float gainL,
                   float gainR, int channel, int blocks = 8, int blockSize = 512)
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
            const auto value = static_cast<float> (std::sin (
                2.0 * juce::MathConstants<double>::pi * frequency
                * (position + sample) / 48000.0));
            buffer.setSample (0, sample, value * gainL);
            buffer.setSample (1, sample, value * gainR);
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

void settleAudition (Processor& processor, int blocks = 8)
{
    // Explicitly cleared: audition settling must never be excited by
    // uninitialized buffer memory, which would make region measurements
    // depend on whatever bytes happened to be on the stack.
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
        processor.processBlock (buffer, midi);
}

} // namespace

class ParametricEQAuditionTests final : public juce::UnitTest
{
public:
    ParametricEQAuditionTests()
        : UnitTest ("ParametricEQ.Audition", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testConfiguredCoefficientsSurviveHistoryReset();
        testGainShapeRegionIsUnityProbeResidual();
        testCutAuditionRevealsRemovedRegion();
        testPlacementAuditionChannelSemantics();
        testAuditionIsGainIndependent();
        testAuditionNeverLatches();
        testAuditionIsClickFreeAndBlockInvariant();
        testAuditionAllocatesNothing();
        testAuditionIsNeverSerialized();
    }

private:
    void testConfiguredCoefficientsSurviveHistoryReset()
    {
        beginTest ("history reset never erases freshly configured coefficients");
        const auto rate = 48000.0;
        const BandSettings settings = []()
        {
            BandSettings result;
            result.enabled = true;
            result.shape = FilterShape::Bell;
            result.placement = ChannelPlacement::Mid;
            result.frequencyHz = 1000.0;
            result.gainDb = 12.0;
            result.q = 2.0;
            return result;
        }();

        AuditionBand band;
        band.configure (settings, DesignMode::Realtime, rate, true);
        expect (band.settings.enabled, "configured enabled flag must survive");
        expect (! band.isIdentity(),
                "configured coefficients must not be erased to identity");
        expect (band.placement == ChannelPlacement::Mid,
                "configured placement must survive");
        expect (band.sourceShape == FilterShape::Bell,
                "configured shape must survive");

        const auto designed = band.lower.response (1000.0, rate);
        expect (std::abs (designed) > 1.0,
                "designed probe filter must retain its transfer");
    }
    void testGainShapeRegionIsUnityProbeResidual()
    {
        beginTest ("bell audition is the exact unity-probe residual");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Stereo,
                       1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        expect (processor.beginAudition (0, 7));
        settleAudition (processor);
        expect (processor.isAuditionHoldingForTesting());
        expectWithinAbsoluteError (processor.getAuditionMixForTesting(), 1.0, 1.0e-12);

        // 937.5 Hz = exactly 10 cycles per 512-sample block at 48 kHz, so the
        // finite-window RMS of the last block is exactly 1/sqrt(2) and the
        // measured region level equals |H_probe - 1| to filter precision.
        const double frequency = 937.5;
        const auto measured = settledRms (processor, frequency, 1.0f, 1.0f, 0);
        expect (processor.isAuditionHoldingForTesting(),
                "hold must be maintained into measurement");
        expectWithinAbsoluteError (processor.getAuditionMixForTesting(),
                                   1.0, 1.0e-12,
                                   "mix must be fully settled before measurement");

        // The probe oracle must use the exact float-quantized band settings
        // the runtime adopted, not the ideal doubles the test requested.
        BandSettings probe;
        probe.enabled = true;
        probe.shape = FilterShape::Bell;
        probe.placement = ChannelPlacement::Stereo;
        probe.frequencyHz = static_cast<double> (processor.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Frequency))->getUnitsValue());
        probe.gainDb = AuditionBand::kAuditionProbeGainDb;
        probe.q = static_cast<double> (processor.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Q))->getUnitsValue());
        const auto h = FilterDesigner::designBand (probe, 48000.0,
                                                   DesignMode::Realtime)
                          .response (frequency, 48000.0);
        const auto expected = std::abs (h - std::complex<double> { 1.0, 0.0 })
                            * (1.0 / std::sqrt (2.0));
        expectWithinAbsoluteError (measured, expected, 0.002,
                                   "region RMS must equal |H_probe - 1| * input RMS");

        processor.endAudition (7);
        settleAudition (processor, 4);
        expect (! processor.isAuditionHoldingForTesting());
        expectWithinAbsoluteError (processor.getAuditionMixForTesting(), 0.0, 1.0e-12);
    }

    void testCutAuditionRevealsRemovedRegion()
    {
        beginTest ("low/high cut audition exposes the exact removed region");
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::LowCut,
                           ChannelPlacement::Stereo, 300.0f, 0.0f, 0.7f, 24.0f);
            processor.prepareToPlay (48000.0, 512);
            processor.beginAudition (0, 11);
            settleAudition (processor);
            const auto low = settledRms (processor, 60.0, 1.0f, 1.0f, 0);
            const auto high = settledRms (processor, 3000.0, 1.0f, 1.0f, 0);
            const auto inputRms = 1.0 / std::sqrt (2.0);
            // Removed-region audition exposes what the cut removes. Stopband
            // content is removed almost entirely and is therefore audible at
            // full level; passband content leaves only a small phase
            // residual, so it must be strongly attenuated RELATIVE to the
            // removed region (a truthful null audition, not silence).
            expect (low > 0.9 * inputRms,
                    "removed low content must be audible");
            expect (high < 0.5 * low,
                    "passed high content must be dominated by the removed region");
            expect (high < 0.7 * inputRms,
                    "passed high content must be small in removed-region audition");
            processor.endAudition (11);
        }
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::HighCut,
                           ChannelPlacement::Stereo, 4000.0f, 0.0f, 0.7f, 24.0f);
            processor.prepareToPlay (48000.0, 512);
            processor.beginAudition (0, 12);
            settleAudition (processor);
            const auto low = settledRms (processor, 500.0, 1.0f, 1.0f, 0);
            const auto high = settledRms (processor, 8000.0, 1.0f, 1.0f, 0);
            const auto inputRms = 1.0 / std::sqrt (2.0);
            expect (high > 0.9 * inputRms,
                    "removed high content must be audible");
            expect (low < 0.5 * high,
                    "passed low content must be dominated by the removed region");
            expect (low < 0.7 * inputRms,
                    "passed low content must be small in removed-region audition");
            processor.endAudition (12);
        }
    }

    void testPlacementAuditionChannelSemantics()
    {
        beginTest ("placement audition preserves M/S and L/R semantics");
        {
            // Mid audition on anti-phase content: M = 0, so the region is silent.
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Mid,
                           1000.0f, 9.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);
            processor.beginAudition (0, 21);
            settleAudition (processor);
            const auto level = settledRms (processor, 997.0, 1.0f, -1.0f, 0);
            expect (level < 0.002, "Mid audition must not collapse Side content");
            processor.endAudition (21);
        }
        {
            // Side audition on correlated content: S = 0, so the region is silent.
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Side,
                           1000.0f, 9.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);
            processor.beginAudition (0, 22);
            settleAudition (processor);
            const auto level = settledRms (processor, 997.0, 1.0f, 1.0f, 0);
            expect (level < 0.002, "Side audition must not collapse Mid content");
            processor.endAudition (22);
        }
        {
            // Side audition on anti-phase content is fully audible and stays
            // anti-phase (no mono collapse onto one physical channel).
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Side,
                           1000.0f, 9.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);
            processor.beginAudition (0, 23);
            settleAudition (processor);
            const auto left = settledRms (processor, 997.0, 1.0f, -1.0f, 0);
            const auto right = settledRms (processor, 997.0, 1.0f, -1.0f, 1);
            expectWithinAbsoluteError (left, right, 0.01,
                                       "Side audition must stay anti-phase symmetric");
            expect (left > 0.1, "Side audition must be audible for Side content");
            processor.endAudition (23);
        }
        {
            // Left audition leaves the physical right channel untouched.
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Left,
                           1000.0f, 9.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);
            processor.beginAudition (0, 24);
            settleAudition (processor);
            const auto right = settledRms (processor, 997.0, 1.0f, 1.0f, 1);
            expect (right < 0.002,
                    "Left audition must not produce a physical right output");
            processor.endAudition (24);
        }
    }

    void testAuditionIsGainIndependent()
    {
        beginTest ("audition region does not depend on the band's current gain");
        auto zeroStorage = std::make_unique<Processor>();
        auto boostStorage = std::make_unique<Processor>();
        auto& zero = *zeroStorage;
        auto& boost = *boostStorage;
        for (auto* processor : { &zero, &boost })
        {
            configureBand (*processor, 0, FilterShape::Bell,
                           ChannelPlacement::Stereo, 1000.0f,
                           processor == &zero ? 0.0f : 18.0f, 2.0f);
            processor->prepareToPlay (48000.0, 512);
            processor->beginAudition (0, 31);
            settleAudition (*processor);
        }
        const auto zeroRms = settledRms (zero, 937.5, 1.0f, 1.0f, 0);
        const auto boostRms = settledRms (boost, 937.5, 1.0f, 1.0f, 0);
        expect (zero.isAuditionHoldingForTesting()
                    && boost.isAuditionHoldingForTesting(),
                "both processors must hold audition into measurement");
        expectWithinAbsoluteError (zero.getAuditionMixForTesting(), 1.0, 1.0e-12,
                                   "zero-gain mix settled");
        expectWithinAbsoluteError (boost.getAuditionMixForTesting(), 1.0, 1.0e-12,
                                   "boost mix settled");
        expectWithinAbsoluteError (zeroRms, boostRms, 0.002,
                                   "gain must not change the audited region");

        // Sample-exact cross-check: with identical probe filters and settled
        // mix, both processors must produce bitwise-identical region outputs
        // for identical inputs.
        {
            juce::AudioBuffer<float> identical (2, 512);
            juce::MidiBuffer midi;
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto value = static_cast<float> (std::sin (
                    2.0 * juce::MathConstants<double>::pi * 937.5
                    * sample / 48000.0));
                identical.setSample (0, sample, value);
                identical.setSample (1, sample, value);
            }
            juce::AudioBuffer<float> boostCopy;
            boostCopy.makeCopyOf (identical);
            zero.processBlock (identical, midi);
            boost.processBlock (boostCopy, midi);
            for (int sample = 0; sample < 512; ++sample)
                expectEquals (identical.getSample (0, sample),
                              boostCopy.getSample (0, sample),
                              "region outputs must be bitwise identical");
        }
        zero.endAudition (31);
        boost.endAudition (31);
    }

    void testAuditionNeverLatches()
    {
        beginTest ("audition cannot latch across destructive lifecycle events");
        auto exercise = [&] (const std::function<void (Processor&)>& destroy,
                             const juce::String& label)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::Bell,
                           ChannelPlacement::Stereo, 1000.0f, 9.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);
            expect (processor.beginAudition (0, 41));
            settleAudition (processor);
            expect (processor.isAuditionHoldingForTesting(), label);
            destroy (processor);
            settleAudition (processor, 6);
            expect (! processor.isAuditionHoldingForTesting(),
                    label + " must cancel audition");
            expectWithinAbsoluteError (processor.getAuditionMixForTesting(),
                                       0.0, 1.0e-12, label + " mix must reach zero");
        };

        exercise ([] (Processor& processor)
        {
            setUnits (processor, parameterIndex (0, BandParameterOffset::Enabled), 0.0f);
        }, "delete band");
        exercise ([] (Processor& processor)
        {
            setUnits (processor, parameterIndex (0, BandParameterOffset::Bypass), 1.0f);
        }, "band bypass");
        exercise ([] (Processor& processor)
        {
            setUnits (processor, parameterIndex (0, BandParameterOffset::Shape), 2.0f);
        }, "shape change");
        exercise ([] (Processor& processor)
        {
            setUnits (processor, placementParameterIndex (0),
                      static_cast<float> (ChannelPlacement::Mid));
        }, "placement change");
        exercise ([] (Processor& processor)
        {
            processor.reset();
        }, "processor reset");
        exercise ([] (Processor& processor)
        {
            processor.setAnalyzerMode (SpectrumTapMode::Closed);
            const juce::MemoryBlock state;
            processor.setStateInformation (state.getData(), 0);
        }, "empty state restore");
        exercise ([] (Processor& processor)
        {
            processor.cancelAudition();
        }, "explicit cancel");

        // Stale end tokens must not cancel a newer gesture, and the matching
        // token must.
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Stereo,
                       1000.0f, 9.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        processor.beginAudition (0, 51);
        settleAudition (processor);
        processor.endAudition (52);
        settleAudition (processor, 2);
        expect (processor.isAuditionHoldingForTesting(),
                "stale end token must not cancel the active gesture");
        processor.endAudition (51);
        settleAudition (processor, 6);
        expect (! processor.isAuditionHoldingForTesting(),
                "matching end token must cancel the active gesture");

        // A fresh begin supersedes the previous gesture. Band 1 must be a
        // valid audition target first: auditioning a disabled band is
        // correctly rejected.
        setUnits (processor, parameterIndex (1, BandParameterOffset::Enabled), 1.0f);
        processor.beginAudition (1, 53);
        settleAudition (processor);
        expect (processor.isAuditionHoldingForTesting(),
                "new begin must re-arm audition");
        processor.endAudition (53);
        settleAudition (processor, 6);
        expect (! processor.isAuditionHoldingForTesting());
    }

    void testAuditionIsClickFreeAndBlockInvariant()
    {
        beginTest ("audition ramps follow the exact 10 ms law and stay click-free");
        constexpr double rampStep = 1.0 / 480.0;

        // Exact ramp law: one-sample blocks align the adoption boundary with
        // the sample index, so the per-sample mix progression is observable.
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::LowCut,
                           ChannelPlacement::Stereo, 250.0f, 0.0f, 0.7f, 36.0f);
            processor.prepareToPlay (48000.0, 1);
            juce::AudioBuffer<float> block (2, 1);
            block.clear();
            juce::MidiBuffer midi;
            processor.processBlock (block, midi);

            processor.beginAudition (0, 61);
            double previous = processor.getAuditionMixForTesting();
            for (int sample = 0; sample < 600; ++sample)
            {
                processor.processBlock (block, midi);
                const auto mix = processor.getAuditionMixForTesting();
                if (mix < 1.0)
                {
                    expectWithinAbsoluteError (mix - previous, rampStep, 1.0e-12,
                                               "exact 10 ms ramp step");
                    previous = mix;
                }
                else
                    expectWithinAbsoluteError (mix, 1.0, 1.0e-12, "hold endpoint");
            }
            expectWithinAbsoluteError (processor.getAuditionMixForTesting(),
                                       1.0, 1.0e-12, "settled hold mix");

            processor.endAudition (61);
            previous = processor.getAuditionMixForTesting();
            for (int sample = 0; sample < 600; ++sample)
            {
                processor.processBlock (block, midi);
                const auto mix = processor.getAuditionMixForTesting();
                if (mix > 0.0)
                {
                    expectWithinAbsoluteError (previous - mix, rampStep, 1.0e-12,
                                               "exact 10 ms release step");
                    previous = mix;
                }
                else
                    expectWithinAbsoluteError (mix, 0.0, 1.0e-12,
                                               "release endpoint");
            }
            expectWithinAbsoluteError (processor.getAuditionMixForTesting(),
                                       0.0, 1.0e-12, "settled release mix");
        }

        // Hostile block sizes: finite, click-bounded through the full
        // engage/hold/release cycle, and converged to the never-auditioned
        // reference after release.
        constexpr int total = 16384;
        auto render = [&] (int blockSize, bool withAudition)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::LowCut,
                           ChannelPlacement::Stereo, 250.0f, 0.0f, 0.7f, 36.0f);
            processor.prepareToPlay (48000.0, blockSize);

            std::vector<float> result (total);
            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;
            int position = 0;
            bool begun = false;
            bool ended = false;
            while (position < total)
            {
                if (withAudition && ! begun && position >= 2048)
                {
                    processor.beginAudition (0, 61);
                    begun = true;
                }
                if (withAudition && ! ended && position >= 12288)
                {
                    processor.endAudition (61);
                    ended = true;
                }
                const int count = std::min (blockSize, total - position);
                for (int sample = 0; sample < count; ++sample)
                    for (int channel = 0; channel < 2; ++channel)
                        block.setSample (channel, sample, 0.4f * std::sin (
                            2.0 * juce::MathConstants<double>::pi * 180.0
                            * (position + sample) / 48000.0));
                juce::AudioBuffer<float> view (block.getArrayOfWritePointers(),
                                               2, 0, count);
                processor.processBlock (view, midi);
                for (int sample = 0; sample < count; ++sample)
                    result[static_cast<std::size_t> (position + sample)]
                        = block.getSample (0, sample);
                position += count;
            }
            return result;
        };

        for (const int blockSize : { 31, 512 })
        {
            const auto rendered = render (blockSize, true);
            expect (ParametricEQTest::allFinite (rendered),
                    "finite @ block " + juce::String (blockSize));
            double maximumJump = 0.0;
            for (std::size_t sample = 1; sample < rendered.size(); ++sample)
                maximumJump = std::max (maximumJump,
                                        std::abs (static_cast<double> (rendered[sample])
                                                - rendered[sample - 1]));
            expect (maximumJump < 0.25,
                    "click bound @ block " + juce::String (blockSize) + ": "
                        + juce::String (maximumJump, 6));

            const auto reference = render (blockSize, false);
            const std::size_t convergedFrom = 12288 + 600;
            for (std::size_t sample = convergedFrom; sample < rendered.size(); ++sample)
                expectEquals (rendered[sample], reference[sample],
                              "post-release convergence @ block "
                                  + juce::String (blockSize)
                                  + " sample " + juce::String (static_cast<int> (sample)));
        }
    }

    void testAuditionAllocatesNothing()
    {
        beginTest ("audition engage, hold and release allocate zero memory");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, FilterShape::HighCut,
                       ChannelPlacement::Side, 5000.0f, 0.0f, 0.7f, 48.0f);
        processor.prepareToPlay (48000.0, 128);
        processor.beginAudition (0, 71);

        juce::AudioBuffer<float> buffer (2, 8193);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 40; ++iteration)
                processor.processBlock (buffer, midi);
        }
        expect (processor.isAuditionHoldingForTesting());
        processor.endAudition (71);
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 40; ++iteration)
                processor.processBlock (buffer, midi);
        }
        expect (! processor.isAuditionHoldingForTesting());
        expect (ParametricEQTest::allFinite (buffer));
    }

    void testAuditionIsNeverSerialized()
    {
        beginTest ("audition is transient and absent from hosted state");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Mid,
                       1200.0f, 7.0f, 2.5f);
        processor.prepareToPlay (48000.0, 512);
        processor.beginAudition (0, 81);
        settleAudition (processor);
        expect (processor.isAuditionHoldingForTesting());

        juce::MemoryBlock state;
        processor.getStateInformation (state);
        const auto tree = juce::ValueTree::readFromData (state.getData(),
                                                         static_cast<std::size_t> (
                                                             state.getSize()));
        expect (tree.isValid());
        if (tree.isValid())
        {
            for (int property = 0; property < tree.getNumProperties(); ++property)
                expect (! tree.getPropertyName (property).toString()
                              .containsIgnoreCase ("audition"),
                        "audition must not be a serialized property");
        }

        auto restoredStorage = std::make_unique<Processor>();
        auto& restored = *restoredStorage;
        restored.setStateInformation (state.getData(),
                                     static_cast<int> (state.getSize()));
        restored.prepareToPlay (48000.0, 512);
        settleAudition (restored);
        expect (! restored.isAuditionHoldingForTesting(),
                "state restore must not re-arm audition");
        expectWithinAbsoluteError (restored.getAuditionMixForTesting(), 0.0, 1.0e-12);
    }
};

static ParametricEQAuditionTests parametricEQAuditionTests;
