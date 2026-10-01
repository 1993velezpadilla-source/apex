#include <JuceHeader.h>

class RunnerSmokeTests final : public juce::UnitTest
{
public:
    RunnerSmokeTests() : juce::UnitTest ("runner.discovery.v1", "APEX.Smoke") {}

    void runTest() override
    {
        beginTest ("always-on assertion executes");
        expectEquals (2 + 2, 4);
    }
};

static RunnerSmokeTests runnerSmokeTests;
