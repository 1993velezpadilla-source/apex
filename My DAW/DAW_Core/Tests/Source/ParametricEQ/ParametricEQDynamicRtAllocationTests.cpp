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

class ParametricEQDynamicRtAllocationTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicRtAllocationTests()
        : UnitTest ("ParametricEQ.DynamicRtAllocation", "APEX.ParametricEQ") {}

    void runTest() override
    {
        for (const int dynamicBands : { 0, 1, 4, 12, 24 })
            for (const bool peak : { false, true })
                testAllocationFor (dynamicBands, peak);

        testAllocationWithAuditionAndBypass();
        testAllocationWithExternalKey();
        testAllocationWithActiveAutomation();
        testDiagnosticCpuCharacterization();
    }

private:
    void testDiagnosticCpuCharacterization()
    {
        beginTest ("dynamic EQ CPU characterization (evidence only)");
        for (const int dynamicBands : { 0, 1, 4, 12, 24 })
        {
            for (const bool peak : { false, true })
            {
                auto processorStorage = std::make_unique<Processor>();
                auto& processor = *processorStorage;
                for (int band = 0; band < 12; ++band)
                    configureBell (processor, band, 200.0f * (band + 1), 6.0f, 2.0f);
                processor.prepareToPlay (48000.0, 512);
                setUnits (processor, kDynamicDetectorParameter, peak ? 0.0f : 1.0f);
                for (int band = 0; band < dynamicBands; ++band)
                {
                    setUnits (processor,
                              dynamicParameterIndex (band, DynamicBandOffset::Enable),
                              1.0f);
                    setUnits (processor,
                              dynamicParameterIndex (band, DynamicBandOffset::Range),
                              9.0f);
                }

                juce::AudioBuffer<float> buffer (2, 512);
                ParametricEQTest::fillDeterministic (buffer, 0xC0FFEE11u);
                juce::MidiBuffer midi;
                constexpr int blocks = 2000;
                const auto start = std::clock();
                for (int block = 0; block < blocks; ++block)
                    processor.processBlock (buffer, midi);
                const auto elapsed = std::clock() - start;
                const double seconds = static_cast<double> (elapsed) / CLOCKS_PER_SEC;
                logMessage ("PEQ.Dynamic CPU: " + juce::String (dynamicBands)
                            + " bands " + (peak ? "Peak" : "RMS")
                            + ": " + juce::String (seconds / blocks * 1.0e6, 1)
                            + " us per 512-sample stereo block");
            }
        }
    }
    void testAllocationFor (int dynamicBands, bool peak)
    {
        beginTest ("zero allocations: " + juce::String (dynamicBands)
                   + " dynamic bands, " + (peak ? "Peak" : "RMS"));
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        for (int band = 0; band < 12; ++band)
            configureBell (processor, band, 200.0f * (band + 1), 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 128);
        setUnits (processor, kDynamicDetectorParameter, peak ? 0.0f : 1.0f);
        for (int band = 0; band < dynamicBands; ++band)
        {
            setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Enable),
                      1.0f);
            setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Threshold),
                      -24.0f);
            setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Range),
                      9.0f);
            setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Attack),
                      0.001f);
            setUnits (processor, dynamicParameterIndex (band, DynamicBandOffset::Release),
                      0.050f);
        }

        juce::AudioBuffer<float> buffer (2, 8193);
        ParametricEQTest::fillDeterministic (buffer, 0xD11A7Eu);
        juce::MidiBuffer midi;
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 24; ++iteration)
                processor.processBlock (buffer, midi);
        }
        expect (ParametricEQTest::allFinite (buffer));
    }

    void testAllocationWithAuditionAndBypass()
    {
        beginTest ("zero allocations: active audition and bypass transitions with dynamics");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 128);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        processor.beginAudition (0, 71);

        juce::AudioBuffer<float> buffer (2, 128);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 48; ++iteration)
            {
                if (iteration == 24)
                    setUnits (processor, kGlobalBypassParameter, 1.0f);
                if (iteration == 36)
                    setUnits (processor, kGlobalBypassParameter, 0.0f);
                processor.processBlock (buffer, midi);
            }
        }
        processor.endAudition (71);
    }

    void testAllocationWithExternalKey()
    {
        beginTest ("zero allocations: external key submission and consumption");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 128);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        setUnits (processor, kDynamicSidechainParameter, 1.0f);

        juce::AudioBuffer<float> key (2, 128);
        ParametricEQTest::fillDeterministic (key, 0x4E75u);
        juce::AudioBuffer<float> buffer (2, 128);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        processor.submitExternalDetectorKey (key);
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 48; ++iteration)
                processor.processBlock (buffer, midi);
        }
    }

    void testAllocationWithActiveAutomation()
    {
        beginTest ("zero allocations: per-block dynamics automation");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 128);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);

        juce::AudioBuffer<float> buffer (2, 128);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 48; ++iteration)
            {
                setUnits (processor,
                          dynamicParameterIndex (0, DynamicBandOffset::Range),
                          24.0f * std::sin (iteration * 0.13));
                setUnits (processor,
                          dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                          -48.0f + 30.0f * std::abs (std::cos (iteration * 0.07)));
                processor.processBlock (buffer, midi);
            }
        }
        expect (ParametricEQTest::allFinite (buffer));
    }
};

static ParametricEQDynamicRtAllocationTests parametricEQDynamicRtAllocationTests;
