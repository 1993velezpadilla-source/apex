#include "ParametricEQTestUtils.h"

class ParametricEQEngineTests final : public juce::UnitTest
{
public:
    ParametricEQEngineTests() : UnitTest ("ParametricEQ.Engine", "APEX.ParametricEQ") {}

    void runTest() override
    {
        beginTest ("fixed professional band capacity and index bounds");
        auto engine = ParametricEQTest::preparedEngine();
        for (int index = 0; index < APEX::ParametricEQ::kMaxBands; ++index)
        {
            const auto frequency = 30.0 * std::pow (1.08, index);
            expect (engine.setBand (index, ParametricEQTest::band (
                APEX::ParametricEQ::FilterShape::Bell, frequency,
                (index & 1) != 0 ? 2.0 : -2.0, 0.7 + 0.05 * index)));
        }
        expect (! engine.setBand (-1, {}));
        expect (! engine.setBand (APEX::ParametricEQ::kMaxBands, {}));

        juce::AudioBuffer<float> buffer (2, 4097);
        ParametricEQTest::fillDeterministic (buffer);
        ParametricEQTest::processBuffer (engine, buffer, 0, buffer.getNumSamples());
        expect (ParametricEQTest::allFinite (buffer));

        beginTest ("zero samples and null channel pointers are bounded no-ops");
        float sample = 0.25f;
        float* channels[] = { &sample };
        engine.process (channels, 1, 0);
        expectEquals (sample, 0.25f);
        engine.process (nullptr, 1, 1);
        expectEquals (sample, 0.25f);

        beginTest ("host block larger than prepare hint is processed completely");
        auto oversized = ParametricEQTest::preparedEngine (48000.0, 16, 1);
        oversized.setBand (0, ParametricEQTest::band (
            APEX::ParametricEQ::FilterShape::Bell, 1000.0, 12.0, 1.0));
        std::vector<float> data (8193, 0.0f);
        data.back() = 0.125f;
        float* oneChannel[] = { data.data() };
        oversized.process (oneChannel, 1, static_cast<int> (data.size()));
        expect (ParametricEQTest::allFinite (data));
        expect (data.back() != 0.125f || data[data.size() - 2] != 0.0f,
                "tail sample must not be silently truncated");
    }
};

static ParametricEQEngineTests parametricEQEngineTests;
