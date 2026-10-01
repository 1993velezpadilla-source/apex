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
    the tested setSampleRate path (length rescale + rate update); PPQ
    automation is untouched by construction.
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
