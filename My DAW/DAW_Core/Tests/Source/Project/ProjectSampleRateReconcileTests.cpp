#include <JuceHeader.h>
#include "../../../Source/ProjectCore/ProjectSampleRateReconcileCore.h"
#include "../../../Source/MidiCore/MidiClip.h"
#include <cmath>

namespace R = DAW::ProjectSampleRateReconcile;

/**
    Project sample-rate identity (2026-07-26, pre-beta project integrity).

    Projects now persist their authoritative (device-granted) sample rate.
    On reopen the stored rate is compared with the currently granted device
    rate and a seconds-preserving reconcile is applied ONLY when both rates
    are proven. Legacy projects (no metadata) are never silently
    reinterpreted; a rate is never invented. MIDI clips reconcile through
    the tested setSampleRate path (length rescale + rate update). Sample-domain
    AutomationManagerCore positions reconcile with the timeline; PPQ automation
    remains untouched by construction.
*/
class ProjectSampleRateReconcileTests final : public juce::UnitTest
{
public:
    ProjectSampleRateReconcileTests()
        : juce::UnitTest ("project.sample-rate-reconcile.v1", "APEX.Project") {}

    void runTest() override
    {
        beginTest ("same-rate reopen: plan is none, factor 1.0");
        {
            const auto plan = R::makePlan (48000.0, 48000.0, true);
            expect (plan.action == R::Action::none);
            expectWithinAbsoluteError (plan.factor, 1.0, 1.0e-12);
            expect (! R::shouldReconcile (plan));
        }

        beginTest ("44.1 -> 48 reopen: reconcile with factor 48000/44100");
        {
            const auto plan = R::makePlan (44100.0, 48000.0, true);
            expect (plan.action == R::Action::reconcile);
            expect (R::shouldReconcile (plan));
            expectWithinAbsoluteError (plan.factor, 48000.0 / 44100.0, 1.0e-12);
        }

        beginTest ("48 -> 96 reopen: reconcile with factor 2.0");
        {
            const auto plan = R::makePlan (48000.0, 96000.0, true);
            expect (plan.action == R::Action::reconcile);
            expectWithinAbsoluteError (plan.factor, 2.0, 1.0e-12);
        }

        beginTest ("96 -> 48 reopen: reconcile with factor 0.5");
        {
            const auto plan = R::makePlan (96000.0, 48000.0, true);
            expect (plan.action == R::Action::reconcile);
            expectWithinAbsoluteError (plan.factor, 0.5, 1.0e-12);
        }

        beginTest ("legacy project without metadata: never reinterpreted");
        {
            const auto plan = R::makePlan (0.0, 48000.0, false);
            expect (plan.action == R::Action::legacyUnverified);
            expect (! R::shouldReconcile (plan));
            expect (! plan.metadataPresent);

            const auto zeroStored = R::makePlan (0.0, 48000.0, true); // corrupt zero = unprovable
            expect (zeroStored.action == R::Action::legacyUnverified);
            expect (! R::shouldReconcile (zeroStored));
        }

        beginTest ("device rate unavailable: never fabricate a rate");
        {
            const auto plan = R::makePlan (96000.0, 0.0, true);
            expect (plan.action == R::Action::deviceRateUnknown);
            expect (! R::shouldReconcile (plan));
        }

        beginTest ("scalePosition rounding");
        {
            expectEquals (R::scalePosition (44100, 48000.0 / 44100.0), (int64_t) 48000);
            expectEquals (R::scalePosition (44101, 48000.0 / 44100.0), (int64_t) 48001);
            expectEquals (R::scalePosition (96000, 0.5), (int64_t) 48000);
            expectEquals (R::scalePosition (0, 2.0), (int64_t) 0);
        }

        beginTest ("clip automation lane and region points remain aligned after save/reopen at 48 kHz");
        {
            const auto plan = R::makePlan (44100.0, 48000.0, true);
            DAW::ClipManager clips;
            auto* clip = clips.createEmptyClip (DAW::TrackID ("t-auto"), "vocal",
                                                (DAW::SamplePosition) 44100,
                                                (DAW::SamplePosition) 44100);

            DAW::AutomationManagerCore savedAutomation;
            const auto tapeStopId = DAW::AutomationLaneCore::makeClipTapeStopParameterId(clip->getID());
            savedAutomation.addPoint(clip->getTrackID(), tapeStopId, 55125, 0.25f); // 0.25 s into clip
            savedAutomation.addPoint(clip->getTrackID(), tapeStopId, 77175, 0.75f); // 0.75 s into clip

            DAW::AutomationClipRegionCore savedRegion;
            savedRegion.trackId = clip->getTrackID();
            savedRegion.parameterId = "clip.region.tape_stop";
            savedRegion.startSample = 88200;
            savedRegion.lengthSamples = 44100;
            savedRegion.localPoints = { { 0, 0.0f }, { 22050, 1.0f }, { 44100, 0.0f } };
            savedAutomation.addClipRegion(savedRegion);

            // Exercise the same ValueTree save/restore boundary used by projects.
            DAW::AutomationManagerCore reopenedAutomation;
            reopenedAutomation.restoreState(savedAutomation.getState());

            expect(R::shouldReconcile(plan));
            R::reconcileClips(clips, plan.factor, plan.deviceRate);
            R::reconcileAutomation(reopenedAutomation, plan.factor);

            expectEquals((juce::int64)clip->getStartPosition(), (juce::int64)48000);
            expectEquals((juce::int64)clip->getLength(), (juce::int64)48000);

            const auto* lane = reopenedAutomation.findLane(clip->getTrackID(), tapeStopId);
            expect(lane != nullptr);
            if (lane != nullptr)
            {
                expectEquals((juce::int64)lane->points.size(), (juce::int64)2);
                if (lane->points.size() == 2)
                {
                    expectEquals((juce::int64)lane->points[0].timeSamples, (juce::int64)60000);
                    expectEquals((juce::int64)lane->points[1].timeSamples, (juce::int64)84000);
                    expectWithinAbsoluteError((double)(lane->points[0].timeSamples - clip->getStartPosition()) / 48000.0,
                                              0.25, 1.0e-9);
                    expectWithinAbsoluteError((double)(lane->points[1].timeSamples - clip->getStartPosition()) / 48000.0,
                                              0.75, 1.0e-9);
                }
            }

            expectEquals((juce::int64)reopenedAutomation.getClipRegions().size(), (juce::int64)1);
            if (reopenedAutomation.getClipRegions().size() == 1)
            {
                const auto& region = reopenedAutomation.getClipRegions().front();
                expectEquals((juce::int64)region.startSample, (juce::int64)96000);
                expectEquals((juce::int64)region.lengthSamples, (juce::int64)48000);
                expectEquals((juce::int64)region.localPoints.size(), (juce::int64)3);
                if (region.localPoints.size() == 3)
                {
                    expectEquals((juce::int64)region.localPoints[0].timeSamples, (juce::int64)0);
                    expectEquals((juce::int64)region.localPoints[1].timeSamples, (juce::int64)24000);
                    expectEquals((juce::int64)region.localPoints[2].timeSamples, (juce::int64)48000);
                }
            }
        }

        beginTest ("clip timeline fields reconcile seconds-preserving (44.1 -> 48)");
        {
            DAW::ClipManager clips;
            auto* clip = clips.createEmptyClip (DAW::TrackID ("t1"), "a",
                                                (DAW::SamplePosition) 44100,
                                                (DAW::SamplePosition) 44100); // 1.0 s at 44.1k
            clip->setSourceOffset ((DAW::SamplePosition) 22050);               // 0.5 s at 44.1k

            R::reconcileClips (clips, 48000.0 / 44100.0, 48000.0);

            expectWithinAbsoluteError ((double) clip->getStartPosition() / 48000.0, 1.0, 0.001);
            expectWithinAbsoluteError ((double) clip->getLength() / 48000.0, 1.0, 0.001);
            expectWithinAbsoluteError ((double) clip->getSourceOffset() / 48000.0, 0.5, 0.001);
        }

        beginTest ("clip length is clamped to >= 1 sample at small factors");
        {
            DAW::ClipManager clips;
            auto* clip = clips.createEmptyClip (DAW::TrackID ("t1"), "a", 0, (DAW::SamplePosition) 1);
            R::reconcileClips (clips, 0.25, 11025.0);
            expectEquals ((juce::int64) clip->getLength(), (juce::int64) 1);
        }

        beginTest ("MIDI clip: seconds + tick extent preserved, clip rate updated (48 -> 96)");
        {
            DAW::ClipManager clips;
            auto* midi = clips.createMIDIClip ("m", 48000.0);
            expectWithinAbsoluteError ((double) midi->getLength() / 48000.0, 8.0, 0.001);
            const auto ticksBefore = midi->samplesToTicks (midi->getLength());

            R::reconcileClips (clips, 2.0, 96000.0);

            expectWithinAbsoluteError ((double) midi->getLength() / 96000.0, 8.0, 0.001);
            expectWithinAbsoluteError (midi->getSampleRate(), 96000.0, 0.001);
            const auto ticksAfter = midi->samplesToTicks (midi->getLength());
            expect (std::abs ((double) (ticksAfter - ticksBefore)) <= 2.0,
                    juce::String ("tick extent drifted: ") + juce::String ((juce::int64) ticksBefore)
                        + " -> " + juce::String ((juce::int64) ticksAfter));
        }

        beginTest ("invalid factor or rate is a no-op");
        {
            DAW::ClipManager clips;
            auto* clip = clips.createEmptyClip (DAW::TrackID ("t1"), "a", 100, 200);
            R::reconcileClips (clips, 0.0, 48000.0);
            R::reconcileClips (clips, -1.0, 48000.0);
            R::reconcileClips (clips, 2.0, 0.0);
            expectEquals ((juce::int64) clip->getStartPosition(), (juce::int64) 100);
            expectEquals ((juce::int64) clip->getLength(), (juce::int64) 200);
        }
    }
};

static ProjectSampleRateReconcileTests projectSampleRateReconcileTests;
