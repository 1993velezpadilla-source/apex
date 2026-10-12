#include <JuceHeader.h>
#include "../../../Source/TrackCore/TrackPolarityCore.h"

class RunnerSmokeTests final : public juce::UnitTest
{
public:
    RunnerSmokeTests() : juce::UnitTest ("runner.discovery.v1", "APEX.Smoke") {}

    void runTest() override
    {
        beginTest ("always-on assertion executes");
        expectEquals (2 + 2, 4);

        beginTest ("polarity off is transparent, on negates all channels, and two inversions restore");
        juce::AudioBuffer<float> audio(2, 4);
        for (int sample = 0; sample < 4; ++sample)
        {
            audio.setSample(0, sample, 0.25f * (float)(sample + 1));
            audio.setSample(1, sample, -0.5f * (float)(sample + 1));
        }

        DAW::TrackPolarityCore::apply(audio, 3, false);
        expectWithinAbsoluteError(audio.getSample(0, 0), 0.25f, 0.000001f);

        DAW::TrackPolarityCore::apply(audio, 3, true);
        expectWithinAbsoluteError(audio.getSample(0, 0), -0.25f, 0.000001f);
        expectWithinAbsoluteError(audio.getSample(1, 1), 1.0f, 0.000001f);
        expectWithinAbsoluteError(audio.getSample(0, 3), 1.0f, 0.000001f);

        DAW::TrackPolarityCore::apply(audio, 3, true);
        expectWithinAbsoluteError(audio.getSample(0, 0), 0.25f, 0.000001f);
        expectWithinAbsoluteError(audio.getSample(1, 1), -1.0f, 0.000001f);

        beginTest ("mono polarity inversion");
        juce::AudioBuffer<float> mono(1, 1);
        mono.setSample(0, 0, 0.75f);
        DAW::TrackPolarityCore::apply(mono, 1, true);
        expectWithinAbsoluteError(mono.getSample(0, 0), -0.75f, 0.000001f);
    }
};

static RunnerSmokeTests runnerSmokeTests;
