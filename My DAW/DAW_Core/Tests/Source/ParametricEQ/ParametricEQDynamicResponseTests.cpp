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

void configureBell (Processor& processor, int band, float frequency, float gain, float q)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (FilterShape::Bell));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
}

} // namespace

class ParametricEQDynamicResponseTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicResponseTests()
        : UnitTest ("ParametricEQ.DynamicResponse", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testPublishedDynamicStateMatchesProcessor();
        testStaticBandsPublishEmptyDynamicState();
        testPublicationIsBoundedAndDropSafe();
    }

private:
    void testPublishedDynamicStateMatchesProcessor()
    {
        beginTest ("published dynamic mask and gains match the processor");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        configureBell (processor, 2, 4000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        setUnits (processor, dynamicParameterIndex (2, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (2, DynamicBandOffset::Range), -9.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                  -30.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        for (int sample = 0; sample < 512; ++sample)
        {
            const auto value = static_cast<float> (0.8 * std::sin (
                2.0 * juce::MathConstants<double>::pi * 937.5 * sample / 48000.0));
            buffer.setSample (0, sample, value);
            buffer.setSample (1, sample, value);
        }
        for (int block = 0; block < 8; ++block)
            processor.processBlock (buffer, midi);

        ResponseFrame frame;
        expect (processor.getResponseCore().consumeLatest (frame),
                "a response frame is published");
        expect ((frame.dynamicActiveMask & 1u) != 0, "band 0 dynamic bit set");
        expect ((frame.dynamicActiveMask & (1u << 2)) != 0, "band 2 dynamic bit set");
        expect ((frame.dynamicActiveMask & (1u << 1)) == 0, "band 1 not dynamic");
        expectWithinAbsoluteError (frame.dynamicGainDb[0],
                                   processor.getDynamicGainDbForTesting (0),
                                   1.0e-6, "band 0 published gain matches");
        expectWithinAbsoluteError (frame.dynamicGainDb[2],
                                   processor.getDynamicGainDbForTesting (2),
                                   1.0e-6, "band 2 published gain matches");
        expect (frame.dynamicGainDb[0] < -6.0,
                "band 0 reduction visible to the UI");
        expect (frame.dynamicGainDb[2] > 0.0,
                "band 2 negative-range boost visible to the UI");
    }

    void testStaticBandsPublishEmptyDynamicState()
    {
        beginTest ("static processors publish an empty dynamic mask");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        ResponseFrame frame;
        expect (processor.getResponseCore().consumeLatest (frame));
        expectEquals (frame.dynamicActiveMask, 0u);
        for (int band = 0; band < kMaxBands; ++band)
            expectWithinAbsoluteError (frame.dynamicGainDb[static_cast<std::size_t> (band)],
                                       0.0, 1.0e-12,
                                       "static published dynamic gain is zero");
    }

    void testPublicationIsBoundedAndDropSafe()
    {
        beginTest ("dynamic response publication drops safely when unread");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 200; ++iteration)
                processor.processBlock (buffer, midi);
        }
        ResponseFrame frame;
        expect (processor.getResponseCore().consumeLatest (frame),
                "newest frame survives unread backlog");
        expect ((frame.dynamicActiveMask & 1u) != 0,
                "dropped frames never corrupt the newest frame");
    }
};

static ParametricEQDynamicResponseTests parametricEQDynamicResponseTests;
