#include <JuceHeader.h>
#include "../../../Source/TrackCore/TrackPolarityCore.h"
#include "../../../Source/TrackCore/Track.h"

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

        beginTest ("polarity defaults OFF, persists, and restores old projects as OFF");
        DAW::Track recordedTrack("polarity-source", "Snare Bottom");
        expect(!recordedTrack.isPolarityInverted());
        recordedTrack.setPolarityInverted(true);
        expect(recordedTrack.isPolarityInverted());

        const auto saved = recordedTrack.getState();
        expect((bool) saved.getProperty("polarityInverted", false));

        DAW::Track loadedTrack("polarity-restored", "placeholder");
        loadedTrack.restoreState(saved);
        expect(loadedTrack.isPolarityInverted());
        loadedTrack.setPolarityInverted(false);
        expect(!loadedTrack.isPolarityInverted());

        auto oldProject = saved.createCopy();
        oldProject.removeProperty("polarityInverted", nullptr);
        DAW::Track oldSessionTrack("polarity-legacy", "legacy");
        oldSessionTrack.restoreState(oldProject);
        expect(!oldSessionTrack.isPolarityInverted());

        beginTest ("master does not accept or restore channel polarity inversion");
        DAW::Track masterTrack("polarity-master", "Master");
        masterTrack.setMaster(true);
        masterTrack.setPolarityInverted(true);
        expect(!masterTrack.isPolarityInverted());

        auto modifiedMasterSave = saved.createCopy();
        modifiedMasterSave.setProperty(juce::Identifier("isMaster"), true, nullptr);
        masterTrack.restoreState(modifiedMasterSave);
        expect(!masterTrack.isPolarityInverted());

        beginTest ("smoothed polarity has no hard sign switch at block entry");
        juce::AudioBuffer<float> smoothBuffer(2, 128);
        DAW::TrackPolarityCore::TransitionState transition;
        smoothBuffer.clear();
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 128; ++sample)
                smoothBuffer.setSample(channel, sample, 0.5f);

        DAW::TrackPolarityCore::applyWithTransition(smoothBuffer, 128, true, transition);
        expect(std::abs(smoothBuffer.getSample(0, 0) - 0.5f) < 0.02f,
               "first inverted frame preserves continuity from +0.5 signal");
        expectWithinAbsoluteError(smoothBuffer.getSample(0, 63), -0.5f, 0.000001f);
        expectWithinAbsoluteError(smoothBuffer.getSample(1, 64), -0.5f, 0.000001f);
        expectWithinAbsoluteError(smoothBuffer.getSample(0, 0),
                                  smoothBuffer.getSample(1, 0), 0.000001f);
        expectEquals(transition.remainingSamples, 0);
        expectWithinAbsoluteError(transition.currentGain, -1.0f, 0.000001f);

        // Sample-exact post-transition sign is crucial to multi-mic nulling.
        smoothBuffer.clear();
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 128; ++sample)
                smoothBuffer.setSample(channel, sample, 0.5f);
        DAW::TrackPolarityCore::applyWithTransition(smoothBuffer, 128, true, transition);
        expectWithinAbsoluteError(smoothBuffer.getSample(0, 0), -0.5f, 0.000001f);
        expectWithinAbsoluteError(smoothBuffer.getSample(1, 127), -0.5f, 0.000001f);

        beginTest ("two identical signals cancel when just one reverses polarity");
        juce::AudioBuffer<float> reference(2, 4), second(2, 4);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 4; ++sample)
                reference.setSample(channel, sample, 0.1f * (sample + 1));
        second.makeCopyOf(reference);
        DAW::TrackPolarityCore::apply(second, 4, true);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 4; ++sample)
                expectWithinAbsoluteError(reference.getSample(channel, sample)
                    + second.getSample(channel, sample), 0.0f, 0.000001f);
    }
};

static RunnerSmokeTests runnerSmokeTests;
