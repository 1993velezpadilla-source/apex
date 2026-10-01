#include <JuceHeader.h>
#include "../../Source/ClipCore/PatternClip.h"
#include "../../Source/ClipCore/Clip.h"
#include "../../Source/StepSequencerCore/StepSequencerModel.h"

class PatternClipTests final : public juce::UnitTest
{
public:
    PatternClipTests() : juce::UnitTest ("StepSequencer.PatternClip", "APEX.StepSequencer") {}

    void runTest() override
    {
        beginTest ("PatternClip creation and defaults");
        {
            DAW::StablePatternId pid;
            DAW::PatternClip clip("id1", "Test Pattern", pid);
            expect (clip.getType() == DAW::ClipType::Pattern);
            expect (clip.getName() == "Test Pattern");
            expect (clip.getPatternId() == pid);
            expect (clip.isLinked());
            expectWithinAbsoluteError (clip.getGain(), 1.0f, 0.001f);
            expectWithinAbsoluteError (clip.getTranspose(), 0.0f, 0.001f);
            expectWithinAbsoluteError (clip.getProbability(), 1.0f, 0.001f);
        }

        beginTest ("PatternClip setters");
        {
            DAW::StablePatternId pid;
            DAW::PatternClip clip("id2", "Test", pid);
            clip.setLinked(false);
            expect (! clip.isLinked());
            clip.setGain(0.5f);
            expectWithinAbsoluteError (clip.getGain(), 0.5f, 0.001f);
            clip.setTranspose(5.0f);
            expectWithinAbsoluteError (clip.getTranspose(), 5.0f, 0.001f);
            clip.setProbability(0.75f);
            expectWithinAbsoluteError (clip.getProbability(), 0.75f, 0.001f);
        }

        beginTest ("PatternClip gain clamping");
        {
            DAW::StablePatternId pid;
            DAW::PatternClip clip("id3", "Test", pid);
            clip.setGain(3.0f);
            expectWithinAbsoluteError (clip.getGain(), 2.0f, 0.001f);
            clip.setGain(-1.0f);
            expectWithinAbsoluteError (clip.getGain(), 0.0f, 0.001f);
        }

        beginTest ("PatternClip serialization round-trip");
        {
            DAW::StablePatternId pid;
            DAW::PatternClip clip("id4", "My Pattern", pid);
            clip.setLinked(false);
            clip.setGain(0.8f);
            clip.setTranspose(3.0f);
            clip.setProbability(0.5f);
            clip.setStartPosition(44100);
            clip.setLength(88200);
            clip.setTrackID("track1");

            auto state = clip.getState();
            expect (state.isValid());

            DAW::StablePatternId pid2;
            DAW::PatternClip clip2("id5", "", pid2);
            clip2.restoreState(state);

            expect (clip2.getName() == "My Pattern");
            expect (! clip2.isLinked());
            expectWithinAbsoluteError (clip2.getGain(), 0.8f, 0.001f);
            expectWithinAbsoluteError (clip2.getTranspose(), 3.0f, 0.001f);
            expectWithinAbsoluteError (clip2.getProbability(), 0.5f, 0.001f);
            expect (clip2.getStartPosition() == 44100);
            expect (clip2.getLength() == 88200);
            expect (clip2.getTrackID() == "track1");
        }

        beginTest ("ClipManager createPatternClip");
        {
            DAW::ClipManager mgr;
            DAW::StablePatternId pid;
            auto* clip = mgr.createPatternClip("Pattern Clip", pid);
            expect (clip != nullptr);
            expect (clip->getType() == DAW::ClipType::Pattern);
            expect (clip->getName() == "Pattern Clip");
            expect (mgr.getAllClips().size() == 1);
        }

        beginTest ("ClipManager recreateClipFromState for Pattern type");
        {
            DAW::ClipManager mgr;
            DAW::StablePatternId pid;
            auto* clip = mgr.createPatternClip("Original", pid);
            auto state = clip->getState();

            DAW::ClipManager mgr2;
            auto* restored = mgr2.recreateClipFromState(state);
            expect (restored != nullptr);
            expect (restored->getType() == DAW::ClipType::Pattern);
            expect (restored->getName() == "Original");

            auto* patternClip = dynamic_cast<DAW::PatternClip*>(restored);
            expect (patternClip != nullptr);
            if (patternClip)
                expect (patternClip->getPatternId() == pid);
        }

        beginTest ("StepSequencerModel clonePatternWithId returns unique ID");
        {
            DAW::StepSequencerModel model;
            model.addLane("Kick");
            model.toggleStep(0, 0);

            auto newId = model.clonePatternWithId(0);
            expect (! newId.isNull());
            expect (model.getPatternCount() == 2);
            expect (model.getPattern(1).name.contains("unique"));
        }

        beginTest ("PatternClip updatePatternId via setPatternId");
        {
            DAW::StablePatternId pid1;
            DAW::StablePatternId pid2;
            DAW::PatternClip clip("id6", "Test", pid1);
            expect (clip.getPatternId() == pid1);
            clip.setPatternId(pid2);
            expect (clip.getPatternId() == pid2);
        }
    }
};

static PatternClipTests patternClipTests;
