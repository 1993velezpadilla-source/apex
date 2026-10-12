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

#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/DSPCore/TrackPolarityCore.h"

/** Mixer Ø regression: audible DSP behavior and real track project round-trip. */
class MixerPolarityTests final : public juce::UnitTest
{
public:
    MixerPolarityTests() : juce::UnitTest("mixer.polarity.v1", "APEX.Mixer") {}

    void runTest() override
    {
        beginTest("unchanged normal audio; steady inversion in both stereo channels");
        DAW::TrackPolarityCore polarity;
        juce::AudioBuffer<float> audio(2, 128);
        const auto fill = [&audio]()
        {
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                audio.setSample(0, i, 0.5f);
                audio.setSample(1, i, -0.25f);
            }
        };

        fill();
        polarity.process(audio, 128, false);
        expectWithinAbsoluteError(audio.getSample(0, 0), 0.5f, 1.0e-6f);
        expectWithinAbsoluteError(audio.getSample(1, 127), -0.25f, 1.0e-6f);

        beginTest("polarity transition ramps once, never jumps from +1 to -1");
        fill();
        polarity.process(audio, 128, true);
        expectWithinAbsoluteError(audio.getSample(0, 0), 0.5f, 0.03f);
        expectWithinAbsoluteError(audio.getSample(0, 127), -0.5f, 0.03f);
        expectWithinAbsoluteError(audio.getSample(1, 127), 0.25f, 0.03f);

        beginTest("subsequent inverted blocks are exactly negative");
        fill();
        polarity.process(audio, 128, true);
        expectWithinAbsoluteError(audio.getSample(0, 0), -0.5f, 1.0e-6f);
        expectWithinAbsoluteError(audio.getSample(1, 127), 0.25f, 1.0e-6f);

        beginTest("switching Ø back on follows a bounded ramp");
        fill();
        polarity.process(audio, 128, false);
        expectWithinAbsoluteError(audio.getSample(0, 127), 0.5f, 0.03f);
        fill();
        polarity.process(audio, 128, false);
        expectWithinAbsoluteError(audio.getSample(0, 0), 0.5f, 1.0e-6f);
        expectWithinAbsoluteError(audio.getSample(1, 127), -0.25f, 1.0e-6f);

        beginTest("project save and reopen preserves polarity; old project defaults normal");
        DAW::Track original(DAW::TrackID("mic-A"), "Drum close mic");
        expect(!original.isPolarityInverted());
        original.setPolarityInverted(true);
        const auto state = original.getState();
        DAW::Track reopened(DAW::TrackID("mic-A"), "Reopened");
        reopened.restoreState(state);
        expect(reopened.isPolarityInverted());
        juce::AudioBuffer<float> reopenedAudio(2, 32);
        reopenedAudio.clear();
        reopenedAudio.setSample(0, 0, 0.75f);
        reopened.applyOutputPolarity(reopenedAudio, 32);
        expectWithinAbsoluteError(reopenedAudio.getSample(0, 0), -0.75f, 1.0e-6f);

        auto legacyState = state.createCopy();
        legacyState.removeProperty("phasePolarityInverted", nullptr);
        DAW::Track legacy(DAW::TrackID("mic-A"), "Legacy");
        legacy.restoreState(legacyState);
        expect(!legacy.isPolarityInverted());
    }
};

static MixerPolarityTests mixerPolarityTests;
