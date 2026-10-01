#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <cmath>

namespace
{

using namespace APEX::ParametricEQ;

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

void processSilence (Processor& processor, int blocks = 4, int blockSize = 512)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
        processor.processBlock (buffer, midi);
}

} // namespace

class ParametricEQLifecycleTests final : public juce::UnitTest
{
public:
    ParametricEQLifecycleTests()
        : UnitTest ("ParametricEQ.Lifecycle", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testAuditionCancelledByLifecycleEvents();
        testPlacementResetReproducesImpulseExactly();
        testMonoStereoToggleWithPlacements();
        testDomainChangeResetsStaleHistory();
        testFreshInstancesAreInactiveAndDefault();
    }

private:
    void testAuditionCancelledByLifecycleEvents()
    {
        beginTest ("audition is cancelled by reprepare, release and reset");
        auto exercise = [&] (const std::function<void (Processor&)>& event,
                             const juce::String& label)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::Bell,
                           ChannelPlacement::Mid, 1000.0f, 9.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);
            processor.beginAudition (0, 91);
            processSilence (processor);
            expect (processor.isAuditionHoldingForTesting(), label);
            event (processor);
            processSilence (processor);
            expect (! processor.isAuditionHoldingForTesting(),
                    label + " must cancel audition");
        };

        exercise ([] (Processor& processor)
        {
            processor.prepareToPlay (96000.0, 256);
        }, "reprepare");
        exercise ([] (Processor& processor)
        {
            processor.releaseResources();
        }, "release");
        exercise ([] (Processor& processor)
        {
            processor.reset();
        }, "reset");
    }

    void testPlacementResetReproducesImpulseExactly()
    {
        beginTest ("reset after placement processing reproduces the impulse exactly");
        for (const auto placement : { ChannelPlacement::Stereo,
                                      ChannelPlacement::Left,
                                      ChannelPlacement::Mid,
                                      ChannelPlacement::Side,
                                      ChannelPlacement::Right })
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            configureBand (processor, 0, FilterShape::Bell, placement,
                           1000.0f, 12.0f, 6.0f);
            processor.prepareToPlay (48000.0, 128);

            juce::AudioBuffer<float> impulse (2, 128);
            juce::MidiBuffer midi;
            impulse.clear();
            impulse.setSample (0, 0, 1.0f);
            impulse.setSample (1, 0, 1.0f);
            processor.processBlock (impulse, midi);
            std::array<float, 128> firstLeft {};
            std::array<float, 128> firstRight {};
            for (int sample = 0; sample < 128; ++sample)
            {
                firstLeft[static_cast<std::size_t> (sample)]
                    = impulse.getSample (0, sample);
                firstRight[static_cast<std::size_t> (sample)]
                    = impulse.getSample (1, sample);
            }

            processor.reset();
            impulse.clear();
            impulse.setSample (0, 0, 1.0f);
            impulse.setSample (1, 0, 1.0f);
            processor.processBlock (impulse, midi);
            bool same = true;
            for (int sample = 0; sample < 128; ++sample)
            {
                same &= impulse.getSample (0, sample)
                     == firstLeft[static_cast<std::size_t> (sample)];
                same &= impulse.getSample (1, sample)
                     == firstRight[static_cast<std::size_t> (sample)];
            }
            expect (same, juce::String ("reset reproduction at ")
                              + channelPlacementName (placement));
        }
    }

    void testMonoStereoToggleWithPlacements()
    {
        beginTest ("mono/stereo reprepare respects absent-component semantics");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBand (processor, 0, FilterShape::Bell, ChannelPlacement::Right,
                       1000.0f, 18.0f, 4.0f);
        configureBand (processor, 1, FilterShape::Bell, ChannelPlacement::Mid,
                       2000.0f, 9.0f, 3.0f);
        processor.prepareToPlay (48000.0, 128);

        juce::AudioBuffer<float> mono (1, 8193);
        juce::MidiBuffer midi;
        ParametricEQTest::fillDeterministic (mono, 0x411u);
        {
            juce::AudioBuffer<float> view (mono.getArrayOfWritePointers(), 1, 0,
                                           mono.getNumSamples());
            processor.processBlock (view, midi);
        }
        expect (ParametricEQTest::allFinite (mono),
                "oversized mono block must stay finite");

        processor.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> stereo (2, 128);
        ParametricEQTest::fillDeterministic (stereo, 0x522u);
        processor.processBlock (stereo, midi);
        expect (ParametricEQTest::allFinite (stereo),
                "stereo reprepare must stay finite");

        processor.prepareToPlay (96000.0, 64);
        juce::AudioBuffer<float> resized (2, 8193);
        ParametricEQTest::fillDeterministic (resized, 0x633u);
        processor.processBlock (resized, midi);
        expect (ParametricEQTest::allFinite (resized),
                "rate change with oversized block must stay finite");
    }

    void testDomainChangeResetsStaleHistory()
    {
        beginTest ("placement domain changes reset filter history deliberately");
        const auto rate = 48000.0;
        auto warmed = ParametricEQTest::preparedEngine (rate, 257, 2);
        BandSettings bell = ParametricEQTest::band (FilterShape::Bell, 1000.0,
                                                    12.0, 6.0);
        bell.placement = ChannelPlacement::Left;
        warmed.setBand (0, bell);
        juce::AudioBuffer<float> drive (2, 4096);
        ParametricEQTest::fillDeterministic (drive, 0x741A1u);
        ParametricEQTest::processBuffer (warmed, drive, 0, drive.getNumSamples());

        BandSettings midBell = bell;
        midBell.placement = ChannelPlacement::Mid;
        warmed.setBand (0, midBell); // domain change: must reset history

        auto fresh = ParametricEQTest::preparedEngine (rate, 257, 2);
        fresh.setBand (0, midBell);

        const auto warmedPaths = ParametricEQTest::stereoImpulseResponses (
            warmed, 4096);
        const auto freshPaths = ParametricEQTest::stereoImpulseResponses (
            fresh, 4096);
        for (int path = 0; path < 4; ++path)
            for (std::size_t sample = 0; sample < 4096; ++sample)
                expectEquals (warmedPaths[static_cast<std::size_t> (path)][sample],
                              freshPaths[static_cast<std::size_t> (path)][sample],
                              "domain change must equal a fresh engine");
    }

    void testFreshInstancesAreInactiveAndDefault()
    {
        beginTest ("fresh processors default to Stereo and no audition");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        for (int band = 0; band < kMaxBands; ++band)
            expectEquals (processor.getParametricEQParameter (
                              placementParameterIndex (band))->getChoiceIndex(),
                          0);
        expect (! processor.isAuditionHoldingForTesting());
        expect (! processor.beginAudition (0, 101),
                "audition must be rejected before prepare");
        processor.prepareToPlay (48000.0, 128);
        processSilence (processor);
        expect (! processor.isAuditionHoldingForTesting());
    }
};

static ParametricEQLifecycleTests parametricEQLifecycleTests;
