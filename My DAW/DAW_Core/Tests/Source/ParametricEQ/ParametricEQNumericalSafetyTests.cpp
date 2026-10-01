#include "ParametricEQTestUtils.h"

class ParametricEQNumericalSafetyTests final : public juce::UnitTest
{
public:
    ParametricEQNumericalSafetyTests()
        : UnitTest ("ParametricEQ.NumericalSafety", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0,
                                 176400.0, 192000.0 };
        const FilterShape shapes[] = {
            FilterShape::Bell, FilterShape::LowShelf, FilterShape::HighShelf,
            FilterShape::LowCut, FilterShape::HighCut, FilterShape::Notch,
            FilterShape::BandPass, FilterShape::Tilt, FilterShape::FlatTilt,
            FilterShape::AllPass
        };

        beginTest ("all shapes survive legal extremes at every supported rate");
        for (const auto rate : rates)
            for (const auto shape : shapes)
            {
                for (const auto mode : { DesignMode::Realtime,
                                         DesignMode::AnalogMatched })
                {
                    auto engine = ParametricEQTest::preparedEngine (rate, 3, 2);
                    auto settings = ParametricEQTest::band (
                        shape, maximumUsableFrequency (rate), 36.0, 100.0, 96.0);
                    expect (engine.setBand (0, settings, mode));

                    juce::AudioBuffer<float> buffer (2, 32771);
                    ParametricEQTest::fillDeterministic (buffer,
                        static_cast<std::uint32_t> (rate)
                            ^ static_cast<std::uint32_t> (shape)
                            ^ static_cast<std::uint32_t> (mode));
                    ParametricEQTest::processBuffer (engine, buffer, 0,
                                                     buffer.getNumSamples());
                    expect (ParametricEQTest::allFinite (buffer),
                            "non-finite rate=" + juce::String (rate)
                                + " shape=" + juce::String ((int) shape)
                                + " mode=" + juce::String ((int) mode));
                }
            }

        beginTest ("non-finite settings are sanitised and cannot poison state");
        auto engine = ParametricEQTest::preparedEngine();
        auto invalid = ParametricEQTest::band (FilterShape::Bell, 1000.0, 6.0, 1.0);
        invalid.frequencyHz = std::numeric_limits<double>::quiet_NaN();
        invalid.gainDb = std::numeric_limits<double>::infinity();
        invalid.q = -std::numeric_limits<double>::infinity();
        invalid.slopeDbPerOctave = std::numeric_limits<double>::quiet_NaN();
        expect (engine.setBand (0, invalid, DesignMode::AnalogMatched));
        expect (std::isfinite (engine.getBand (0).frequencyHz));
        expect (std::isfinite (engine.getMagnitudeDb (1000.0)));

        beginTest ("shallow cut continuum produces no DC from silence");
        for (const auto slope : { 0.0, 0.000001, 0.01, 0.1, 1.0, 5.999999,
                                  6.0, 6.000001 })
        {
            auto shallow = ParametricEQTest::preparedEngine (48000.0, 7, 2);
            shallow.setBand (0, ParametricEQTest::band (
                FilterShape::LowCut, 20.0, 0.0, 1.0, slope));
            juce::AudioBuffer<float> silence (2, 48001);
            silence.clear();
            ParametricEQTest::processBuffer (shallow, silence, 0,
                                             silence.getNumSamples());
            expect (ParametricEQTest::allFinite (silence));
            float maximum = 0.0f;
            for (int channel = 0; channel < silence.getNumChannels(); ++channel)
                for (int sample = 0; sample < silence.getNumSamples(); ++sample)
                    maximum = std::max (maximum,
                                        std::abs (silence.getSample (channel, sample)));
            expectEquals (maximum, 0.0f,
                          "silence/DC transient at slope " + juce::String (slope, 8));
        }
    }
};

static ParametricEQNumericalSafetyTests parametricEQNumericalSafetyTests;
